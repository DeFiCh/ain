use anyhow::{format_err, Result};
use proc_macro2::{Span, TokenStream};
use prost_build::{Config, Service, ServiceGenerator};
use quote::{quote, ToTokens};
use regex::Regex;
use syn::{Attribute, Fields, GenericArgument, Ident, Item, ItemStruct, PathArguments, Type};

use std::cell::RefCell;
use std::collections::HashMap;
use std::fs::{DirEntry, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::rc::Rc;
use std::{env, fs, io};

fn main() -> Result<()> {
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);
    let proto_path = manifest_path
        .parent()
        .ok_or(format_err!("path err: no parent"))?
        .join("proto");

    let src_path = manifest_path.join("src");
    let out_dir: PathBuf = PathBuf::from(env::var("OUT_DIR")?);
    let proto_rs_target_path = out_dir.join("proto");
    std::fs::create_dir_all(&proto_rs_target_path)?;

    let proto_include = std::env::var("PROTOC_INCLUDE_DIR")
        .map(PathBuf::from)
        .unwrap_or(proto_rs_target_path.clone());

    let methods = compile_proto_and_generate_services(
        &proto_path,
        Path::new(&proto_rs_target_path),
        Path::new(&proto_include),
    );
    modify_generate_code(methods, &Path::new(&proto_rs_target_path).join("types.rs"));

    println!(
        "cargo:rerun-if-changed={}",
        src_path.join("rpc.rs").to_string_lossy()
    );
    // Using a direct path for now
    let git_head_path = manifest_path.join("../../.git/HEAD");
    if git_head_path.exists() {
        println!("cargo:rerun-if-changed={}", git_head_path.to_string_lossy());
    }

    // Set GIT_HASH
    let output = Command::new("git")
        .args(&["rev-parse", "HEAD"])
        .output()
        .unwrap();
    let git_hash = String::from_utf8(output.stdout).unwrap();
    env::set_var("GIT_HASH", git_hash.trim());

    Ok(())
}

fn visit_files(dir: &Path, f: &mut dyn FnMut(&DirEntry)) -> io::Result<()> {
    if dir.is_dir() {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.is_dir() {
                visit_files(&path, f)?;
            } else {
                f(&entry);
            }
        }
    }
    Ok(())
}

struct Attr {
    matcher: &'static str,         // matcher for names
    attr: Option<&'static str>,    // attribute to be added to the entity
    rename: Option<&'static str>,  // whether entity should be renamed
    skip: &'static [&'static str], // entities that should be skipped
}

impl Attr {
    fn parse(attr: &str) -> Vec<Attribute> {
        let attr = attr.parse::<TokenStream>().unwrap();
        // This is an easier way to parse the attributes instead of writing a custom parser
        let empty_struct: ItemStruct = syn::parse2(quote! {
            #attr
            struct S;
        })
        .unwrap();
        empty_struct.attrs
    }

    fn matches(&self, name: &str, parent: Option<&str>, ty: Option<&str>) -> bool {
        let name = parent.map(|p| p.to_owned() + ".").unwrap_or_default() + name;
        let combined = format!(
            "{}.{}:{}",
            parent.unwrap_or_default(),
            name,
            ty.unwrap_or_default()
        );
        let re = Regex::new(self.matcher).unwrap();
        re.is_match(&combined.replace(' ', ""))
            && !self.skip.iter().any(|&n| {
                let re = Regex::new(n).unwrap();
                re.is_match(&name)
            })
    }
}

const TYPE_ATTRS: &[Attr] = &[
    Attr {
        matcher: ".*",
        attr: Some("#[derive(Serialize, Deserialize)] #[serde(rename_all=\"camelCase\")]"),
        rename: None,
        skip: &["^BlockResult", "NonUtxo", "^Transaction"],
    },
    Attr {
        matcher: ".*",
        attr: Some("#[allow(clippy::derive_partial_eq_without_eq)]"),
        rename: None,
        skip: &[],
    },
    Attr {
        matcher: "NonUtxo",
        attr: Some("#[derive(Serialize, Deserialize)] #[serde(rename_all=\"PascalCase\")]"),
        rename: None,
        skip: &[],
    },
];

const FIELD_ATTRS: &[Attr] = &[
    Attr {
        matcher: ":::prost::alloc::string::String",
        attr: Some("#[serde(skip_serializing_if = \"String::is_empty\")]"),
        rename: None,
        skip: &["^BlockResult", "NonUtxo", "^Transaction"],
    },
    Attr {
        matcher: ":::prost::alloc::vec::Vec",
        attr: Some("#[serde(skip_serializing_if = \"Vec::is_empty\")]"),
        rename: None,
        skip: &[],
    },
    Attr {
        matcher: "req_sigs",
        attr: Some("#[serde(skip_serializing_if = \"ignore_integer\")]"),
        rename: None,
        skip: &[],
    },
    // Attr {
    //     matcher: "currentblocktx|currentblockweight",
    // attr: Some("#[serde(skip_serializing_if = \"is_zero\")]"),
    // rename: None,
    // skip: &[],
    // },
    Attr {
        matcher: "asm",
        attr: Some("#[serde(rename=\"asm\")]"),
        rename: Some("field_asm"),
        skip: &[],
    },
    Attr {
        matcher: "operator",
        attr: Some("#[serde(rename = \"operator\")]"),
        rename: Some("field_operator"),
        skip: &["isoperator"],
    },
    Attr {
        matcher: "type",
        attr: Some("#[serde(rename=\"type\")]"),
        rename: Some("field_type"),
        skip: &[],
    },
    Attr {
        matcher: "previous_block_hash",
        attr: Some("#[serde(rename=\"previousblockhash\")]"),
        rename: None,
        skip: &[],
    },
    Attr {
        matcher: "next_block_hash",
        attr: Some("#[serde(rename=\"nextblockhash\")]"),
        rename: None,
        skip: &[],
    },
];

// Custom generator to collect RPC call signatures
struct WrappedGenerator {
    methods: Rc<RefCell<HashMap<String, Vec<Rpc>>>>,
    inner: Box<dyn ServiceGenerator>,
}

#[derive(Debug)]
struct Rpc {
    _name: String,
    url: Option<String>,
    client: bool,
    server: bool,
    _input_ty: String,
    _output_ty: String,
}

impl ServiceGenerator for WrappedGenerator {
    fn generate(&mut self, service: Service, buf: &mut String) {
        let re = Regex::new("\\[rpc: (.*?)\\]").unwrap();
        for method in &service.methods {
            let mut ref_map = self.methods.borrow_mut();
            let vec = ref_map.entry(service.name.clone()).or_default();
            let mut rpc = Rpc {
                _name: method.proto_name.clone(),
                _input_ty: method.input_proto_type.clone(),
                url: None,
                client: true,
                server: true,
                _output_ty: method.output_proto_type.clone(),
            };
            for line in method
                .comments
                .leading_detached
                .iter()
                .flatten()
                .chain(method.comments.leading.iter())
                .chain(method.comments.trailing.iter())
            {
                if line.contains("[ignore]") {
                    rpc.client = false;
                    rpc.server = false;
                }
                if line.contains("[client]") {
                    rpc.server = false;
                }
                if line.contains("[server]") {
                    rpc.client = false;
                }
                if let Some(captures) = re.captures(line) {
                    rpc.url = Some(captures.get(1).unwrap().as_str().into());
                }
            }
            vec.push(rpc);
        }
        self.inner.generate(service, buf);
    }

    fn finalize(&mut self, buf: &mut String) {
        self.inner.finalize(buf);
    }
}

fn compile_proto_and_generate_services(
    dir: &Path,
    out_dir: &Path,
    protoc_include: &Path,
) -> HashMap<String, Vec<Rpc>> {
    let methods = Rc::new(RefCell::new(HashMap::new()));
    let gen = WrappedGenerator {
        methods: methods.clone(),
        inner: tonic_build::configure()
            .build_client(false)
            .service_generator(),
    };

    let mut protos = vec![];
    visit_files(dir, &mut |entry: &DirEntry| {
        let path = entry.path();
        let file_name = path.file_name().unwrap().to_str().unwrap();
        if file_name.ends_with(".proto") {
            println!("cargo:rerun-if-changed={}", path.display());
            protos.push(path);
        }
    })
    .expect("visiting files");

    {
        // There's no way to compile protos using custom generator in tonic,
        // so we're left with creating a prost config and using that for codegen.
        let mut config = Config::new();
        config.out_dir(out_dir);
        config.service_generator(Box::new(gen));
        config
            .compile_protos(&protos, &[dir, protoc_include])
            .expect("compiling protobuf");
    } // drop it so we release rc count

    Rc::try_unwrap(methods).unwrap().into_inner()
}

fn modify_generate_code(_methods: HashMap<String, Vec<Rpc>>, types_path: &Path) {
    let mut contents = String::new();
    File::open(types_path)
        .unwrap()
        .read_to_string(&mut contents)
        .unwrap();
    let parsed_file = syn::parse_file(&contents).unwrap();

    // Modify structs if needed
    let structs = change_types(parsed_file);
    contents.clear();

    let syntax_tree: syn::File = syn::parse2(structs).unwrap();
    let pretty = prettyplease::unparse(&syntax_tree);
    contents.push_str(&pretty);
    File::create(types_path)
        .unwrap()
        .write_all(contents.as_bytes())
        .unwrap();
}

fn change_types(file: syn::File) -> TokenStream {
    let mut map = HashMap::new();
    let mut modified = quote! {
        fn ignore_integer<T: num_traits::PrimInt + num_traits::Signed + num_traits::NumCast>(i: &T) -> bool {
            T::from(-1).unwrap() == *i
        }
        // fn is_zero(i: &i64) -> bool {
        //     *i == 0
        // }
    };

    let mut copied = quote!();
    // Replace prost-specific fields with defaults
    for item in file.items {
        let mut s = match item {
            Item::Struct(s) => s,
            _ => continue,
        };

        let name = s.ident.to_string();
        for rule in TYPE_ATTRS {
            if !rule.matches(&name, None, None) {
                continue;
            }

            if let Some(attr) = rule.attr {
                s.attrs.extend(Attr::parse(attr));
            }

            if let Some(new_name) = rule.rename {
                s.ident = Ident::new(new_name, Span::call_site());
            }
        }

        let fields = match &mut s.fields {
            Fields::Named(ref mut f) => f,
            _ => panic!("unsupported struct"),
        };
        for field in &mut fields.named {
            let f_name = field.ident.as_ref().unwrap().to_string();
            let t_name = field.ty.to_token_stream().to_string();
            for rule in FIELD_ATTRS {
                if !rule.matches(&f_name, Some(&name), Some(&t_name)) {
                    continue;
                }

                if let Some(attr) = rule.attr {
                    field.attrs.extend(Attr::parse(attr));
                }

                if let Some(new_name) = rule.rename {
                    field.ident = Some(Ident::new(new_name, Span::call_site()));
                }
            }
        }

        modified.extend(quote!(#s));
        map.insert(s.ident.to_string(), s.clone());

        s.attrs = Attr::parse("#[derive(Debug, Default, Serialize , Deserialize, PartialEq)]");
        let fields = match &mut s.fields {
            Fields::Named(ref mut f) => f,
            _ => unreachable!(),
        };

        for field in &mut fields.named {
            field.attrs.clear(); // clear attributes
            fix_type(&mut field.ty);
        }

        copied.extend(quote! {
            #s
        });
    }

    modified
}

fn fix_type(ty: &mut Type) {
    let t = quote!(#ty).to_string().replace(' ', "");
    if t.contains("::prost::alloc::string::") {
        *ty = syn::parse2(quote!(String)).unwrap();
    }
    if t.contains("::prost::alloc::vec::") {
        let mut inner = get_path_bracketed_ty_simple(ty);
        fix_type(&mut inner);
        *ty = syn::parse2(quote!(Vec<#inner>)).unwrap();
    }
    if t.contains("::core::option::") {
        *ty = get_path_bracketed_ty_simple(ty);
    }
}

/// Extracts "T" from `std::option::Option`<T> for example
fn get_path_bracketed_ty_simple(ty: &Type) -> Type {
    match ty {
        Type::Path(ref p) => {
            let last = p.path.segments.last().unwrap();
            match &last.arguments {
                PathArguments::AngleBracketed(ref a) => match a.args.first().unwrap() {
                    GenericArgument::Type(ref t) => t.clone(),
                    _ => panic!("unsupported generic type: {}", quote!(#ty)),
                },
                PathArguments::None => ty.clone(),
                _ => panic!("parenthesis type {} not supported", quote!(#ty)),
            }
        }
        _ => panic!("unsupported type {}", quote!(#ty)),
    }
}

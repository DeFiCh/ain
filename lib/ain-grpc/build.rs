use heck::{ToLowerCamelCase, ToPascalCase, ToSnekCase};
use proc_macro2::{Span, TokenStream};
use prost_build::{Config, Service, ServiceGenerator};
use quote::{quote, ToTokens};
use regex::Regex;
use syn::{
    Attribute, Field, Fields, GenericArgument, Ident, Item, ItemStruct, PathArguments, Type,
};

use std::cell::RefCell;
use std::collections::HashMap;
use std::fs::{DirEntry, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::{env, fs, io};

fn to_eth_case(str: &str) -> String {
    match str.split("_").collect::<Vec<_>>()[..] {
        [_, method_name] => format!("eth_{}", method_name.to_lower_camel_case()),
        _ => panic!("Method name should be in the eth_<MethodName> format"),
    }
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
    name: String,
    url: Option<String>,
    client: bool,
    server: bool,
    input_ty: String,
    output_ty: String,
}

impl ServiceGenerator for WrappedGenerator {
    fn generate(&mut self, service: Service, buf: &mut String) {
        let re = Regex::new("\\[rpc: (.*?)\\]").unwrap();
        for method in &service.methods {
            let mut ref_map = self.methods.borrow_mut();
            let vec = ref_map.entry(service.name.clone()).or_default();
            let mut rpc = Rpc {
                name: method.proto_name.clone(),
                input_ty: method.input_proto_type.clone(),
                url: None,
                client: true,
                server: true,
                output_ty: method.output_proto_type.clone(),
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

fn generate_from_protobuf(dir: &Path, out_dir: &Path) -> HashMap<String, Vec<Rpc>> {
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
            .compile_protos(&protos, &[dir])
            .expect("compiling protobuf");
    } // drop it so we release rc count

    Rc::try_unwrap(methods).unwrap().into_inner()
}

fn modify_codegen(
    methods: HashMap<String, Vec<Rpc>>,
    types_path: &Path,
    rpc_path: &Path,
    lib_path: &Path,
) -> TokenStream {
    let mut contents = String::new();
    File::open(types_path)
        .unwrap()
        .read_to_string(&mut contents)
        .unwrap();
    let parsed_file = syn::parse_file(&contents).unwrap();

    // Modify structs if needed
    let (struct_map, structs, ffi_structs) = change_types(parsed_file);
    contents.clear();
    contents.push_str(&structs.to_string());
    File::create(types_path)
        .unwrap()
        .write_all(contents.as_bytes())
        .unwrap();

    let (ffi_tt, impl_tt, rpc_tt) = apply_substitutions(ffi_structs, struct_map, methods);

    // Append additional RPC impls next to proto-generated RPC impls
    contents.clear();
    File::open(rpc_path)
        .unwrap()
        .read_to_string(&mut contents)
        .unwrap();

    let mut codegen = String::new();
    codegen.push_str("\n#[cxx::bridge]\npub mod ffi {\n");
    codegen.push_str(&ffi_tt.to_string());
    codegen.push_str("\n}\n");

    codegen.push_str(&impl_tt.to_string());

    for (server_mod, (jrpc_tt, grpc_tt)) in rpc_tt {
        let (server, service, svc_mod, svc_trait) = (
            Ident::new(
                &format!("{}Server", server_mod.to_pascal_case()),
                Span::call_site(),
            ),
            Ident::new(
                &format!("{}Service", server_mod.to_pascal_case()),
                Span::call_site(),
            ),
            Ident::new(
                &format!("{server_mod}Server").to_snek_case(),
                Span::call_site(),
            ),
            Ident::new(&server_mod.to_pascal_case(), Span::call_site()),
        );
        codegen.push_str(
            &quote!(
                #[derive(Clone)]
                pub struct #service {
                    #[allow(dead_code)] adapter: Arc<Handlers>
                }

                impl #service {
                    #[inline]
                    #[allow(dead_code)]
                    pub fn new(adapter: Arc<Handlers>) -> #service {
                        #service {
                            adapter
                        }
                    }
                    #[inline]
                    #[allow(dead_code)]
                    pub fn service(&self) -> #svc_mod::#server<#service> {
                        #svc_mod::#server::new(self.clone())
                    }
                    #[inline]
                    #[allow(unused_mut, dead_code)]
                    pub fn module(&self) -> Result<jsonrpsee_http_server::RpcModule<()>, jsonrpsee_core::Error> {
                        let mut module = jsonrpsee_http_server::RpcModule::new(());
                        #jrpc_tt
                        Ok(module)
                    }
                }

                #[tonic::async_trait]
                impl #svc_mod::#svc_trait for #service {
                    #grpc_tt
                }
            )
            .to_string(),
        );
    }
    contents.push_str(&codegen);
    File::create(rpc_path)
        .unwrap()
        .write_all(contents.as_bytes())
        .unwrap();

    contents.clear();
    File::open(lib_path)
        .unwrap()
        .read_to_string(&mut contents)
        .unwrap();
    codegen.push_str(&contents);

    codegen.parse().unwrap() // given to cxx codegen
}

fn change_types(file: syn::File) -> (HashMap<String, ItemStruct>, TokenStream, TokenStream) {
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

    (map, modified, copied)
}

fn apply_substitutions(
    mut gen: TokenStream,
    map: HashMap<String, ItemStruct>,
    methods: HashMap<String, Vec<Rpc>>,
) -> (
    TokenStream,
    TokenStream,
    HashMap<String, (TokenStream, TokenStream)>,
) {
    // FIXME: We don't have to regenerate if the struct only has scalar types
    // (in which case it'll have the same schema in both FFI and protobuf)
    let mut impls = quote! {
        use jsonrpsee_core::client::ClientT;
        use jsonrpsee_http_client::{HttpClient, HttpClientBuilder};
        use std::sync::Arc;
        use crate::{CLIENTS};
        use ain_evm::runtime::RUNTIME;
        use crate::rpc::*;
        #[allow(unused_imports)]
        use self::ffi::*;
        use ain_evm::handler::Handlers;
        #[derive(Clone)]
        pub struct Client {
            inner: Arc<HttpClient>,
            handle: tokio::runtime::Handle,
        }
        #[allow(non_snake_case)]
        fn NewClient(addr: &str) -> Result<Box<Client>, Box<dyn std::error::Error>> {
            if CLIENTS.read().unwrap().get(addr).is_none() {
                log::info!("Initializing RPC client for {}", addr);
                let c = Client {
                    inner: Arc::new(HttpClientBuilder::default().build(addr)?),
                    handle: RUNTIME.rt_handle.clone(),
                };
                CLIENTS.write().unwrap().insert(addr.into(), c);
            }

            Ok(Box::new(CLIENTS.read().unwrap().get(addr).unwrap().clone()))
        }
        #[allow(dead_code)]
        fn missing_param(field: &str) -> jsonrpsee_core::Error {
            jsonrpsee_core::Error::Call(jsonrpsee_types::error::CallError::Custom(
                jsonrpsee_types::ErrorObject::borrowed(-1, &format!("Missing required parameter '{field}'"), None).into_owned()
            ))
        }
    };
    let (mut funcs, mut sigs) = (quote!(), quote!());
    let mut calls = HashMap::new();
    for (_name, s) in &map {
        let mut copy_block_rs = quote!();
        let mut copy_block_ffi = quote!();
        let fields = match &s.fields {
            Fields::Named(ref f) => f,
            _ => unreachable!(),
        };

        for field in &fields.named {
            let name = &field.ident;
            let ty = &field.ty;
            let t = quote!(#ty).to_string().replace(' ', "");
            let (into_rs, into_ffi) = if t.contains("::core::option::") {
                (
                    quote!(Some(other.#name.into())),
                    quote!(other.#name.map(Into::into).unwrap_or_default()),
                )
            } else if t.contains("::alloc::vec::") {
                (
                    quote!(other.#name.into_iter().map(Into::into).collect()),
                    quote!(other.#name.into_iter().map(Into::into).collect()),
                )
            } else {
                (quote!(other.#name.into()), quote!(other.#name.into()))
            };

            copy_block_rs.extend(quote!(
                #name: #into_rs,
            ));
            copy_block_ffi.extend(quote!(
                #name: #into_ffi,
            ));
        }

        let name = &s.ident;
        impls.extend(quote!(
            impl From<ffi::#name> for super::types::#name {
                fn from(other: ffi::#name) -> Self {
                    super::types::#name {
                        #copy_block_rs
                    }
                }
            }

            impl From<super::types::#name> for ffi::#name {
                fn from(other: super::types::#name) -> Self {
                    ffi::#name {
                        #copy_block_ffi
                    }
                }
            }
        ));
    }

    let mut rpc = quote!();
    for (mod_name, mod_methods) in methods {
        let server_mod = calls.entry(mod_name).or_insert((quote!(), quote!()));
        for method in mod_methods {
            let (name, client_name, name_rs, ivar, ity, oty) = (
                Ident::new(&method.name, Span::call_site()),
                Ident::new(&format!("Call{}", &method.name), Span::call_site()),
                Ident::new(&method.name.to_snek_case(), Span::call_site()),
                Ident::new(
                    &method.input_ty.split('.').last().unwrap().to_snek_case(),
                    Span::call_site(),
                ),
                Ident::new(
                    method.input_ty.split('.').last().unwrap(),
                    Span::call_site(),
                ),
                Ident::new(
                    method.output_ty.split('.').last().unwrap(),
                    Span::call_site(),
                ),
            );
            let mut param_ffi = quote!();
            let (input_rs, input_ffi, client_ffi, client_params, into_ffi, call_ffi) =
                if method.input_ty == ".google.protobuf.Empty" {
                    (
                        quote!(&self, _request: tonic::Request<()>),
                        quote!(result: &mut #oty),
                        quote!(client: &Box<Client>),
                        quote! {
                            let params = jsonrpsee_core::rpc_params![];
                        },
                        quote!(),
                        quote!(),
                        // quote!(&mut out),
                    )
                } else {
                    let mut values = quote!();
                    param_ffi = quote! {
                        let mut #ivar = super::types::#ity::default();
                    };
                    let struct_name = ity.to_string();
                    let type_struct = map.get(&struct_name).unwrap();
                    match &type_struct.fields {
                        Fields::Named(ref f) => {
                            let mut extract_fields = quote!();
                            for (i, field) in f.named.iter().enumerate() {
                                let name = &field.ident;
                                let name_str = name.as_ref().unwrap().to_string();
                                let seq_extract = if let Some(value) = extract_default(field) {
                                    quote!(seq.next().unwrap_or(#value))
                                } else {
                                    quote!(seq.next().map_err(|_| missing_param(#name_str))?)
                                };
                                values.extend(quote!(
                                    &#ivar.#name
                                ));
                                if i < f.named.len() - 1 {
                                    values.extend(quote!(,));
                                }
                                extract_fields.extend(quote!(
                                    #ivar.#name = #seq_extract;
                                ));
                            }
                            param_ffi.extend(quote!(
                                if _params.is_object() {
                                    #ivar = _params.parse()?;
                                } else {
                                    let mut seq = _params.sequence();
                                    #extract_fields
                                }
                                let mut input = #ivar.into();
                            ));
                        }
                        _ => unreachable!(),
                    }

                    (
                        quote!(&self, request: tonic::Request<super::types::#ity>),
                        quote!(#ivar: &mut #ity, result: &mut #oty),
                        quote!(client: &Box<Client>, #ivar: #ity),
                        quote! {
                            let #ivar = super::types::#ity::from(#ivar);
                            let params = jsonrpsee_core::rpc_params![#values];
                        },
                        quote! { let input = request.into_inner().into(); },
                        quote!(input),
                        // quote!(input, &mut out),
                    )
                };

            if !method.server {
                server_mod.1.extend(quote!(
                    #[allow(unused_variables)]
                    async fn #name_rs(#input_rs) -> Result<tonic::Response<super::types::#oty>, tonic::Status> {
                        unimplemented!();
                    }
                ));
            }

            if method.server {
                rpc.extend(quote!(
                    fn #name(#input_ffi) -> Result<()>;
                ));
            }
            if method.client {
                sigs.extend(quote!(
                    #[allow(clippy::borrowed_box)]
                    fn #client_name(#client_ffi) -> Result<#oty>;
                ));
            }

            let rpc_name = method
                .url
                .as_ref()
                .map(String::from)
                .unwrap_or_else(|| to_eth_case(&method.name));
            if method.client {
                funcs.extend(quote! {
                    #[allow(non_snake_case)]
                    #[allow(clippy::borrowed_box)]
                    fn #client_name(#client_ffi) -> Result<ffi::#oty, Box<dyn std::error::Error>> {
                        let (tx, mut rx) = tokio::sync::mpsc::channel(1);
                        let c = client.inner.clone();
                        client.handle.spawn(async move {
                            #client_params
                            let resp: Result<super::types::#oty, _> = c.request(#rpc_name, params).await;
                            let _ = tx.send(resp).await;
                        });
                        Ok(rx.blocking_recv().unwrap().map(Into::into)?)
                    }
                });
            }
            if method.server {
                server_mod.0.extend(quote!(
                    let adapter = self.adapter.clone();
                    module.register_method(#rpc_name, move |_params, _| {
                        #param_ffi
                        // let mut out = ffi::#oty::default();
                        Self::#name(adapter.clone(), #call_ffi)
                        // #name(adapter.clone(), #call_ffi)
                            // .map(|_| out)
                    })?;
                ));
            }
            if method.server {
                server_mod.1.extend(quote!(
                        async fn #name_rs(#input_rs) -> Result<tonic::Response<super::types::#oty>, tonic::Status> {
                            let adapter = self.adapter.clone();
                            let result = tokio::task::spawn_blocking(move || {
                                // let out = ffi::#oty::default();
                                #into_ffi
                                Self::#name(adapter.clone(), #call_ffi).map_err(|e| tonic::Status::unknown(e.to_string()))
                                // #name(adapter.clone(), #call_ffi)
                                // .map(|_| out)
                                // .map_err(|e| tonic::Status::unknown(e.to_string()))
                            }).await
                            .map_err(|e| {
                                tonic::Status::unknown(format!("failed to invoke RPC call: {}", e))
                            })??;
                            Ok(tonic::Response::new(result.into()))
                        }
                    ));
            }
        }
    }

    impls.extend(quote!(
        #funcs
    ));

    gen.extend(quote!(
        extern "Rust" {
            type Client;
            fn NewClient(addr: &str) -> Result<Box<Client>>;
            #sigs
            // #rpc
        }
    ));

    (gen, impls, calls)
}

fn extract_default(field: &Field) -> Option<TokenStream> {
    let re = Regex::new("\\[default: (.*?)\\]").unwrap();
    for attr in &field.attrs {
        match attr.path.get_ident() {
            Some(ident) if ident == "doc" => {
                let comment = attr.tokens.to_string();
                if let Some(captures) = re.captures(&comment) {
                    return captures.get(1).unwrap().as_str().parse().ok();
                }
            }
            _ => (),
        }
    }

    None
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

/// Extracts "T" from std::option::Option<T> for example
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

fn main() {
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let proto_path = manifest_path.parent().unwrap().join("proto");
    let src_path = manifest_path.join("src");
    let gen_path = src_path.join("gen");
    std::fs::create_dir_all(&gen_path).unwrap();

    let methods = generate_from_protobuf(&proto_path, Path::new(&gen_path));
    let _tt = modify_codegen(
        methods,
        &Path::new(&gen_path).join("types.rs"),
        &Path::new(&gen_path).join("rpc.rs"),
        &src_path.join("lib.rs"),
    );

    println!(
        "cargo:rerun-if-changed={}",
        src_path.join("rpc.rs").to_string_lossy()
    );
}

extern crate proc_macro;

use proc_macro::TokenStream;
use quote::quote;
use syn::{
    parse_macro_input, Attribute, Data, DeriveInput, Fields, ItemFn, LitStr, ReturnType, Type,
};

#[proc_macro_attribute]
pub fn ffi_fallible(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemFn);
    let name = &input.sig.ident;
    let inputs = &input.sig.inputs;
    let output = &input.sig.output;

    let inner_type = match output {
        ReturnType::Type(_, type_box) => match &**type_box {
            Type::Path(type_path) => type_path.path.segments.last().and_then(|pair| {
                if let syn::PathArguments::AngleBracketed(angle_bracketed_args) = &pair.arguments {
                    angle_bracketed_args.args.first()
                } else {
                    None
                }
            }),
            _ => None,
        },
        _ => None,
    };

    let param_names: Vec<_> = inputs
        .iter()
        .filter_map(|arg| {
            if let syn::FnArg::Typed(pat_type) = arg {
                Some(&pat_type.pat)
            } else {
                None
            }
        })
        .collect();

    let expanded = quote! {
        pub fn #name(result: &mut ffi::CrossBoundaryResult, #inputs) -> #inner_type {
            #input

            match #name(#(#param_names),*) {
                Ok(success_value) => {
                    cross_boundary_success_return(result, success_value)
                }
                Err(err_msg) => {
                    cross_boundary_error_return(result, err_msg.to_string())
                }
            }
        }
    };

    TokenStream::from(expanded)
}

fn parse_repository_attr(attr: &Attribute) -> syn::Result<(String, String)> {
    let mut key_type = None;
    let mut value_type = None;

    attr.parse_nested_meta(|meta| {
        if meta.path.is_ident("K") {
            let val = meta.value()?;
            let s: LitStr = val.parse()?;
            key_type = Some(s);
        }
        if meta.path.is_ident("V") {
            let val = meta.value()?;
            let s: LitStr = val.parse()?;
            value_type = Some(s);
        }
        Ok(())
    })?;

    Ok((
        key_type.expect("Missing attribute 'K'").value(),
        value_type.expect("Missing attribute 'V'").value(),
    ))
}

#[proc_macro_derive(Repository, attributes(repository))]
pub fn repository_derive(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = &input.ident; // Struct name

    let mut key_type_str = String::new();
    let mut value_type_str = String::new();

    for attr in &input.attrs {
        if attr.path().is_ident("repository") {
            let (key, value) =
                parse_repository_attr(attr).expect("Error parsing 'repository' attribute");
            key_type_str = key;
            value_type_str = value;
        }
    }

    let key_type_ident = syn::Ident::new(&key_type_str, proc_macro2::Span::call_site());
    let value_type_ident = syn::Ident::new(&value_type_str, proc_macro2::Span::call_site());

    // Generate the implementation
    let expanded = quote! {
        impl RepositoryOps<#key_type_ident, #value_type_ident> for #name {
            type ListItem = std::result::Result<(#key_type_ident, #value_type_ident), ain_db::DBError>;

            fn get(&self, id: &#key_type_ident) -> Result<Option<#value_type_ident>> {
                Ok(self.col.get(id)?)
            }

            fn put(&self, id: &#key_type_ident, item: &#value_type_ident) -> Result<()> {
                Ok(self.col.put(id, item)?)
            }

            fn delete(&self, id: &#key_type_ident) -> Result<()> {
                Ok(self.col.delete(id)?)
            }

            fn list<'a>(&'a self, from: Option<#key_type_ident>, dir: crate::storage::SortOrder) -> Result<Box<dyn Iterator<Item = Self::ListItem> + 'a>> {
                let it = self.col.iter(from, dir.into())?;
                Ok(Box::new(it))
            }
        }
    };

    TokenStream::from(expanded)
}

#[proc_macro_attribute]
pub fn ocean_endpoint(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input = parse_macro_input!(item as ItemFn);
    let inputs = &input.sig.inputs;

    let name = &input.sig.ident;

    let output = &input.sig.output;
    let inner_type = match output {
        ReturnType::Type(_, type_box) => match &**type_box {
            Type::Path(type_path) => type_path.path.segments.last().and_then(|pair| {
                if let syn::PathArguments::AngleBracketed(angle_bracketed_args) = &pair.arguments {
                    angle_bracketed_args.args.first()
                } else {
                    None
                }
            }),
            _ => None,
        },
        _ => None,
    };

    let param_names: Vec<_> = inputs
        .iter()
        .filter_map(|arg| {
            if let syn::FnArg::Typed(pat_type) = arg {
                Some(&pat_type.pat)
            } else {
                None
            }
        })
        .collect();

    let expanded = quote! {
        pub async fn #name(axum::extract::OriginalUri(uri): axum::extract::OriginalUri, #inputs) -> std::result::Result<axum::Json<#inner_type>, ApiError> {
            #input

            match #name(#(#param_names),*).await {
                Err(e) => {
                let (status, message) = e.into_code_and_message();
                Err(ApiError::new(
                    status,
                    message,
                    uri.to_string()
                ))
            },
                Ok(v) => Ok(axum::Json(v))
            }
        }
    };

    TokenStream::from(expanded)
}
#[proc_macro_derive(ConsensusEncoding)]
pub fn consensus_encoding_derive(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = input.ident;

    let fields = if let Data::Struct(data) = input.data {
        if let Fields::Named(fields) = data.fields {
            fields
                .named
                .into_iter()
                .map(|f| f.ident)
                .collect::<Vec<_>>()
        } else {
            Vec::new()
        }
    } else {
        Vec::new()
    };

    let field_names = fields.iter().filter_map(|f| f.as_ref()).collect::<Vec<_>>();

    let expanded = quote! {
        bitcoin::impl_consensus_encoding!(#name, #(#field_names),*);
    };

    TokenStream::from(expanded)
}

#[proc_macro_attribute]
pub fn test_dftx_serialization(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let input_fn = parse_macro_input!(item as ItemFn);
    let fn_name = &input_fn.sig.ident;
    let test_name = fn_name.to_string().replace('_', "");

    let path = format!("./tests/data/{}.txt", &test_name[4..].to_lowercase());

    let fn_body = &input_fn.block;

    let output = quote! {
        fn #fn_name() {
            #fn_body
            let s = std::fs::read_to_string(#path).unwrap();
            for line in s.lines() {
                if line.starts_with("//") {
                    continue;
                }
                let l = line.split(' ').next().unwrap();
                let hex = &hex::decode(l).unwrap();

                let offset = 1 + match hex[1] {
                    0x4c => 2,
                    0x4d => 3,
                    0x4e => 4,
                    _ => 1,
                };

                let raw_tx = &hex[offset..];

                let dftx = bitcoin::consensus::deserialize::<ain_dftx::Stack>(&raw_tx).unwrap();
                let ser = bitcoin::consensus::serialize::<ain_dftx::Stack>(&dftx);
                assert_eq!(ser, raw_tx);
            }
        }
    };

    TokenStream::from(output)
}

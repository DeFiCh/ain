extern crate proc_macro;

use proc_macro::TokenStream;
use quote::quote;
use syn::{
    parse_macro_input, parse_quote, Attribute, DeriveInput, Expr, FnArg, ItemFn, LitStr,
    ReturnType, Type,
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
            fn get(&self, id: &#key_type_ident) -> Result<Option<#value_type_ident>> {
                Ok(self.col.get(id)?)
            }

            fn put(&self, id: &#key_type_ident, item: &#value_type_ident) -> Result<()> {
                Ok(self.col.put(id, item)?)
            }

            fn delete(&self, id: &#key_type_ident) -> Result<()> {
                Ok(self.col.delete(id)?)
            }

            fn list<'a>(&'a self, from: Option<#key_type_ident>) -> Result<Box<dyn Iterator<Item = std::result::Result<(#key_type_ident, #value_type_ident), ain_db::DBError>> + 'a>>
            {
                let it = self.col.iter(from)?;
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

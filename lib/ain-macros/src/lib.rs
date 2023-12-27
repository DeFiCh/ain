extern crate proc_macro;

use proc_macro::TokenStream;
use quote::quote;
use syn::{parse_macro_input, Attribute, DeriveInput, ItemFn, LitStr, ReturnType, Type};

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

fn parse_repository_attr(attr: &Attribute) -> syn::Result<(String, String, String)> {
    let mut key_type = None;
    let mut value_type = None;
    let mut column_type = None;

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
        if meta.path.is_ident("Column") {
            let val = meta.value()?;
            let s: LitStr = val.parse()?;
            column_type = Some(s);
        }
        Ok(())
    })?;

    Ok((
        key_type.expect("Missing attribute 'K'").value(),
        value_type.expect("Missing attribute 'V'").value(),
        column_type.expect("Missing attribute 'column'").value(),
    ))
}

#[proc_macro_derive(Repository, attributes(repository))]
pub fn repository_derive(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let name = &input.ident; // Struct name

    let mut key_type_str = String::new();
    let mut value_type_str = String::new();
    let mut column_type_str = String::new();

    for attr in &input.attrs {
        if attr.path().is_ident("repository") {
            let (key, value, column) =
                parse_repository_attr(attr).expect("Error parsing 'repository' attribute");
            key_type_str = key;
            value_type_str = value;
            column_type_str = column;
        }
    }

    let key_type_ident = syn::Ident::new(&key_type_str, proc_macro2::Span::call_site());
    let value_type_ident = syn::Ident::new(&value_type_str, proc_macro2::Span::call_site());
    let column_type_ident = syn::Ident::new(&column_type_str, proc_macro2::Span::call_site());
    println!("column_type_ident : {:?}", column_type_ident);
    // Generate the implementation
    let expanded = quote! {
        impl RepositoryOps<#key_type_ident, #value_type_ident> for #name {
            fn get(&self, id: #key_type_ident) -> Result<Option<#value_type_ident>> {
                Ok(self.store.get::<columns::#column_type_ident>(id)?)
            }

            fn put(&self, id: &#key_type_ident, item: &#value_type_ident) -> Result<()> {
                Ok(self.store.put::<columns::#column_type_ident>(id, item)?)
            }

            fn delete(&self, id: &#key_type_ident) -> Result<()> {
                Ok(self.store.delete::<columns::#column_type_ident>(id)?)
            }

            fn list(&self, from: Option<#key_type_ident>, limit: usize) -> Result<Vec<(#key_type_ident, #value_type_ident)>> {
                Ok(self.store.list::<columns::#column_type_ident>(from, limit)?)
            }
        }
    };

    TokenStream::from(expanded)
}

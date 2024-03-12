extern crate proc_macro;

use proc_macro::TokenStream;
use quote::quote;
use syn::{parse_macro_input, ItemFn, ReturnType, Type};

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

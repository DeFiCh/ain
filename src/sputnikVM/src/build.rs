use proc_macro2::TokenStream;
use substrate_build_script_utils::{generate_cargo_keys, rerun_if_git_head_changed};

use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::path::PathBuf;

fn main() {
	generate_cargo_keys();

	rerun_if_git_head_changed();

	let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
	let parent = root.clone();
	let lib_path = &parent.join("src").join("ffi.rs");
	let target_dir = &root.join("target");

	let mut content = String::new();
	File::open(lib_path)
		.unwrap()
		.read_to_string(&mut content)
		.unwrap();
	let tt: TokenStream = content.parse().unwrap();
	let mut opt = cxx_gen::Opt::default();
	opt.include.push(cxx_gen::Include {
		path: "libevm.h".to_string(),
		kind: cxx_gen::IncludeKind::Bracketed,
	});
	let codegen = cxx_gen::generate_header_and_cc(tt, &opt).unwrap();

	let cpp_stuff = String::from_utf8(codegen.implementation).unwrap();
	File::create(target_dir.join("libevm.h"))
		.unwrap()
		.write_all(&codegen.header)
		.unwrap();
	File::create(target_dir.join("libevm.cpp"))
		.unwrap()
		.write_all(cpp_stuff.as_bytes())
		.unwrap();
}

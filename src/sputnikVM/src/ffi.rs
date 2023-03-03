#[cxx::bridge]
mod ffi {
	extern "Rust" {
		fn hello_sputnik_vm();
	}
}

#[inline]
fn hello_sputnik_vm() {
	println!("Running sputnikVM");
}

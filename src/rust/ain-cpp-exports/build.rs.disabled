fn main() {
    cxx_build::bridge("src/lib.rs")
        .include("../../../../depends/x86_64-pc-linux-gnu/include/")
        .include("../../../")
        .include("../../../leveldb/include/")
        .include("../../../univalue/include/")
        .flag_if_supported("-std=c++17")
        .flag_if_supported("-Wno-unused-parameter")
        .compile("ain-evm-cpp-ffi");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=../../../masternodes/evm_ffi.h");
}

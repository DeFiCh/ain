package=native_clang
$(package)_version=16
$(package)_long_version=$($(package)_version).0.0
$(package)_download_path=https://github.com/llvm/llvm-project/releases/download/llvmorg-$($(package)_long_version)
ifneq (,$(findstring aarch64,$(BUILD)))
$(package)_file_name=clang+llvm-$($(package)_long_version)-aarch64-linux-gnu.tar.xz
$(package)_sha256_hash=b750ba3120e6153fc5b316092f19b52cf3eb64e19e5f44bd1b962cb54a20cf0a
else
$(package)_file_name=clang+llvm-$($(package)_long_version)-x86_64-linux-gnu-ubuntu-18.04.tar.xz
$(package)_sha256_hash=2b8a69798e8dddeb57a186ecac217a35ea45607cb2b3cf30014431cff4340ad1
endif

define $(package)_preprocess_cmds
  rm -f $($(package)_extract_dir)/lib/libc++abi.so*
endef

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib/clang/$($(package)_version)/include && \
  mkdir -p $($(package)_staging_prefix_dir)/bin && \
  mkdir -p $($(package)_staging_prefix_dir)/include/llvm-c && \
  cp bin/clang $($(package)_staging_prefix_dir)/bin/ && \
  cp -P bin/clang++ $($(package)_staging_prefix_dir)/bin/ && \
  cp bin/dsymutil $($(package)_staging_prefix_dir)/bin/$(host)-dsymutil && \
  cp bin/llvm-config $($(package)_staging_prefix_dir)/bin/ && \
  cp include/llvm-c/ExternC.h $($(package)_staging_prefix_dir)/include/llvm-c && \
  cp include/llvm-c/lto.h $($(package)_staging_prefix_dir)/include/llvm-c && \
  cp lib/libLTO.so $($(package)_staging_prefix_dir)/lib/ && \
  cp -r lib/clang/$($(package)_version)/include/* $($(package)_staging_prefix_dir)/lib/clang/$($(package)_version)/include/
endef
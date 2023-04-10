package=native_cctools
$(package)_version=f28fb5e9c31efd3d0552afcce2d2c03cae25c1ca
$(package)_download_path=https://github.com/tpoechtrager/cctools-port/archive
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=4a1359b6a79738b375b39ae05852712a77ff24d7ef2a498e99d35de78fff42c7
$(package)_build_subdir=cctools
$(package)_clang_version=15
$(package)_clang_long_version=15.0.6
$(package)_clang_download_path=https://github.com/llvm/llvm-project/releases/download/llvmorg-$($(package)_clang_long_version)
$(package)_clang_download_file=clang+llvm-$($(package)_clang_long_version)-x86_64-linux-gnu-ubuntu-18.04.tar.xz
$(package)_clang_file_name=clang-llvm-$($(package)_clang_long_version)-x86_64-linux-gnu-ubuntu-18.04.tar.xz
$(package)_clang_sha256_hash=38bc7f5563642e73e69ac5626724e206d6d539fbef653541b34cae0ba9c3f036

$(package)_libtapi_version=b7b5bdbfda9e8062d405b48da3b811afad98ae76
$(package)_libtapi_download_path=https://github.com/tpoechtrager/apple-libtapi/archive
$(package)_libtapi_download_file=$($(package)_libtapi_version).tar.gz
$(package)_libtapi_file_name=$($(package)_libtapi_version).tar.gz
$(package)_libtapi_sha256_hash=577b86f5729f24dc10eba48995363cffd5d62bb0804c8051e1c1a2f08a710737

$(package)_extra_sources=$($(package)_clang_file_name)
$(package)_extra_sources += $($(package)_libtapi_file_name)

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_clang_download_path),$($(package)_clang_download_file),$($(package)_clang_file_name),$($(package)_clang_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_libtapi_download_path),$($(package)_libtapi_download_file),$($(package)_libtapi_file_name),$($(package)_libtapi_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_clang_sha256_hash)  $($(package)_source_dir)/$($(package)_clang_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_libtapi_sha256_hash)  $($(package)_source_dir)/$($(package)_libtapi_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir -p toolchain/bin toolchain/lib/clang/$($(package)_clang_version)/include && \
  mkdir -p libtapi && \
  tar --no-same-owner --strip-components=1 -C libtapi -xf $($(package)_source_dir)/$($(package)_libtapi_file_name) && \
  tar --no-same-owner --strip-components=1 -C toolchain -xf $($(package)_source_dir)/$($(package)_clang_file_name) && \
  rm -f toolchain/lib/libc++abi.so* && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source)
endef

define $(package)_set_vars
  $(package)_config_opts=--target=$(host) --disable-lto-support --with-libtapi=$($(package)_extract_dir)
  $(package)_ldflags+=-Wl,-rpath=\\$$$$$$$$\$$$$$$$$ORIGIN/../lib
  $(package)_cc=$($(package)_extract_dir)/toolchain/bin/clang
  $(package)_cxx=$($(package)_extract_dir)/toolchain/bin/clang++
endef

# Note: Works around 2 bugs:
# - DESTDIR isn't passed on in libtapi port. This patches that.
# - DESTDIR behavior is buggy, likely due to symlinks involved in the tree and also
#   different behaviors on headers and lib install.
#   - Attempts to install to absolute path sometimes, and sometimes in the tree with nested prefix.
#   - Workaround for now: 
#       - DESTDIR tames it into the tree, despite having the nested prefix.
#       - Then, just do a move to the right location where autoconf later expects it
define $(package)_preprocess_cmds
  sed -i.old 's/$$MAKE install-libtapi/$$MAKE DESTDIR=\"$$$$INSTALLPREFIX\" install-libtapi/' ./libtapi/install.sh && \
  CC=$($(package)_cc) CXX=$($(package)_cxx) INSTALLPREFIX="$($(package)_extract_dir)" ./libtapi/build.sh && \
  CC=$($(package)_cc) CXX=$($(package)_cxx) INSTALLPREFIX="$($(package)_extract_dir)" ./libtapi/install.sh && \
  sed -i.old "/define HAVE_PTHREADS/d" $($(package)_build_subdir)/ld64/src/ld/InputFiles.h && \
  mv "$($(package)_extract_dir)/$($(package)_extract_dir)/include" $($(package)_extract_dir)/ && \
  mv "$($(package)_extract_dir)/$($(package)_extract_dir)/lib" $($(package)_extract_dir)/
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install && \
  mkdir -p $($(package)_staging_prefix_dir)/lib/ && \
  cd $($(package)_extract_dir) && \
  cp lib/libtapi.so* $($(package)_staging_prefix_dir)/lib/ && \
  cd $($(package)_extract_dir)/toolchain && \
  mkdir -p $($(package)_staging_prefix_dir)/lib/clang/$($(package)_clang_version)/include && \
  mkdir -p $($(package)_staging_prefix_dir)/bin $($(package)_staging_prefix_dir)/include && \
  cp bin/clang $($(package)_staging_prefix_dir)/bin/ &&\
  cp -P bin/clang++ $($(package)_staging_prefix_dir)/bin/ &&\
  cp lib/libLTO.so $($(package)_staging_prefix_dir)/lib/ && \
  cp -rf lib/clang/$($(package)_clang_version)/include/* $($(package)_staging_prefix_dir)/lib/clang/$($(package)_clang_version)/include/ && \
  cp bin/dsymutil $($(package)_staging_prefix_dir)/bin/$(host)-dsymutil
endef


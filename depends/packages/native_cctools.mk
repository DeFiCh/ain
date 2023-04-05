package=native_cctools
$(package)_version=f28fb5e9c31efd3d0552afcce2d2c03cae25c1ca
$(package)_download_path=https://github.com/tpoechtrager/cctools-port/archive
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=4a1359b6a79738b375b39ae05852712a77ff24d7ef2a498e99d35de78fff42c7
$(package)_build_subdir=cctools
$(package)_dependencies=native_libtapi

define $(package)_set_vars
  $(package)_config_opts=--target=$(host) --enable-lto-support
  $(package)_config_opts+=--with-llvm-config=$(llvm_config_prog)
  $(package)_ldflags+=-Wl,-rpath=\\$$$$$$$$\$$$$$$$$ORIGIN/../lib
  $(package)_cc=$(clang_prog)
  $(package)_cxx=$(clangxx_prog)
endef

ifneq ($(strip $(FORCE_USE_SYSTEM_CLANG)),)
define $(package)_preprocess_cmds
  mkdir -p $($(package)_staging_prefix_dir)/lib && \
  cp $(llvm_lib_dir)/libLTO.so $($(package)_staging_prefix_dir)/lib/ && \
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub cctools
endef
else
define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub cctools
endef
endif

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share
endef
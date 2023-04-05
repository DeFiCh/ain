package=native_libtapi
$(package)_version=b7b5bdbfda9e8062d405b48da3b811afad98ae76
$(package)_download_path=https://github.com/tpoechtrager/apple-libtapi/archive
$(package)_file_name=$($(package)_version).tar.gz
$(package)_sha256_hash=577b86f5729f24dc10eba48995363cffd5d62bb0804c8051e1c1a2f08a710737

ifeq ($(strip $(FORCE_USE_SYSTEM_CLANG)),)
$(package)_dependencies=native_clang
endif

define $(package)_build_cmds
  CC=$(clang_prog) CXX=$(clangxx_prog) INSTALLPREFIX=$($(package)_staging_prefix_dir) ./build.sh
endef

define $(package)_stage_cmds
  ./install.sh
endef
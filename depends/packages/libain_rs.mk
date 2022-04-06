package=libain_rs
$(package)_version=master
$(package)_download_path=https://github.com/DeFiCh/libain-rs/archive/
$(package)_git_path=https://github.com/DeFiCh/libain-rs
# $(package)_file_name=$($(package)_version).tar.gz
# $(package)_sha256_hash=0019dfc4b32d63c1392aa264aed2253c1e0c2fb09216f8e2cc269bbfb8bb49b5
$(package)_dependencies=native_rust

define $(package)_set_vars
$(package)_build_env+=RUSTC=$(build_prefix)/bin/rustc
$(package)_build_env+=CARGO=$(build_prefix)/bin/cargo
$(package)_build_env+=CBINDGEN=$(build_prefix)/bin/cbindgen
$(package)_build_env+=CARGO_HOME=$(build_prefix)
endef

define $(package)_fetch_cmds
git clone --single-branch --branch @jouzo/linking-to-ain $$($(package)_git_path) $$($(package)_build_dir)$(package)
endef

# override default extract
define $(package)_extract_cmds
echo "extracted"
endef

define $(package)_build_cmds
cd $$($(package)_build_dir)$(package) && \
$($(package)_build_env) $(build_prefix)/bin/cargo install cbindgen && \
$($(package)_build_env) $(MAKE) build-pkg
endef

define $(package)_stage_cmds
mkdir -p $($(package)_staging_dir)$(host_prefix)/wasm && \
mkdir -p $($(package)_staging_dir)$(host_prefix)/lib && \
cp $($(package)_build_dir)$(package)/pkg/modules-wasm/*.wasm $($(package)_staging_dir)$(host_prefix)/wasm/ && \
cp -r $($(package)_build_dir)$(package)/pkg/runtime-cpp/* $($(package)_staging_dir)$(host_prefix)/
endef

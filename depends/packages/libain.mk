package=libain
$(package)_version=master
$(package)_download_path=https://github.com/DeFiCh/libain-rs/archive/
$(package)_git_path=git@github.com:DeFiCh/libain-rs

define $(package)_fetch_cmds
git clone --single-branch --branch wallet $$($(package)_git_path) $$($(package)_extract_dir)
endef

define $(package)_extract_cmds
echo "Skipping extraction for Git repo"
endef

define $(package)_build_cmds
	$(MAKE) build-grpc-pkg
endef

define $(package)_stage_cmds
	cp -r pkg/ain-grpc/* $($(package)_staging_dir)$(host_prefix)/
endef

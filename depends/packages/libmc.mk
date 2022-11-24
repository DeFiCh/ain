package=libmc
$(package)_version=master
$(package)_git_path=https://github.com/DeFiCh/metachain

define $(package)_fetch_cmds
	if [ ! -z ${LIBMC_PATH} ]; then \
		echo "Using local path for metachain"; \
		mkdir -p $$(base_build_dir)/$$(host)/$(package); \
		ln -s $(LIBMC_PATH) $$($(package)_extract_dir); \
	else \
		echo "Cloning from Git repository for metachain"; \
		git clone --single-branch --branch libmc $$($(package)_git_path) $$($(package)_extract_dir); \
	fi
endef

define $(package)_extract_cmds
echo "Skipping extraction for metachain source"
endef

# Perform target substitutions for Rust
LIBMC_TARGET := $(HOST)
ifeq ($(LIBMC_TARGET),x86_64-pc-linux-gnu)
	LIBMC_TARGET=x86_64-unknown-linux-gnu
endif
ifeq ($(LIBMC_TARGET),arm-linux-gnueabihf)
	LIBMC_TARGET=arm-unknown-linux-gnueabihf
endif
ifeq ($(LIBMC_TARGET),x86_64-apple-darwin18)
	LIBMC_TARGET=aarch64-apple-darwin
endif
ifeq ($(LIBMC_TARGET),x86_64-w64-mingw32)
	LIBMC_TARGET=x86_64-pc-windows-gnu
endif

define $(package)_build_cmds
	$(MAKE) build-native-pkg TARGET=$(LIBMC_TARGET)
endef

define $(package)_stage_cmds
	cp -r pkg/metachain/* $($(package)_staging_dir)$(host_prefix)/
endef

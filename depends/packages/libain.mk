package=libain
$(package)_version=master
$(package)_git_path=https://github.com/DeFiCh/libain-rs

define $(package)_fetch_cmds
	if [ ! -z ${LIBAIN_PATH} ]; then \
		echo "Using local path for libain-rs"; \
		mkdir -p $$($(package)_extract_dir); \
		cp -r $(LIBAIN_PATH)/* $$($(package)_extract_dir)/; \
	else \
		echo "Cloning from Git repository for libain-rs"; \
		git clone --single-branch --branch amount $$($(package)_git_path) $$($(package)_extract_dir); \
	fi
endef

define $(package)_extract_cmds
echo "Skipping extraction for libain source"
endef

# Perform target substitutions for Rust
LIBAIN_TARGET := $(HOST)
ifeq ($(LIBAIN_TARGET),x86_64-pc-linux-gnu)
	LIBAIN_TARGET=x86_64-unknown-linux-gnu
endif
ifeq ($(LIBAIN_TARGET),arm-linux-gnueabihf)
	LIBAIN_TARGET=arm-unknown-linux-gnueabihf
endif
ifeq ($(LIBAIN_TARGET),x86_64-apple-darwin18)
	LIBAIN_TARGET=x86_64-apple-darwin
endif
ifeq ($(LIBAIN_TARGET),x86_64-w64-mingw32)
	LIBAIN_TARGET=x86_64-pc-windows-gnu
endif

define $(package)_build_cmds
	$(MAKE) build-core-pkg TARGET=$(LIBAIN_TARGET)
endef

define $(package)_stage_cmds
	cp -r pkg/ain-core/* $($(package)_staging_dir)$(host_prefix)/
endef

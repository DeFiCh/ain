CARGO_HOME ?= 
RUSTUP_HOME ?= 
CARGO := $(if $(CARGO),$(CARGO),cargo)

# RUST_TARGET is set from configure
TARGET = $(if $(RUST_TARGET),$(RUST_TARGET),x86_64-unknown-linux-gnu)

# This is set from configure
RUST_DEBUG ?= 
# We use DEBUG as well, since that's the more conventional
# setup. Either of them set will result in debug builds
DEBUG ?= $(RUST_DEBUG)

TARGET_DIR ?= $(abs_builddir)/target
SPECIFIC_TARGET_DIR = $(TARGET_DIR)/$(TARGET)/$(if $(DEBUG),debug,release)
CARGO_MANIFEST_PATH = $(abs_srcdir)/Cargo.toml

# We're resetting DESTDIR so that nested autotools based builds
# don't end up in unexpected places (Currently protobuf-src is affected)

.PHONY:
all: 
	DESTDIR= TARGET_DIR=$(TARGET_DIR) \
	RUSTUP_HOME=$(RUSTUP_HOME) \
    CARGO_HOME=$(CARGO_HOME) \
    $(CARGO) build \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" \
		$(if $(DEBUG),,--release) \
		$(if $(TARGET),--target $(TARGET),) && \
	cp $(SPECIFIC_TARGET_DIR)/libain_rs_exports.a $(TARGET_DIR)/lib/

.PHONY:
clean-local:
	RUSTUP_HOME=$(RUSTUP_HOME) \
    CARGO_HOME=$(CARGO_HOME) \
    $(CARGO) clean \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" && \
	rm -rf $(TARGET_DIR)/{include,lib,src}

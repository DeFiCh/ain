CARGO ?= cargo

# RUST_TARGET is set from configure
TARGET ?= $(RUST_TARGET)
TARGET ?= x86_64-unknown-linux-gnu

# This is set from configure
RUST_DEBUG ?= 
# We use DEBUG as well, since that's the more conventional
# setup. Either of them set will result in debug builds
DEBUG ?= $(RUST_DEBUG)

TARGET_DIR ?= $(abs_builddir)/target
SPECIFIC_TARGET_DIR = $(TARGET_DIR)/$(TARGET)/$(if $(DEBUG),debug,release)
CARGO_MANIFEST_PATH = $(abs_srcdir)/Cargo.toml

.PHONY:
all: 
	TARGET_DIR=$(TARGET_DIR) $(CARGO) build \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" \
		$(if $(DEBUG),,--release) \
		$(if $(TARGET),--target $(TARGET),) && \
	cp $(SPECIFIC_TARGET_DIR)/libain_rs_exports.a $(TARGET_DIR)/lib/

.PHONY:
clean-local:
	$(CARGO) clean \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" && \
	rm -rf $(TARGET_DIR)/{include,lib,src}

CARGO := $(if $(CARGO),$(CARGO),cargo)

# RUST_TARGET is set from configure. Can be empty. If it is we skip
# setting target. This is done to emulate cargo's default behavior where
# it puts things into `target/` directly where no `--target <triplet>`
# is set. If set, it uses `target/<triplet>` and there's no way to force
# cargo to always just stick to one.
# 
# If it's empty, this collapses dir for SPECIFIC_TARGET_DIR to retain
# cargo's behavior
TARGET = $(if $(RUST_TARGET),$(RUST_TARGET),)

# This is set from configure
ENABLE_DEBUG ?= 
# We use DEBUG as well, since that's the more conventional
# setup. Either of them set will result in debug builds
DEBUG ?= $(ENABLE_DEBUG)

TARGET_DIR ?= $(abs_builddir)/target
SPECIFIC_TARGET_DIR = $(TARGET_DIR)/$(TARGET)/$(if $(DEBUG),debug,release)
CARGO_MANIFEST_PATH = $(abs_srcdir)/Cargo.toml

# We're resetting DESTDIR so that nested autotools based builds
# don't end up in unexpected places (Currently protobuf-src is affected)

.PHONY:
build: 
	DESTDIR= TARGET_DIR="$(TARGET_DIR)" \
	CC="$(CC)" CXX="$(CXX)" \
	CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" CPPFLAGS="$(CPPFLAGS)" \
	LDFLAGS="$(LDFLAGS)" AR="$(AR)" NM="$(NM)" RANLIB="$(RANLIB)" \
	PROTOC="$(PROTOC)" PROTOC_INCLUDE_DIR="$(PROTOC_INCLUDE_DIR)" \
	$(CARGO) build \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" \
		$(if $(DEBUG),,--release) \
		$(if $(TARGET),--target $(TARGET),) && \
	cp $(SPECIFIC_TARGET_DIR)/libain_rs_exports.a $(TARGET_DIR)/lib/

check:
	DESTDIR= TARGET_DIR="$(TARGET_DIR)" \
	CC="$(CC)" CXX="$(CXX)" \
	CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" CPPFLAGS="$(CPPFLAGS)" \
	LDFLAGS="$(LDFLAGS)" AR="$(AR)" NM="$(NM)" RANLIB="$(RANLIB)" \
	PROTOC="$(PROTOC)" PROTOC_INCLUDE_DIR="$(PROTOC_INCLUDE_DIR)" \
	$(CARGO) test \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" \
		$(if $(TARGET),--target $(TARGET),)

clippy:
	DESTDIR= TARGET_DIR="$(TARGET_DIR)" \
	CC="$(CC)" CXX="$(CXX)" \
	CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" CPPFLAGS="$(CPPFLAGS)" \
	LDFLAGS="$(LDFLAGS)" AR="$(AR)" NM="$(NM)" RANLIB="$(RANLIB)" \
	PROTOC="$(PROTOC)" PROTOC_INCLUDE_DIR="$(PROTOC_INCLUDE_DIR)" \
	$(CARGO) clippy \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" \
		$(if $(TARGET),--target $(TARGET),)

.PHONY:
clean-local:
	$(CARGO) clean \
		--manifest-path "$(CARGO_MANIFEST_PATH)" \
		--target-dir "$(TARGET_DIR)" && \
	rm -rf $(TARGET_DIR)/{include,lib,src}

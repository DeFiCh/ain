CARGO := $(if $(CARGO),$(CARGO),cargo)

# ENABLE_DEBUG is set from configure. We respect DEBUG too if set
DEBUG ?= $(ENABLE_DEBUG)

# RUST_TARGET and HAVE_RUST_TARGET is set from configure. 
# Can be empty. If empty, we skip export it. Otherwise we do. 
#  
# We do this to emulate cargo's default behavior (--target=<triplet>) where
# it puts things into `target/` directly when not given or `target/<triplet>`.
# There's no way to force cargo to a stick to one behavior at the moment.
# 
# Note: We use HAVE checks due to automake ifeq/ifneq not working outside rules
if HAVE_RUST_TARGET
export CARGO_BUILD_TARGET = $(RUST_TARGET)
endif
TARGET = $(RUST_TARGET)

export CARGO_TARGET_DIR ?= $(abs_builddir)/target
export CARGO_BUILD_JOBS ?= $(if $(JOBS),$(JOBS),-1)

CARGO_MANIFEST_PATH = $(abs_srcdir)/Cargo.toml
SPECIFIC_TARGET_DIR = $(CARGO_TARGET_DIR)/$(TARGET)/$(if $(DEBUG),debug,release)

CARGO_MANIFEST_ARG = --manifest-path "$(CARGO_MANIFEST_PATH)"
CARGO_BUILD_TYPE_ARG = $(if $(DEBUG),,--release)

# If $build == $host, we add --all-targets otherwise we skip
# since compiling with `--test` fails on cross compilation
CARGO_EXTRA_ARGS ?= $(if $(subst $(build),,$(host)),,--all-targets)

# Export compiler flags
export CC CXX CFLAGS CXXFLAGS CPPFLAGS LDFLAGS AR NM RANLIB
# Export compiler support
export PKG_CONFIG_PATH PKGCONFIG_LIBDIR PYTHONPATH
# Export protoc vars
export PROTOC PROTOC_INCLUDE_DIR

# Ensure nested autotools calls by cargo don't end up in unexpected places 
unexport DESTDIR

.PHONY:
all: build

.PHONY: build
build:
	$(CARGO) build $(CARGO_MANIFEST_ARG) $(CARGO_BUILD_TYPE_ARG) $(CARGO_EXTRA_ARGS) && \
	cp $(SPECIFIC_TARGET_DIR)/libain_rs_exports.a $(CARGO_TARGET_DIR)/lib/

.PHONY: check
check:
	$(CARGO) check $(CARGO_MANIFEST_ARG) $(CARGO_EXTRA_ARGS)

.PHONY: test
test:
	$(CARGO) test $(CARGO_MANIFEST_ARG) $(CARGO_EXTRA_ARGS)

.PHONY: clippy
clippy:
	$(CARGO) clippy $(CARGO_MANIFEST_ARG) $(CARGO_EXTRA_ARGS)

.PHONY: fmt-check
fmt-check:
	$(CARGO) fmt $(CARGO_MANIFEST_ARG) --all --check

.PHONY: fmt
fmt:
	$(CARGO) fmt $(CARGO_MANIFEST_ARG) --all

clean-local:
	$(CARGO) clean $(CARGO_MANIFEST_ARG) && \
	rm -rf $(CARGO_TARGET_DIR)/{include,lib,src}

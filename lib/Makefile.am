CARGO := $(if $(CARGO),$(CARGO),cargo)
CARGO_EXTRA_ARGS ?= --all-targets

export CARGO_BUILD_JOBS := $(if $(CARGO_BUILD_JOBS),$(CARGO_BUILD_JOBS),-1)

# ENABLE_DEBUG is set from configure. We respect DEBUG too if set
DEBUG ?= $(ENABLE_DEBUG)

# CARGO_BUILD_TARGET and HAVE_CARGO_BUILD_TARGET is set from configure. 
# Can be empty. If empty, we skip export it. Otherwise we do. 
#  
# We do this to emulate cargo's default behavior (--target=<triplet>) where
# it puts things into `target/` directly when not given or `target/<triplet>`.
# There's no way to force cargo to a stick to one behavior at the moment.
# 
# Note: We use HAVE checks due to automake ifeq/ifneq not working outside rules
if HAVE_CARGO_BUILD_TARGET
export CARGO_BUILD_TARGET := $(CARGO_BUILD_TARGET)
endif

export CARGO_TARGET_DIR ?= $(abs_builddir)/target
CARGO_MANIFEST_PATH = $(abs_srcdir)/Cargo.toml
SPECIFIC_TARGET_DIR = $(CARGO_TARGET_DIR)/$(CARGO_BUILD_TARGET)/$(if $(DEBUG),debug,release)

CARGO_MANIFEST_ARG = --manifest-path "$(CARGO_MANIFEST_PATH)"
CARGO_BUILD_TYPE_ARG = $(if $(DEBUG),,--release)

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

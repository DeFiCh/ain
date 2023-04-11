SHELL := /bin/bash
CARGO_TOOLCHAIN ?= stable
CARGO_ARGS ?=
CARGO ?= cargo
LIB_ROOT_DIR := $(dir $(realpath ../$(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(LIB_ROOT_DIR)/target

STAMP := $(BUILD_DIR)/.stamp

# On the makefile that calls it, we'll ensure to default it 0.
# This makes for easier dev workflow since except for the final build and
# select scenarios, debug builds are the more common workflow
export DEBUG ?= 1
export LIB_TARGET_DIR := $(if $(findstring 1,$(DEBUG)),$(BUILD_DIR)/debug,$(BUILD_DIR)/release)

.ONESHELL:

.PHONY:
default:

.PHONY: release
release: DEBUG=0
release: clean build

.PHONY: debug
debug: DEBUG=1
debug: build

# For dev workflow
.PHONY: prepare
prepare: fmt check clippy

.PHONY: clean
clean: 
	@$(call cargo,clean)
	@rm -f $(STAMP)

.PHONY: build
build: 
	@$(call cargo,build)

.PHONY: run
run: 
	@$(call cargo,run)

.PHONY: check
check: 
	@$(call cargo,check)

.PHONY: test
test: 
	@$(call cargo,test)

.PHONY: fmt
fmt: 
	@$(call cargo,fmt)

.PHONY: clippy
clippy: 
	@$(call cargo,clippy)

$(STAMP): $(ALL_CARGO_SRC) 
	@touch $@

define cargo =
set -e;
cargo_args="$(CARGO_ARGS)";
if [[ "$(DEBUG)" != "1" ]]; then cargo_args="--release $(CARGO_ARGS)"; fi;
$(CARGO) +$(CARGO_TOOLCHAIN) $(1) $${cargo_args}
endef

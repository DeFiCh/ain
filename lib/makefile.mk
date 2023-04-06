SHELL := /bin/bash
CARGO_TOOLCHAIN ?= stable
CARGO_ARGS ?=
CARGO ?= cargo

# On the makefile that calls it, we'll ensure to default it 0.
# This makes for easier dev workflow
export DEBUG ?= 1

.ONESHELL:

.PHONY: default
default: build

.PHONY: release
release: DEBUG=0
release: clean build

.PHONY: debug
debug: DEBUG=1
debug: build

.PHONY: dev
dev: DEBUG=1
dev: run

.PHONY: clean
clean:
	@$(call cargo,clean)

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
check: 
	@$(call cargo,test)

.PHONY: fmt
fmt: 
	$(CARGO) +$(CARGO_TOOLCHAIN) fmt

.PHONY: clippy
clippy: 
	$(CARGO) +$(CARGO_TOOLCHAIN) clippy

# For dev workflow
.PHONY: prepare
prepare: check fmt clippy


define cargo =
set -e;
cargo_args="$(CARGO_ARGS)";
if [[ "$(DEBUG)" != "1" ]]; then cargo_args="--release $(CARGO_ARGS)"; fi;
$(CARGO) +$(CARGO_TOOLCHAIN) $(1) $${cargo_args}
endef
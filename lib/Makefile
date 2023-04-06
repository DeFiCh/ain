SHELL := /bin/bash
SUBDIRS := $(wildcard ain-*/.)
SUB_TARGETS := build debug clean check test fmt clippy

.ONESHELL:

.PHONY: $(SUB_TARGETS)
$(SUB_TARGETS): $(SUBDIRS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

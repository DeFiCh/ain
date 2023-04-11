SHELL := /bin/bash
SUBDIRS := $(wildcard ain-*/.)
SUB_TARGETS := build debug clean check test fmt clippy prepare
CARGO ?= 
TARGET ?= 

.ONESHELL:

.PHONY:
version:
 cargo version

.PHONY: $(SUB_TARGETS)
$(SUB_TARGETS): $(SUBDIRS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

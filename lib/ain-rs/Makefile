ALL_CARGO_SRC = Cargo.toml $(shell find . -type f -name '*.rs')

include ../makefile.mk

LIB_AIN_RS := $(LIB_TARGET_DIR)/libain_rs.rlib

$(LIB_AIN_RS): $(STAMP)
	$(MAKE) build
	@touch $@

.PHONY:
libain_rs: $(LIB_AIN_RS)

default: libain_rs
CARGO ?= cargo
TARGET ?=

ifeq ($(TARGET), x86_64-pc-linux-gnu)
	TARGET="x86_64-unknown-linux-gnu"
endif

ifeq ($(TARGET), arm-linux-gnueabihf)
	TARGET="arm-unknown-linux-gnueabihf"
endif

ifeq ($(TARGET), x86_64-w64-mingw32)
	TARGET="x86_64-pc-windows-gnu"
endif

ifeq ($(TARGET), x86_64-apple-darwin18)
	ARCH=$(shell uname -s)
	ifeq ($(ARCH), arm64)
			TARGET="aarch64-apple-darwin"
	else
			TARGET="x86_64-apple-darwin"
	endif
endif

all: build-evm-pkg build-grpc-pkg

build-evm-pkg:
	CRATE_CC_NO_DEFAULTS=1 $(CARGO) build --package ain-evm-ffi --release $(if $(TARGET),--target $(TARGET),)
	mkdir -p include
	cp target/$(if $(TARGET),$(TARGET)/,)release/libain_evm_ffi.a libain_evm_ffi.a
	cp target/libain_evm.h include/
	cp target/libain_evm.cpp libain_evm.cpp

build-grpc-pkg:
	CRATE_CC_NO_DEFAULTS=1 $(CARGO) build --package ain-grpc --release $(if $(TARGET),--target $(TARGET),)

clean:
	$(CARGO) clean
	rm -rf include
	rm -rf libain_evm_ffi.a libain_evm.cpp

distclean: clean

.PHONY: all

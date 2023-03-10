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
  $(info $(ARCH))
  ifeq ($(ARCH), arm64)
      TARGET="aarch64-apple-darwin"
  else
      TARGET="x86_64-apple-darwin"
  endif
endif

$(info $(TARGET))

all:
	CRATE_CC_NO_DEFAULTS=1 $(CARGO) build --release $(if $(TARGET),--target $(TARGET),)
	mkdir -p include
	cp target/$(if $(TARGET),$(TARGET)/,)release/libevm.a  libevm.a
	cp target/libevm.h include/
	cp target/libevm.cpp libevm.cpp

clean:
	$(CARGO) clean
	rm -rf include
	rm libevm.a libevm.cpp


define translate_to_rust_target
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
endef
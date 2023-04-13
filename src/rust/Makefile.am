CARGO ?= cargo
TARGET ?=

RUST_TARGET ?= x86_64-unknown-linux-gnu

.PHONY:
all: build-evm-pkg build-grpc-pkg

.PHONY:
build-evm-pkg: build-grpc-pkg
	@cd $(abs_srcdir) && \
	CRATE_CC_NO_DEFAULTS=1 BUILD_DIR="$(abs_builddir)" $(CARGO) build --package ain-evm-ffi \
		--release $(if $(RUST_TARGET),--target $(RUST_TARGET),) --target-dir $(abs_builddir)/target && \
	mkdir -p $(abs_builddir)/{include,lib,src} && \
	cp $(abs_builddir)/target/$(RUST_TARGET)/release/libain_evm_ffi.a $(abs_builddir)/lib/ && \
	cp $(abs_builddir)/libain_evm.h $(abs_builddir)/include/ && \
	cp $(abs_builddir)/libain_evm.cpp $(abs_builddir)/src/libain_evm.cpp

.PHONY:
build-grpc-pkg:
	@cd $(abs_srcdir) && \
	CRATE_CC_NO_DEFAULTS=1 BUILD_DIR="$(abs_builddir)" $(CARGO) build --package ain-grpc \
		--release $(if $(RUST_TARGET),--target $(RUST_TARGET),) --target-dir $(abs_builddir)

.PHONY:
clean-local:
	@cd $(abs_srcdir) && \
	$(CARGO) clean --target-dir $(abs_builddir) && \
	rm -rf {include,lib,src}

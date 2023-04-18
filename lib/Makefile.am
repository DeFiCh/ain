CARGO ?= cargo

RUST_TARGET ?= x86_64-unknown-linux-gnu

.PHONY:
all: build-ain-rs-exports build-ain-grpc


# TODO: assumes release builds atm
.PHONY:
build-ain-rs-exports: build-ain-grpc
	@cd $(abs_srcdir) && \
	CRATE_CC_NO_DEFAULTS=1 BUILD_DIR="$(abs_builddir)" $(CARGO) build --package ain-rs-exports \
		--release $(if $(RUST_TARGET),--target $(RUST_TARGET),) --target-dir $(abs_builddir)/target && \
	mkdir -p $(abs_builddir)/gen/{include,lib,src} && \
	cp $(abs_builddir)/target/$(RUST_TARGET)/release/libain_rs_exports.a $(abs_builddir)/gen/lib/ && \
	cp $(abs_builddir)/ain_rs_exports.h $(abs_builddir)/gen/include/ && \
	cp $(abs_builddir)/ain_rs_exports.cpp $(abs_builddir)/gen/src/

.PHONY:
build-ain-grpc:
	@cd $(abs_srcdir) && \
	CRATE_CC_NO_DEFAULTS=1 BUILD_DIR="$(abs_builddir)" $(CARGO) build --package ain-grpc \
		--release $(if $(RUST_TARGET),--target $(RUST_TARGET),) --target-dir $(abs_builddir)

.PHONY:
clean-local:
	@cd $(abs_srcdir) && \
	$(CARGO) clean --target-dir $(abs_builddir) && \
	rm -rf $(abs_builddir)/gen
package=rust
$(package)_version=1.25.2
$(package)_download_path=https://github.com/rust-lang/rustup/archive
$(package)_sha256_hash=dc9bb5d3dbac5cea9afa9b9c3c96fcf644a1e7ed6188a6b419dfe3605223b5f3
$(package)_file_name=$($(package)_version).tar.gz

# Note we don't support arm that doesn't have hf. So any armv7 or below
# wihtout hard floats, we just ignore it
define $(package)_set_vars
  $(package)_ROOT="$($(package)_staging_dir)/$(host_prefix)"
  $(package)_RUSTUP_HOME="$($(package)_staging_dir)/$(host_prefix)/.rustup"
  $(package)_CARGO_HOME="$($(package)_staging_dir)/$(host_prefix)/"

  ifeq ($(host_os)-$(host_arch),linux-x86_64)
    $(package)_target=x86_64-unknown-linux-gnu
  endif 
  ifeq ($(host_os)-$(host_arch),linux-aarch64)
    $(package)_target=aarch64-unknown-linux-gnu
  endif
  ifeq ($(host_os)-$(host_arch),linux-arm)
    $(package)_target=armv7-unknown-linux-gnueabihf
  endif
  ifeq ($(host_os)-$(host_arch),darwin-x86_64)
    $(package)_target=x86_64-apple-darwin
  endif
  ifeq ($(host_os)-$(host_arch),darwin-arm)
    $(package)_target=aarch64-apple-darwin
  endif
endef

# We don't limit at the moment
# define $(package)_preprocess_cmds
#   test "x$($(package)_target)" = "x" && \
#     echo "Unsupported host platform: $(HOST)" && \
#     exit 1 || exit 0
# endef

# This autoinstalls the build os target
define $(package)_build_cmds
  RUSTUP_HOME="$($(package)_RUSTUP_HOME)" \
  CARGO_HOME="$($(package)_CARGO_HOME)" \
  ./rustup-init.sh --no-modify-path -y
endef

# We add the host os target
# Calling rustup target add with the current installed again to ensure if
# the target var is empty, the command still succeeds
define $(package)_stage_cmds
    RUSTUP_HOME="$($(package)_RUSTUP_HOME)" \
    CARGO_HOME="$($(package)_CARGO_HOME)" \
    $($(package)_CARGO_HOME)/bin/rustup target add \
      $$($($(package)_CARGO_HOME)/bin/rustup target list --installed) \
      $($(package)_target)
endef

define $(package)_postprocess_cmds
  mkdir -p $($(package)_ROOT)/share/cargo && \
  mv $($(package)_ROOT)/env $($(package)_ROOT)/share/cargo/env && \
  sed -i'' 's#$($(package)_staging_dir)/##' $($(package)_ROOT)/share/cargo/env
endef


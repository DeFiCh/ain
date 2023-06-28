package=protobuf
$(package)_version=23.3
$(package)_download_path=https://github.com/protocolbuffers/protobuf/releases/download/v$($(package)_version)/

# NOTE: protoc gets invoked on the BUILD OS during compile. So, we don't
# care about HOST OS, unlike all other dependencies. 
# That is: protoc is run on the BUILD OS, not targeted to run on the 
# HOST OS.

ifeq ($(build_os)-$(build_arch),linux-x86_64)
  $(package)_file_name=protoc-$($(package)_version)-linux-x86_64.zip
  $(package)_sha256_hash=8f5abeb19c0403a7bf6e48f4fa1bb8b97724d8701f6823a327922df8cc1da4f5
endif
ifeq ($(build_os)-$(build_arch),linux-aarch64)
  $(package)_file_name=protoc-$($(package)_version)-linux-aarch_64.zip
  $(package)_sha256_hash=4e5154e51744c288ca1362f9cca942188003fc7b860431a984a30cb1e73aed9e
endif
ifeq ($(build_os),darwin)
  $(package)_file_name=protoc-$($(package)_version)-osx-universal_binary.zip
  $(package)_sha256_hash=2783d50e35cbb546c0d75fbc2a46f76e2c620fb66c9f6bfc5ff70f5dda234100
endif

ifeq ($($(package)_file_name),)
    $(error Unsupported build platform: $(BUILD))
endif

define $(package)_extract_cmds
  mkdir -p $$($(package)_extract_dir) && echo "$$($(package)_sha256_hash)  $$($(package)_source)" > $$($(package)_extract_dir)/.$$($(package)_file_name).hash &&  $(build_SHA256SUM) -c $$($(package)_extract_dir)/.$$($(package)_file_name).hash && unzip $$($(package)_source)
endef

define $(package)_set_vars
  $(package)_ROOT="$($(package)_staging_dir)/$(host_prefix)"
endef

define $(package)_preprocess_cmds
  rm -f readme.txt
endef

define $(package)_build_cmds
  mkdir -p $($(package)_ROOT) && \
  mv bin include $($(package)_ROOT)/
endef


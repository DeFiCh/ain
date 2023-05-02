package=protobuf
$(package)_version=22.2
$(package)_download_path=https://github.com/protocolbuffers/protobuf/releases/download/v22.2/

# NOTE: protoc gets invoked on the BUILD OS during compile. So, we don't
# care about HOST OS, unlike all other dependencies

# NOTE: We likley don't need this at all. But just keeping this until it's
# merged into master so we can use it if we ever need it later. 
# Meanwhile, protobuf is compiled as a part of rust dep.

# TODO: Fill up hashes later
ifeq ($(build_os)-$(build_arch),linux-x86_64)
  $(package)_file_name=protoc-$($(package)_version)-linux-x86_64.zip
  $(package)_sha256_hash=15f281b36897e0ffbbe3a02f687ff9108c7a0f98bb653fb433e4bd62e698abe7
endif
ifeq ($(build_os)-$(build_arch),linux-aarch64)
  $(package)_file_name=protoc-$($(package)_version)-linux-aarch_64.zip
  $(package)_sha256_hash=
endif
ifeq ($(build_os)-$(build_arch),darwin-x86_64)
  $(package)_file_name=protoc-$($(package)_version)-osx-x86_64.zip
  $(package)_sha256_hash=
endif
ifeq ($(build_os)-$(build_arch),darwin-aarch64)
  $(package)_file_name=protoc-$($(package)_version)-osx-aarch_64.zip
  $(package)_sha256_hash=
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


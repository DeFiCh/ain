package=protobuf
$(package)_version=22.2
$(package)_download_path=https://github.com/protocolbuffers/protobuf/releases/download/v22.2/

# NOTE: protoc gets invoked on the BUILD OS during compile. So, we don't
# care about HOST OS, unlike all other dependencies. 
# That is: protoc is run on the BUILD OS, not targeted to run on the 
# HOST OS.

# TODO: Fill up hashes later
ifeq ($(build_os)-$(build_arch),linux-x86_64)
  $(package)_file_name=protoc-$($(package)_version)-linux-x86_64.zip
  $(package)_sha256_hash=15f281b36897e0ffbbe3a02f687ff9108c7a0f98bb653fb433e4bd62e698abe7
endif
ifeq ($(build_os)-$(build_arch),linux-aarch64)
  $(package)_file_name=protoc-$($(package)_version)-linux-aarch_64.zip
  $(package)_sha256_hash=aa2efbb2d481b7ad3c2428e0aa4dd6d813e4538e6c0a1cd4d921ac998187e07e
endif
ifeq ($(build_os)-$(build_arch),darwin-x86_64)
  $(package)_file_name=protoc-$($(package)_version)-osx-x86_64.zip
  $(package)_sha256_hash=8bb75680c376190d960ef1d073618c1103960f70dc4fafa7bde872029562aec1
endif
ifeq ($(build_os)-$(build_arch),darwin-arm)
  $(package)_file_name=protoc-$($(package)_version)-osx-aarch_64.zip
  $(package)_sha256_hash=a196fd39acd312688b58e81266fd88e9f7799967c5439738c10345a29049261d
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


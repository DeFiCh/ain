package=solc
$(package)_version=0.8.20
$(package)_download_path=https://github.com/ethereum/solidity/releases/download/v$($(package)_version)/

# NOTE: solc gets invoked on the BUILD OS during compile. So, we don't
# care about HOST OS, unlike all other dependencies.
# That is: solc is run on the BUILD OS, not targeted to run on the
# HOST OS.

ifeq ($(build_os)-$(build_arch),linux-x86_64)
  $(package)_file_name=solc-static-linux
  $(package)_sha256_hash=0479d44fdf9c501c25337fdc540419f1593b884a87b47f023da4f1c700fda782
endif
ifeq ($(build_os),darwin)
  $(package)_file_name=solc-macos
  $(package)_sha256_hash=fc329945e0068e4e955d0a7b583776dc8d25e72ab657a044618a7ce7dd0519aa
endif
ifeq ($(build_os),windows)
  $(package)_file_name=solc-windows.exe
  $(package)_sha256_hash=a0fa8eb77805c530cfb6962f400643dbe83991387d27b319efd6b89482061946
endif

ifeq ($($(package)_file_name),)
    $(error Unsupported build platform: $(BUILD))
endif

define $(package)_extract_cmds
  mkdir -p $$($(package)_extract_dir) && echo "$$($(package)_sha256_hash)  $$($(package)_source)" > $$($(package)_extract_dir)/.$$($(package)_file_name).hash &&  $(build_SHA256SUM) -c $$($(package)_extract_dir)/.$$($(package)_file_name).hash && cp $$($(package)_source) solc
endef

define $(package)_set_vars
  $(package)_ROOT="$($(package)_staging_dir)/$(host_prefix)/bin"
endef

define $(package)_build_cmds
  mkdir -p $($(package)_ROOT) && \
  echo $(PWD) && \
  chmod +x solc && \
  mv solc $($(package)_ROOT)
endef


package=boost

# Official version hashes
# 71: d73a8da01e8bf8c7eda40b4c84915071a8c8a0df4a6734537ddde4a8580524ee
# 72: 59c9b274bc451cf91a9ba1dd2c7fdcaf5d60b1b3aa83f2c9fa143417cc660722
# 73: 4eb3b8d442b426dc35346235c8733b5ae35ba431690e38c6a8263dce9fcbb402
# 74: 83bfc1507731a0906e387fc28b7ef5417d591429e51e788417fe9ff025e116b1
# 75: 953db31e016db7bb207f11432bef7df100516eeb746843fa0486a222e3fd49cb
# 76: f0397ba6e982c4450f27bf32a2a83292aba035b827a5623a14636ea583318c41
# 77: fc9f85fc030e233142908241af7a846e60630aa7388de9a5fafb1f3a26840854
# 78: 8681f175d4bdb26c52222665793eef08490d7758529330f98d3b29dd0735bccc

$(package)_version=1_77_0
$(package)_download_path=https://boostorg.jfrog.io/artifactory/main/release/$($(package)_version)/source/
$(package)_file_name=boost_$($(package)_version).tar.bz2
$(package)_sha256_hash=fc9f85fc030e233142908241af7a846e60630aa7388de9a5fafb1f3a26840854
$(package)_dependencies=native_b2

define $(package)_set_vars
$(package)_config_opts_release=variant=release
$(package)_config_opts_debug=variant=debug
$(package)_config_opts=--layout=tagged --build-type=complete --user-config=user-config.jam
$(package)_config_opts+=threading=multi link=static -sNO_COMPRESSION=1
$(package)_config_opts_linux=target-os=linux threadapi=pthread runtime-link=shared
$(package)_config_opts_darwin=target-os=darwin runtime-link=shared
$(package)_config_opts_mingw32=target-os=windows binary-format=pe threadapi=win32 runtime-link=static
$(package)_config_opts_x86_64=architecture=x86 address-model=64
$(package)_config_opts_i686=architecture=x86 address-model=32
ifneq (,$(findstring clang,$($(package)_cxx)))
$(package)_toolset_$(host_os)=clang
else
$(package)_toolset_$(host_os)=gcc
endif
$(package)_config_libraries=filesystem,system,test
$(package)_cxxflags+=-std=c++17
$(package)_cxxflags_linux=-fPIC
$(package)_cxxflags_x86_64_darwin=-fcf-protection=full
endef

define $(package)_preprocess_cmds
  echo "using $($(package)_toolset_$(host_os)) : : $($(package)_cxx) : <cflags>\"$($(package)_cflags)\" <cxxflags>\"$($(package)_cxxflags)\" <compileflags>\"$($(package)_cppflags)\" <linkflags>\"$($(package)_ldflags)\" <archiver>\"$($(package)_ar)\" <striper>\"$(host_STRIP)\"  <ranlib>\"$(host_RANLIB)\" <rc>\"$(host_WINDRES)\" : ;" > user-config.jam
endef

define $(package)_config_cmds
  ./bootstrap.sh --without-icu --with-libraries=$($(package)_config_libraries) --with-toolset=$($(package)_toolset_$(host_os)) --with-bjam=b2
endef

define $(package)_build_cmds
  b2 -d2 -j2 -d1 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) toolset=$($(package)_toolset_$(host_os)) stage
endef

define $(package)_stage_cmds
  b2 -d0 -j4 --prefix=$($(package)_staging_prefix_dir) $($(package)_config_opts) toolset=$($(package)_toolset_$(host_os)) --no-cmake-config install
endef

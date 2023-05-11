OSX_MIN_VERSION=10.15
OSX_SDK_VERSION=11.0
XCODE_VERSION=12.2
XCODE_BUILD_ID=12B45b
LD64_VERSION=609

OSX_SDK=$(SDK_PATH)/Xcode-$(XCODE_VERSION)-$(XCODE_BUILD_ID)-extracted-SDK-with-libcxx-headers

SILENCED_WARNINGS=-Wno-unused-command-line-argument -Wno-deprecated-non-prototype -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-variable
DARWIN_SHAREDCC_FLAGS=-target $(host) -mmacosx-version-min=$(OSX_MIN_VERSION) --sysroot $(OSX_SDK) -mlinker-version=$(LD64_VERSION) $(SILENCED_WARNINGS)

darwin_CC=clang $(DARWIN_SHAREDCC_FLAGS)
darwin_CXX=clang++ -stdlib=libc++ $(DARWIN_SHAREDCC_FLAGS)

darwin_CFLAGS=-pipe
darwin_CXXFLAGS=$(darwin_CFLAGS)

darwin_release_CFLAGS=-O2
darwin_release_CXXFLAGS=$(darwin_release_CFLAGS)

darwin_debug_CFLAGS=-O1
darwin_debug_CXXFLAGS=$(darwin_debug_CFLAGS)

darwin_native_toolchain=native_cctools

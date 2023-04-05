packages:=boost libevent

rapidcheck_packages = rapidcheck

wallet_packages=bdb

zmq_packages=zeromq

upnp_packages=miniupnpc

darwin_native_packages = 
ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools native_libtapi

ifeq ($(strip $(FORCE_USE_SYSTEM_CLANG)),)
darwin_native_packages+= native_clang
endif

endif
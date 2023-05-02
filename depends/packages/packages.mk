packages := boost libevent rust

rapidcheck_packages = rapidcheck
wallet_packages = bdb
zmq_packages = zeromq
upnp_packages = miniupnpc

darwin_native_packages = 
ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools
endif

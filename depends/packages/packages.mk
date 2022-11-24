packages:=boost libevent

rapidcheck_packages = rapidcheck

wallet_packages=bdb

zmq_packages=zeromq

upnp_packages=miniupnpc

libain_packages = libain

libmc_packages = libmc

darwin_native_packages = native_biplist native_ds_store native_mac_alias

ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools native_cdrkit native_libdmg-hfsplus
endif

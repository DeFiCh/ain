packages:=boost libevent

rapidcheck_packages = rapidcheck

wallet_packages=bdb

zmq_packages=zeromq

upnp_packages=miniupnpc

libain_packages=libain_rs

darwin_native_packages = native_biplist native_ds_store native_mac_alias

$(host_arch)_$(host_os)_native_packages += native_b2
$(host_arch)_$(host_os)_native_packages += native_rust

ifneq ($(build_os),darwin)
darwin_native_packages += native_cctools native_cdrkit native_libdmg-hfsplus
endif

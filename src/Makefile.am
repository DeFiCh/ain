# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

DIST_SUBDIRS = secp256k1 univalue

AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS) $(GPROF_LDFLAGS) $(SANITIZER_LDFLAGS)
AM_CXXFLAGS = $(DEBUG_CXXFLAGS) $(HARDENED_CXXFLAGS) $(WARN_CXXFLAGS) $(NOWARN_CXXFLAGS) $(ERROR_CXXFLAGS) $(GPROF_CXXFLAGS) $(SANITIZER_CXXFLAGS)
AM_CPPFLAGS = $(DEBUG_CPPFLAGS) $(HARDENED_CPPFLAGS)
AM_LIBTOOLFLAGS = --preserve-dup-deps
EXTRA_LIBRARIES =

if EMBEDDED_UNIVALUE
LIBUNIVALUE = univalue/libunivalue.la

$(LIBUNIVALUE): $(wildcard univalue/lib/*) $(wildcard univalue/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)
else
LIBUNIVALUE = $(UNIVALUE_LIBS)
endif

LIBSPV=spv/libspv.a
$(LIBSPV):
	$(MAKE) -C spv lib
SPV_CFLAGS=-I. -I./spv -I./spv/support -I./spv/bitcoin -I./spv/bcash
# -I./secp256k1

DEFI_INCLUDES=-I$(builddir) $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

DEFI_INCLUDES += -I$(srcdir)/secp256k1/include
DEFI_INCLUDES += $(UNIVALUE_CFLAGS)
DEFI_INCLUDES += $(SPV_CFLAGS)

LIBDEFI_SERVER=libdefi_server.a
LIBDEFI_COMMON=libdefi_common.a
LIBDEFI_CONSENSUS=libdefi_consensus.a
LIBDEFI_CLI=libdefi_cli.a
LIBDEFI_UTIL=libdefi_util.a
LIBDEFI_CRYPTO_BASE=crypto/libdefi_crypto_base.a
LIBDEFIQT=qt/libdefiqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBDEFI_ZMQ=libdefi_zmq.a
endif
if BUILD_DEFI_LIBS
LIBDEFICONSENSUS=libdeficonsensus.la
endif
if ENABLE_WALLET
LIBDEFI_WALLET=libdefi_wallet.a
LIBDEFI_WALLET_TOOL=libdefi_wallet_tool.a
endif

LIBDEFI_CRYPTO= $(LIBDEFI_CRYPTO_BASE)
if ENABLE_SSE41
LIBDEFI_CRYPTO_SSE41 = crypto/libdefi_crypto_sse41.a
LIBDEFI_CRYPTO += $(LIBDEFI_CRYPTO_SSE41)
endif
if ENABLE_AVX2
LIBDEFI_CRYPTO_AVX2 = crypto/libdefi_crypto_avx2.a
LIBDEFI_CRYPTO += $(LIBDEFI_CRYPTO_AVX2)
endif
if ENABLE_SHANI
LIBDEFI_CRYPTO_SHANI = crypto/libdefi_crypto_shani.a
LIBDEFI_CRYPTO += $(LIBDEFI_CRYPTO_SHANI)
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*.h) $(wildcard secp256k1/src/*.c) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBDEFI_CRYPTO) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_COMMON) \
  $(LIBDEFI_CONSENSUS) \
  $(LIBDEFI_SERVER) \
  $(LIBDEFI_CLI) \
  $(LIBDEFI_WALLET) \
  $(LIBDEFI_WALLET_TOOL) \
  $(LIBDEFI_ZMQ)

lib_LTLIBRARIES = $(LIBDEFICONSENSUS)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_DEFID
  bin_PROGRAMS += defid
endif

if BUILD_DEFI_CLI
  bin_PROGRAMS += defi-cli
endif
if BUILD_DEFI_TX
  bin_PROGRAMS += defi-tx
endif
if ENABLE_WALLET
if BUILD_DEFI_WALLET
  bin_PROGRAMS += defi-wallet
endif
endif

.PHONY: FORCE check-symbols check-security
# defi core #
DEFI_CORE_H = \
  addrdb.h \
  addrman.h \
  attributes.h \
  banman.h \
  base58.h \
  bech32.h \
  bloom.h \
  blockencodings.h \
  blockfilter.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkqueue.h \
  clientversion.h \
  coins.h \
  compat.h \
  compat/assumptions.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/tx_check.h \
  consensus/tx_verify.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  flatfile.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  index/base.h \
  index/blockfilterindex.h \
  index/txindex.h \
  indirectmap.h \
  init.h \
  interfaces/chain.h \
  interfaces/handler.h \
  interfaces/node.h \
  interfaces/wallet.h \
  key.h \
  key_io.h \
  dbwrapper.h \
  limitedmap.h \
  logging.h \
  masternodes/anchors.h \
  masternodes/masternodes.h \
  masternodes/mn_checks.h \
  masternodes/mn_txdb.h \
  memusage.h \
  merkleblock.h \
  miner.h \
  net.h \
  net_permissions.h \
  net_processing.h \
  netaddress.h \
  netbase.h \
  netmessagemaker.h \
  node/coin.h \
  node/coinstats.h \
  node/psbt.h \
  node/transaction.h \
  noui.h \
  optional.h \
  outputtype.h \
  policy/feerate.h \
  policy/fees.h \
  policy/policy.h \
  policy/rbf.h \
  policy/settings.h \
  pos.h \
  pos_kernel.h \
  protocol.h \
  psbt.h \
  random.h \
  reverse_iterator.h \
  reverselock.h \
  rpc/blockchain.h \
  rpc/client.h \
  rpc/protocol.h \
  rpc/rawtransaction_util.h \
  rpc/register.h \
  rpc/request.h \
  rpc/server.h \
  rpc/util.h \
  scheduler.h \
  script/descriptor.h \
  script/keyorigin.h \
  script/sigcache.h \
  script/sign.h \
  script/signingprovider.h \
  script/standard.h \
  shutdown.h \
  spv/btctransaction.h \
  spv/spv_wrapper.h \
  streams.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  threadsafety.h \
  threadinterrupt.h \
  timedata.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  undo.h \
  util/bip32.h \
  util/bytevectorhash.h \
  util/error.h \
  util/fees.h \
  util/system.h \
  util/memory.h \
  util/moneystr.h \
  util/rbf.h \
  util/string.h \
  util/threadnames.h \
  util/time.h \
  util/translation.h \
  util/url.h \
  util/validation.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  versionbitsinfo.h \
  walletinitinterface.h \
  wallet/coincontrol.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/feebumper.h \
  wallet/fees.h \
  wallet/ismine.h \
  wallet/load.h \
  wallet/psbtwallet.h \
  wallet/rpcwallet.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  wallet/wallettool.h \
  wallet/walletutil.h \
  wallet/coinselection.h \
  warnings.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h \
  zmq/zmqrpc.h


obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libdefi_util_a-clientversion.$(OBJEXT): obj/build.h

# server: shared between defid and defi-qt
# Contains code accessing mempool and chain state that is meant to be separated
# from wallet and gui code (see node/README.md). Shared code should go in
# libdefi_common or libdefi_util libraries, instead.
libdefi_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libdefi_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_server_a_SOURCES = \
  addrdb.cpp \
  addrman.cpp \
  banman.cpp \
  blockencodings.cpp \
  blockfilter.cpp \
  chain.cpp \
  consensus/tx_verify.cpp \
  flatfile.cpp \
  httprpc.cpp \
  httpserver.cpp \
  index/base.cpp \
  index/blockfilterindex.cpp \
  index/txindex.cpp \
  interfaces/chain.cpp \
  interfaces/node.cpp \
  init.cpp \
  dbwrapper.cpp \
  masternodes/anchors.cpp \
  masternodes/masternodes.cpp \
  masternodes/mn_checks.cpp \
  masternodes/mn_txdb.cpp \
  masternodes/mn_rpc.cpp \
  miner.cpp \
  net.cpp \
  net_processing.cpp \
  node/coin.cpp \
  node/coinstats.cpp \
  node/psbt.cpp \
  node/transaction.cpp \
  noui.cpp \
  policy/fees.cpp \
  policy/rbf.cpp \
  policy/settings.cpp \
  pos.cpp \
  pos_kernel.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/server.cpp \
  script/sigcache.cpp \
  shutdown.cpp \
  spv/btctransaction.cpp \
  spv/spv_wrapper.cpp \
  spv/spv_rpc.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  ui_interface.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  $(DEFI_CORE_H)

if ENABLE_WALLET
libdefi_server_a_SOURCES += wallet/init.cpp
endif
if !ENABLE_WALLET
libdefi_server_a_SOURCES += dummywallet.cpp
endif

if ENABLE_ZMQ
libdefi_zmq_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES) $(ZMQ_CFLAGS)
libdefi_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp \
  zmq/zmqrpc.cpp
endif


# wallet: shared between defid and defi-qt, but only linked
# when wallet enabled
libdefi_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
libdefi_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_wallet_a_SOURCES = \
  interfaces/wallet.cpp \
  wallet/coincontrol.cpp \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/feebumper.cpp \
  wallet/fees.cpp \
  wallet/ismine.cpp \
  wallet/load.cpp \
  wallet/psbtwallet.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  wallet/walletutil.cpp \
  wallet/coinselection.cpp \
  $(DEFI_CORE_H)

libdefi_wallet_tool_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
libdefi_wallet_tool_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_wallet_tool_a_SOURCES = \
  wallet/wallettool.cpp \
  $(DEFI_CORE_H)

# crypto primitives library
crypto_libdefi_crypto_base_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libdefi_crypto_base_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libdefi_crypto_base_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha_poly_aead.h \
  crypto/chacha_poly_aead.cpp \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hkdf_sha256_32.cpp \
  crypto/hkdf_sha256_32.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/poly1305.h \
  crypto/poly1305.cpp \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h \
  crypto/siphash.cpp \
  crypto/siphash.h

if USE_ASM
crypto_libdefi_crypto_base_a_SOURCES += crypto/sha256_sse4.cpp
endif

crypto_libdefi_crypto_sse41_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libdefi_crypto_sse41_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libdefi_crypto_sse41_a_CXXFLAGS += $(SSE41_CXXFLAGS)
crypto_libdefi_crypto_sse41_a_CPPFLAGS += -DENABLE_SSE41
crypto_libdefi_crypto_sse41_a_SOURCES = crypto/sha256_sse41.cpp

crypto_libdefi_crypto_avx2_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libdefi_crypto_avx2_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libdefi_crypto_avx2_a_CXXFLAGS += $(AVX2_CXXFLAGS)
crypto_libdefi_crypto_avx2_a_CPPFLAGS += -DENABLE_AVX2
crypto_libdefi_crypto_avx2_a_SOURCES = crypto/sha256_avx2.cpp

crypto_libdefi_crypto_shani_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libdefi_crypto_shani_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libdefi_crypto_shani_a_CXXFLAGS += $(SHANI_CXXFLAGS)
crypto_libdefi_crypto_shani_a_CPPFLAGS += -DENABLE_SHANI
crypto_libdefi_crypto_shani_a_SOURCES = crypto/sha256_shani.cpp

# consensus: shared between all executables that validate any consensus rules.
libdefi_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
libdefi_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_consensus_a_SOURCES = \
  amount.h \
  arith_uint256.cpp \
  arith_uint256.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/tx_check.cpp \
  consensus/validation.h \
  hash.cpp \
  hash.h \
  prevector.h \
  primitives/block.cpp \
  primitives/block.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  pubkey.cpp \
  pubkey.h \
  script/deficonsensus.cpp \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  span.h \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  util/strencodings.cpp \
  util/strencodings.h \
  version.h

# common: shared between defid, and defi-qt and non-server tools
libdefi_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
libdefi_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_common_a_SOURCES = \
  base58.cpp \
  bech32.cpp \
  bloom.cpp \
  chainparams.cpp \
  coins.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  key.cpp \
  key_io.cpp \
  merkleblock.cpp \
  netaddress.cpp \
  netbase.cpp \
  net_permissions.cpp \
  outputtype.cpp \
  policy/feerate.cpp \
  policy/policy.cpp \
  protocol.cpp \
  psbt.cpp \
  rpc/rawtransaction_util.cpp \
  rpc/util.cpp \
  scheduler.cpp \
  script/descriptor.cpp \
  script/sign.cpp \
  script/signingprovider.cpp \
  script/standard.cpp \
  versionbitsinfo.cpp \
  warnings.cpp \
  $(DEFI_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libdefi_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
libdefi_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_util_a_SOURCES = \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  fs.cpp \
  interfaces/handler.cpp \
  logging.cpp \
  random.cpp \
  rpc/request.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  util/bip32.cpp \
  util/bytevectorhash.cpp \
  util/error.cpp \
  util/fees.cpp \
  util/system.cpp \
  util/moneystr.cpp \
  util/rbf.cpp \
  util/threadnames.cpp \
  util/strencodings.cpp \
  util/string.cpp \
  util/time.cpp \
  util/url.cpp \
  util/validation.cpp \
  $(DEFI_CORE_H)

if GLIBC_BACK_COMPAT
libdefi_util_a_SOURCES += compat/glibc_compat.cpp
AM_LDFLAGS += $(COMPAT_LDFLAGS)
endif

# cli: shared between defi-cli and defi-qt
libdefi_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
libdefi_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_cli_a_SOURCES = \
  rpc/client.cpp \
  $(DEFI_CORE_H)

nodist_libdefi_util_a_SOURCES = $(srcdir)/obj/build.h
#

# defid binary #
defid_SOURCES = defid.cpp
defid_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
defid_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
defid_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
defid_SOURCES += defid-res.rc
endif

# Libraries below may be listed more than once to resolve circular dependencies (see
# https://eli.thegreenplace.net/2013/07/09/library-order-in-static-linking#circular-dependency)
defid_LDADD = \
  $(LIBDEFI_SERVER) \
  $(LIBDEFI_WALLET) \
  $(LIBDEFI_SERVER) \
  $(LIBDEFI_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBSPV) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_ZMQ) \
  $(LIBDEFI_CONSENSUS) \
  $(LIBDEFI_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

defid_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(CRYPTO_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS)

# defi-cli binary #
defi_cli_SOURCES = defi-cli.cpp
defi_cli_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES) $(EVENT_CFLAGS)
defi_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
defi_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
defi_cli_SOURCES += defi-cli-res.rc
endif

defi_cli_LDADD = \
  $(LIBDEFI_CLI) \
  $(LIBUNIVALUE) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_CRYPTO)

defi_cli_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS) $(EVENT_LIBS)
#

# defi-tx binary #
defi_tx_SOURCES = defi-tx.cpp
defi_tx_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
defi_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
defi_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
defi_tx_SOURCES += defi-tx-res.rc
endif

defi_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBDEFI_COMMON) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_CONSENSUS) \
  $(LIBDEFI_CRYPTO) \
  $(LIBSECP256K1)

defi_tx_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS)
#

# defi-wallet binary #
defi_wallet_SOURCES = defi-wallet.cpp
defi_wallet_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
defi_wallet_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
defi_wallet_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
defi_wallet_SOURCES += defi-wallet-res.rc
endif

defi_wallet_LDADD = \
  $(LIBDEFI_WALLET_TOOL) \
  $(LIBDEFI_WALLET) \
  $(LIBDEFI_COMMON) \
  $(LIBDEFI_CONSENSUS) \
  $(LIBSPV) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_CRYPTO) \
  $(LIBDEFI_ZMQ) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) \
  $(LIBUNIVALUE)

defi_wallet_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(CRYPTO_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(MINIUPNPC_LIBS) $(ZMQ_LIBS)
#

# deficonsensus library #
if BUILD_DEFI_LIBS
include_HEADERS = script/deficonsensus.h
libdeficonsensus_la_SOURCES = support/cleanse.cpp $(crypto_libdefi_crypto_base_a_SOURCES) $(libdefi_consensus_a_SOURCES)

if GLIBC_BACK_COMPAT
  libdeficonsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libdeficonsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libdeficonsensus_la_LIBADD = $(LIBSECP256K1)
libdeficonsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_DEFI_INTERNAL
libdeficonsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno
CLEANFILES += obj/build.h

EXTRA_DIST = $(CTAES_DIST)


config/defi-config.h: config/stamp-h1
	@$(MAKE) -C $(top_builddir) $(subdir)/$(@)
config/stamp-h1: $(top_srcdir)/$(subdir)/config/defi-config.h.in $(top_builddir)/config.status
	$(AM_V_at)$(MAKE) -C $(top_builddir) $(subdir)/$(@)
$(top_srcdir)/$(subdir)/config/defi-config.h.in:  $(am__configure_deps)
	$(AM_V_at)$(MAKE) -C $(top_srcdir) $(subdir)/config/defi-config.h.in

clean-local:
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C univalue clean
	-$(MAKE) -C spv clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(PYTHON) $(top_srcdir)/contrib/devtools/symbol-check.py < $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(PYTHON) $(top_srcdir)/contrib/devtools/security-check.py < $(bin_PROGRAMS)
endif

if ENABLE_BIP70
%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(<D) $<
endif

if EMBEDDED_LEVELDB
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif

if ENABLE_QT
include Makefile.qt.include
endif

if ENABLE_QT_TESTS
include Makefile.qttest.include
endif

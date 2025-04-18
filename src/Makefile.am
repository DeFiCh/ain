# Copyright (c) 2013-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

SUBDIRS = ../lib
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

LIBDEFI_SPV=libdefi_spv.a
LIBDEFI_SPV_INCLUDES = \
  -I$(srcdir)/spv \
	-I$(srcdir)/spv/support \
	-I$(srcdir)/spv/bitcoin \
	-I$(srcdir)/spv/bcash

LIBAIN_DIR = $(abs_top_builddir)/lib/target
LIBAIN_RS_SRC = $(LIBAIN_DIR)/src/ain_rs_exports.cpp
LIBAIN_RS_H = $(LIBAIN_DIR)/include/ain_rs_exports.h
LIBAIN_RS_LIB = -L$(LIBAIN_DIR)/lib -lain_rs_exports
LIBAIN_RS_LIB_PATH = $(LIBAIN_DIR)/lib/libain_rs_exports.a
LIBAIN_RS_INCLUDES = -I$(LIBAIN_DIR)/include

$(LIBAIN_RS_SRC):
$(LIBAIN_RS_H):
$(LIBAIN_RS_LIB_PATH): FORCE
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(abs_top_builddir)/lib


DEFI_INCLUDES = -I$(builddir) $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS)
DEFI_INCLUDES += -I$(srcdir)/secp256k1/include
DEFI_INCLUDES += $(UNIVALUE_CFLAGS)
DEFI_INCLUDES += $(LIBDEFI_SPV_INCLUDES)
DEFI_INCLUDES += $(LIBAIN_RS_INCLUDES)

LIBDEFI_SERVER=libdefi_server.a
LIBDEFI_COMMON=libdefi_common.a
LIBDEFI_CONSENSUS=libdefi_consensus.a
LIBDEFI_CLI=libdefi_cli.a
LIBDEFI_UTIL=libdefi_util.a
LIBDEFI_CRYPTO_BASE=crypto/libdefi_crypto_base.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBDEFI_ZMQ=libdefi_zmq.a
endif
if ENABLE_WALLET
LIBDEFI_WALLET=libdefi_wallet.a
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
  $(LIBDEFI_SERVER) \
  $(LIBDEFI_CLI) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_COMMON) \
  $(LIBDEFI_CONSENSUS) \
  $(LIBDEFI_SPV) \
  $(LIBDEFI_CRYPTO) \
  $(LIBDEFI_WALLET) \
  $(LIBDEFI_ZMQ)

lib_LIBRARIES =
lib_LTLIBRARIES =

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
  flushablestorage.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  index/base.h \
  index/blockfilterindex.h \
  index/txindex.h \
  indirectmap.h \
  ocean.h \
  init.h \
  interfaces/chain.h \
  interfaces/handler.h \
  key.h \
  key_io.h \
  dbwrapper.h \
  limitedmap.h \
  logging.h \
  ffi/ffiexports.h \
  ffi/ffihelpers.h \
  ffi/ffiocean.h \
  dfi/accounts.h \
  dfi/accountshistory.h \
  dfi/anchors.h \
  dfi/auctionhistory.h \
  dfi/balances.h \
  dfi/coinselect.h \
  dfi/communityaccounttypes.h \
  dfi/consensus/accounts.h \
  dfi/consensus/governance.h \
  dfi/consensus/icxorders.h \
  dfi/consensus/loans.h \
  dfi/consensus/masternodes.h \
  dfi/consensus/oracles.h \
  dfi/consensus/poolpairs.h \
  dfi/consensus/proposals.h \
  dfi/consensus/smartcontracts.h \
  dfi/consensus/tokens.h \
  dfi/consensus/txvisitor.h \
  dfi/consensus/vaults.h \
  dfi/consensus/xvm.h \
  dfi/customtx.h \
  dfi/errors.h \
  dfi/evm.h \
  dfi/factory.h \
  dfi/govvariables/attributes.h \
  dfi/govvariables/icx_takerfee_per_btc.h \
  dfi/govvariables/loan_daily_reward.h \
  dfi/govvariables/loan_liquidation_penalty.h \
  dfi/govvariables/loan_splits.h \
  dfi/govvariables/lp_daily_dfi_reward.h \
  dfi/govvariables/lp_splits.h \
  dfi/govvariables/oracle_block_interval.h \
  dfi/govvariables/oracle_deviation.h \
  dfi/gv.h \
  dfi/historywriter.h \
  dfi/icxorder.h \
  dfi/incentivefunding.h \
  dfi/loan.h \
  dfi/masternodes.h \
  dfi/mn_checks.h \
  dfi/mn_rpc.h \
  dfi/res.h \
  dfi/oracles.h \
  dfi/poolpairs.h \
  dfi/proposals.h \
  dfi/snapshotmanager.h \
  dfi/tokens.h \
  dfi/threadpool.h \
  dfi/coinselect.h \
  dfi/undo.h \
  dfi/undos.h \
  dfi/validation.h \
  dfi/vault.h \
  dfi/vaulthistory.h \
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
  rpc/stats.h \
  rpc/resultcache.h \
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
  util/tokenpipe.h \
  util/system.h \
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
  zmq/zmqrpc.h \
  $(LIBAIN_RS_H)

obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/contrib/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libdefi_util_a-clientversion.$(OBJEXT): obj/build.h

libdefi_spv_a_CPPFLAGS = $(AM_CPPFLAGS) -Wpointer-arith -fpermissive \
  -I$(srcdir)/spv \
  -I$(srcdir)/spv/support \
  -I$(srcdir)/secp256k1 \
  -I$(srcdir)/secp256k1/src \
  -Wno-format-extra-args

libdefi_spv_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libdefi_spv_a_SOURCES = \
  spv/support/BRAddress.cpp \
  spv/support/BRAddress.h \
  spv/support/BRArray.h \
  spv/support/BRBase58.cpp \
  spv/support/BRBase58.h \
  spv/support/BRBech32.cpp \
  spv/support/BRBech32.h \
  spv/support/BRBIP32Sequence.cpp \
  spv/support/BRBIP32Sequence.h \
  spv/support/BRBIP39Mnemonic.cpp \
  spv/support/BRBIP39Mnemonic.h \
  spv/support/BRBIP39WordsEn.h \
  spv/support/BRCrypto.cpp \
  spv/support/BRCrypto.h \
  spv/support/BRInt.h \
  spv/support/BRKey.cpp \
  spv/support/BRKeyECIES.cpp \
  spv/support/BRKeyECIES.h \
  spv/support/BRKey.h \
  spv/support/BRLargeInt.h \
  spv/support/BRSet.cpp \
  spv/support/BRSet.h \
  spv/bcash/BRBCashAddr.cpp \
  spv/bcash/BRBCashAddr.h \
  spv/bcash/BRBCashParams.cpp \
  spv/bcash/BRBCashParams.h \
  spv/bitcoin/BRBIP38Key.cpp \
  spv/bitcoin/BRBIP38Key.h \
  spv/bitcoin/BRBloomFilter.cpp \
  spv/bitcoin/BRBloomFilter.h \
  spv/bitcoin/BRChainParams.cpp \
  spv/bitcoin/BRChainParams.h \
  spv/bitcoin/BRMerkleBlock.cpp \
  spv/bitcoin/BRMerkleBlock.h \
  spv/bitcoin/BRPaymentProtocol.cpp \
  spv/bitcoin/BRPaymentProtocol.h \
  spv/bitcoin/BRPeer.cpp \
  spv/bitcoin/BRPeer.h \
  spv/bitcoin/BRPeerManager.cpp \
  spv/bitcoin/BRPeerManager.h \
  spv/bitcoin/BRTransaction.cpp \
  spv/bitcoin/BRTransaction.h \
  spv/bitcoin/BRWallet.cpp \
  spv/bitcoin/BRWallet.h
#  spv/support/BRAssert.cpp
#  spv/support/BRAssert.h
#  spv/support/BRFileService.cpp
#  spv/support/BRFileService.h
#  spv/bitcoin/BRWalletManager.cpp
#  spv/bitcoin/BRWalletManager.h

# for gen /secp256k1/src/ecmult_static_context.h
LIBSECP256K1_GEN = $(srcdir)/secp256k1/src/ecmult_static_context.h
nodist_libdefi_spv_a_SOURCES = $(LIBSECP256K1_GEN)
BUILT_SOURCES = $(LIBSECP256K1_GEN)
$(LIBSECP256K1_GEN): $(LIBSECP256K1)

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
  ocean.cpp \
  init.cpp \
  dbwrapper.cpp \
  ffi/ffiexports.cpp \
  ffi/ffiocean.cpp \
  dfi/accounts.cpp \
  dfi/accountshistory.cpp \
  dfi/anchors.cpp \
  dfi/auctionhistory.cpp \
  dfi/consensus/accounts.cpp \
  dfi/consensus/governance.cpp \
  dfi/consensus/icxorders.cpp \
  dfi/consensus/loans.cpp \
  dfi/consensus/masternodes.cpp \
  dfi/consensus/oracles.cpp \
  dfi/consensus/poolpairs.cpp \
  dfi/consensus/proposals.cpp \
  dfi/consensus/smartcontracts.cpp \
  dfi/consensus/tokens.cpp \
  dfi/consensus/tokenlock.cpp \
  dfi/consensus/txvisitor.cpp \
  dfi/consensus/vaults.cpp \
  dfi/consensus/xvm.cpp \
  dfi/evm.cpp  \
  dfi/govvariables/attributes.cpp \
  dfi/govvariables/icx_takerfee_per_btc.cpp \
  dfi/govvariables/loan_daily_reward.cpp \
  dfi/govvariables/loan_liquidation_penalty.cpp \
  dfi/govvariables/loan_splits.cpp \
  dfi/govvariables/lp_daily_dfi_reward.cpp \
  dfi/govvariables/lp_splits.cpp \
  dfi/govvariables/oracle_block_interval.cpp \
  dfi/govvariables/oracle_deviation.cpp \
  dfi/gv.cpp \
  dfi/historywriter.cpp \
  dfi/icxorder.cpp \
  dfi/incentivefunding.cpp \
  dfi/loan.cpp \
  dfi/masternodes.cpp \
  dfi/mn_checks.cpp \
  dfi/mn_rpc.cpp \
  dfi/oracles.cpp \
  dfi/poolpairs.cpp \
  dfi/proposals.cpp \
  dfi/rpc_accounts.cpp \
  dfi/rpc_customtx.cpp \
  dfi/rpc_evm.cpp \
  dfi/rpc_masternodes.cpp \
  dfi/rpc_icxorderbook.cpp \
  dfi/rpc_loan.cpp \
  dfi/rpc_oracles.cpp \
  dfi/rpc_poolpair.cpp \
  dfi/rpc_proposals.cpp \
  dfi/rpc_tokens.cpp \
  dfi/rpc_vault.cpp \
  dfi/skipped_txs.cpp \
  dfi/snapshotmanager.cpp \
  dfi/tokens.cpp \
  dfi/threadpool.cpp \
  dfi/undos.cpp \
  dfi/validation.cpp \
  dfi/vault.cpp \
  dfi/vaulthistory.cpp \
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
  rpc/resultcache.cpp \
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
  $(LIBAIN_RS_SRC) \
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
  crypto/sha3.cpp \
  crypto/sha3.h \
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
  script/deficonsensus.h \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  serialize_optional.h \
  span.h \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  util/strencodings.cpp \
  util/strencodings.h \
  version.h

if GLIBC_BACK_COMPAT
  libdefi_consensus_a_SOURCES += compat/glibc_compat.cpp
endif

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
  dfi/customtx.cpp \
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
  rpc/stats.cpp \
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
  util/tokenpipe.cpp \
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

# defid binary
defid_SOURCES = defid.cpp
defid_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES)
defid_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
defid_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

# Libraries below may be listed more than once to resolve circular dependencies (see
# https://eli.thegreenplace.net/2013/07/09/library-order-in-static-linking#circular-dependency)
defid_LDADD = \
  $(LIBDEFI_SERVER) \
  $(LIBDEFI_WALLET) \
  $(LIBAIN_RS_LIB) \
  $(LIBDEFI_SERVER) \
  $(LIBDEFI_COMMON) \
  $(LIBDEFI_UTIL) \
  $(LIBDEFI_CONSENSUS) \
  $(LIBDEFI_SPV) \
  $(LIBDEFI_ZMQ) \
  $(LIBDEFI_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBMEMENV) \
  $(LIBUNIVALUE) \
  $(LIBSECP256K1)

defid_LDADD += $(BOOST_LIBS) $(BDB_LIBS) $(MINIUPNPC_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS)

if TARGET_DARWIN
defid_LDFLAGS += -framework Security -framework SystemConfiguration -framework CoreFoundation
endif
if TARGET_NOT_DARWIN
defid_LDFLAGS += -Wl,--allow-multiple-definition
endif
if TARGET_NOT_WINDOWS
defid_LDADD += -ldl
endif
if TARGET_WINDOWS
defid_LDADD += -luserenv -lbcrypt
endif

EXTRA_defid_DEPENDENCIES: $(LIBAIN_RS_LIB_PATH)

# defi-cli binary #
defi_cli_SOURCES = defi-cli.cpp
defi_cli_CPPFLAGS = $(AM_CPPFLAGS) $(DEFI_INCLUDES) $(EVENT_CFLAGS)
defi_cli_CPPFLAGS += -DDEFI_CLI
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

defi_cli_LDADD += $(BOOST_LIBS) $(EVENT_LIBS)
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

defi_tx_LDADD += $(BOOST_LIBS)
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
CLEANFILES += crc32c/src/*.gcda crc32c/src/*.gcno
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
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(subst -isystem ,-I,$(CPPFLAGS)) -DWINDRES_PREPROC -i $< -o $@

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

include Makefile.crc32c.include
include Makefile.leveldb.include

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif

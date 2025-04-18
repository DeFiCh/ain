// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/defi-config.h>
#endif

#include <init.h>

#include <amount.h>
#include <banman.h>
#include <blockfilter.h>
#include <chain.h>
#include <chainparams.h>
#include <compat/sanity.h>
#include <consensus/validation.h>
#include <fs.h>
#include <httprpc.h>
#include <httpserver.h>
#include <index/blockfilterindex.h>
#include <index/txindex.h>
#include <key.h>
#include <key_io.h>
#include <ain_rs_exports.h>
#include <dfi/accountshistory.h>
#include <dfi/anchors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/vaulthistory.h>
#include <dfi/threadpool.h>
#include <miner.h>
#include <net.h>
#include <net_permissions.h>
#include <net_processing.h>
#include <netbase.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <pos_kernel.h>
#include <rpc/blockchain.h>
#include <rpc/register.h>
#include <rpc/stats.h>
#include <rpc/resultcache.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <scheduler.h>
#include <script/sigcache.h>
#include <script/standard.h>
#include <shutdown.h>
#include <spv/spv_wrapper.h>
#include <timedata.h>
#include <torcontrol.h>
#include <txdb.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <util/validation.h>
#include <validation.h>
#include <validationinterface.h>
#include <walletinitinterface.h>
#include <wallet/wallet.h>
#include <ffi/ffihelpers.h>
#include <ffi/ffiexports.h>
#include <ocean.h>

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#ifndef WIN32
#include <attributes.h>
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>
#endif

#include <boost/algorithm/string/replace.hpp>

#if ENABLE_ZMQ
#include <zmq/zmqabstractnotifier.h>
#include <zmq/zmqnotificationinterface.h>
#include <zmq/zmqrpc.h>
#endif

static bool fFeeEstimatesInitialized = false;
static const bool DEFAULT_PROXYRANDOMIZE = true;
static const bool DEFAULT_REST_ENABLE = false;
static const bool DEFAULT_HEALTH_ENDPOINTS_ENABLE = true;
static const bool DEFAULT_STOPAFTERBLOCKIMPORT = false;

// Dump addresses to banlist.dat every 15 minutes (900s)
static constexpr int DUMP_BANS_INTERVAL = 60 * 15;

std::unique_ptr<CConnman> g_connman;
std::unique_ptr<PeerLogicValidation> peerLogic;
std::unique_ptr<BanMan> g_banman;

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

static const char* FEE_ESTIMATES_FILENAME="fee_estimates.dat";

/**
 * The PID file facilities.
 */
static const char* DEFI_PID_FILENAME = "defid.pid";

static fs::path GetPidFile()
{
    return AbsPathForConfigVal(fs::PathFromString(gArgs.GetArg("-pid", DEFI_PID_FILENAME)));
}

NODISCARD static bool CreatePidFile()
{
    std::ofstream file{GetPidFile()};
    if (file) {
#ifdef WIN32
        tfm::format(file, "%d\n", GetCurrentProcessId());
#else
        tfm::format(file, "%d\n", getpid());
#endif
        return true;
    } else {
        return InitError(strprintf(_("Unable to create the PID file '%s': %s").translated, fs::PathToString(GetPidFile()), std::strerror(errno)));
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets ShutdownRequested(), which makes main thread's
// WaitForShutdown() interrupts the thread group.
// And then, WaitForShutdown() makes all other on-going threads
// in the thread group join the main thread.
// Shutdown() is then called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// ShutdownRequested() getting set, and then does the normal Qt
// shutdown thing.
//

static std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;

std::vector<std::thread> threadGroup;
static CScheduler scheduler;

#if HAVE_SYSTEM
static void ShutdownNotify()
{
    std::vector<std::thread> threads;
    for (const auto& cmd : gArgs.GetArgs("-shutdownnotify")) {
        threads.emplace_back(runCommand, cmd);
    }
    for (auto& t : threads) {
        t.join();
    }
}
#endif

void Interrupt()
{
#if HAVE_SYSTEM
    ShutdownNotify();
#endif
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptHealthEndpoints();
    InterruptTorControl();
    InterruptMapPort();
    if (g_connman)
        g_connman->Interrupt();
    if (g_txindex) {
        g_txindex->Interrupt();
    }
    ForEachBlockFilterIndex([](BlockFilterIndex& index) { index.Interrupt(); });
}

void Shutdown(InitInterfaces& interfaces)
{
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which initialization failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    util::ThreadRename("shutoff");
    mempool.AddTransactionsUpdated(1);

    /// @attention outside of cs_main lock! Before http cause spv rpc may be pending
    if (spv::pspv) {
        spv::pspv->CancelPendingTxs();
        spv::pspv->Disconnect();
    }

    StopHTTPRPC();
    StopREST();
    StopHealthEndpoints();
    StopRPC();
    StopHTTPServer();
    for (const auto& client : interfaces.chain_clients) {
        client->flush();
    }
    XResultStatusLogged(ain_rs_stop_network_services(result));
    StopMapPort();

    // Because these depend on each-other, we make sure that neither can be
    // using the other before destroying them.
    if (peerLogic) UnregisterValidationInterface(peerLogic.get());
    if (g_connman) g_connman->Stop();
    if (g_txindex) g_txindex->Stop();
    ForEachBlockFilterIndex([](BlockFilterIndex& index) { index.Stop(); });

    StopTorControl();

    // After everything has been shut down, but before things get flushed, stop the
    // CScheduler/checkqueue threaGroup
    scheduler.stop();
    for (auto& thread : threadGroup) {
        if (thread.joinable()) thread.join();
    }
    StopScriptCheckWorkerThreads();

    // After the threads that potentially access these pointers have been stopped,
    // destruct and reset all to nullptr.
    peerLogic.reset();
    g_connman.reset();
    g_banman.reset();
    g_txindex.reset();
    DestroyAllBlockFilterIndexes();

    if (::mempool.IsLoaded() && gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        DumpMempool(::mempool);
    }

    if (fFeeEstimatesInitialized)
    {
        ::feeEstimator.FlushUnconfirmed();
        fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fsbridge::fopen(est_path, "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            ::feeEstimator.Write(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, fs::PathToString(est_path));
        fFeeEstimatesInitialized = false;
    }

    //  generates a ChainStateFlushed callback, which we should avoid missing
    //
    // g_chainstate is referenced here directly (instead of ::ChainstateActive()) because it
    // may not have been initialized yet.
    {
        LOCK(cs_main);
        if (g_chainstate && g_chainstate->CanFlushToDisk()) {
            g_chainstate->ForceFlushStateToDisk();
        }
    }

    // After there are no more peers/RPC left to give us new data which may generate
    // CValidationInterface callbacks, flush them...
    GetMainSignals().FlushBackgroundCallbacks();

    // Any future callbacks will be dropped. This should absolutely be safe - if
    // missing a callback results in an unrecoverable situation, unclean shutdown
    // would too. The only reason to do the above flushes is to let the wallet catch
    // up with our current chain to avoid any strange pruning edge cases and make
    // next startup faster by avoiding rescan.

    ShutdownDfTxGlobalTaskPool();
    XResultStatusLogged(ain_rs_stop_core_services(result));
    LogPrint(BCLog::SPV, "Releasing\n");
    spv::pspv.reset();
    {
        LOCK(cs_main);
        if (g_chainstate && g_chainstate->CanFlushToDisk()) {
            g_chainstate->ForceFlushStateToDisk();
            g_chainstate->ResetCoinsViews();
        }
        panchors.reset();
        panchorAwaitingConfirms.reset();
        panchorauths.reset();
        pcustomcsview.reset();
        pcustomcsDB.reset();
        pblocktree.reset();
    }
    for (const auto& client : interfaces.chain_clients) {
        client->stop();
    }

#if ENABLE_ZMQ
    if (g_zmq_notification_interface) {
        UnregisterValidationInterface(g_zmq_notification_interface);
        delete g_zmq_notification_interface;
        g_zmq_notification_interface = nullptr;
    }
#endif

    try {
        if (!fs::remove(GetPidFile())) {
            LogPrintf("%s: Unable to remove PID file: File does not exist\n", __func__);
        }
    } catch (const fs::filesystem_error& e) {
        LogPrintf("%s: Unable to remove PID file: %s\n", __func__, fsbridge::get_filesystem_error_message(e));
    }
    interfaces.chain_clients.clear();
    UnregisterAllValidationInterfaces();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
    GetMainSignals().UnregisterWithMempoolSignals(mempool);
    globalVerifyHandle.reset();
    ECC_Stop();
    RemovePortUsage();
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do.
 * The execution context the handler is invoked in is not guaranteed,
 * so we restrict handler operations to just touching variables:
 */
#ifndef WIN32
static void HandleSIGTERM(int)
{
    StartShutdown();
}

static void HandleSIGHUP(int)
{
    LogInstance().m_reopen_file = true;
}
#else
static BOOL WINAPI consoleCtrlHandler(DWORD dwCtrlType)
{
    StartShutdown();
    Sleep(INFINITE);
    return true;
}
#endif

#ifndef WIN32
static void registerSignalHandler(int signal, void(*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signal, &sa, nullptr);
}
#endif

static boost::signals2::connection rpc_notify_block_change_connection;
static void OnRPCStarted()
{
    rpc_notify_block_change_connection = uiInterface.NotifyBlockTip_connect(&RPCNotifyBlockChange);
}

static void OnRPCStopped()
{
    rpc_notify_block_change_connection.disconnect();
    RPCNotifyBlockChange(false, nullptr);
    g_best_block_cv.notify_all();
    LogPrint(BCLog::RPC, "RPC stopped.\n");
}

void SetupServerArgs()
{
    SetupHelpOptions(gArgs);
    gArgs.AddArg("-help-debug", "Print help message with debugging options and exit", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST); // server-only for now

    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto changiBaseParams = CreateBaseChainParams(CBaseChainParams::CHANGI);
    const auto devnetBaseParams  = CreateBaseChainParams(CBaseChainParams::DEVNET);
    const auto regtestBaseParams = CreateBaseChainParams(CBaseChainParams::REGTEST);
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CBaseChainParams::TESTNET);
    const auto changiChainParams = CreateChainParams(CBaseChainParams::CHANGI);
    const auto devnetChainParams  = CreateChainParams(CBaseChainParams::DEVNET);
    const auto regtestChainParams = CreateChainParams(CBaseChainParams::REGTEST);

    // Hidden Options
    std::vector<std::string> hidden_args = {
        "-dbcrashratio", "-forcecompactdb",
        "-interrupt-block=<hash|height>",
        "-mocknet", "-mocknet-blocktime=<secs>", "-mocknet-key=<pubkey>",
        "-checkpoints-file",
        // GUI args. These will be overwritten by SetupUIArgs for the GUI
        "-choosedatadir", "-lang=<lang>", "-min", "-resetguisettings", "-splash"};

    gArgs.AddArg("-version", "Print version and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    gArgs.AddArg("-alertnotify=<cmd>", "Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
    gArgs.AddArg("-assumevalid=<hex>", strprintf("If this block is in the chain assume that it and its ancestors are valid and potentially skip their script verification (0 to verify all, default: %s, testnet: %s, changi: %s, devnet: %s)", defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(), testnetChainParams->GetConsensus().defaultAssumeValid.GetHex(), changiChainParams->GetConsensus().defaultAssumeValid.GetHex(), devnetChainParams->GetConsensus().defaultAssumeValid.GetHex()), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blocksdir=<dir>", "Specify directory to hold blocks subdirectory for *.dat files (default: <datadir>)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    gArgs.AddArg("-blocknotify=<cmd>", "Execute command when the best block changes (%s in cmd is replaced by block hash)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-spvblocknotify=<cmd>", "Execute command when the last Bitcoin block changes (%s in cmd is replaced by block hash)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-spvwalletnotify=<cmd>", "Execute command when an SPV Bitcoin wallet transaction changes (%s in cmd is replaced by TxID)", ArgsManager::ALLOW_ANY, OptionsCategory::WALLET);
#endif
    gArgs.AddArg("-blockreconstructionextratxn=<n>", strprintf("Extra transactions to keep in memory for compact block reconstructions (default: %u)", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blocksonly", strprintf("Whether to reject transactions from network peers. Transactions from the wallet, RPC and relay whitelisted inbound peers are not affected. (default: %u)", DEFAULT_BLOCKSONLY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-conf=<file>", strprintf("Specify configuration file. Relative paths will be prefixed by datadir location. (default: %s)", DEFI_CONF_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-datadir=<dir>", "Specify data directory", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-dbbatchsize", strprintf("Maximum database write batch size in bytes (default: %u)", nDefaultDbBatchSize), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-dbcache=<n>", strprintf("Maximum database cache size <n> MiB (%d to %d, default: %d). In addition, unused mempool memory is shared for this cache (see -maxmempool).", nMinDbCache, nMaxDbCache, nDefaultDbCache), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-ecclrucache=<n>", strprintf("Maximum ECC LRU cache size <n> items (default: %d).", DEFAULT_ECC_LRU_CACHE_COUNT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-evmvlrucache=<n>", strprintf("Maximum EVM TX Validator LRU cache size <n> items (default: %d).", DEFAULT_EVMV_LRU_CACHE_COUNT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-eccprecache=<n>", strprintf("ECC pre-cache concurrency control (default: %d, (-1: auto, 0: disable, <n>: workers).", DEFAULT_ECC_PRECACHE_WORKERS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-evmnotificationchannel=<n>", strprintf("Maximum EVM notification channel's buffer size (default: %d).", DEFAULT_EVM_NOTIFICATION_CHANNEL_BUFFER_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-debuglogfile=<file>", strprintf("Specify location of debug log file. Relative paths will be prefixed by a net-specific datadir location. (-nodebuglogfile to disable; default: %s)", DEFAULT_DEBUGLOGFILE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-feefilter", strprintf("Tell other nodes to filter invs to us by our mempool min fee (default: %u)", DEFAULT_FEEFILTER), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-includeconf=<file>", "Specify additional configuration file, relative to the -datadir path (only useable from configuration file, not command line)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-loadblock=<file>", "Imports blocks from external blk000??.dat file on startup", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-maxmempool=<n>", strprintf("Keep the transaction memory pool below <n> megabytes (default: %u)", DEFAULT_MAX_MEMPOOL_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-maxorphantx=<n>", strprintf("Keep at most <n> unconnectable transactions in memory (default: %u)", DEFAULT_MAX_ORPHAN_TRANSACTIONS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-mempoolexpiry=<n>", strprintf("Do not keep transactions in the mempool longer than <n> hours (default: %u)", DEFAULT_MEMPOOL_DVM_EXPIRY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-mempoolexpiryevm=<n>", strprintf("Do not keep EVM transactions in the mempool longer than <n> hours (default: %u)", DEFAULT_MEMPOOL_EVM_EXPIRY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-minimumchainwork=<hex>", strprintf("Minimum work assumed to exist on a valid chain in hex (default: %s, testnet: %s, changi: %s, devnet: %s)", defaultChainParams->GetConsensus().nMinimumChainWork.GetHex(), testnetChainParams->GetConsensus().nMinimumChainWork.GetHex(), changiChainParams->GetConsensus().nMinimumChainWork.GetHex(), devnetChainParams->GetConsensus().nMinimumChainWork.GetHex()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-par=<n>", strprintf("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)",
        -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-persistmempool", strprintf("Whether to save the mempool on shutdown and load on restart (default: %u)", DEFAULT_PERSIST_MEMPOOL), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-pid=<file>", strprintf("Specify pid file. Relative paths will be prefixed by a net-specific datadir location. (default: %s)", DEFI_PID_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-prune=<n>", strprintf("Reduce storage requirements by enabling pruning (deleting) of old blocks. This allows the pruneblockchain RPC to be called to delete specific blocks, and enables automatic pruning of old blocks if a target size in MiB is provided. This mode is incompatible with -txindex and -rescan. "
            "Warning: Reverting this setting requires re-downloading the entire blockchain. "
            "(default: 0 = disable pruning blocks, 1 = allow manual pruning via RPC, >=%u = automatically prune block files to stay under the specified target size in MiB)", MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-reindex", "Rebuild chain state and block index from the blk*.dat files on disk", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-reindex-chainstate", "Rebuild chain state from the currently indexed blocks. When in pruning mode or if blocks on disk might be corrupted, use full -reindex instead.", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    gArgs.AddArg("-shutdownnotify=<cmd>", "Execute command immediately before beginning shutdown. The need for shutdown may be urgent, so be careful not to delay it long (if the command doesn't require interaction with the server, consider having it fork into the background).", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
#ifndef WIN32
    gArgs.AddArg("-sysperms", "Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-sysperms");
#endif
    gArgs.AddArg("-txindex", strprintf("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)", DEFAULT_TXINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-acindex", strprintf("Maintain a full account history index, tracking all accounts balances changes. Used by the listaccounthistory, getaccounthistory and accounthistorycount rpc calls (default: %u)", DEFAULT_ACINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-vaultindex", strprintf("Maintain a full vault history index, tracking all vault changes. Used by the listvaulthistory rpc call (default: %u)", DEFAULT_VAULTINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blockfilterindex=<type>",
                 strprintf("Maintain an index of compact filters by block (default: %s, values: %s).", DEFAULT_BLOCKFILTERINDEX, ListBlockFilterTypes()) +
                 " If <type> is not supplied or if <type> = 1, indexes for all known types are enabled.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);

    gArgs.AddArg("-addnode=<ip>", "Add a node to connect to and attempt to keep the connection open (see the `addnode` RPC command help for more info). This option can be specified multiple times to add multiple nodes.", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-banscore=<n>", strprintf("Threshold for disconnecting misbehaving peers (default: %u)", DEFAULT_BANSCORE_THRESHOLD), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-bantime=<n>", strprintf("Number of seconds to keep misbehaving peers from reconnecting (default: %u)", DEFAULT_MISBEHAVING_BANTIME), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-bind=<addr>", "Bind to given address and always listen on it. Use [host]:port notation for IPv6", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-connect=<ip>", "Connect only to the specified node; -noconnect disables automatic connections (the rules for this peer are the same as for -addnode). This option can be specified multiple times to connect to multiple nodes.", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-discover", "Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-dns", strprintf("Allow DNS lookups for -addnode, -seednode and -connect (default: %u)", DEFAULT_NAME_LOOKUP), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-dnsseed", "Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect used)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-externalip=<ip>", "Specify your own public address", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-forcednsseed", strprintf("Always query for peer addresses via DNS lookup (default: %u)", DEFAULT_FORCEDNSSEED), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-listen", "Accept connections from outside (default: 1 if no -proxy or -connect)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-listenonion", strprintf("Automatically create Tor hidden service (default: %d)", DEFAULT_LISTEN_ONION), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxconnections=<n>", strprintf("Maintain at most <n> connections to peers (default: %u)", DEFAULT_MAX_PEER_CONNECTIONS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxreceivebuffer=<n>", strprintf("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXRECEIVEBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxsendbuffer=<n>", strprintf("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXSENDBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxtimeadjustment", strprintf("Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount. (default: %u seconds)", DEFAULT_MAX_TIME_ADJUSTMENT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxuploadtarget=<n>", strprintf("Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)", DEFAULT_MAX_UPLOAD_TARGET), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-onion=<ip:port>", "Use separate SOCKS5 proxy to reach peers via Tor hidden services, set -noonion to disable (default: -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-onlynet=<net>", "Make outgoing connections only through network <net> (ipv4, ipv6 or onion). Incoming connections are not affected by this option. This option can be specified multiple times to allow multiple networks.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-peerbloomfilters", strprintf("Support filtering of blocks and transaction with bloom filters (default: %u)", DEFAULT_PEERBLOOMFILTERS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-permitbaremultisig", strprintf("Relay non-P2SH multisig (default: %u)", DEFAULT_PERMIT_BAREMULTISIG), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-port=<port>", strprintf("Listen for connections on <port> (default: %u, testnet: %u, changi: %u, devnet: %u, regtest: %u)", defaultChainParams->GetDefaultPort(), testnetChainParams->GetDefaultPort(), changiChainParams->GetDefaultPort(), devnetChainParams->GetDefaultPort(), regtestChainParams->GetDefaultPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-ports=auto", "Automaticlly set P2P, RPC, WebSocket and Eth RPC ports. Overrides defaults and other manually set values.", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-proxy=<ip:port>", "Connect through SOCKS5 proxy, set -noproxy to disable (default: disabled)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-proxyrandomize", strprintf("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)", DEFAULT_PROXYRANDOMIZE), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-seednode=<ip>", "Connect to a node to retrieve peer addresses, and disconnect. This option can be specified multiple times to connect to multiple nodes.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-timeout=<n>", strprintf("Specify connection timeout in milliseconds (minimum: 1, default: %d)", DEFAULT_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-peertimeout=<n>", strprintf("Specify p2p connection timeout in seconds. This option determines the amount of time a peer may be inactive before the connection to it is dropped. (minimum: 1, default: %d)", DEFAULT_PEER_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-torcontrol=<ip>:<port>", strprintf("Tor control port to use if onion listening enabled (default: %s)", DEFAULT_TOR_CONTROL), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-torpassword=<pass>", "Tor control port password (default: empty)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-masternode_owner=<address>", "Masternode owner address (default: empty)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-masternode_operator=<address>", "Masternode operator address (default: empty)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-dummypos", "Flag to skip PoS-related checks (regtest only)", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-txnotokens", "Flag to force old tx serialization (regtest only)", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-subsidytest", "Flag to enable new subsidy rules (regtest only)", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-anchorquorum", "Min quorum size (regtest only)", ArgsManager::ALLOW_ANY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-spv", "Enable SPV to bitcoin blockchain (default: 1)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-spv_resync", "Flag to reset spv database and resync from zero block (default: 0)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-amkheight", "AMK fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-bayfrontheight", "Bayfront fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-bayfrontmarinaheight", "Bayfront Marina fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-bayfrontgardensheight", "Bayfront Gardens fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-clarkequayheight", "ClarkeQuay fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-dakotaheight", "Dakota fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-dakotacrescentheight", "DakotaCrescent fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-eunosheight", "Eunos fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-eunospayaheight", "EunosPaya fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningheight", "Fort Canning fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningmuseumheight", "Fort Canning Museum fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningparkheight", "Fort Canning Park fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanninghillheight", "Fort Canning Hill fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningroadheight", "Fort Canning Road fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningcrunchheight", "Fort Canning Crunch fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningspringheight", "Fort Canning Spring fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanninggreatworldheight", "Fort Canning Great World fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-greatworldheight", "Alias for Fort Canning Great World fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-fortcanningepilogueheight", "Alias for Fort Canning Epilogue fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-grandcentralheight", "Grand Central fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-grandcentralepilogueheight", "Grand Central Epilogue fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-metachainheight", "Metachain fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-df23height", "DF23 fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-df24height", "DF24 fork activation height (regtest only)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-jellyfish_regtest", "Configure the regtest network for jellyfish testing", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-regtest-skip-loan-collateral-validation", "Skip loan collateral check for jellyfish testing", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-regtest-minttoken-simulate-mainnet", "Simulate mainnet for minttokens on regtest -  default behavior on regtest is to allow anyone to mint mintable tokens for ease of testing", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-simulatemainnet", "Configure the regtest network to mainnet target timespan and spacing ", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-dexstats", strprintf("Enable storing live dex data in DB (default: %u)", DEFAULT_DEXSTATS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blocktimeordering", strprintf("(Deprecated) Whether to order transactions by time, otherwise ordered by fee (default: %u)", false), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-txordering", strprintf("Whether to order transactions by entry time, fee or both randomly (0: mixed, 1: fee based, 2: entry time) (default: %u)", DEFAULT_TX_ORDERING), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-ethstartstate", strprintf("Initialise Ethereum state trie using JSON input"), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-enablesnapshots", strprintf("Whether to enable snapshot on each block (default: %u)", DEFAULT_SNAPSHOT), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-ascendingstaketime", strprintf("Test staking forward in time from the current block"), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#ifdef USE_UPNP
#if USE_UPNP
    gArgs.AddArg("-upnp", "Use UPnP to map the listening port (default: 1 when listening and no -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#else
    gArgs.AddArg("-upnp", strprintf("Use UPnP to map the listening port (default: %u)", 0), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#endif
#else
    hidden_args.emplace_back("-upnp");
#endif
    gArgs.AddArg("-whitebind=<[permissions@]addr>", "Bind to given address and whitelist peers connecting to it. "
        "Use [host]:port notation for IPv6. Allowed permissions are bloomfilter (allow requesting BIP37 filtered blocks and transactions), "
        "noban (do not ban for misbehavior), "
        "forcerelay (relay even non-standard transactions), "
        "relay (relay even in -blocksonly mode), "
        "and mempool (allow requesting BIP35 mempool contents). "
        "Specify multiple permissions separated by commas (default: noban,mempool,relay). Can be specified multiple times.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    gArgs.AddArg("-whitelist=<[permissions@]IP address or network>", "Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) or "
        "CIDR notated network(e.g. 1.2.3.0/24). Uses same permissions as "
        "-whitebind. Can be specified multiple times." , ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    g_wallet_init_interface.AddWalletOptions();

#if ENABLE_ZMQ
    gArgs.AddArg("-zmqpubhashblock=<address>", "Enable publish hash block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashtx=<address>", "Enable publish hash transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawblock=<address>", "Enable publish raw block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawtx=<address>", "Enable publish raw transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashblockhwm=<n>", strprintf("Set publish hash block outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashtxhwm=<n>", strprintf("Set publish hash transaction outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawblockhwm=<n>", strprintf("Set publish raw block outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawtxhwm=<n>", strprintf("Set publish raw transaction outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
#else
    hidden_args.emplace_back("-zmqpubhashblock=<address>");
    hidden_args.emplace_back("-zmqpubhashtx=<address>");
    hidden_args.emplace_back("-zmqpubrawblock=<address>");
    hidden_args.emplace_back("-zmqpubrawtx=<address>");
    hidden_args.emplace_back("-zmqpubhashblockhwm=<n>");
    hidden_args.emplace_back("-zmqpubhashtxhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawblockhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawtxhwm=<n>");
#endif

    gArgs.AddArg("-checkblocks=<n>", strprintf("How many blocks to check at startup (default: %u, 0 = all)", DEFAULT_CHECKBLOCKS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checklevel=<n>", strprintf("How thorough the block verification of -checkblocks is: "
        "level 0 reads the blocks from disk, "
        "level 1 verifies block validity, "
        "level 2 verifies undo data, "
        "level 3 checks disconnection of tip blocks, "
        "and level 4 tries to reconnect the blocks, "
        "each level includes the checks of the previous levels "
        "(0-4, default: %u)", DEFAULT_CHECKLEVEL), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checkblockindex", strprintf("Do a full consistency check for the block tree, setBlockIndexCandidates, ::ChainActive() and mapBlocksUnlinked occasionally. (default: %u, regtest: %u)", defaultChainParams->DefaultConsistencyChecks(), regtestChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checkmempool=<n>", strprintf("Run checks every <n> transactions (default: %u, regtest: %u)", defaultChainParams->DefaultConsistencyChecks(), regtestChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checkpoints", strprintf("Disable expensive verification for known chain history (default: %u)", DEFAULT_CHECKPOINTS_ENABLED), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-deprecatedrpc=<method>", "Allows deprecated RPC method(s) to be used", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-dropmessagestest=<n>", "Randomly drop 1 of every <n> network messages", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-stopafterblockimport", strprintf("Stop running after importing blocks from disk (default: %u)", DEFAULT_STOPAFTERBLOCKIMPORT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-stopatheight", strprintf("Stop running after reaching the given height in the main chain (default: %u)", DEFAULT_STOPATHEIGHT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitancestorcount=<n>", strprintf("Do not accept transactions if number of in-mempool ancestors is <n> or more (default: %u)", DEFAULT_ANCESTOR_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitancestorsize=<n>", strprintf("Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes (default: %u)", DEFAULT_ANCESTOR_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitdescendantcount=<n>", strprintf("Do not accept transactions if any ancestor would have <n> or more in-mempool descendants (default: %u)", DEFAULT_DESCENDANT_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitdescendantsize=<n>", strprintf("Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants (default: %u).", DEFAULT_DESCENDANT_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-addrmantest", "Allows to test address relay on localhost", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-debug=<category>", "Output debugging information (default: -nodebug, supplying <category> is optional). "
        "If <category> is not supplied or if <category> = 1, output all debugging information. <category> can be: " + ListLogCategories() + ".", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-debugexclude=<category>", strprintf("Exclude debugging information for a category. Can be used in conjunction with -debug=1 to output debug logs for all categories except one or more specified categories."), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-gen", strprintf("Generate coins (default: %u)", DEFAULT_GENERATE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-rewardaddress", strprintf("Generate coins for selected address instead of masternode's owner"), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logips", strprintf("Include IP addresses in debug output (default: %u)", DEFAULT_LOGIPS), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logtimestamps", strprintf("Prepend debug output with timestamp (default: %u)", DEFAULT_LOGTIMESTAMPS), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logthreadnames", strprintf("Prepend debug output with name of the originating thread (only available on platforms supporting thread_local) (default: %u)", DEFAULT_LOGTHREADNAMES), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logtimemicros", strprintf("Add microsecond precision to debug timestamps (default: %u)", DEFAULT_LOGTIMEMICROS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-mocktime=<n>", "Replace actual time with <n> seconds since epoch (default: 0)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-maxsigcachesize=<n>", strprintf("Limit sum of signature cache and script execution cache sizes to <n> MiB (default: %u)", DEFAULT_MAX_SIG_CACHE_SIZE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-maxtipage=<n>", strprintf("Maximum tip age in seconds to consider node in initial block download (default: %u)", DEFAULT_MAX_TIP_AGE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-printpriority", strprintf("Log transaction fee per kB when mining blocks (default: %u)", DEFAULT_PRINTPRIORITY), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-printtoconsole", "Send trace/debug info to console (default: 1 when no -daemon. To disable logging to file, set -nodebuglogfile)", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-shrinkdebugfile", "Shrink debug.log file on client startup (default: 1 when no -debug)", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-tdsinglekeycheck", "Set the single key check flag for transferdomain RPC. If enabled, transfers between domain are only allowed if the addresses specified corresponds to the same key (default: true)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-dvmownerskipcheck", "If enabled, utxostoaccount, sendtokenstoaddress and accounttoaccount APIs enforce a check to only allow to owned addresses (default: true)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-evmtxpriorityfeepercentile", strprintf("Set the suggested priority fee for EVM transactions (default: %u)", DEFAULT_SUGGESTED_PRIORITY_FEE_PERCENTILE), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-evmestimategaserrorratio", strprintf("Set the gas estimation error ratio for eth_estimateGas RPC (default: %u)", DEFAULT_ESTIMATE_GAS_ERROR_RATIO), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-uacomment=<cmt>", "Append comment to the user agent string", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);

    SetupChainParamsBaseOptions();

    gArgs.AddArg("-acceptnonstdtxn", strprintf("Relay and mine \"non-standard\" transactions (default: (testnet: %u, changi: %u, devnet: %u, regtest: %u))", !testnetChainParams->RequireStandard(), !changiChainParams->RequireStandard(), !devnetChainParams->RequireStandard(), !regtestChainParams->RequireStandard()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-incrementalrelayfee=<amt>", strprintf("Fee rate (in %s/kB) used to define cost of relay, used for mempool limiting and BIP 125 replacement. (default: %s)", CURRENCY_UNIT, FormatMoney(DEFAULT_INCREMENTAL_RELAY_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-dustrelayfee=<amt>", strprintf("Fee rate (in %s/kB) used to define dust, the value of an output such that it will cost more than its value in fees at this fee rate to spend it. (default: %s)", CURRENCY_UNIT, FormatMoney(DUST_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-bytespersigop", strprintf("Equivalent bytes per sigop in transactions for relay and mining (default: %u)", DEFAULT_BYTES_PER_SIGOP), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-datacarrier", strprintf("Relay and mine data carrier transactions (default: %u)", DEFAULT_ACCEPT_DATACARRIER), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-datacarriersize", strprintf("Maximum size of data in data carrier transactions we relay and mine (default: %u)", MAX_OP_RETURN_RELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-minrelaytxfee=<amt>", strprintf("Fees (in %s/kB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)",
        CURRENCY_UNIT, FormatMoney(DEFAULT_MIN_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-whitelistforcerelay", strprintf("Add 'forcerelay' permission to whitelisted inbound peers with default permissions. This will relay transactions even if the transactions were already in the mempool or violate local relay policy. (default: %d)", DEFAULT_WHITELISTFORCERELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-whitelistrelay", strprintf("Add 'relay' permission to whitelisted inbound peers with default permissions. The will accept relayed transactions even when not relaying transactions (default: %d)", DEFAULT_WHITELISTRELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);


    gArgs.AddArg("-blockmaxweight=<n>", strprintf("Set maximum BIP141 block weight (default: %d)", DEFAULT_BLOCK_MAX_WEIGHT), ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    gArgs.AddArg("-blockmintxfee=<amt>", strprintf("Set lowest fee rate (in %s/kB) for transactions to be included in block creation. (default: %s)", CURRENCY_UNIT, FormatMoney(DEFAULT_BLOCK_MIN_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    gArgs.AddArg("-blockversion=<n>", "Override block version to test forking scenarios", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::BLOCK_CREATION);

    gArgs.AddArg("-rest", strprintf("Accept public REST requests (default: %u)", DEFAULT_REST_ENABLE), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-healthendpoints", strprintf("Provide health check endpoints to check for the current status of the node.(default: %u)", DEFAULT_HEALTH_ENDPOINTS_ENABLE), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcallowip=<ip>", "Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcauth=<userpw>", "Username and HMAC-SHA-256 hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcauth. The client then connects normally using the rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This option can be specified multiple times", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcbind=<addr>[:port]", "Bind to given address to listen for JSON-RPC connections. Do not expose the RPC server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -rpcport. Use [host]:port notation for IPv6. This option can be specified multiple times (default: 127.0.0.1 and ::1 i.e., localhost)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-rpccookiefile=<loc>", "Location of the auth cookie. Relative paths will be prefixed by a net-specific datadir location. (default: data dir)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcpassword=<pw>", "Password for JSON-RPC connections", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcport=<port>", strprintf("Listen for JSON-RPC connections on <port> (default: %u, testnet: %u, devnet: %u, regtest: %u)", defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort(), devnetBaseParams->RPCPort(), regtestBaseParams->RPCPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcserialversion", strprintf("Sets the serialization of raw transaction or block hex returned in non-verbose mode, non-segwit(0) or segwit(1) (default: %d)", DEFAULT_RPC_SERIALIZE_VERSION), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcservertimeout=<n>", strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcthreads=<n>", strprintf("Set the number of threads to service RPC calls (default: %d)", DEFAULT_HTTP_THREADS), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcuser=<user>", "Username for JSON-RPC connections", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-server", "Accept command line and JSON-RPC commands", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcallowcors=<host>", "Allow CORS requests from the given host origin. Include scheme and port (eg: -rpcallowcors=http://127.0.0.1:5000)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcstats", strprintf("Log RPC stats. (default: %u)", DEFAULT_RPC_STATS), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-consolidaterewards=<token-or-pool-symbol>", "Consolidate rewards on startup. Accepted multiple times for each token symbol", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-rpccache=<0/1/2>", "Cache rpc results - uses additional memory to hold on to the last results per block, but faster (0=none, 1=all, 2=smart)", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-negativeinterest", "(experimental) Track negative interest values", ArgsManager::ALLOW_ANY, OptionsCategory::HIDDEN);
    gArgs.AddArg("-rpc-governance-accept-neutral", "Allow voting with neutral votes for JellyFish purpose", ArgsManager::ALLOW_ANY, OptionsCategory::HIDDEN);
    gArgs.AddArg("-dftxworkers=<n>", strprintf("No. of parallel workers associated with the DfTx related work pool. Stock splits, parallel processing of the chain where appropriate, etc use this worker pool (default: %d)", DEFAULT_DFTX_WORKERS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-maxaddrratepersecond=<n>", strprintf("Sets MAX_ADDR_RATE_PER_SECOND limit for ADDR messages(default: %f)", MAX_ADDR_RATE_PER_SECOND), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxaddrprocessingtokenbucket=<n>", strprintf("Sets MAX_ADDR_PROCESSING_TOKEN_BUCKET limit for ADDR messages(default: %d)", MAX_ADDR_PROCESSING_TOKEN_BUCKET), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-ethrpcbind=<addr>[:port]", "Bind to given address to listen for ETH-JSON-RPC connections. Do not expose the ETH-RPC server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -ethrpcport. This option can be specified multiple times (default: 127.0.0.1 i.e., localhost)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-ethrpcport=<port>", strprintf("Listen for ETH-JSON-RPC connections on <port>. If -1 flag specified, ETH RPC server initialization will be disabled. (default: %u, testnet: %u, changi: %u, devnet: %u, regtest: %u)", defaultBaseParams->ETHRPCPort(), testnetBaseParams->ETHRPCPort(), changiBaseParams->ETHRPCPort(), devnetBaseParams->ETHRPCPort(), regtestBaseParams->ETHRPCPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-wsbind=<addr>[:port]", "Bind to given address to listen for ETH-WebSockets connections. Do not expose the Eth-WebSockets server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -wsport. This option can be specified multiple times (default: 127.0.0.1 i.e., localhost)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-wsport=<port>", strprintf("Listen for ETH-WebSockets connections on <port>. If -1 flag specified, ws server initialization will be disabled. (default: %u, testnet: %u, changi: %u, devnet: %u, regtest: %u)", defaultBaseParams->WSPort(), testnetBaseParams->WSPort(), changiBaseParams->WSPort(), devnetBaseParams->WSPort(), regtestBaseParams->WSPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-ethmaxconnections=<connections>", strprintf("Set the maximum number of connections allowed by the ETH-RPC server (default: %u, testnet: %u, changi: %u, devnet: %u, regtest: %u)", DEFAULT_ETH_MAX_CONNECTIONS, DEFAULT_ETH_MAX_CONNECTIONS, DEFAULT_ETH_MAX_CONNECTIONS, DEFAULT_ETH_MAX_CONNECTIONS, DEFAULT_ETH_MAX_CONNECTIONS), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-ethmaxresponsesize=<size>", strprintf("Set the maximum response size in MB by the ETH-RPC server (default: %u, testnet: %u, changi: %u, devnet: %u, regtest: %u)", DEFAULT_ETH_MAX_RESPONSE_SIZE_MB, DEFAULT_ETH_MAX_RESPONSE_SIZE_MB, DEFAULT_ETH_MAX_RESPONSE_SIZE_MB, DEFAULT_ETH_MAX_RESPONSE_SIZE_MB, DEFAULT_ETH_MAX_RESPONSE_SIZE_MB), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-ethtracingmaxmemoryusage=<size>", strprintf("Set the maximum taw max memory usage size in bytes by the ETH-RPC server (default: %u, testnet: %u, changi: %u, devnet: %u, regtest: %u)", DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES, DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES, DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES, DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES, DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-ethdebug", strprintf("Enable debug_* ETH RPCs (default: %b)", DEFAULT_ETH_DEBUG_ENABLED), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-ethdebugtrace", strprintf("Enable debug_trace* ETH RPCs (default: %b)", DEFAULT_ETH_DEBUG_TRACE_ENABLED), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-ethsubscription", strprintf("Enable subscription notifications ETH RPCs (default: %b)", DEFAULT_ETH_SUBSCRIPTION_ENABLED), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-oceanarchive", strprintf("Enable ocean archive indexer (default: %b)", DEFAULT_OCEAN_INDEXER_ENABLED), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-expr-oceanarchive", strprintf("Enable ocean archive indexer (default: %b)", DEFAULT_OCEAN_INDEXER_ENABLED), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-oceanarchiveserver", strprintf("Enable ocean archive server (default: %b)", DEFAULT_OCEAN_SERVER_ENABLED), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-oceanarchiveport=<port>", strprintf("Listen for ocean archive connections on <port> (default: %u)", DEFAULT_OCEAN_SERVER_PORT), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-oceanarchivebind=<addr>[:port]", "Bind to given address to listen for Ocean connections. Do not expose the Ocean server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -oceanarchiveport. This option can be specified multiple times (default: 127.0.0.1 i.e., localhost)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-minerstrategy", "Staking optimisation. Options are none, numeric value indicating the number of subnodes to stake (default: none)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);


#if HAVE_DECL_FORK
    gArgs.AddArg("-daemon", strprintf("Run in the background as a daemon and accept commands (default: %d)", DEFAULT_DAEMON), ArgsManager::ALLOW_BOOL, OptionsCategory::OPTIONS);
    gArgs.AddArg("-daemonwait", strprintf("Wait for initialization to be finished before exiting. This implies -daemon (default: %d)", DEFAULT_DAEMONWAIT), ArgsManager::ALLOW_BOOL, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-daemon");
    hidden_args.emplace_back("-daemonwait");
#endif

    RPCMetadata::SetupArgs(gArgs);
    // Add the hidden options
    gArgs.AddHiddenArgs(hidden_args);
}

std::string LicenseInfo()
{
    const std::string URL_SOURCE_CODE = "<https://github.com/DeFiCh/ain>";
    const std::string URL_WEBSITE = "<https://defichain.com>";

    return CopyrightHolders(strprintf(_("Copyright (C) %i-%i").translated, 2009, COPYRIGHT_YEAR) + " ") + "\n" +
           "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                       "Visit %s for further information about the software.").translated,
               PACKAGE_NAME, URL_WEBSITE) +
           "\n" +
           strprintf(_("The source code is available from %s.").translated,
               URL_SOURCE_CODE) +
           "\n" +
           "\n" +
           _("This is experimental software.").translated + "\n" +
           strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s").translated, "COPYING", "<https://opensource.org/licenses/MIT>") +
           "\n";
}

#if HAVE_SYSTEM
static void BlockNotifyCallback(bool initialSync, const CBlockIndex *pBlockIndex)
{
    if (initialSync || !pBlockIndex)
        return;

    std::string strCmd = gArgs.GetArg("-blocknotify", "");
    if (!strCmd.empty()) {
        boost::replace_all(strCmd, "%s", pBlockIndex->GetBlockHash().GetHex());
        std::thread t(runCommand, strCmd);
        t.detach(); // thread runs free
    }
}
#endif

static bool fHaveGenesis = false;
static Mutex g_genesis_wait_mutex;
static std::condition_variable g_genesis_wait_cv;

static void BlockNotifyGenesisWait(bool, const CBlockIndex *pBlockIndex)
{
    if (pBlockIndex != nullptr) {
        {
            LOCK(g_genesis_wait_mutex);
            fHaveGenesis = true;
        }
        g_genesis_wait_cv.notify_all();
    }
}

struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};


// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
static void CleanupBlockRevFiles()
{
    std::map<std::string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    fs::path blocksdir = GetBlocksDir();
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator(); it++) {
        const std::string path = fs::PathToString(it->path().filename());
        if (fs::is_regular_file(*it) &&
            path.length() == 12 &&
            path.substr(8,4) == ".dat")
        {
            if (path.substr(0,3) == "blk")
                mapBlockFiles[path.substr(3,5)] = it->path();
            else if (path.substr(0,3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    for (const std::pair<const std::string, fs::path>& item : mapBlockFiles) {
        if (atoi(item.first) == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

static void ThreadImport(std::vector<fs::path> vImportFiles)
{
    const CChainParams& chainparams = Params();
    util::ThreadRename("loadblk");
    ScheduleBatchPriority();

    {
    CImportingNow imp;

    // -reindex
    if (fReindex) {
        int nFile = 0;
        while (true) {
            FlatFilePos pos(nFile, 0);
            if (!fs::exists(GetBlockPosFilename(pos)))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            LogPrintf("Reindexing block file blk%05u.dat...\n", (unsigned int)nFile);
            LoadExternalBlockFile(chainparams, file, &pos);
            if (ShutdownRequested()) {
                LogPrintf("Shutdown requested. Exit %s\n", __func__);
                return;
            }
            nFile++;
        }
        pblocktree->WriteReindexing(false);
        fReindex = false;
        LogPrintf("Reindexing finished\n");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        LoadGenesisBlock(chainparams);
    }

    // hardcoded $DATADIR/bootstrap.dat
    fs::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (fs::exists(pathBootstrap)) {
        FILE *file = fsbridge::fopen(pathBootstrap, "rb");
        if (file) {
            fs::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadExternalBlockFile(chainparams, file);
            if (ShutdownRequested()) {
                LogPrintf("Shutdown requested. Exit %s\n", __func__);
                return;
            }
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrintf("Warning: Could not open bootstrap file %s\n", fs::PathToString(pathBootstrap));
        }
    }

    // -loadblock=
    for (const fs::path& path : vImportFiles) {
        FILE *file = fsbridge::fopen(path, "rb");
        if (file) {
            LogPrintf("Importing blocks file %s...\n", fs::PathToString(path));
            LoadExternalBlockFile(chainparams, file);
            if (ShutdownRequested()) {
                LogPrintf("Shutdown requested. Exit %s\n", __func__);
                return;
            }
        } else {
            LogPrintf("Warning: Could not open blocks file %s\n", fs::PathToString(path));
        }
    }

    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state;
    if (!ActivateBestChain(state, chainparams)) {
        LogPrintf("Failed to connect best block (%s)\n", FormatStateMessage(state));
        StartShutdown();
        return;
    }

    if (gArgs.GetBoolArg("-stopafterblockimport", DEFAULT_STOPAFTERBLOCKIMPORT)) {
        LogPrintf("Stopping after block import\n");
        StartShutdown();
        return;
    }
    } // End scope of CImportingNow
    if (gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        LoadMempool(::mempool);
    }
    ::mempool.SetIsLoaded(!ShutdownRequested());
}

/** Sanity checks
 *  Ensure that the DeFi Blockchain is running in a usable environment with all
 *  necessary library support.
 */
static bool InitSanityCheck()
{
    if(!ECC_InitSanityCheck()) {
        InitError("Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }

    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    if (!Random_SanityCheck()) {
        InitError("OS cryptographic RNG sanity check failure. Aborting.");
        return false;
    }

    return true;
}

static bool AppInitServers()
{
    if (!gArgs.GetBoolArg("-rpcstats", DEFAULT_RPC_STATS))
        statsRPC.setActive(false);

    auto rpcCacheModeVal = gArgs.GetArg("-rpccache", 1);
    auto rpcCacheMode = [=](){
        switch (rpcCacheModeVal) {
        case 1: return RPCResultCache::RPCCacheMode::All;
        // For the moment, there is smart is dumb, just redirects to all.
        // Future implementations could be smarter based on size / latency.
        case 2: return RPCResultCache::RPCCacheMode::All;
        default: return RPCResultCache::RPCCacheMode::None;
    }}();
    GetRPCResultCache().Init(rpcCacheMode);
    GetMemoizedResultCache().Init(rpcCacheMode);

    RPCServer::OnStarted(&OnRPCStarted);
    RPCServer::OnStopped(&OnRPCStopped);
    if (!InitHTTPServer())
        return false;
    StartRPC();
    if (!StartHTTPRPC())
        return false;
    if (gArgs.GetBoolArg("-rest", DEFAULT_REST_ENABLE)) StartREST();
    if (gArgs.GetBoolArg("-healthendpoints", DEFAULT_HEALTH_ENDPOINTS_ENABLE)) StartHealthEndpoints();

    StartHTTPServer();
    return true;
}

// Parameter interaction based on rules
void InitParameterInteraction()
{
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (gArgs.IsArgSet("-bind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (gArgs.IsArgSet("-whitebind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (gArgs.IsArgSet("-connect")) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (gArgs.SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (gArgs.IsArgSet("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -upnp=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (gArgs.IsArgSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // disable whitelistrelay in blocksonly mode
    if (gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", false))
            LogPrintf("%s: parameter interaction: -blocksonly=1 -> setting -whitelistrelay=0\n", __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (gArgs.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", true))
            LogPrintf("%s: parameter interaction: -whitelistforcerelay=1 -> setting -whitelistrelay=1\n", __func__);
    }

    // Parse leveldb checksum
    const auto checksumArg = gArgs.GetArg("-leveldbchecksum", DEFAULT_LEVELDB_CHECKSUM);
    if (checksumArg == "true"){
        levelDBChecksum = true;
    } else if (checksumArg == "false") {
        levelDBChecksum = false;
    } else {
        if (checksumArg != "auto"){
            InitWarning("Invalid value for -leveldbchecksum, setting default value -> 'auto'");
        }
        if (levelDBChecksum = gArgs.IsArgSet("-masternode_operator"); levelDBChecksum) {
            LogPrintf("%s: parameter interaction: -masternode_operator -> setting -leveldbchecksum='true'\n", __func__);
        }
    }

    txOrdering = static_cast<TxOrderings>(gArgs.GetArg("-txordering", DEFAULT_TX_ORDERING));

    if (gArgs.GetBoolArg("-blocktimeordering", false))
        txOrdering = TxOrderings::ENTRYTIME_ORDERING;
}

/**
 * Initialize global loggers.
 *
 * Note that this is called very early in the process lifetime, so you should be
 * careful about what global state you rely on here.
 */
void InitLogging()
{
    LogInstance().m_print_to_file = !gArgs.IsArgNegated("-debuglogfile");
    LogInstance().m_file_path = AbsPathForConfigVal(fs::PathFromString(gArgs.GetArg("-debuglogfile", DEFAULT_DEBUGLOGFILE)));
    LogInstance().m_print_to_console = gArgs.GetBoolArg("-printtoconsole", !gArgs.GetBoolArg("-daemon", false));
    LogInstance().m_log_timestamps = gArgs.GetBoolArg("-logtimestamps", DEFAULT_LOGTIMESTAMPS);
    LogInstance().m_log_time_micros = gArgs.GetBoolArg("-logtimemicros", DEFAULT_LOGTIMEMICROS);
    LogInstance().m_log_threadnames = gArgs.GetBoolArg("-logthreadnames", DEFAULT_LOGTHREADNAMES);

    fLogIPs = gArgs.GetBoolArg("-logips", DEFAULT_LOGIPS);

    std::string version_string = FormatVersionAndSuffix();
#ifdef DEBUG
    version_string += " (debug build)";
#else
    version_string += " (release build)";
#endif
    LogPrintf(PACKAGE_NAME " version %s\n", version_string);
}

namespace { // Variables internal to initialization process only

int nMaxConnections;
int nUserMaxConnections;
int nFD;
ServiceFlags nLocalServices = ServiceFlags(NODE_NETWORK | NODE_NETWORK_LIMITED);
int64_t peer_connect_timeout;
std::vector<BlockFilterType> g_enabled_filter_types;

} // namespace

[[noreturn]] static void new_handler_terminate()
{
    // Rather than throwing std::bad-alloc if allocation fails, terminate
    // immediately to (try to) avoid chain corruption.
    // Since LogPrintf may itself allocate memory, set the handler directly
    // to terminate first.
    std::set_new_handler(std::terminate);
    LogPrintf("Error: Out of memory. Terminating.\n");

    // The log was successful, terminate now.
    std::terminate();
};

bool AppInitBasicSetup()
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable Data Execution Prevention (DEP)
    SetProcessDEPPolicy(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking())
        return InitError("Initializing networking failed");

#ifndef WIN32
    if (!gArgs.GetBoolArg("-sysperms", false)) {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    registerSignalHandler(SIGTERM, HandleSIGTERM);
    registerSignalHandler(SIGINT, HandleSIGTERM);

    // Reopen debug.log on SIGHUP
    registerSignalHandler(SIGHUP, HandleSIGHUP);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#else
    SetConsoleCtrlHandler(consoleCtrlHandler, true);
#endif

    std::set_new_handler(new_handler_terminate);

    return true;
}

bool AppInitParameterInteraction()
{
    const CChainParams& chainparams = Params();
    // ********************************************************* Step 2: parameter interactions

    // also see: InitParameterInteraction()

    // Warn if network-specific options (-addnode, -connect, etc) are
    // specified in default section of config file, but not overridden
    // on the command line or in this network's section of the config file.
    std::string network = gArgs.GetChainName();
    for (const auto& arg : gArgs.GetUnsuitableSectionOnlyArgs()) {
        return InitError(strprintf(_("Config setting for %s only applied on %s network when in [%s] section.").translated, arg, network, network));
    }

    // Warn if unrecognized section name are present in the config file.
    for (const auto& section : gArgs.GetUnrecognizedSections()) {
        InitWarning(strprintf("%s:%i " + _("Section [%s] is not recognized.").translated, section.m_file, section.m_line, section.m_name));
    }

    if (!fs::is_directory(GetBlocksDir())) {
        return InitError(strprintf(_("Specified blocks directory \"%s\" does not exist.").translated, gArgs.GetArg("-blocksdir", "").c_str()));
    }

    // parse and validate enabled filter types
    std::string blockfilterindex_value = gArgs.GetArg("-blockfilterindex", DEFAULT_BLOCKFILTERINDEX);
    if (blockfilterindex_value == "" || blockfilterindex_value == "1") {
        g_enabled_filter_types = AllBlockFilterTypes();
    } else if (blockfilterindex_value != "0") {
        const std::vector<std::string> names = gArgs.GetArgs("-blockfilterindex");
        g_enabled_filter_types.reserve(names.size());
        for (const auto& name : names) {
            BlockFilterType filter_type;
            if (!BlockFilterTypeByName(name, filter_type)) {
                return InitError(strprintf(_("Unknown -blockfilterindex value %s.").translated, name));
            }
            g_enabled_filter_types.push_back(filter_type);
        }
    }

    // if using block pruning, then disallow txindex
    if (gArgs.GetArg("-prune", 0)) {
        if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX))
            return InitError(_("Prune mode is incompatible with -txindex.").translated);
        if (!g_enabled_filter_types.empty()) {
            return InitError(_("Prune mode is incompatible with -blockfilterindex.").translated);
        }
    }

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = gArgs.GetArgs("-bind").size() + gArgs.GetArgs("-whitebind").size();
    if (nUserBind != 0 && !gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        return InitError("Cannot set -bind or -whitebind together with -listen=0");
    }

    // Make sure enough file descriptors are available
    int nBind = std::max(nUserBind, size_t(1));
    nUserMaxConnections = gArgs.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    nMaxConnections = std::max(nUserMaxConnections, 0);

    // Trim requested connection counts, to fit into system limitations
    // <int> in std::min<int>(...) to work around FreeBSD compilation issue described in #2695
    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + MAX_ADDNODE_CONNECTIONS);
#ifdef USE_POLL
    int fd_max = nFD;
#else
    int fd_max = FD_SETSIZE;
#endif
    nMaxConnections = std::max(std::min<int>(nMaxConnections, fd_max - nBind - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS), 0);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available.").translated);
    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations.").translated, nUserMaxConnections, nMaxConnections));

    // ********************************************************* Step 3: parameter-to-internal-flags
    if (gArgs.IsArgSet("-debug")) {
        // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
        const std::vector<std::string> categories = gArgs.GetArgs("-debug");

        if (std::none_of(categories.begin(), categories.end(),
            [](std::string cat){return cat == "0" || cat == "none";})) {
            for (const auto& cat : categories) {
                if (!LogInstance().EnableCategory(cat)) {
                    InitWarning(strprintf(_("Unsupported logging category %s=%s.").translated, "-debug", cat));
                }
            }
        }
    }

    // Now remove the logging categories which were explicitly excluded
    for (const std::string& cat : gArgs.GetArgs("-debugexclude")) {
        if (!LogInstance().DisableCategory(cat)) {
            InitWarning(strprintf(_("Unsupported logging category %s=%s.").translated, "-debugexclude", cat));
        }
    }

    // Checkmempool and checkblockindex default to true in regtest mode
    int ratio = std::min<int>(std::max<int>(gArgs.GetArg("-checkmempool", chainparams.DefaultConsistencyChecks() ? 1 : 0), 0), 1000000);
    if (ratio != 0) {
        mempool.setSanityCheck(1.0 / ratio);
    }
    fCheckBlockIndex = gArgs.GetBoolArg("-checkblockindex", chainparams.DefaultConsistencyChecks());

    auto checkpoints_file = gArgs.GetArg("-checkpoints-file", "");
    if (!checkpoints_file.empty()) {
        auto res = UpdateCheckpointsFromFile(const_cast<CChainParams&>(chainparams), checkpoints_file);
        if (!res)
            return InitError(strprintf(_("Error in checkpoints file : %s").translated, res.msg));
    }

    if (!gArgs.GetBoolArg("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED)) {
        LogPrintf("conf: checkpoints disabled.\n");
        // Safe to const_cast, as we know it's always allocated, and is always in the global var
        // and it is not used anywhere yet.
        ClearCheckpoints(const_cast<CChainParams&>(chainparams));
    } else {
        LogPrintf("conf: checkpoints enabled.\n");
    }

    hashAssumeValid = uint256S(gArgs.GetArg("-assumevalid", chainparams.GetConsensus().defaultAssumeValid.GetHex()));
    if (!hashAssumeValid.IsNull())
        LogPrintf("Assuming ancestors of block %s have valid signatures.\n", hashAssumeValid.GetHex());
    else
        LogPrintf("Validating signatures for all blocks.\n");

    if (gArgs.IsArgSet("-minimumchainwork")) {
        const std::string minChainWorkStr = gArgs.GetArg("-minimumchainwork", "");
        if (!IsHexNumber(minChainWorkStr)) {
            return InitError(strprintf("Invalid non-hex (%s) minimum chain work value specified", minChainWorkStr));
        }
        nMinimumChainWork = UintToArith256(uint256S(minChainWorkStr));
    } else {
        nMinimumChainWork = UintToArith256(chainparams.GetConsensus().nMinimumChainWork);
    }
    LogPrintf("Setting nMinimumChainWork=%s\n", nMinimumChainWork.GetHex());
    if (nMinimumChainWork < UintToArith256(chainparams.GetConsensus().nMinimumChainWork)) {
        LogPrintf("Warning: nMinimumChainWork set below default value of %s\n", chainparams.GetConsensus().nMinimumChainWork.GetHex());
    }

    // mempool limits
    int64_t nMempoolSizeMax = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nMempoolSizeMin = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
        return InitError(strprintf(_("-maxmempool must be at least %d MB").translated, std::ceil(nMempoolSizeMin / 1000000.0)));
    // incremental relay fee sets the minimum feerate increase necessary for BIP 125 replacement in the mempool
    // and the amount the mempool min fee increases above the feerate of txs evicted due to mempool limiting.
    if (gArgs.IsArgSet("-incrementalrelayfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-incrementalrelayfee", ""), n))
            return InitError(AmountErrMsg("incrementalrelayfee", gArgs.GetArg("-incrementalrelayfee", "")));
        incrementalRelayFee = CFeeRate(n);
    }

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t nPruneArg = gArgs.GetArg("-prune", 0);
    if (nPruneArg < 0) {
        return InitError(_("Prune cannot be configured with a negative value.").translated);
    }
    nPruneTarget = (uint64_t) nPruneArg * 1024 * 1024;
    if (nPruneArg == 1) {  // manual pruning: -prune=1
        LogPrintf("Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.\n");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget) {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
            return InitError(strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number.").translated, MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
        }
        LogPrintf("Prune configured to target %u MiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    nConnectTimeout = gArgs.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0) {
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;
    }

    peer_connect_timeout = gArgs.GetArg("-peertimeout", DEFAULT_PEER_CONNECT_TIMEOUT);
    if (peer_connect_timeout <= 0) {
        return InitError("peertimeout cannot be configured with a negative value.");
    }

    maxAddrRatePerSecond = gArgs.GetDoubleArg("-maxaddrratepersecond", Params().NetworkIDString() == CBaseChainParams::REGTEST ? MAX_ADDR_RATE_PER_SECOND_REGTEST : MAX_ADDR_RATE_PER_SECOND);
    if (maxAddrRatePerSecond <= static_cast<double>(0)) {
        return InitError("maxaddrratepersecond cannot be configured with a negative value.");
    }

    maxAddrProcessingTokenBucket = gArgs.GetArg("-maxaddrprocessingtokenbucket", MAX_ADDR_PROCESSING_TOKEN_BUCKET);
    if (maxAddrProcessingTokenBucket <= 0) {
        return InitError("maxaddrprocessingtokenbucket cannot be configured with a negative value.");
    }

    if (gArgs.IsArgSet("-minrelaytxfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-minrelaytxfee", ""), n)) {
            return InitError(AmountErrMsg("minrelaytxfee", gArgs.GetArg("-minrelaytxfee", "")));
        }
        // High fee check is done afterward in WalletParameterInteraction()
        ::minRelayTxFee = CFeeRate(n);
    } else if (incrementalRelayFee > ::minRelayTxFee) {
        // Allow only setting incrementalRelayFee to control both
        ::minRelayTxFee = incrementalRelayFee;
        LogPrintf("Increasing minrelaytxfee to %s to match incrementalrelayfee\n",::minRelayTxFee.ToString());
    }

    // Sanity check argument for min fee for including tx in block
    // TODO: Harmonize which arguments need sanity checking and where that happens
    if (gArgs.IsArgSet("-blockmintxfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n))
            return InitError(AmountErrMsg("blockmintxfee", gArgs.GetArg("-blockmintxfee", "")));
    }

    // Feerate used to define dust.  Shouldn't be changed lightly as old
    // implementations may inadvertently create non-standard transactions
    if (gArgs.IsArgSet("-dustrelayfee"))
    {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-dustrelayfee", ""), n))
            return InitError(AmountErrMsg("dustrelayfee", gArgs.GetArg("-dustrelayfee", "")));
        dustRelayFee = CFeeRate(n);
    }

    fRequireStandard = !gArgs.GetBoolArg("-acceptnonstdtxn", !chainparams.RequireStandard());
    if (!chainparams.IsTestChain() && !fRequireStandard) {
        return InitError(strprintf("acceptnonstdtxn is not currently supported for %s chain", chainparams.NetworkIDString()));
    }
    nBytesPerSigOp = gArgs.GetArg("-bytespersigop", nBytesPerSigOp);

    if (!g_wallet_init_interface.ParameterInteraction()) return false;

    fIsBareMultisigStd = gArgs.GetBoolArg("-permitbaremultisig", DEFAULT_PERMIT_BAREMULTISIG);
    fAcceptDatacarrier = gArgs.GetBoolArg("-datacarrier", DEFAULT_ACCEPT_DATACARRIER);
    nMaxDatacarrierBytes = gArgs.GetArg("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(gArgs.GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (gArgs.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices = ServiceFlags(nLocalServices | NODE_BLOOM);

    if (gArgs.GetArg("-rpcserialversion", DEFAULT_RPC_SERIALIZE_VERSION) < 0)
        return InitError("rpcserialversion must be non-negative.");

    if (gArgs.GetArg("-rpcserialversion", DEFAULT_RPC_SERIALIZE_VERSION) > 1)
        return InitError("unknown rpcserialversion requested.");

    nMaxTipAge = gArgs.GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE);
    fIsFakeNet = Params().NetworkIDString() == "regtest" && gArgs.GetArg("-dummypos", false);
    CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = Params().NetworkIDString() == "regtest" && gArgs.GetArg("-txnotokens", false);

    return true;
}

static bool LockDataDirectory(bool probeOnly)
{
    // Make sure only a single DeFi Blockchain process is using the data directory.
    fs::path datadir = GetDataDir();
    switch (util::LockDirectory(datadir, ".lock", probeOnly)) {
    case util::LockResult::ErrorWrite:
        return InitError(strprintf(_("Cannot write to data directory '%s'; check permissions.").translated, fs::PathToString(datadir)));
    case util::LockResult::ErrorLock:
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. %s is probably already running.").translated, fs::PathToString(datadir), PACKAGE_NAME));
    case util::LockResult::Success: return true;
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

bool AppInitSanityChecks()
{
    // ********************************************************* Step 4: sanity checks

    // Initialize elliptic curve code
    std::string sha256_algo = SHA256AutoDetect();
    LogPrintf("Using the '%s' SHA256 implementation\n", sha256_algo);
    RandomInit();
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
        return InitError(strprintf(_("Initialization sanity check failed. %s is shutting down.").translated, PACKAGE_NAME));

    // Probe the data directory lock to give an early error message, if possible
    // We cannot hold the data directory lock here, as the forking for daemon() hasn't yet happened,
    // and a fork will cause weird behavior to it.
    return LockDataDirectory(true);
}

bool AppInitLockDataDirectory()
{
    // After daemonization get the data directory lock again and hold on to it until exit
    // This creates a slight window for a race condition to happen, however this condition is harmless: it
    // will at most make us exit without printing a message to console.
    if (!LockDataDirectory(false)) {
        // Detailed error printed inside LockDataDirectory
        return false;
    }
    return true;
}

bool SetupLogging() {
    if (LogInstance().m_print_to_file) {
        if (gArgs.GetBoolArg("-shrinkdebugfile", LogInstance().DefaultShrinkDebugFile())) {
            // Do this first since it both loads a bunch of debug.log into memory,
            // and because this needs to happen before any other debug.log printing
            LogInstance().ShrinkDebugFile();
        }
    }
    if (!LogInstance().StartLogging()) {
            return InitError(strprintf("Could not open debug log file %s",
                                       fs::PathToString(LogInstance().m_file_path)));
    }

    if (!LogInstance().m_log_timestamps)
        LogPrintf("Startup time: %s\n", FormatISO8601DateTime(GetTime()));
    LogPrintf("Default data directory %s\n", fs::PathToString(GetDefaultDataDir()));
    LogPrintf("Using data directory %s\n", fs::PathToString(GetDataDir()));

    return true;
}

void SetupScriptCheckThreads() {
    int script_threads = gArgs.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (script_threads <= 0) {
        // -par=0 means autodetect (number of cores - 1 script threads)
        // -par=-n means "leave n cores free" (number of cores - n - 1 script threads)
        script_threads += GetNumCores();
        // DeFiChain specific:
        // Set this to a max value, since most custom TXs don't utilize this unfortunately
        // and is just a waste of resources.
        script_threads = std::min(script_threads, 4);
    }

    // Subtract 1 because the main thread counts towards the par threads
    script_threads = std::max(script_threads - 1, 0);

    // Number of script-checking threads <= MAX_SCRIPTCHECK_THREADS
    script_threads = std::min(script_threads, MAX_SCRIPTCHECK_THREADS);

    LogPrintf("Script verification uses %d additional threads\n", script_threads);
    if (script_threads >= 1) {
        g_parallel_script_checks = true;
        StartScriptCheckWorkerThreads(script_threads);
    }
}

bool SetupNetwork() {
    assert(!g_banman);
    g_banman = std::make_unique<BanMan>(GetDataDir() / "banlist.dat", &uiInterface, gArgs.GetArg("-bantime", DEFAULT_MISBEHAVING_BANTIME));
    assert(!g_connman);
    g_connman = std::unique_ptr<CConnman>(new CConnman(GetRand(std::numeric_limits<uint64_t>::max()), GetRand(std::numeric_limits<uint64_t>::max())));

    peerLogic.reset(new PeerLogicValidation(g_connman.get(), g_banman.get(), scheduler));
    RegisterValidationInterface(peerLogic.get());

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector<std::string> uacomments;
    for (const std::string& cmt : gArgs.GetArgs("-uacomment")) {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
            return InitError(strprintf(_("User Agent comment (%s) contains unsafe characters.").translated, cmt));
        uacomments.push_back(cmt);
    }
    strSubVersion = FormatUserAgentString(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH) {
        return InitError(strprintf(_("Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments.").translated,
            strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (gArgs.IsArgSet("-onlynet")) {
        std::set<enum Network> nets;
        for (const std::string& snet : gArgs.GetArgs("-onlynet")) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'").translated, snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetReachable(net, false);
        }
    }

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = gArgs.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

    bool proxyRandomize = gArgs.GetBoolArg("-proxyrandomize", DEFAULT_PROXYRANDOMIZE);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = gArgs.GetArg("-proxy", "");
    SetReachable(NET_ONION, false);
    if (proxyArg != "" && proxyArg != "0") {
        CService proxyAddr;
        if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup)) {
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'").translated, proxyArg));
        }

        proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'").translated, proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_ONION, addrProxy);
        SetNameProxy(addrProxy);
        SetReachable(NET_ONION, true); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = gArgs.GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetReachable(NET_ONION, false);
        } else {
            CService onionProxy;
            if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup)) {
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'").translated, onionArg));
            }
            proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'").translated, onionArg));
            SetProxy(NET_ONION, addrOnion);
            SetReachable(NET_ONION, true);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = gArgs.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = gArgs.GetBoolArg("-discover", true);
    g_relay_txes = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);

    for (const std::string& strAddr : gArgs.GetArgs("-externalip")) {
        CService addrLocal;
        if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid())
            AddLocal(addrLocal, LOCAL_MANUAL);
        else
            return InitError(ResolveErrMsg("externalip", strAddr));
    }
    return true;
}

void SetupCacheSizes(CacheSizes& cacheSizes) {
    // Cache size calculations
    int64_t totalCache = (gArgs.GetArg("-dbcache", nDefaultDbCache) << 20);
    totalCache = std::max(totalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    totalCache = std::min(totalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache

    cacheSizes.customCacheSize = totalCache; // used for customs
    cacheSizes.blockTreeDBCache = std::min(totalCache / 8, nMaxBlockDBCache << 20);
    totalCache -= cacheSizes.blockTreeDBCache;
    cacheSizes.txIndexCache = std::min(totalCache / 8, gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX) ? nMaxTxIndexCache << 20 : 0);
    totalCache -= cacheSizes.txIndexCache;

    cacheSizes.filterIndexCache = 0;
    if (!g_enabled_filter_types.empty()) {
        size_t n_indexes = g_enabled_filter_types.size();
        int64_t max_cache = std::min(totalCache / 8, max_filter_index_cache << 20);
        cacheSizes.filterIndexCache = max_cache / n_indexes;
        totalCache -= cacheSizes.filterIndexCache * n_indexes;
    }

    cacheSizes.coinDBCache = std::min(totalCache / 2, (totalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    cacheSizes.coinDBCache = std::min(cacheSizes.coinDBCache, nMaxCoinsDBCache << 20); // cap total coins db cache
    totalCache -= cacheSizes.coinDBCache;

    nCoinCacheUsage = totalCache; // the rest goes to in-memory cache
    nCustomMemUsage = std::max((totalCache >> 8), (nMinDbCache << 16)); // use significant less in-memory cache

    int64_t nMempoolSizeMax = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;

    // Log cache configurations
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Using %.1f MiB for block index database\n", cacheSizes.blockTreeDBCache * (1.0 / 1024 / 1024));
    if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
        LogPrintf("* Using %.1f MiB for transaction index database\n", cacheSizes.txIndexCache * (1.0 / 1024 / 1024));
    }
    for (BlockFilterType filter_type : g_enabled_filter_types) {
        LogPrintf("* Using %.1f MiB for %s block filter index database\n",
                  cacheSizes.filterIndexCache * (1.0 / 1024 / 1024), BlockFilterTypeName(filter_type));
    }
    LogPrintf("* Using %.1f MiB for chain state database\n", cacheSizes.coinDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1f MiB for in-memory UTXO set (plus up to %.1f MiB of unused mempool space)\n", nCoinCacheUsage * (1.0 / 1024 / 1024), nMempoolSizeMax * (1.0 / 1024 / 1024));
}

static void SetupRPCPorts(std::vector<std::string>& ethEndpoints, std::vector<std::string>& wsEndpoints, std::vector<std::string>& oceanEndpoints) {
    std::string default_address = "127.0.0.1";

    bool setAutoPort{};
    if (const auto autoPort = gArgs.GetArg("-ports", ""); autoPort == "auto") {
        setAutoPort = true;
    }

    // Determine which addresses to bind to ETH RPC server
    int eth_rpc_port = gArgs.GetArg("-ethrpcport", BaseParams().ETHRPCPort());
    if (eth_rpc_port == -1) {
        LogPrintf("ETH RPC server disabled.\n");
    } else {
        if (setAutoPort) {
            eth_rpc_port = 0;
        }
        if (!(gArgs.IsArgSet("-rpcallowip") && gArgs.IsArgSet("-ethrpcbind"))) { // Default to loopback if not allowing external IPs
            auto endpoint = default_address + ":" + std::to_string(eth_rpc_port);
            ethEndpoints.push_back(endpoint);
            if (gArgs.IsArgSet("-rpcallowip")) {
                LogPrintf("WARNING: option -rpcallowip was specified without -ethrpcbind; this doesn't usually make sense\n");
            }
            if (gArgs.IsArgSet("-ethrpcbind")) {
                LogPrintf("WARNING: option -ethrpcbind was ignored because -rpcallowip was not specified, refusing to allow everyone to connect\n");
            }
        } else if (gArgs.IsArgSet("-ethrpcbind")) { // Specific bind address
            for (const std::string& strETHRPCBind : gArgs.GetArgs("-ethrpcbind")) {
                int port = eth_rpc_port;
                std::string host;
                SplitHostPort(strETHRPCBind, port, host);
                auto endpoint = host + ":" + std::to_string(port);
                ethEndpoints.push_back(endpoint);
            }
        }
    }

    // Determine which addresses to bind to websocket server
    int ws_port = gArgs.GetArg("-wsport", BaseParams().WSPort());
    if (ws_port == -1) {
        LogPrintf("Websocket server disabled.\n");
    } else {
        if (setAutoPort) {
            ws_port = 0;
        }
        if (!(gArgs.IsArgSet("-rpcallowip") && gArgs.IsArgSet("-wsbind"))) { // Default to loopback if not allowing external IPs
            auto endpoint = default_address + ":" + std::to_string(ws_port);
            wsEndpoints.push_back(endpoint);
            if (gArgs.IsArgSet("-rpcallowip")) {
                LogPrintf("WARNING: option -rpcallowip was specified without -wsbind; this doesn't usually make sense\n");
            }
            if (gArgs.IsArgSet("-wsbind")) {
                LogPrintf("WARNING: option -wsbind was ignored because -rpcallowip was not specified, refusing to allow everyone to connect\n");
            }
        } else if (gArgs.IsArgSet("-wsbind")) { // Specific bind address
            for (const std::string& strWSBind : gArgs.GetArgs("-wsbind")) {
                int port = ws_port;
                std::string host;
                SplitHostPort(strWSBind, port, host);
                auto endpoint = host + ":" + std::to_string(port);
                wsEndpoints.push_back(endpoint);
            }
        }
    }

    // Determine which addresses to bind to ocean server
    int ocean_port = gArgs.GetArg("-oceanarchiveport", DEFAULT_OCEAN_SERVER_PORT);
    if (ocean_port == -1) {
        LogPrintf("Ocean server disabled.\n");
    } else {
        if (setAutoPort) {
            ocean_port = 0;
        }
        if (!(gArgs.IsArgSet("-rpcallowip") && gArgs.IsArgSet("-oceanarchivebind"))) { // Default to loopback if not allowing external IPs
            auto endpoint = default_address + ":" + std::to_string(ocean_port);
            oceanEndpoints.push_back(endpoint);
            if (gArgs.IsArgSet("-rpcallowip")) {
                LogPrintf("WARNING: option -rpcallowip was specified without -oceanarchivebind; this doesn't usually make sense\n");
            }
            if (gArgs.IsArgSet("-oceanarchivebind")) {
                LogPrintf("WARNING: option -oceanarchivebind was ignored because -rpcallowip was not specified, refusing to allow everyone to connect\n");
            }
        } else if (gArgs.IsArgSet("-oceanarchivebind")) { // Specific bind address
            for (const std::string& strOCEANBind : gArgs.GetArgs("-oceanarchivebind")) {
                int port = ocean_port;
                std::string host;
                SplitHostPort(strOCEANBind, port, host);
                auto endpoint = host + ":" + std::to_string(port);
                oceanEndpoints.push_back(endpoint);
            }
        }
    }
}

void SetupAnchorSPVDatabases(bool resync, int64_t customCache) {
    // Close and open database
    panchors.reset();
    panchors = std::make_unique<CAnchorIndex>(customCache, false, gArgs.GetBoolArg("-spv", true) && resync);

    // load anchors after spv due to spv (and spv height) not set before (no last height yet)
    if (gArgs.GetBoolArg("-spv", true)) {
        // Close database
        spv::pspv.reset();

        // Open database based on network
        if (Params().NetworkIDString() == "regtest") {
            spv::pspv = std::make_unique<spv::CFakeSpvWrapper>();
        } else if (Params().NetworkIDString() == "test" || Params().NetworkIDString() == "changi" || Params().NetworkIDString() == "devnet") {
            spv::pspv = std::make_unique<spv::CSpvWrapper>(false, customCache, false, resync);
        } else {
            spv::pspv = std::make_unique<spv::CSpvWrapper>(true, customCache, false, resync);
        }
    }
}

bool SetupInterruptArg(const std::string &argName, std::string &hashStore, int &heightStore) {
    // Experimental: Block height or hash to invalidate on and stop sync
    auto val = gArgs.GetArg(argName, "");
    auto flagName = argName.substr(1);
    if (val.empty())
        return false;
    if (val.size() == 64) {
        hashStore = val;
        LogPrintf("flag: %s hash: %s\n", flagName, hashStore);
    } else {
        std::stringstream ss(val);
        ss >> heightStore;
       if (heightStore) {
            LogPrintf("flag: %s height: %d\n", flagName, heightStore);
       } else {
            LogPrintf("%s: invalid hash or height provided: %s\n", flagName, val);
       }
    }
    return true;
}

void SetupInterrupts() {
    fInterrupt = SetupInterruptArg("-interrupt-block", fInterruptBlockHash, fInterruptBlockHeight);
}

bool AppInitMain(InitInterfaces& interfaces)
{
    const CChainParams& chainparams = Params();
    // ********************************************************* Step 5: application initialization
    if (!CreatePidFile()) {
        return false;
    }

    if (!SetupLogging()) {
        return false;
    }

    // Only log conf file usage message if conf file actually exists.
    fs::path config_file_path = GetConfigFile(gArgs.GetArg("-conf", DEFI_CONF_FILENAME));
    if (fs::exists(config_file_path)) {
        LogPrintf("Config file: %s\n", fs::PathToString(config_file_path));
    } else if (gArgs.IsArgSet("-conf")) {
        // Warn if no conf file exists at path provided by user
        InitWarning(strprintf(_("The specified config file %s does not exist\n").translated, fs::PathToString(config_file_path)));
    } else {
        // Not categorizing as "Warning" because it's the default behavior
        LogPrintf("Config file: %s (not found, skipping)\n", fs::PathToString(config_file_path));
    }

    LogPrintf("Using at most %i automatic connections (%i file descriptors available)\n", nMaxConnections, nFD);

    // Warn about relative -datadir path.
    if (gArgs.IsArgSet("-datadir") && !gArgs.GetPathArg("-datadir").is_absolute()) {
        LogPrintf("Warning: relative datadir option '%s' specified, which will be interpreted relative to the " /* Continued */
                  "current working directory '%s'. This is fragile, because if defid is started in the future "
                  "from a different location, it will be unable to locate the current data files. There could "
                  "also be data loss if defi is started while in a temporary directory.\n",
            gArgs.GetArg("-datadir", ""), fs::PathToString(fs::current_path()));
    }

    InitSignatureCache();
    InitScriptExecutionCache();
    RPCMetadata::InitFromArgs(gArgs);
    SetupScriptCheckThreads();

    // Start the lightweight task scheduler thread
    scheduler.m_service_thread = std::thread([&] { TraceThread("scheduler", [&] { scheduler.serviceQueue(); }); });
    GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
    GetMainSignals().RegisterWithMempoolSignals(mempool);

    // Create client interfaces for wallets that are supposed to be loaded
    // according to -wallet and -disablewallet options. This only constructs
    // the interfaces, it doesn't load wallet data. Wallets actually get loaded
    // when load() and start() interface methods are called below.
    g_wallet_init_interface.Construct(interfaces);

    /* Register RPC commands regardless of -server setting so they will be
     * available in the GUI RPC console even if external calls are disabled.
     */
    RegisterAllCoreRPCCommands(tableRPC);
    for (const auto& client : interfaces.chain_clients) {
        client->registerRpcs();
    }
    g_rpc_interfaces = &interfaces;
#if ENABLE_ZMQ
    RegisterZMQRPCCommands(tableRPC);
#endif

    // Remove ports.lock on startup in case of an unclean shutdown.
    RemovePortUsage();

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (gArgs.GetBoolArg("-server", false))
    {
        uiInterface.InitMessage_connect(SetRPCWarmupStatus);
        if (!AppInitServers())
            return InitError(_("Unable to start HTTP server. See debug log for details.").translated);
    }

    // ********************************************************* Step 6: verify wallet database integrity
    for (const auto& client : interfaces.chain_clients) {
        if (!client->verify()) {
            return false;
        }
    }

    // ********************************************************* Step 7: network initialization
    // Note that we absolutely cannot open any actual connections
    // until the very end ("start node") as the UTXO/block state
    // is not yet setup and may end up being set up twice if we
    // need to reindex later.
    if (!SetupNetwork()) {
        return false;
    }

#if ENABLE_ZMQ
    g_zmq_notification_interface = CZMQNotificationInterface::Create();

    if (g_zmq_notification_interface) {
        RegisterValidationInterface(g_zmq_notification_interface);
    }
#endif
    uint64_t nMaxOutboundLimit = 0; //unlimited unless -maxuploadtarget is set
    uint64_t nMaxOutboundTimeframe = MAX_UPLOAD_TIMEFRAME;

    if (gArgs.IsArgSet("-maxuploadtarget")) {
        nMaxOutboundLimit = gArgs.GetArg("-maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET)*1024*1024;
    }

    // Setup interrupts
    SetupInterrupts();

    // ********************************************************* Step 8: load block chain
    CacheSizes nCacheSizes;
    SetupCacheSizes(nCacheSizes);
    InitDfTxGlobalTaskPool();

    bool fLoaded = false;
    fReindex = gArgs.GetBoolArg("-reindex", false);
    bool fReindexChainState = gArgs.GetBoolArg("-reindex-chainstate", false);
    while (!fLoaded && !ShutdownRequested()) {
        bool fReset = fReindex;
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index...").translated);

        do {
            const int64_t load_block_index_start_time = GetTimeMillis();
            bool is_coinsview_empty;
            try {
                LOCK(cs_main);
                // This statement makes ::ChainstateActive() usable.
                g_chainstate = std::make_unique<CChainState>();
                UnloadBlockIndex();

                // new CBlockTreeDB tries to delete the existing file, which
                // fails if it's still open from the previous loop. Close it first:
                pblocktree.reset();
                pblocktree.reset(new CBlockTreeDB(nCacheSizes.blockTreeDBCache, false, fReset));

                if (fReset) {
                    pblocktree->WriteReindexing(true);
                    //If we're reindexing in prune mode, wipe away unusable block files and all undo data files
                    if (fPruneMode)
                        CleanupBlockRevFiles();
                }

                if (ShutdownRequested()) break;

                // LoadBlockIndex will load fHavePruned if we've ever removed a
                // block file from disk.
                // Note that it also sets fReindex based on the disk flag!
                // From here on out fReindex and fReset mean something different!
                if (!LoadBlockIndex(chainparams)) {
                    if (ShutdownRequested()) break;
                    strLoadError = _("Error loading block database").translated;
                    break;
                }

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
                if (!::BlockIndex().empty() &&
                        !LookupBlockIndex(chainparams.GetConsensus().hashGenesisBlock)) {
                    return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?").translated);
                }

                // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
                // in the past, but is now trying to run unpruned.
                if (fHavePruned && !fPruneMode) {
                    strLoadError = _("You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain").translated;
                    break;
                }

                // At this point blocktree args are consistent with what's on disk.
                // If we're not mid-reindex (based on disk + args), add a genesis block on disk
                // (otherwise we use the one already on disk).
                // This is called again in ThreadImport after the reindex completes.
                if (!fReindex && !LoadGenesisBlock(chainparams)) {
                    strLoadError = _("Error initializing block database").translated;
                    break;
                }

                // At this point we're either in reindex or we've loaded a useful
                // block tree into BlockIndex()!

                ::ChainstateActive().InitCoinsDB(
                    /* cache_size_bytes */ nCacheSizes.coinDBCache,
                    /* in_memory */ false,
                    /* should_wipe */ fReset || fReindexChainState);

                ::ChainstateActive().CoinsErrorCatcher().AddReadErrCallback([]() {
                    uiInterface.ThreadSafeMessageBox(
                        _("Error reading from database, shutting down.").translated,
                        "", CClientUIInterface::MSG_ERROR);
                });

                pcustomcsDB.reset();
                pcustomcsDB = std::make_unique<CStorageLevelDB>(GetDataDir() / "enhancedcs", nCacheSizes.customCacheSize, false, fReset || fReindexChainState);
                pcustomcsview.reset();
                pcustomcsview = std::make_unique<CCustomCSView>(*pcustomcsDB.get());

                if (!fReset && !fReindexChainState) {
                    if (!pcustomcsDB->IsEmpty() && pcustomcsview->GetDbVersion() != CCustomCSView::DbVersion) {
                        strLoadError = _("Account database is unsuitable").translated;
                        break;
                    }
                }

                // Ensure we are on latest DB version
                pcustomcsview->SetDbVersion(CCustomCSView::DbVersion);

                // make account history db
                paccountHistoryDB.reset();
                if (gArgs.GetBoolArg("-acindex", DEFAULT_ACINDEX)) {
                    paccountHistoryDB = std::make_unique<CAccountHistoryStorage>(GetDataDir() / "history", nCacheSizes.customCacheSize, false, fReset || fReindexChainState);
                    paccountHistoryDB->CreateMultiIndexIfNeeded();
                }

                pburnHistoryDB.reset();
                pburnHistoryDB = std::make_unique<CBurnHistoryStorage>(GetDataDir() / "burn", nCacheSizes.customCacheSize, false, fReset || fReindexChainState);
                pburnHistoryDB->CreateMultiIndexIfNeeded();

                // Create vault history DB
                pvaultHistoryDB.reset();
                if (gArgs.GetBoolArg("-vaultindex", DEFAULT_VAULTINDEX)) {
                    pvaultHistoryDB = std::make_unique<CVaultHistoryStorage>(GetDataDir() / "vault", nCacheSizes.customCacheSize, false, fReset || fReindexChainState);
                }

                // If necessary, upgrade from older database format.
                // This is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
                if (!::ChainstateActive().CoinsDB().Upgrade()) {
                    strLoadError = _("Error upgrading chainstate database").translated;
                    break;
                }

                // Wipe EVM folder on reindex
                if (fReset || fReindexChainState) {
                    auto res = XResultStatusLogged(ain_rs_wipe_evm_folder(result));
                    if (!res) {
                        return false;
                    }
                }

                // All DBs have been initialized. We start the rust core services to ensure that
                // it's initialized as late as possible, but before anything can start rolling blocks
                // back or forth. `ReplayBlocks, VerifyDB` etc.
                auto res = XResultStatusLogged(ain_rs_init_core_services(result));
                if (!res) return false;

                // ReplayBlocks is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
                if (!ReplayBlocks(chainparams, &::ChainstateActive().CoinsDB(), pcustomcsview.get())) {
                    strLoadError = _("Unable to replay blocks. You will need to rebuild the database using -reindex-chainstate.").translated;
                    break;
                }

                // The on-disk coinsdb is now in a good state, create the cache
                ::ChainstateActive().InitCoinsCache();
                assert(::ChainstateActive().CanFlushToDisk());

                is_coinsview_empty = fReset || fReindexChainState ||
                    ::ChainstateActive().CoinsTip().GetBestBlock().IsNull();
                if (!is_coinsview_empty) {
                    // LoadChainTip sets ::ChainActive() based on CoinsTip()'s best block
                    if (!LoadChainTip(chainparams)) {
                        strLoadError = _("Error initializing block database").translated;
                        break;
                    }
                    assert(::ChainActive().Tip() != nullptr);
                }

                auto dexStats = gArgs.GetBoolArg("-dexstats", DEFAULT_DEXSTATS);
                pcustomcsview->SetDexStatsEnabled(dexStats);

                if (!fReset && !fReindexChainState && !pcustomcsDB->IsEmpty() && dexStats) {
                    // force reindex if there is no dex data at the tip
                    PoolHeightKey anyPoolSwap{DCT_ID{}, ~0u};
                    auto it = pcustomcsview->LowerBound<CPoolPairView::ByPoolSwap>(anyPoolSwap);
                    auto shouldReindex = it.Valid();
                    auto lastHeight = pcustomcsview->GetDexStatsLastHeight();
                    if (lastHeight.has_value())
                        shouldReindex &= !(*lastHeight == ::ChainActive().Tip()->nHeight);

                    if (shouldReindex) {
                        strLoadError = _("Live dex needs reindex").translated;
                        break;
                    }
                }

                if (::ChainActive().Tip() != nullptr) {
                    // Prune based on checkpoints
                    auto &checkpoints = chainparams.Checkpoints().mapCheckpoints;
                    auto it = checkpoints.lower_bound(::ChainActive().Tip()->nHeight);
                    if (it != checkpoints.begin()) {
                        auto &[height, _] = *(--it);
                        std::vector<unsigned char> compactBegin;
                        std::vector<unsigned char> compactEnd;
                        PruneCheckpoint(*pcustomcsview, height, compactBegin, compactEnd);
                        if (!compactBegin.empty() && !compactEnd.empty()) {
                            pcustomcsview->Flush();
                            pcustomcsDB->Flush();
                            auto time = GetTimeMillis();
                            pcustomcsDB->Compact(compactBegin, compactEnd);
                            compactBegin.clear();
                            compactEnd.clear();
                            LogPrint(BCLog::BENCH, "    - DB compacting takes: %dms\n", GetTimeMillis() - time);
                        }
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s\n", e.what());
                strLoadError = _("Error opening block database").translated;
                break;
            }

            if (!fReset) {
                // Note that RewindBlockIndex MUST run even if we're about to -reindex-chainstate.
                // It both disconnects blocks based on ::ChainActive(), and drops block data in
                // BlockIndex() based on lack of available witness data.
                uiInterface.InitMessage(_("Rewinding blocks...").translated);
                if (!RewindBlockIndex(chainparams)) {
                    strLoadError = _("Unable to rewind the database to a pre-fork state. You will need to redownload the blockchain").translated;
                    break;
                }
            }

            try {
                LOCK(cs_main);
                if (!is_coinsview_empty) {
                    uiInterface.InitMessage(_("Verifying blocks...").translated);
                    if (fHavePruned && gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS) > MIN_BLOCKS_TO_KEEP) {
                        LogPrintf("Prune: pruned datadir may not have more than %d blocks; only checking available blocks\n",
                            MIN_BLOCKS_TO_KEEP);
                    }

                    CBlockIndex* tip = ::ChainActive().Tip();
                    RPCNotifyBlockChange(true, tip);
                    if (tip && tip->nTime > GetAdjustedTime() + 2 * 60 * 60) {
                        strLoadError = _("The block database contains a block which appears to be from the future. "
                                "This may be due to your computer's date and time being set incorrectly. "
                                "Only rebuild the block database if you are sure that your computer's date and time are correct").translated;
                        break;
                    }

                    if (!CVerifyDB().VerifyDB(chainparams, &::ChainstateActive().CoinsDB(), gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL),
                                  gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS))) {
                        strLoadError = _("Corrupted block database detected").translated;
                        break;
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s\n", e.what());
                strLoadError = _("Error opening block database").translated;
                break;
            }

            // State consistency check is skipped for regtest (EVM state can be initialized with state input)
            if (Params().NetworkIDString() != CBaseChainParams::REGTEST)  {
                // Check that EVM db and DVM db states are consistent
                auto res = XResultValueLogged(evm_try_get_latest_block_hash(result));
                if (res) {
                    // After EVM activation
                    auto evmBlockHash = uint256::FromByteArray(*res).GetHex();
                    auto dvmBlockHash = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, evmBlockHash);
                    if (!dvmBlockHash.val.has_value()) {
                        strLoadError = _("Unable to get DVM block hash from latest EVM block hash, inconsistent chainstate detected. "
                                        "This may be due to corrupted block databases between DVM and EVM, and you will need to "
                                        "rebuild the database using -reindex.").translated;
                        break;
                    }
                    CBlockIndex *pindex = LookupBlockIndex(uint256S(*dvmBlockHash.val));
                    if (!pindex) {
                        strLoadError = _("Unable to get DVM block index from block hash, possible corrupted block database detected. "
                                        "You will need to rebuild the database using -reindex.").translated;
                        break;
                    }
                    auto dvmBlockHeight = pindex->nHeight;

                    if (dvmBlockHeight != ::ChainActive().Tip()->nHeight) {
                        strLoadError = _("Inconsistent chainstate detected between DVM block database and EVM block database. "
                                        "This may be due to corrupted block databases between DVM and EVM, and you will need to "
                                        "rebuild the database using -reindex.").translated;
                        break;
                    }
                }
            }

            fLoaded = true;
            LogPrintf(" block index %15dms\n", GetTimeMillis() - load_block_index_start_time);
        } while(false);

        if (!fLoaded && !ShutdownRequested()) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeQuestion(
                    strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?").translated,
                    strLoadError + ".\nPlease restart with -reindex or -reindex-chainstate to recover.",
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    AbortShutdown();
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    CAutoFile est_filein(fsbridge::fopen(est_path, "rb"), SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        ::feeEstimator.Read(est_filein);
    fFeeEstimatesInitialized = true;

    // ********************************************************* Step 9: start indexers
    if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
        g_txindex = std::make_unique<TxIndex>(nCacheSizes.txIndexCache, false, fReindex);
        g_txindex->Start();
    }

    for (const auto& filter_type : g_enabled_filter_types) {
        InitBlockFilterIndex(filter_type, nCacheSizes.filterIndexCache, false, fReindex);
        GetBlockFilterIndex(filter_type)->Start();
    }

    // ********************************************************* Step 10.a: load wallet
    for (const auto& client : interfaces.chain_clients) {
        if (!client->load()) {
            return false;
        }
    }

    // ********************************************************* Step 10.b: load anchors / SPV wallet

    try {
        LOCK(cs_main);

        panchorauths.reset();
        panchorauths = std::make_unique<CAnchorAuthIndex>();
        panchorAwaitingConfirms.reset();
        panchorAwaitingConfirms = std::make_unique<CAnchorAwaitingConfirms>();
        SetupAnchorSPVDatabases(gArgs.GetBoolArg("-spv_resync", fReindex || fReindexChainState), nCacheSizes.customCacheSize);

        // Check if DB version changed
        if (spv::pspv && SPV_DB_VERSION != spv::pspv->GetDBVersion()) {
            SetupAnchorSPVDatabases(true, nCacheSizes.customCacheSize);
            assert(spv::pspv->SetDBVersion() == SPV_DB_VERSION);
            LogPrintf("Cleared anchor and SPV database. SPV DB version set to %d\n", SPV_DB_VERSION);
        }

        if (spv::pspv) {
            spv::pspv->Load();
        }
        panchors->Load();

    } catch (const std::exception& e) {
        LogPrintf("%s\n", e.what());
        return InitError("Error opening SPV database");
    }

    // ********************************************************* Step 11: data directory maintenance

    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode) {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices = ServiceFlags(nLocalServices & ~NODE_NETWORK);
        if (!fReindex) {
            uiInterface.InitMessage(_("Pruning blockstore...").translated);
            ::ChainstateActive().PruneAndFlush();
        }
    }

    if (chainparams.GetConsensus().SegwitHeight != std::numeric_limits<int>::max()) {
        // Advertise witness capabilities.
        // The option to not set NODE_WITNESS is only used in the tests and should be removed.
        nLocalServices = ServiceFlags(nLocalServices | NODE_WITNESS);
    }

    if (gArgs.IsArgSet("-consolidaterewards")) {
        const std::vector<std::string> tokenSymbolArgs = gArgs.GetArgs("-consolidaterewards");
        auto fullRewardConsolidation = false;
        for (const auto &tokenSymbolInput : tokenSymbolArgs) {
            auto tokenSymbol = trim_ws(tokenSymbolInput);
            if (tokenSymbol.empty()) {
                fullRewardConsolidation = true;
                break;
            }
        }

        {
            auto [hashHex, hashHexNoUndo, hashHexAccount] = GetDVMDBHashes(*pcustomcsview);
            LogPrintf("Pre-consolidate rewards for DVM hash: %s hash-no-undo: %s hash-account: %s\n", hashHex, hashHexNoUndo, hashHexAccount);
        }

        if (fullRewardConsolidation) {
            LogPrintf("Consolidate rewards for all addresses..\n");

            std::unordered_set<CScript, CScriptHasher> ownersToConsolidate;
            pcustomcsview->ForEachBalance([&](const CScript &owner, CTokenAmount balance) {
                if (balance.nValue > 0) {
                    ownersToConsolidate.emplace(owner);
                }
                return true;
            });
            ConsolidateRewards(*pcustomcsview, ::ChainActive().Height(), ownersToConsolidate, true, true);
        } else {
            //one set for all tokens, ConsolidateRewards runs on the address, so no need to run multiple times for multiple token inputs
            std::unordered_set<CScript, CScriptHasher> ownersToConsolidate;
            for (const auto &tokenSymbolInput : tokenSymbolArgs) {
                auto tokenSymbol = trim_ws(tokenSymbolInput);
                LogPrintf("Consolidate rewards for token: %s\n", tokenSymbol);
                auto token = pcustomcsview->GetToken(tokenSymbol);
                if (!token) {
                    InitError(strprintf("Invalid token \"%s\" for reward consolidation.\n", tokenSymbol));
                    return false;
                }

                pcustomcsview->ForEachBalance([&, tokenId = token->first](const CScript &owner, CTokenAmount balance) {
                    if (tokenId.v == balance.nTokenId.v && balance.nValue > 0) {
                        ownersToConsolidate.emplace(owner);
                    }
                    return true;
                });
            }
            ConsolidateRewards(*pcustomcsview, ::ChainActive().Height(), ownersToConsolidate, true, true);
        }
        pcustomcsview->Flush();
        pcustomcsDB->Flush();

        {
            auto [hashHex, hashHexNoUndo, hashHexAccount] = GetDVMDBHashes(*pcustomcsview);
            LogPrintf("Post-consolidate rewards for DVM hash: %s hash-no-undo: %s hash-account: %s\n", hashHex, hashHexNoUndo, hashHexAccount);
        }
    }

    // ********************************************************* Step 12: import blocks

    if (!CheckDiskSpace(GetDataDir())) {
        InitError(strprintf(_("Error: Disk space is low for %s").translated, fs::quoted(fs::PathToString(GetDataDir()))));
        return false;
    }
    if (!CheckDiskSpace(GetBlocksDir())) {
        InitError(strprintf(_("Error: Disk space is low for %s").translated, fs::quoted(fs::PathToString(GetBlocksDir()))));
        return false;
    }

    // Either install a handler to notify us when genesis activates, or set fHaveGenesis directly.
    // No locking, as this happens before any background thread is started.
    boost::signals2::connection block_notify_genesis_wait_connection;
    if (::ChainActive().Tip() == nullptr) {
        block_notify_genesis_wait_connection = uiInterface.NotifyBlockTip_connect(BlockNotifyGenesisWait);
    } else {
        fHaveGenesis = true;
    }

#if HAVE_SYSTEM
    if (gArgs.IsArgSet("-blocknotify"))
        uiInterface.NotifyBlockTip_connect(BlockNotifyCallback);
#endif

    std::vector<fs::path> vImportFiles;
    for (const std::string& strFile : gArgs.GetArgs("-loadblock")) {
        vImportFiles.push_back(fs::PathFromString(strFile));
    }

    threadGroup.emplace_back(ThreadImport, vImportFiles);

    // Wait for genesis block to be processed
    {
        WAIT_LOCK(g_genesis_wait_mutex, lock);
        // We previously could hang here if StartShutdown() is called prior to
        // ThreadImport getting started, so instead we just wait on a timer to
        // check ShutdownRequested() regularly.
        while (!fHaveGenesis && !ShutdownRequested()) {
            g_genesis_wait_cv.wait_for(lock, std::chrono::milliseconds(500));
        }
        block_notify_genesis_wait_connection.disconnect();
    }

    // Set snapshot now chain has loaded
    psnapshotManager = std::make_unique<CSnapshotManager>(pcustomcsview, paccountHistoryDB, pvaultHistoryDB);


    if (ShutdownRequested()) {
        return false;
    }

    // ********************************************************* Step 13: start node


    int chain_active_height;

    //// debug print
    {
        LOCK(cs_main);
        LogPrintf("block tree size = %u\n", ::BlockIndex().size());
        chain_active_height = ::ChainActive().Height();
    }
    LogPrintf("nBestHeight = %d\n", chain_active_height);

    if (gArgs.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl();

    Discover();

    // Map ports with UPnP
    if (gArgs.GetBoolArg("-upnp", DEFAULT_UPNP)) {
        StartMapPort();
    }

    CConnman::Options connOptions;
    connOptions.nLocalServices = nLocalServices;
    connOptions.nMaxConnections = nMaxConnections;
    connOptions.nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, connOptions.nMaxConnections);
    connOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
    connOptions.nMaxFeeler = 1;
    connOptions.nBestHeight = chain_active_height;
    connOptions.uiInterface = &uiInterface;
    connOptions.m_banman = g_banman.get();
    connOptions.m_msgproc = peerLogic.get();
    connOptions.nSendBufferMaxSize = 1000*gArgs.GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
    connOptions.nReceiveFloodSize = 1000*gArgs.GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);
    connOptions.m_added_nodes = gArgs.GetArgs("-addnode");

    connOptions.nMaxOutboundTimeframe = nMaxOutboundTimeframe;
    connOptions.nMaxOutboundLimit = nMaxOutboundLimit;
    connOptions.m_peer_connect_timeout = peer_connect_timeout;

    for (const std::string& strBind : gArgs.GetArgs("-bind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false)) {
            return InitError(ResolveErrMsg("bind", strBind));
        }
        connOptions.vBinds.push_back(addrBind);
    }
    for (const std::string& strBind : gArgs.GetArgs("-whitebind")) {
        NetWhitebindPermissions whitebind;
        std::string error;
        if (!NetWhitebindPermissions::TryParse(strBind, whitebind, error)) return InitError(error);
        connOptions.vWhiteBinds.push_back(whitebind);
    }

    for (const auto& net : gArgs.GetArgs("-whitelist")) {
        NetWhitelistPermissions subnet;
        std::string error;
        if (!NetWhitelistPermissions::TryParse(net, subnet, error)) return InitError(error);
        connOptions.vWhitelistedRange.push_back(subnet);
    }

    connOptions.vSeedNodes = gArgs.GetArgs("-seednode");

    // Initiate outbound connections unless connect=0
    connOptions.m_use_addrman_outgoing = !gArgs.IsArgSet("-connect");
    if (!connOptions.m_use_addrman_outgoing) {
        const auto connect = gArgs.GetArgs("-connect");
        if (connect.size() != 1 || connect[0] != "0") {
            connOptions.m_specified_outgoing = connect;
        }
    }
    if (!g_connman->Start(scheduler, connOptions)) {
        return false;
    }

    // ********************************************************* Step 14: finished

    SetRPCWarmupFinished();
    // Start the ETH RPC, gRPC and websocket servers
    // We start the evm RPC servers as late as possible.
    {
        std::vector<std::string> eth_endpoints, ws_endpoints, ocean_endpoints;
        SetupRPCPorts(eth_endpoints, ws_endpoints, ocean_endpoints);
        CrossBoundaryResult result;

        // Bind ETH RPC addresses
        for (auto it = eth_endpoints.begin(); it != eth_endpoints.end(); ++it) {
            LogPrint(BCLog::HTTP, "Binding ETH RPC server on endpoint %s\n", *it);
            const auto addr = rs_try_from_utf8(result, ffi_from_string_to_slice(*it));
            if (!result.ok) {
                LogPrint(BCLog::HTTP, "Invalid ETH RPC address, not UTF-8 valid\n");
                return false;
            }
            auto res =  XResultStatusLogged(ain_rs_init_network_json_rpc_service(result, addr))
            if (!res) {
                LogPrintf("Binding ETH RPC server on endpoint %s failed.\n", *it);
                return false;
            }
        }

        if (gArgs.GetBoolArg("-ethsubscription", DEFAULT_ETH_SUBSCRIPTION_ENABLED)) {
            // bind websocket addresses
            for (auto it = ws_endpoints.begin(); it != ws_endpoints.end(); ++it) {
                LogPrint(BCLog::HTTP, "Binding websocket server on endpoint %s\n", *it);
                const auto addr = rs_try_from_utf8(result, ffi_from_string_to_slice(*it));
                if (!result.ok) {
                    LogPrint(BCLog::HTTP, "Invalid websocket address, not UTF-8 valid\n");
                    return false;
                }
                auto res =  XResultStatusLogged(ain_rs_init_network_subscriptions_service(result, addr))
                if (!res) {
                    LogPrintf("Binding websocket server on endpoint %s failed.\n", *it);
                    return false;
                }
            }
        }

        // bind ocean REST addresses
        if (gArgs.GetBoolArg("-oceanarchiveserver", DEFAULT_OCEAN_SERVER_ENABLED)) {
            // bind ocean addresses
            for (auto it = ocean_endpoints.begin(); it != ocean_endpoints.end(); ++it) {
                LogPrint(BCLog::HTTP, "Binding ocean server on endpoint %s\n", *it);
                const auto addr = rs_try_from_utf8(result, ffi_from_string_to_slice(*it));
                if (!result.ok) {
                    LogPrint(BCLog::HTTP, "Invalid ocean address, not UTF-8 valid\n");
                    return false;
                }
                auto res =  XResultStatusLogged(ain_rs_init_network_rest_ocean(result, addr))
                if (!res) {
                    LogPrintf("Binding ocean server on endpoint %s failed.\n", *it);
                    return false;
                }
            }
        }
    }
    uiInterface.InitMessage(_("Done loading").translated);

    for (const auto& client : interfaces.chain_clients) {
        client->start(scheduler);
    }

    scheduler.scheduleEvery([]{
        g_banman->DumpBanlist();
    }, DUMP_BANS_INTERVAL * 1000);

    // ********************************************************* Step XX.a: create mocknet MN
    // MN: 0000000000000000000000000000000000000000000000000000000000000000

    if (fMockNetwork && HasWallets()) {
        // Mainnet: df1qmnv9c6jt9fmgmzvynp8r0gjm39awa027hsfzq3
        // Test networks: tf1qmnv9c6jt9fmgmzvynp8r0gjm39awa027yqn3q4
        const auto rawPrivKey = uint256S("4c0883a69102937d623414e5f791a5a5a4591d899d0e3a1b03f0b7421932b72e");
        CKey key;
        key.Set(rawPrivKey.begin(), rawPrivKey.end(), true);
        const auto pubkey = key.GetPubKey();
        const auto keyID = pubkey.GetID();
        WitnessV0KeyHash dest(pubkey);

        {
            auto pwallet = GetWallets()[0];
            LOCK(pwallet->cs_wallet);

            pwallet->SetAddressBook(dest, "", "receive");
            pwallet->ImportPrivKeys({{keyID, key}}, GetTime());
        }

        // Set operator for mining
        gArgs.ForceSetArg("-masternode_operator", EncodeDestination(dest));

        // Create masternode
        CMasternode node;
        node.creationHeight = chain_active_height - GetMnActivationDelay(chain_active_height);
        node.ownerType = WitV0KeyHashType;
        node.ownerAuthAddress = keyID;
        node.operatorType = WitV0KeyHashType;
        node.operatorAuthAddress = keyID;
        node.version = CMasternode::VERSION0;

        {
            LOCK(cs_main);
            pcustomcsview->CreateMasternode(uint256S(std::string{64, '0'}), node, CMasternode::ZEROYEAR);
        }
    }

    // ********************************************************* Step XX.b: start spv
    if (spv::pspv)
    {
        spv::pspv->Connect();
    }


    // ********************************************************* Step 15: start genesis ocean indexing
    if (gArgs.GetBoolArg("-oceanarchive", DEFAULT_OCEAN_INDEXER_ENABLED) ||
        gArgs.GetBoolArg("-expr-oceanarchive", DEFAULT_OCEAN_INDEXER_ENABLED)) {
        const CBlock &block = chainparams.GenesisBlock();

        const CBlockIndex* pblockindex;
        const CBlockIndex* tip;
        {
            LOCK(cs_main);

            pblockindex = LookupBlockIndex(block.GetHash());
            assert(pblockindex);

            tip = ::ChainActive().Tip();
        }

        const UniValue b = blockToJSON(*pcustomcsview, block, tip, pblockindex, true, 2);

        if (bool isIndexed = OceanIndex(b, 0); !isIndexed) {
            return false;
        }

        LogPrintf("WARNING: -expr-oceanarchive flag is turned on. This feature is not yet stable. Please do not use in production unless you're aware of the risks\n");
    }

    // ********************************************************* Step 16: start ocean catchup
    if (!CatchupOceanIndexer()) {
        return false;
    }

    // ********************************************************* Step 17: start minter thread
    if(gArgs.GetBoolArg("-gen", DEFAULT_GENERATE)) {
        if (!pos::StartStakingThreads(threadGroup)) {
            return false;
        }
    }

    return true;
}

// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <spv/spv_wrapper.h>

#include <base58.h>
#include <chainparams.h>
#include <core_io.h>
#include <masternodes/anchors.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <sync.h>
#include <util/strencodings.h>
#include <wallet/wallet.h>

#include <spv/support/BRKey.h>
#include <spv/support/BRAddress.h>
#include <spv/support/BRBIP39Mnemonic.h>
#include <spv/support/BRBIP32Sequence.h>
#include <spv/bitcoin/BRPeerManager.h>
#include <spv/bitcoin/BRChainParams.h>
#include <spv/bcash/BRBCashParams.h>
#include <spv/support/BRLargeInt.h>
#include <spv/support/BRSet.h>

#include <string.h>
#include <inttypes.h>

#include <boost/algorithm/string/replace.hpp>

extern RecursiveMutex cs_main;

RecursiveMutex cs_spvcallback;

const int ENOSPV         = 100000;
const int EPARSINGTX     = 100001;
const int ETXNOTSIGNED   = 100002;

std::string DecodeSendResult(int result)
{
    switch (result) {
        case ENOSPV:
            return "SPV module disabled";
        case EPARSINGTX:
            return "Cannot parse transaction";
        case ETXNOTSIGNED:
            return "Transaction not signed";
        default:
            return strerror(result);
    }
}

namespace spv
{

std::unique_ptr<CSpvWrapper> pspv;

// Prefixes to the masternodes database (masternodes/)
static const char DB_SPVBLOCKS = 'B';     // spv "blocks" table
static const char DB_SPVTXS    = 'T';     // spv "tx2msg" table
static const char DB_VERSION   = 'V';

uint64_t const DEFAULT_BTC_FEERATE = TX_FEE_PER_KB;
uint64_t const DEFAULT_BTC_FEE_PER_KB = DEFAULT_FEE_PER_KB;

/// spv wallet manager's callbacks wrappers:
void balanceChanged(void *info, uint64_t balance)
{
    /// @attention called under spv manager lock!!!
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnBalanceChanged(balance);
}

void txAdded(void *info, BRTransaction *tx)
{
    /// @attention called under spv manager lock!!!
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnTxAdded(tx);
}

void txUpdated(void *info, const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp, const UInt256& blockHash)
{
    /// @attention called under spv manager lock!!!
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnTxUpdated(txHashes, txCount, blockHeight, timestamp, blockHash);
}

void txDeleted(void *info, UInt256 txHash, int notifyUser, int recommendRescan)
{
    /// @attention called under spv manager lock!!!
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnTxDeleted(txHash, notifyUser, recommendRescan);
}

/// spv peer manager's callbacks wrappers:
void syncStarted(void *info)
{
    LOCK(cs_spvcallback);
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnSyncStarted();
}

void syncStopped(void *info, int error)
{
    LOCK(cs_spvcallback);
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnSyncStopped(error);
}

void txStatusUpdate(void *info)
{
    LOCK(cs_spvcallback);
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnTxStatusUpdate();
}

void saveBlocks(void *info, int replace, BRMerkleBlock *blocks[], size_t blocksCount)
{
    /// @attention called under spv manager lock!!!
//    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnSaveBlocks(replace, blocks, blocksCount);
}

void blockNotify(void *info, const UInt256& blockHash)
{
    static_cast<CSpvWrapper *>(info)->OnBlockNotify(blockHash);
}

void savePeers(void *info, int replace, const BRPeer peers[], size_t peersCount)
{
    LOCK(cs_spvcallback);
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnSavePeers(replace, peers, peersCount);
}

void threadCleanup(void *info)
{
    LOCK(cs_spvcallback);
    if (ShutdownRequested()) return;
    static_cast<CSpvWrapper *>(info)->OnThreadCleanup();
}

static void SetCheckpoints()
{
    /// @attention block number should be multiple of 2016 !!!
    BRMainNetCheckpoints[0]  = {      0, toUInt256("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"), 1231006505, 0x1d00ffff };
    BRMainNetCheckpoints[1]  = {  20160, toUInt256("000000000f1aef56190aee63d33a373e6487132d522ff4cd98ccfc96566d461e"), 1248481816, 0x1d00ffff };
    BRMainNetCheckpoints[2]  = {  40320, toUInt256("0000000045861e169b5a961b7034f8de9e98022e7a39100dde3ae3ea240d7245"), 1266191579, 0x1c654657 };
    BRMainNetCheckpoints[3]  = {  60480, toUInt256("000000000632e22ce73ed38f46d5b408ff1cff2cc9e10daaf437dfd655153837"), 1276298786, 0x1c0eba64 };
    BRMainNetCheckpoints[4]  = {  80640, toUInt256("0000000000307c80b87edf9f6a0697e2f01db67e518c8a4d6065d1d859a3a659"), 1284861847, 0x1b4766ed };
    BRMainNetCheckpoints[5]  = { 100800, toUInt256("000000000000e383d43cc471c64a9a4a46794026989ef4ff9611d5acb704e47a"), 1294031411, 0x1b0404cb };
    BRMainNetCheckpoints[6]  = { 120960, toUInt256("0000000000002c920cf7e4406b969ae9c807b5c4f271f490ca3de1b0770836fc"), 1304131980, 0x1b0098fa };
    BRMainNetCheckpoints[7]  = { 141120, toUInt256("00000000000002d214e1af085eda0a780a8446698ab5c0128b6392e189886114"), 1313451894, 0x1a094a86 };
    BRMainNetCheckpoints[8]  = { 161280, toUInt256("00000000000005911fe26209de7ff510a8306475b75ceffd434b68dc31943b99"), 1326047176, 0x1a0d69d7 };
    BRMainNetCheckpoints[9]  = { 181440, toUInt256("00000000000000e527fc19df0992d58c12b98ef5a17544696bbba67812ef0e64"), 1337883029, 0x1a0a8b5f };
    BRMainNetCheckpoints[10] = { 201600, toUInt256("00000000000003a5e28bef30ad31f1f9be706e91ae9dda54179a95c9f9cd9ad0"), 1349226660, 0x1a057e08 };
    BRMainNetCheckpoints[11] = { 221760, toUInt256("00000000000000fc85dd77ea5ed6020f9e333589392560b40908d3264bd1f401"), 1361148470, 0x1a04985c };
    BRMainNetCheckpoints[12] = { 241920, toUInt256("00000000000000b79f259ad14635739aaf0cc48875874b6aeecc7308267b50fa"), 1371418654, 0x1a00de15 };
    BRMainNetCheckpoints[13] = { 262080, toUInt256("000000000000000aa77be1c33deac6b8d3b7b0757d02ce72fffddc768235d0e2"), 1381070552, 0x1916b0ca };
    BRMainNetCheckpoints[14] = { 282240, toUInt256("0000000000000000ef9ee7529607286669763763e0c46acfdefd8a2306de5ca8"), 1390570126, 0x1901f52c };
    BRMainNetCheckpoints[15] = { 302400, toUInt256("0000000000000000472132c4daaf358acaf461ff1c3e96577a74e5ebf91bb170"), 1400928750, 0x18692842 };
    BRMainNetCheckpoints[16] = { 322560, toUInt256("000000000000000002df2dd9d4fe0578392e519610e341dd09025469f101cfa1"), 1411680080, 0x181fb893 };
    BRMainNetCheckpoints[17] = { 342720, toUInt256("00000000000000000f9cfece8494800d3dcbf9583232825da640c8703bcd27e7"), 1423496415, 0x1818bb87 };
    BRMainNetCheckpoints[18] = { 362880, toUInt256("000000000000000014898b8e6538392702ffb9450f904c80ebf9d82b519a77d5"), 1435475246, 0x1816418e };
    BRMainNetCheckpoints[19] = { 383040, toUInt256("00000000000000000a974fa1a3f84055ad5ef0b2f96328bc96310ce83da801c9"), 1447236692, 0x1810b289 };
    BRMainNetCheckpoints[20] = { 403200, toUInt256("000000000000000000c4272a5c68b4f55e5af734e88ceab09abf73e9ac3b6d01"), 1458292068, 0x1806a4c3 };
    BRMainNetCheckpoints[21] = { 423360, toUInt256("000000000000000001630546cde8482cc183708f076a5e4d6f51cd24518e8f85"), 1470163842, 0x18057228 };
    BRMainNetCheckpoints[22] = { 443520, toUInt256("00000000000000000345d0c7890b2c81ab5139c6e83400e5bed00d23a1f8d239"), 1481765313, 0x18038b85 };
    BRMainNetCheckpoints[23] = { 463680, toUInt256("000000000000000000431a2f4619afe62357cd16589b638bb638f2992058d88e"), 1493259601, 0x18021b3e };
    BRMainNetCheckpoints[24] = { 483840, toUInt256("0000000000000000008e5d72027ef42ca050a0776b7184c96d0d4b300fa5da9e"), 1504704195, 0x1801310b };
    BRMainNetCheckpoints[25] = { 504000, toUInt256("0000000000000000006cd44d7a940c79f94c7c272d159ba19feb15891aa1ea54"), 1515827554, 0x177e578c };
    BRMainNetCheckpoints[26] = { 524160, toUInt256("00000000000000000009d1e9bee76d334347060c6a2985d6cbc5c22e48f14ed2"), 1527168053, 0x17415a49 };
    BRMainNetCheckpoints[27] = { 544320, toUInt256("0000000000000000000a5e9b5e4fbee51f3d53f31f40cd26b8e59ef86acb2ebd"), 1538639362, 0x1725c191 };
    BRMainNetCheckpoints[28] = { 564480, toUInt256("0000000000000000002567dc317da20ddb0d7ef922fe1f9c2375671654f9006c"), 1551026038, 0x172e5b50 };
    BRMainNetCheckpoints[29] = { 584640, toUInt256("0000000000000000000e5af6f531133eb548fe3854486ade75523002a1a27687"), 1562663868, 0x171f0d9b };
    BRMainNetCheckpoints[30] = { 669312, toUInt256("0000000000000000000beb9d24f999168c79fa58394868f9fcc5367c28f137dc"), 1612578303, 0x170d21b9 };
    /// @attention don't forget to increase both 'n' in BRMainNetCheckpoints[n]

    /// @attention block number should be multiple of 2016 !!!
    BRTestNetCheckpoints[0]  = {       0, toUInt256("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"), 1296688602, 0x1d00ffff };
    BRTestNetCheckpoints[1]  = {  100800, toUInt256("0000000000a33112f86f3f7b0aa590cb4949b84c2d9c673e9e303257b3be9000"), 1376543922, 0x1c00d907 };
    BRTestNetCheckpoints[2]  = {  201600, toUInt256("0000000000376bb71314321c45de3015fe958543afcbada242a3b1b072498e38"), 1393813869, 0x1b602ac0 };
    BRTestNetCheckpoints[3]  = {  302400, toUInt256("0000000000001c93ebe0a7c33426e8edb9755505537ef9303a023f80be29d32d"), 1413766239, 0x1a33605e };
    BRTestNetCheckpoints[4]  = {  403200, toUInt256("0000000000ef8b05da54711e2106907737741ac0278d59f358303c71d500f3c4"), 1431821666, 0x1c02346c };
    BRTestNetCheckpoints[5]  = {  504000, toUInt256("0000000000005d105473c916cd9d16334f017368afea6bcee71629e0fcf2f4f5"), 1436951946, 0x1b00ab86 };
    BRTestNetCheckpoints[6]  = {  604800, toUInt256("00000000000008653c7e5c00c703c5a9d53b318837bb1b3586a3d060ce6fff2e"), 1447484641, 0x1a092a20 };
    BRTestNetCheckpoints[7]  = {  705600, toUInt256("00000000004ee3bc2e2dd06c31f2d7a9c3e471ec0251924f59f222e5e9c37e12"), 1455728685, 0x1c0ffff0 };
    BRTestNetCheckpoints[8]  = {  806400, toUInt256("0000000000000faf114ff29df6dbac969c6b4a3b407cd790d3a12742b50c2398"), 1462006183, 0x1a34e280 };
    BRTestNetCheckpoints[9]  = {  907200, toUInt256("0000000000166938e6f172a21fe69fe335e33565539e74bf74eeb00d2022c226"), 1469705562, 0x1c00ffff };
    BRTestNetCheckpoints[10] = { 1008000, toUInt256("000000000000390aca616746a9456a0d64c1bd73661fd60a51b5bf1c92bae5a0"), 1476926743, 0x1a52ccc0 };
    BRTestNetCheckpoints[11] = { 1108800, toUInt256("00000000000288d9a219419d0607fb67cc324d4b6d2945ca81eaa5e739fab81e"), 1490751239, 0x1b09ecf0 };
    BRTestNetCheckpoints[12] = { 1209600, toUInt256("0000000000000026b4692a26f1651bec8e9d4905640bd8e56056c9a9c53badf8"), 1507328506, 0x1973e180 };
    BRTestNetCheckpoints[13] = { 1310400, toUInt256("0000000000013b434bbe5668293c92ef26df6d6d4843228e8958f6a3d8101709"), 1527038604, 0x1b0ffff0 };
    BRTestNetCheckpoints[14] = { 1411200, toUInt256("00000000000000008b3baea0c3de24b9333c169e1543874f4202397f5b8502cb"), 1535535770, 0x194ac105 };
    BRTestNetCheckpoints[15] = { 1512000, toUInt256("000000000000024bed9664952a0e1d7cced222160daaa61cf47f4281eaaf1bbd"), 1556081498, 0x1a03f728 }; // added
    BRTestNetCheckpoints[16] = { 1610784, toUInt256("000000000000038032aa1f49cd37cf32e48ded45de1b53208be999fffa0333ba"), 1575244826, 0x1a03aeec }; // added
    BRTestNetCheckpoints[17] = { 1933344, toUInt256("00000000000000318b9b614dd36ca37e6962b1cdd80e4b32245dffce286ec23a"), 1612416242, 0x1934f1c0 };
    /// @attention don't forget to increase both 'n' in BRTestNetCheckpoints[n]
}

CSpvWrapper::CSpvWrapper(bool isMainnet, size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / (isMainnet ?  "spv" : "spv_testnet"), nCacheSize, fMemory, fWipe))
{
    SetCheckpoints();

    // Configuring spv logs:
    // (we need intermediate persistent storage for filename here)
    spv_internal_logfilename = AbsPathForConfigVal("spv.log").string();
    spv_logfilename = spv_internal_logfilename.c_str();
    LogPrint(BCLog::SPV, "internal logs set to %s\n", spv_logfilename);
    spv_log2console = 0;
    spv_mainnet = isMainnet ? 1 : 0;
}

void CSpvWrapper::Load()
{
    BRMasterPubKey mpk = BR_MASTER_PUBKEY_NONE;
    mpk = BRBIP32ParseMasterPubKey(Params().GetConsensus().spv.wallet_xpub.c_str());

    std::vector<char> xpub_buf(BRBIP32SerializeMasterPubKey(NULL, 0, mpk));
    BRBIP32SerializeMasterPubKey(xpub_buf.data(), xpub_buf.size(), mpk);
    LogPrint(BCLog::SPV, "debug xpub: %s\n", &xpub_buf[0]);

    std::vector<BRTransaction *> txs;
    // load txs
    {
        std::function<void (uint256 const &, db_tx_rec &)> onLoadTx = [&txs] (uint256 const & hash, db_tx_rec & rec) {
            BRTransaction *tx = BRTransactionParse(rec.first.data(), rec.first.size());
            tx->blockHeight = rec.second.first;
            tx->timestamp = rec.second.second;
            txs.push_back(tx);

            LogPrint(BCLog::SPV, "load tx: %s, height: %d\n", to_uint256(tx->txHash).ToString(), tx->blockHeight);

            CAnchor anchor;
            if (IsAnchorTx(tx, anchor)) {
                LogPrint(BCLog::SPV, "LOAD POSSIBLE ANCHOR TX, tx: %s, blockHash: %s, height: %d, btc height: %d\n", to_uint256(tx->txHash).ToString(), anchor.blockHash.ToString(), anchor.height, tx->blockHeight);
            }
        };
        // can't deduce lambda here:
        IterateTable(DB_SPVTXS, onLoadTx);
    }

    auto userAddresses = new BRUserAddresses;
    auto htlcAddresses = new BRUserAddresses;
    const auto wallets = GetWallets();
    for (const auto& item : wallets) {
        for (const auto& entry : item->mapAddressBook)
        {
            if (entry.second.purpose == "spv")
            {
                uint160 userHash;
                if (entry.first.index() == PKHashType) {
                    userHash = std::get<PKHash>(entry.first);
                } else if (entry.first.index() == WitV0KeyHashType) {
                    userHash = std::get<WitnessV0KeyHash>(entry.first);
                } else {
                    continue;
                }

                UInt160 spvHash;
                UIntConvert(userHash.begin(), spvHash);
                userAddresses->insert(spvHash);
            }
            else if (entry.second.purpose == "htlc")
            {
                uint160 userHash = std::get<ScriptHash>(entry.first);
                UInt160 spvHash;
                UIntConvert(userHash.begin(), spvHash);
                htlcAddresses->insert(spvHash);
            }
        }
    }

    wallet = BRWalletNew(txs.data(), txs.size(), mpk, 0, userAddresses, htlcAddresses);
    BRWalletSetCallbacks(wallet, this, balanceChanged, txAdded, txUpdated, txDeleted);
    LogPrint(BCLog::SPV, "wallet created with first receive address: %s\n", BRWalletLegacyAddress(wallet).s);

    std::vector<BRMerkleBlock *> blocks;
    // load blocks
    {
        std::function<void (uint256 const &, db_block_rec &)> onLoadBlock = [&blocks] (uint256 const & hash, db_block_rec & rec) {
            BRMerkleBlock *block = BRMerkleBlockParse (rec.first.data(), rec.first.size());
            block->height = rec.second;
            blocks.push_back(block);
        };
        // can't deduce lambda here:
        IterateTable(DB_SPVBLOCKS, onLoadBlock);
    }

    // no need to load|keep peers!!!
    manager = BRPeerManagerNew(BRGetChainParams(), wallet, 1613692800, blocks.data(), blocks.size(), NULL, 0); // date is 19 Feb 2021

    // can't wrap member function as static "C" function here:
    BRPeerManagerSetCallbacks(manager, this, syncStarted, syncStopped, txStatusUpdate,
                              saveBlocks, blockNotify, savePeers, nullptr /*networkIsReachable*/, threadCleanup);

}

CSpvWrapper::~CSpvWrapper()
{
    LOCK(cs_spvcallback);
    if (manager) {
        BRPeerManagerFree(manager);
        manager = nullptr;
    }
    if (wallet) {
        BRWalletFree(wallet);
        wallet = nullptr;
    }
}

void CSpvWrapper::Connect()
{
    BRPeerManagerConnect(manager);
}

void CSpvWrapper::Disconnect()
{
    AssertLockNotHeld(cs_main); /// @attention due to calling txStatusUpdate() (OnTxUpdated()), savePeers(), syncStopped()
    BRPeerManagerDisconnect(manager);
}

bool CSpvWrapper::IsConnected() const
{
    return BRPeerManagerConnectStatus(manager) == BRPeerStatusConnected;
}

void CSpvWrapper::CancelPendingTxs()
{
    BRPeerManagerCancelPendingTxs(manager);
}

bool CSpvWrapper::Rescan(int height)
{
    if (BRPeerManagerConnectStatus(manager) != BRPeerStatusConnected )
        return false;

    uint32_t curHeight = BRPeerManagerLastBlockHeight(manager);
    if (height < 0) {
        // relative height if negative
        height = std::max(0, static_cast<int>(curHeight) + height);
    }
    LogPrint(BCLog::SPV, "trying to rescan from block %d, current block %u\n", height, curHeight);
    BRPeerManagerRescanFromBlockNumber(manager, static_cast<uint32_t>(height));
    curHeight = BRPeerManagerLastBlockHeight(manager);
    LogPrint(BCLog::SPV, "actual new current block %u\n", curHeight);

    LOCK(cs_main);
    panchors->ActivateBestAnchor(true);

    return true;
}

// we cant return the whole params itself due to conflicts in include files
uint8_t CSpvWrapper::GetPKHashPrefix() const
{
    return BRPeerManagerChainParams(manager)->base58_p2pkh;
}

uint8_t CSpvWrapper::GetP2SHPrefix() const
{
    return BRPeerManagerChainParams(manager)->base58_p2sh;
}

BRWallet * CSpvWrapper::GetWallet()
{
    return wallet;
}

bool CSpvWrapper::IsInitialSync() const
{
    return initialSync;
}

uint32_t CSpvWrapper::GetLastBlockHeight() const
{
    return BRPeerManagerLastBlockHeight(manager);
}

uint32_t CSpvWrapper::GetEstimatedBlockHeight() const
{
    return BRPeerManagerEstimatedBlockHeight(manager);
}

void CSpvWrapper::OnBalanceChanged(uint64_t balance)
{
    /// @attention called under spv manager lock!!!
    LogPrint(BCLog::SPV, "balance changed: %lu\n", balance);
}

std::vector<BRTransaction *> CSpvWrapper::GetWalletTxs() const
{
    std::vector<BRTransaction *> txs;
    txs.resize(BRWalletTransactions(wallet, nullptr, 0));
    size_t count = BRWalletTransactions(wallet, txs.data(), txs.size());

    LogPrint(BCLog::SPV, "wallet txs count: %lu\n", count);

    return txs;
}

void CSpvWrapper::OnTxAdded(BRTransaction * tx)
{
    /// @attention called under spv manager lock!!!
    uint256 const txHash{to_uint256(tx->txHash)};
    WriteTx(tx);
    LogPrint(BCLog::SPV, "tx added %s, at block %d, timestamp %d\n", txHash.ToString(), tx->blockHeight, tx->timestamp);

    CAnchor anchor;
    if (IsAnchorTx(tx, anchor)) {

        LogPrint(BCLog::SPV, "IsAnchorTx(): %s\n", txHash.ToString());

        LOCK(cs_main);
        if (ValidateAnchor(anchor) && panchors->AddToAnchorPending(anchor, txHash, tx->blockHeight)) {
            LogPrint(BCLog::SPV, "adding anchor to pending %s\n", txHash.ToString());
        }
    }

    OnTxNotify(tx->txHash);
}

void CSpvWrapper::OnTxUpdated(const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp, const UInt256& blockHash)
{
    /// @attention called under spv manager lock!!!
    for (size_t i = 0; i < txCount; ++i) {
        uint256 const txHash{to_uint256(txHashes[i])};
        const uint256 btcHash{to_uint256(blockHash)};

        {
            LOCK(cs_main);

            UpdateTx(txHash, blockHeight, timestamp, btcHash);
            LogPrint(BCLog::SPV, "tx updated, hash: %s, blockHeight: %d, timestamp: %d\n", txHash.ToString(), blockHeight, timestamp);

            CAnchorIndex::AnchorRec oldPending;
            if (panchors->GetPendingByBtcTx(txHash, oldPending))
            {
                LogPrint(BCLog::SPV, "updating anchor pending %s\n", txHash.ToString());
                if (panchors->AddToAnchorPending(oldPending.anchor, txHash, blockHeight, true)) {
                    LogPrint(BCLog::ANCHORING, "Anchor pending added/updated %s\n", txHash.ToString());
                }
            }
            else if (auto exist = panchors->GetAnchorByBtcTx(txHash)) // update index. no any checks nor validations
            {
                LogPrint(BCLog::SPV, "updating anchor %s\n", txHash.ToString());
                CAnchor oldAnchor{exist->anchor};
                if (panchors->AddAnchor(oldAnchor, txHash, blockHeight, true)) {
                    LogPrint(BCLog::ANCHORING, "Anchor added/updated %s\n", txHash.ToString());
                }
            }
        }

        OnTxNotify(txHashes[i]);
    }
}

void CSpvWrapper::OnTxDeleted(UInt256 txHash, int notifyUser, int recommendRescan)
{
    /// @attention called under spv manager lock!!!
    uint256 const hash(to_uint256(txHash));
    EraseTx(hash);

    {
        LOCK(cs_main);
        panchors->DeleteAnchorByBtcTx(hash);
        panchors->DeletePendingByBtcTx(hash);
    }

    OnTxNotify(txHash);

    LogPrint(BCLog::SPV, "tx deleted: %s; notifyUser: %d, recommendRescan: %d\n", hash.ToString(), notifyUser, recommendRescan);
}

void CSpvWrapper::OnSyncStarted()
{
    LogPrint(BCLog::SPV, "sync started!\n");
}

void CSpvWrapper::OnSyncStopped(int error)
{
    initialSync = false;
    LogPrint(BCLog::SPV, "sync stopped!\n");
}

void CSpvWrapper::OnTxStatusUpdate()
{
    LogPrint(BCLog::SPV, "tx status update\n");
    panchors->CheckActiveAnchor();
}

void CSpvWrapper::OnSaveBlocks(int replace, BRMerkleBlock * blocks[], size_t blocksCount)
{
    /// @attention called under spv manager lock!!!
    if (replace)
    {
        LogPrint(BCLog::SPV, "BLOCK: 'replace' requested, deleting...\n");
        DeleteTable<uint256>(DB_SPVBLOCKS);
    }
    for (size_t i = 0; i < blocksCount; ++i) {
        WriteBlock(blocks[i]);
        LogPrint(BCLog::SPV, "BLOCK: %u, %s saved\n", blocks[i]->height, to_uint256(blocks[i]->blockHash).ToString());
    }
    CommitBatch();

    /// @attention don't call ANYTHING that could call back to spv here! cause OnSaveBlocks works under spv lock!!!
}

void CSpvWrapper::OnBlockNotify(const UInt256& blockHash)
{
#if HAVE_SYSTEM
    if (initialSync)
    {
        return;
    }

    std::string strCmd = gArgs.GetArg("-spvblocknotify", "");
    if (!strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", to_uint256(blockHash).GetHex());
        std::thread t(runCommand, strCmd);
        t.detach(); // thread runs free
    }
#endif
}

void CSpvWrapper::OnTxNotify(const UInt256& txHash)
{
#if HAVE_SYSTEM
    // notify an external script when a wallet transaction comes in or is updated
    std::string strCmd = gArgs.GetArg("-spvwalletnotify", "");

    if (!strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", to_uint256(txHash).GetHex());
        std::thread t(runCommand, strCmd);
        t.detach(); // thread runs free
    }
#endif
}

void CSpvWrapper::OnSavePeers(int replace, const BRPeer peers[], size_t peersCount)
{
    // this is useless :(
}

void CSpvWrapper::OnThreadCleanup()
{
}

void CSpvWrapper::CommitBatch()
{
    if (batch)
    {
        db->WriteBatch(*batch);
        batch.reset();
    }
}

void CSpvWrapper::WriteTx(const BRTransaction *tx)
{
    static TBytes buf;
    buf.resize(BRTransactionSerialize(tx, NULL, 0));
    BRTransactionSerialize(tx, buf.data(), buf.size());
    db->Write(std::make_pair(DB_SPVTXS, to_uint256(tx->txHash)), std::make_pair(buf, std::make_pair(tx->blockHeight, tx->timestamp)) );
}

void CSpvWrapper::UpdateTx(uint256 const & hash, uint32_t blockHeight, uint32_t timestamp, const uint256& blockHash)
{
    std::pair<char, uint256> const key{std::make_pair(DB_SPVTXS, hash)};
    db_tx_rec txrec;
    if (db->Read(key, txrec)) {
        txrec.second.first = blockHeight;
        txrec.second.second = timestamp;
        db->Write(key, txrec);
    }

    // Store block index in anchors
    panchors->WriteBlock(blockHeight, blockHash);
}

uint32_t CSpvWrapper::ReadTxTimestamp(uint256 const & hash)
{
    std::pair<char, uint256> const key{std::make_pair(DB_SPVTXS, hash)};
    db_tx_rec txrec;
    if (db->Read(key, txrec)) {
        return txrec.second.second;
    }

    return 0;
}

uint32_t CSpvWrapper::ReadTxBlockHeight(uint256 const & hash)
{
    std::pair<char, uint256> const key{std::make_pair(DB_SPVTXS, hash)};
    db_tx_rec txrec;
    if (db->Read(key, txrec)) {
        return txrec.second.first;
    }

    // If not found return the default value of an unconfirmed TX.
    return std::numeric_limits<int32_t>::max();
}

void CSpvWrapper::EraseTx(uint256 const & hash)
{
    db->Erase(std::make_pair(DB_SPVTXS, hash));
}

void CSpvWrapper::WriteBlock(const BRMerkleBlock * block)
{
    static TBytes buf;
    size_t blockSize = BRMerkleBlockSerialize(block, NULL, 0);
    buf.resize(blockSize);
    BRMerkleBlockSerialize(block, buf.data(), blockSize);

    BatchWrite(std::make_pair(DB_SPVBLOCKS, to_uint256(block->blockHash)), std::make_pair(buf, block->height));
}

UniValue CSpvWrapper::GetPeers()
{
    auto peerInfo = BRGetPeers(manager);

    UniValue result(UniValue::VOBJ);

    for (const auto& peer : peerInfo)
    {
        UniValue obj(UniValue::VOBJ);
        for (const auto& json : peer.second)
        {
            obj.pushKV(json.first, json.second);
        }

        result.pushKV(std::to_string(peer.first), obj);
    }

    return result;
}

std::string CSpvWrapper::AddBitcoinAddress(const CPubKey& new_key)
{
    BRAddress addr = BR_ADDRESS_NONE;
    if (!BRWalletAddSingleAddress(wallet, *new_key.data(), new_key.size(), addr)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to add Bitcoin address");
    }

    BRPeerManagerRebuildBloomFilter(manager);

    return addr.s;
}

void CSpvWrapper::AddBitcoinHash(const uint160 &userHash, const bool htlc)
{
    BRWalletImportAddress(wallet, userHash, htlc);
}

void CSpvWrapper::RebuildBloomFilter(bool rescan)
{
    BRPeerManagerRebuildBloomFilter(manager, rescan);
}

std::string CSpvWrapper::DumpBitcoinPrivKey(const CWallet* pwallet, const std::string &strAddress)
{
    CKeyID keyid;
    if (!BRAddressHash160(keyid.begin(), strAddress.c_str())) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");
    }

    CKey vchSecret;
    if (!pwallet->GetKey(keyid, vchSecret)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    }

    return EncodeSecret(vchSecret);
}

int64_t CSpvWrapper::GetBitcoinBalance()
{
    return BRWalletBalance(wallet);
}

// Helper function to convert key and populate vector
void ConvertPrivKeys(std::vector<BRKey> &inputKeys, const std::vector<CKey> &keys)
{
    for (const auto& key : keys) {
        UInt256 rawKey;
        memcpy(&rawKey, &(*key.begin()), key.size());

        BRKey inputKey;
        if (!BRKeySetSecret(&inputKey, &rawKey, key.IsCompressed()))
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to create SPV private key");
        }

        inputKeys.push_back(inputKey);
    }
}

UniValue CSpvWrapper::SendBitcoins(CWallet* const pwallet, std::string address, int64_t amount, uint64_t feeRate)
{
    if (!BRAddressIsValid(address.c_str())) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    auto dust = BRWalletMinOutputAmountWithFeePerKb(wallet, MIN_FEE_PER_KB);
    if (amount < static_cast<int64_t>(dust)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount below dust threshold, minimum required: " + std::to_string(dust));
    }

    // Generate change address while we have CWallet
    CPubKey new_key;
    if (!pwallet->GetKeyFromPool(new_key)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }
    auto changeAddress = AddBitcoinAddress(new_key);
    auto dest = GetDestinationForKey(new_key, OutputType::BECH32);
    pwallet->SetAddressBook(dest, "spv", "spv");

    std::string errorMsg;
    BRTransaction *tx = BRWalletCreateTransaction(wallet, static_cast<uint64_t>(amount), address.c_str(), changeAddress, feeRate, errorMsg);

    if (tx == nullptr) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, errorMsg);
    }

    std::vector<BRKey> inputKeys;
    for (size_t i{0}; i < tx->inCount; ++i) {
        LogPrintf("INPUT TX %s vout %d\n", to_uint256(tx->inputs[i].txHash).ToString(), tx->inputs[i].index);
        CTxDestination dest;
        if (!ExtractDestination({tx->inputs[i].script, tx->inputs[i].script + tx->inputs[i].scriptLen}, dest)) {
            BRTransactionFree(tx);
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Failed to extract destination from script");

        }

        auto keyid = GetKeyForDestination(*pwallet, dest);
        if (keyid.IsNull()) {
            BRTransactionFree(tx);
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get address hash.");
        }

        CKey vchSecret;
        if (!pwallet->GetKey(keyid, vchSecret)) {
            BRTransactionFree(tx);
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get address private key.");
        }

        ConvertPrivKeys(inputKeys, {vchSecret});
    }

    if (!BRTransactionSign(tx, 0, inputKeys.data(), inputKeys.size())) {
        BRTransactionFree(tx);
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction.");
    }

    int sendResult = 0;
    std::promise<int> promise;
    std::string txid = to_uint256(tx->txHash).ToString();
    OnSendRawTx(tx, &promise);
    if (tx) {
        sendResult = promise.get_future().get();
    } else {
        sendResult = EPARSINGTX;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid);
    result.pushKV("sendmessage", sendResult != 0 ? DecodeSendResult(sendResult) : "");
    return result;
}

UniValue CSpvWrapper::ListTransactions()
{
    auto userTransactions = BRListUserTransactions(wallet);

    UniValue result(UniValue::VARR);
    for (const auto& txid : userTransactions)
    {
        result.push_back(to_uint256(txid->txHash).ToString());
    }
    return result;
}

struct tallyitem
{
    CAmount nAmount{0};
    int nConf{std::numeric_limits<int>::max()};
    std::vector<uint256> txids;
    SPVTxType type;
};

UniValue CSpvWrapper::ListReceived(int nMinDepth, std::string address)
{
    if (!address.empty() && !BRAddressIsValid(address.c_str()))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    UInt160 addressFilter = UINT160_ZERO;
    if (!address.empty())
    {
        BRAddressHash160(&addressFilter, address.c_str());
    }

    auto userTransactions = BRListUserTransactions(wallet, addressFilter);

    //UniValue ret(UniValue::VARR);
    std::map<std::string, tallyitem> mapTally;
    for (const auto& txid : userTransactions)
    {
        int32_t confirmations{0};
        const auto txHash = to_uint256(txid->txHash);
        const auto blockHeight = ReadTxBlockHeight(txHash);

        if (blockHeight != std::numeric_limits<int32_t>::max())
        {
            confirmations = spv::pspv->GetLastBlockHeight() - blockHeight + 1;
        }

        if (confirmations < nMinDepth)
        {
            continue;
        }

        for (size_t i{0}; i < txid->outCount; ++i)
        {
            const auto txout = txid->outputs[i];

            if (!address.empty() && address != txout.address)
            {
                continue;
            }

            auto mine = IsMine(txout.address);
            if (mine == SPVTxType::None)
            {
                continue;
            }

            tallyitem& item = mapTally[txout.address];
            item.type = mine;
            item.nAmount += txout.amount;
            item.nConf = std::min(item.nConf, confirmations);
            item.txids.push_back(txHash);
        }
    }

    UniValue ret(UniValue::VARR);

    for (const auto& entry : mapTally)
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("address", entry.first);
        obj.pushKV("type", entry.second.type == SPVTxType::Bech32 ? "Bech32" : "HTLC");
        obj.pushKV("amount", ValueFromAmount(entry.second.nAmount));
        obj.pushKV("confirmations", entry.second.nConf);

        UniValue transactions(UniValue::VARR);
        for (const uint256& item : entry.second.txids)
        {
            transactions.push_back(item.GetHex());
        }
        obj.pushKV("txids", transactions);

        ret.push_back(obj);
    }

    return ret;
}

UniValue CSpvWrapper::GetHTLCReceived(const std::string& addr)
{
    if (!addr.empty() && !BRAddressIsValid(addr.c_str()))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    UInt160 addressFilter = UINT160_ZERO;
    if (!addr.empty())
    {
        BRAddressHash160(&addressFilter, addr.c_str());
    }

    auto htlcTransactions = BRListHTLCReceived(wallet, addressFilter);

    // Sort by Bitcoin address
    std::sort(htlcTransactions.begin(), htlcTransactions.end(), [](const std::pair<BRTransaction*, size_t>& lhs, const std::pair<BRTransaction*, size_t>& rhs){
        return strcmp(lhs.first->outputs[lhs.second].address, rhs.first->outputs[rhs.second].address) < 0;
    });

    UniValue result(UniValue::VARR);
    for (const auto& txInfo : htlcTransactions)
    {
        uint256 txid = to_uint256(txInfo.first->txHash);
        uint64_t confirmations{0};
        uint32_t blockHeight = ReadTxBlockHeight(txid);

        if (blockHeight != std::numeric_limits<int32_t>::max())
        {
            confirmations = spv::pspv->GetLastBlockHeight() - blockHeight + 1;
        }

        UniValue item(UniValue::VOBJ);
        uint64_t output = txInfo.second;
        item.pushKV("txid", txid.ToString());
        item.pushKV("vout", output);
        item.pushKV("amount", ValueFromAmount(txInfo.first->outputs[output].amount));
        item.pushKV("address", txInfo.first->outputs[output].address);
        item.pushKV("confirms", confirmations);

        uint256 spent;
        if (BRWalletTxSpent(wallet, txInfo.first, output, spent))
        {
            blockHeight = ReadTxBlockHeight(spent);
            confirmations = 0;
            if (blockHeight != std::numeric_limits<int32_t>::max())
            {
                confirmations = spv::pspv->GetLastBlockHeight() - blockHeight + 1;
            }

            UniValue spentItem(UniValue::VOBJ);
            spentItem.pushKV("txid", spent.ToString());
            spentItem.pushKV("confirms", confirmations);
            item.pushKV("spent", spentItem);
        }
        result.push_back(item);
    }
    return result;
}

std::string CSpvWrapper::GetRawTransactions(uint256& hash)
{
    UInt256 spvHash;
    UIntConvert(hash.begin(), spvHash);
    return BRGetRawTransaction(wallet, spvHash);
}

std::string CSpvWrapper::GetHTLCSeed(uint8_t* md20)
{
    return BRGetHTLCSeed(wallet, md20);
}

HTLCDetails GetHTLCDetails(CScript& redeemScript)
{
    HTLCDetails script;

    uint32_t scriptSize{1 /* OP_IF */ + 1 /* OP_SHA256 */ + 1 /* seed size */ + 32 /* seed */ + 1 /* OP_EQUALVERIFY */ + 1 /* seller size */};

    if (redeemScript.size() < scriptSize)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect redeemscript length");
    }

    // Check size field is 32 for seed.
    if (redeemScript[2] != 32)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect seed hash length");
    }
    script.hash = std::vector<unsigned char>(&redeemScript[3], &redeemScript[35]);

    uint8_t sellerLength = redeemScript[36];
    if (sellerLength != CPubKey::PUBLIC_KEY_SIZE && sellerLength != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Seller pubkey incorrect pubkey length");
    }

    scriptSize += sellerLength + 1 /* OP_ELSE */ + 1 /* time size */;
    if (redeemScript.size() < scriptSize)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect redeemscript length");
    }

    script.sellerKey.Set(&redeemScript[37], &redeemScript[37] + sellerLength);
    if (!script.sellerKey.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid seller pubkey");
    }

    uint8_t timeoutLength = redeemScript[38 + sellerLength];
    if (timeoutLength > CScriptNum::nDefaultMaxNumSize)
    {
        if (timeoutLength >= OP_1)
        {
            // Time out length encoded into the opcode itself.
            script.locktime = CScript::DecodeOP_N(static_cast<opcodetype>(timeoutLength));
            timeoutLength = 0;
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect timeout length");
        }
    }
    else
    {
        scriptSize += timeoutLength;
        if (redeemScript.size() < scriptSize)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect redeemscript length");
        }

        memcpy(&script.locktime, &redeemScript[39 + sellerLength], timeoutLength);

        // If time more than expected reduce to max value
        uint32_t maxLocktime{1 << 16};
        if (script.locktime > maxLocktime)
        {
            script.locktime = maxLocktime;
        }
    }

    scriptSize += 1 /* OP_CHECKSEQUENCEVERIFY */ + 1 /* OP_DROP */ + 1 /* buyer size*/;
    if (redeemScript.size() < scriptSize)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect redeemscript length");
    }

    uint8_t buyerLength = redeemScript[41 + timeoutLength + sellerLength];
    if (buyerLength != CPubKey::PUBLIC_KEY_SIZE && buyerLength != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Buyer pubkey incorrect pubkey length");
    }

    // Check redeemscript size is now exact
    scriptSize += buyerLength + 1 /* OP_ENDIF */ + 1 /* OP_CHECKSIG */;
    if (redeemScript.size() != scriptSize)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect redeemscript length");
    }

    script.buyerKey.Set(&redeemScript[42 + timeoutLength + sellerLength], &redeemScript[42 + timeoutLength + sellerLength] + buyerLength);
    if (!script.buyerKey.IsValid())
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid buyer pubkey");
    }

    return script;
}

HTLCDetails GetHTLCScript(CWallet* const pwallet, const uint160& hash160, CScript &redeemScript)
{
    // Read redeem script from wallet DB
    WalletBatch batch(pwallet->GetDBHandle());
    if (!batch.ReadCScript(hash160, redeemScript))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "Redeem script not found in wallet");
    }

    // Make sure stored script is at least expected length.
    const unsigned int minScriptLength{110}; // With compressed public keys and small int block count
    const unsigned int maxScriptLength{177}; // With uncompressed public keys and four bits for block count (1 size / 3 value)
    if (redeemScript.size() < minScriptLength && redeemScript.size() > maxScriptLength)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Stored redeem script incorrect length, rerun spv_createhtlc");
    }

    // Get script details
    return GetHTLCDetails(redeemScript);
}

HTLCDetails HTLCScriptRequest(CWallet* const pwallet, const char* address, CScript &redeemScript)
{
    // Validate HTLC address
    if (!BRAddressIsValid(address))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    // Decode address, bech32 will fail here.
    std::vector<unsigned char> data;
    if (!DecodeBase58Check(address, data))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Failed to decode address");
    }

    // Get hash160
    uint160 hash160;
    memcpy(&hash160, &data[1], sizeof(uint160));

    return GetHTLCScript(pwallet, hash160, redeemScript);
}

UniValue CSpvWrapper::GetAddressPubkey(const CWallet* pwallet, const char *addr)
{
    if (!BRAddressIsValid(addr))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    CKeyID key;
    if (!BRAddressHash160(key.begin(), addr))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("%s does not refer to a key", addr));
    }

    CPubKey vchPubKey;
    if (!pwallet->GetPubKey(key, vchPubKey))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("no full public key for address %s", addr));
    }

    return HexStr(vchPubKey);
}

CKeyID CSpvWrapper::GetAddressKeyID(const char *addr)
{
    if (!BRAddressIsValid(addr))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    CKeyID key;
    if (!BRAddressHash160(key.begin(), addr))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("%s does not refer to a key", addr));
    }

    return key;
}

SPVTxType CSpvWrapper::IsMine(const char *address)
{
    if (address == nullptr || !BRAddressIsValid(address))
    {
        return SPVTxType::None;
    }

    UInt160 addressFilter;
    BRAddressHash160(&addressFilter, address);

    return BRWalletIsMine(wallet, addressFilter, true);
}

UniValue CSpvWrapper::ValidateAddress(const char *address)
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", BRAddressIsValid(address) == 1);
    ret.pushKV("ismine", IsMine(address) != SPVTxType::None);
    return ret;
}

UniValue CSpvWrapper::GetAllAddress()
{
    const auto addresses = BRWalletAllUserAddrs(wallet);
    UniValue ret(UniValue::VARR);
    for (const auto& address : addresses)
    {
        ret.push_back(address);
    }
    return ret;
}

uint64_t CSpvWrapper::GetFeeRate()
{
    return BRWalletFeePerKb(wallet);
}

void publishedTxCallback(void *info, int error)
{
    LogPrint(BCLog::SPV, "publishedTxCallback: %s\n", strerror(error));
    if (info) {
        static_cast<std::promise<int> *>(info)->set_value(error);
    }
}

struct TxInput {
    UInt256 txHash;
    int32_t index;
    uint64_t amount;
    TBytes script;
};

struct TxOutput {
    uint64_t amount;
    TBytes script;
};

BRTransaction* CreateTx(std::vector<std::pair<TxInput, uint32_t>> const & inputs, std::vector<TxOutput> const & outputs, uint32_t version = TX_VERSION)
{
    BRTransaction *tx = BRTransactionNew(version);
    for (auto input : inputs) {
        BRTransactionAddInput(tx, input.first.txHash, input.first.index, input.first.amount, input.first.script.data(), input.first.script.size(), NULL, 0, NULL, 0, input.second);
    }
    for (auto output : outputs) {
        BRTransactionAddOutput(tx, output.amount, output.script.data(), output.script.size());
    }
    return tx;
}

TBytes CreateRawTx(std::vector<std::pair<TxInput, uint32_t>> const & inputs, std::vector<TxOutput> const & outputs)
{
    BRTransaction *tx = CreateTx(inputs, outputs);
    size_t len = BRTransactionSerialize(tx, NULL, 0);
    TBytes buf(len);
    len = BRTransactionSerialize(tx, buf.data(), buf.size());
    BRTransactionFree(tx);
    return len ? buf : TBytes{};
}

std::pair<std::string, std::string> CSpvWrapper::PrepareHTLCTransaction(CWallet* const pwallet, const char* scriptAddress, const char *destinationAddress, const std::string& seed, uint64_t feerate, bool seller)
{
    // Get redeemscript and parsed details from script
    CScript redeemScript;
    const auto scriptDetails = HTLCScriptRequest(pwallet, scriptAddress, redeemScript);

    return CreateHTLCTransaction(pwallet, {{scriptDetails, redeemScript}}, destinationAddress, seed, feerate, seller);
}

std::pair<std::string, std::string> CSpvWrapper::CreateHTLCTransaction(CWallet* const pwallet, const std::vector<std::pair<HTLCDetails, CScript>>& scriptDetails, const char *destinationAddress, const std::string& seed, uint64_t feerate, bool seller)
{
    // Should not get here but let's double check to avoid segfault.
    if (scriptDetails.empty()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Redeem script details not found.");
    }

    // Validate HTLC address
    if (!BRAddressIsValid(destinationAddress))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid destination address");
    }

    // Only interested in checking seed from seller
    if (seller && !IsHex(seed))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Provided seed is not in hex form");
    }

    // Calculate seed hash and make sure it matches hash in contract
    auto seedBytes = ParseHex(seed);
    if (seller)
    {
        std::vector<unsigned char> calcSeedBytes(32);
        CSHA256 hash;
        hash.Write(seedBytes.data(), seedBytes.size());
        hash.Finalize(calcSeedBytes.data());

        // Only one script expected on seller
        if (scriptDetails.begin()->first.hash != calcSeedBytes)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Seed provided does not match seed hash in contract");
        }
    }

    // Private keys
    std::vector<CKey> sourceKeys;
    std::vector<BRKey> inputKeys;

    // Inputs
    std::vector<std::pair<TxInput, uint32_t>> inputs;
    CAmount inputTotal{0};
    int64_t sigSize{0};

    for (const auto& script : scriptDetails) {

        // Get private keys
        CKey privKey;
        if (!pwallet->GetKey(seller ? script.first.sellerKey.GetID() : script.first.buyerKey.GetID(), privKey)) {
            continue;
        }
        sourceKeys.push_back(privKey);

        // Get script address
        CScriptID innerID(script.second);
        UInt160 addressFilter;
        UIntConvert(innerID.begin(), addressFilter);

        // Find related outputs
        const auto htlcTransactions = BRListHTLCReceived(wallet, addressFilter);

        uint256 spent;
        std::vector<uint8_t> redeemScript(script.second.begin(), script.second.end());

        // Loop over HTLC TXs and create inputs
        for (const auto& output : htlcTransactions)
        {
            // Skip outputs without enough confirms to meet contract requirements
            if (!seller) {
                uint32_t blockHeight = ReadTxBlockHeight(to_uint256(output.first->txHash));
                uint64_t confirmations = blockHeight != std::numeric_limits<int32_t>::max() ? spv::pspv->GetLastBlockHeight() - blockHeight + 1 : 0;
                if (confirmations < script.first.locktime) {
                    continue;
                }
            }

            // If output unspent add as input for HTLC TX
            if (!BRWalletTxSpent(wallet, output.first, output.second, spent))
            {
                inputs.push_back({{output.first->txHash, static_cast<int32_t>(output.second), output.first->outputs[output.second].amount, redeemScript},
                                  seller ? TXIN_SEQUENCE : script.first.locktime});
                inputTotal += output.first->outputs[output.second].amount;
                sigSize += 73 /* sig */ + 1 /* sighash */ + seedBytes.size() + 1 /* OP_1 || size */ + 1 /* pushdata */ + redeemScript.size();
            }
        }
    }

    if (sourceKeys.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key relating to a HTLC pubkey is not available in the wallet");
    }

    if (inputs.empty())
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "No unspent HTLC outputs found");
    }

    // Convert source private key to SPV private keys
    ConvertPrivKeys(inputKeys, sourceKeys);

    // Create output
    std::vector<TxOutput> outputs;
    outputs.push_back({P2PKH_DUST, CreateScriptForAddress(destinationAddress)});

    // Create transaction
    auto tx = CreateTx(inputs, outputs, TX_VERSION_V2);

    // Calculate and set fee
    feerate = std::max(feerate, BRWalletFeePerKb(wallet));
    CAmount const minFee = BRTransactionHTLCSize(tx, sigSize) * feerate / TX_FEE_PER_KB;

    if (inputTotal < minFee + static_cast<CAmount>(P2PKH_DUST))
    {
        throw JSONRPCError(RPC_MISC_ERROR, "Not enough funds to cover fee");
    }
    tx->outputs[0].amount = inputTotal - minFee;

    // Add seed length
    seedBytes.insert(seedBytes.begin(), static_cast<uint8_t>(seedBytes.size()));

    // Sign transaction
    if (!BRTransactionSign(tx, 0, inputKeys.data(), inputKeys.size(), seller ? ScriptTypeSeller : ScriptTypeBuyer, seedBytes.data()))
    {
        BRTransactionFree(tx);
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction.");
    }

    int sendResult = 0;
    std::promise<int> promise;
    std::string txid = to_uint256(tx->txHash).ToString();
    OnSendRawTx(tx, &promise);
    if (tx)
    {
        sendResult = promise.get_future().get();
    }
    else
    {
        sendResult = EPARSINGTX;
    }

    return {txid, sendResult != 0 ? DecodeSendResult(sendResult) : ""};
}

UniValue CSpvWrapper::RefundAllHTLC(CWallet* const pwallet, const char *destinationAddress, uint64_t feeRate)
{
    // Get all HTLC scripts
    std::set<uint160> htlcAddresses;
    const auto wallets = GetWallets();
    for (const auto& item : wallets) {
        for (const auto& entry : item->mapAddressBook) {
            if (entry.second.purpose == "htlc") {
                htlcAddresses.insert(std::get<ScriptHash>(entry.first));
            }
        }
    }

    // Loop over HTLC addresses and get HTLC details
    std::vector<std::pair<HTLCDetails, CScript>> scriptDetails;
    for (const auto& address : htlcAddresses) {
        CScript script;
        scriptDetails.emplace_back(GetHTLCScript(pwallet, address, script), script);
    }

    const auto pair = spv::pspv->CreateHTLCTransaction(pwallet, scriptDetails, destinationAddress, "", feeRate, false);

    UniValue result(UniValue::VARR);
    result.push_back(pair.first);
    return result;
}

int CSpvWrapper::GetDBVersion() {
    int version{0};
    db->Read(DB_VERSION, version);
    return version;
}

int CSpvWrapper::SetDBVersion() {
    db->Write(DB_VERSION, SPV_DB_VERSION);
    return GetDBVersion();
}

/*
 * Encapsulates metadata into OP_RETURN + N * P2WSH scripts
 */
std::vector<CScript> EncapsulateMeta(TBytes const & meta)
{
    CDataStream ss(spv::BtcAnchorMarker, SER_NETWORK, PROTOCOL_VERSION);
    ss << static_cast<uint32_t>(meta.size());
    ss += CDataStream (meta, SER_NETWORK, PROTOCOL_VERSION);;

    std::vector<CScript> result;
    // first part with OP_RETURN
    size_t opReturnSize = std::min<size_t>(ss.size(), 80);
    CScript opReturnScript = CScript() << OP_RETURN << TBytes(ss.begin(), ss.begin() + opReturnSize);
    result.push_back(opReturnScript);

    // rest of the data in p2wsh keys
    for (auto it = ss.begin() + opReturnSize; it != ss.end(); ) {
        auto chunkSize = std::min<size_t>(ss.end() - it, 32);
        TBytes chunk(it, it + chunkSize);
        if (chunkSize < 32) {
            chunk.resize(32);
        }
        CScript p2wsh = CScript() << OP_0 << chunk;
        result.push_back(p2wsh);
        it += chunkSize;
    }
    return result;
}

uint64_t EstimateAnchorCost(TBytes const & meta, uint64_t feerate)
{
    auto consensus = Params().GetConsensus();

    std::vector<TxOutput> outputs;

    TBytes dummyScript(CreateScriptForAddress(consensus.spv.anchors_address.c_str()));

    // output[0] - anchor address with creation fee
    outputs.push_back({ P2PKH_DUST, dummyScript });

    auto metaScripts = EncapsulateMeta(meta);
    // output[1] - metadata (first part with OP_RETURN)
    outputs.push_back({ 0, ToByteVector(metaScripts[0])});

    // output[2..n-1] - metadata (rest of the data in p2wsh keys)
    for (size_t i = 1; i < metaScripts.size(); ++i) {
        outputs.push_back({ P2WSH_DUST, ToByteVector(metaScripts[i]) });
    }
    // dummy change output
    outputs.push_back({ P2PKH_DUST, dummyScript});

    TxInput dummyInput { toUInt256("1111111111111111111111111111111111111111111111111111111111111111"), 0, 1000000, dummyScript };
    auto rawtx = CreateRawTx({{dummyInput, TXIN_SEQUENCE}}, outputs);
    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());
    if (!tx) {
        LogPrint(BCLog::SPV, "***FAILED*** %s:\n", __func__);
        return 0;
    }
    uint64_t const minFee = BRTransactionStandardFee(tx) * feerate / TX_FEE_PER_KB;

    BRTransactionFree(tx);

    return P2PKH_DUST + (P2WSH_DUST * (metaScripts.size()-1)) + minFee;
}

std::tuple<uint256, TBytes, uint64_t> CreateAnchorTx(std::vector<TxInputData> const & inputsData, TBytes const & meta, uint64_t feerate)
{
    assert(inputsData.size() > 0);
    assert(meta.size() > 0);

    uint64_t inputTotal = 0;
    std::vector<std::pair<TxInput, uint32_t>> inputs;
    std::vector<BRKey> inputKeys;
    for (TxInputData const & input : inputsData) {
        UInt256 inHash = UInt256Reverse(toUInt256(input.txhash.c_str()));

        // creating key(priv/pub) from WIF priv
        BRKey inputKey;
        if (!BRKeySetPrivKey(&inputKey, input.privkey_wif.c_str())) {
            LogPrint(BCLog::SPV, "***FAILED*** %s: Can't parse WIF privkey %s\n", __func__, input.privkey_wif);
            throw std::runtime_error("spv: Can't parse WIF privkey " + input.privkey_wif);
        }
        inputKeys.push_back(inputKey);

        BRAddress address = BR_ADDRESS_NONE;
        BRKeyLegacyAddr(&inputKey, address.s, sizeof(address));
        TBytes inputScript(CreateScriptForAddress(address.s));

        inputTotal += input.amount;
        inputs.push_back({{ inHash, input.txn, input.amount, inputScript}, TXIN_SEQUENCE});
    }

    auto consensus = Params().GetConsensus();

    // for the case of unused new anchor address from the wallet:
//    BRWallet * wallet = pspv->GetWallet();
//    auto anchorAddr = BRWalletLegacyAddress(wallet);
    BRAddress anchorAddr = BR_ADDRESS_NONE;
    consensus.spv.anchors_address.copy(anchorAddr.s, consensus.spv.anchors_address.size());

    TBytes anchorScript(CreateScriptForAddress(anchorAddr.s));

    std::vector<TxOutput> outputs;
    // output[0] - anchor address with creation fee
    outputs.push_back({ P2PKH_DUST, anchorScript });

    auto metaScripts = EncapsulateMeta(meta);
    // output[1] - metadata (first part with OP_RETURN)
    outputs.push_back({ 0, ToByteVector(metaScripts[0])});

    // output[2..n-1] - metadata (rest of the data in p2wsh keys)
    for (size_t i = 1; i < metaScripts.size(); ++i) {
        outputs.push_back({ P2WSH_DUST, ToByteVector(metaScripts[i]) });
    }

    auto rawtx = CreateRawTx(inputs, outputs);
    LogPrint(BCLog::SPV, "TXunsigned: %s\n", HexStr(rawtx));

    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());

    if (!tx) {
        LogPrint(BCLog::SPV, "***FAILED*** %s: BRTransactionParse()\n", __func__);
        throw std::runtime_error("spv: Can't parse created transaction");
    }

    if (tx->inCount != inputs.size() || tx->outCount != outputs.size()) {
        LogPrint(BCLog::SPV, "***FAILED*** %s: inputs: %lu(%lu) outputs %lu(%lu)\n", __func__, tx->inCount, inputs.size(), tx->outCount, outputs.size());
        BRTransactionFree(tx);
        throw std::runtime_error("spv: Can't parse created transaction (inputs/outputs), see log");
    }

    // output[n] (optional) - change
    uint64_t const minFee = BRTransactionStandardFee(tx) * feerate / TX_FEE_PER_KB;
    uint64_t totalCost = P2PKH_DUST + (P2WSH_DUST * (metaScripts.size()-1)) + minFee;

    if (inputTotal < totalCost) {
        LogPrint(BCLog::SPV, "***FAILED*** %s: Not enough money to create anchor: %lu (need %lu)\n", __func__, inputTotal, totalCost);
        BRTransactionFree(tx);
        throw std::runtime_error("Not enough money to create anchor: " + std::to_string(inputTotal) + " (need " + std::to_string(totalCost) + ")");
    }

    auto change = inputTotal - totalCost;
    if (change > P2PKH_DUST) {
        BRTransactionAddOutput(tx, change, inputs[0].first.script.data(), inputs[0].first.script.size());
        totalCost += 34; // 34 is an estimated cost of change output itself
    }
    else {
        totalCost = inputTotal;
    }
    LogPrint(BCLog::SPV, "%s: total cost: %lu\n", __func__, totalCost);

    BRTransactionSign(tx, 0, inputKeys.data(), inputKeys.size());
    {   // just check
        BRAddress addr1;
        BRAddressFromScriptSig(addr1.s, sizeof(addr1), tx->inputs[0].signature, tx->inputs[0].sigLen);

        BRAddress addr2 = BR_ADDRESS_NONE;
        BRKeyLegacyAddr(&inputKeys[0], addr2.s, sizeof(addr2));

        if (!BRTransactionIsSigned(tx) || !BRAddressEq(&addr1, &addr2)) {
            LogPrint(BCLog::SPV, "***FAILED*** %s: BRTransactionSign()\n", __func__);
            BRTransactionFree(tx);
            throw std::runtime_error("spv: Can't sign transaction (wrong keys?)");
        }
    }
    TBytes signedTx(BRTransactionSerialize(tx, NULL, 0));
    BRTransactionSerialize(tx, signedTx.data(), signedTx.size());
    uint256 const txHash = to_uint256(tx->txHash);

    BRTransactionFree(tx);

    return std::make_tuple(txHash, signedTx, totalCost);
}

bool CSpvWrapper::SendRawTx(TBytes rawtx, std::promise<int> * promise)
{
    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());
    if (tx) {
        OnSendRawTx(tx, promise);
    }
    return tx != nullptr;
}

void CSpvWrapper::OnSendRawTx(BRTransaction *tx, std::promise<int> * promise)
{
    assert(tx);
    if (BRTransactionIsSigned(tx)) {
        BRPeerManagerPublishTx(manager, tx, promise, publishedTxCallback);
    }
    else {
        if (promise)
            promise->set_value(WSAEINVAL);
        BRTransactionFree(tx);
    }
}

CFakeSpvWrapper::CFakeSpvWrapper() : CSpvWrapper(false, 1 << 23, true, true) {
    spv_mainnet = 2;
}

// Promise used to toggle between anchor testing and SPV Bitcoin user wallet testing.
void CFakeSpvWrapper::OnSendRawTx(BRTransaction *tx, std::promise<int> * promise)
{
    assert(tx);
    if (!IsConnected()) {
        if (promise)
            promise->set_value(ENOTCONN);

        BRTransactionFree(tx);
        return;
    }

    // Add transaction to wallet
    BRWalletRegisterTransaction(wallet, tx);

    // Use realistic time
    tx->timestamp = GetTime();

    // UInt256 cannot be null or anchor will remain in pending assumed unconfirmed
    OnTxUpdated(&tx->txHash, 1, lastBlockHeight, GetTime() + 1000, UInt256{ .u64 = { 1, 1, 1, 1 } });

    if (promise) {
        promise->set_value(0);
    }
}

UniValue CFakeSpvWrapper::SendBitcoins(CWallet* const pwallet, std::string address, int64_t amount, uint64_t feeRate)
{
    // Normal TX, pass to parent.
    if (amount != -1) {
        return CSpvWrapper::SendBitcoins(pwallet, address, amount, feeRate);
    }

    // Fund Bitcoin wallet for testing with 1 Bitcoin.

    // Get real key for input and signing to make TX with unique TXID
    CPubKey new_key;
    if (!pwallet->GetKeyFromPool(new_key)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    CTxDestination dest = GetDestinationForKey(new_key, OutputType::BECH32);
    CKeyID keyid = GetKeyForDestination(*pwallet, dest);
    if (keyid.IsNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get address hash.");
    }

    CKey vchSecret;
    if (!pwallet->GetKey(keyid, vchSecret)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to get address private key.");
    }

    UInt256 rawKey;
    memcpy(&rawKey, &(*vchSecret.begin()), vchSecret.size());

    BRKey inputKey;
    if (!BRKeySetSecret(&inputKey, &rawKey, vchSecret.IsCompressed())) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to create private key.");
    }

    // Create blank TX
    BRTransaction *tx = BRTransactionNew();

    // Add output
    BRTxOutput o = BR_TX_OUTPUT_NONE;
    BRTxOutputSetAddress(&o, address.c_str());
    BRTransactionAddOutput(tx, SATOSHIS, o.script, o.scriptLen);

    // Add Bech32 input
    TBytes script(2 + sizeof(keyid), OP_0);
    script[1] = 0x14;
    memcpy(script.data() + 2, keyid.begin(), sizeof(keyid));
    BRTransactionAddInput(tx, toUInt256("1111111111111111111111111111111111111111111111111111111111111111"), 0,
                          SATOSHIS + 1000, script.data(), script.size(), nullptr, 0, nullptr, 0, TXIN_SEQUENCE);

    // Sign TX
    BRTransactionSign(tx, 0, &inputKey, 1);
    if (!BRTransactionIsSigned(tx)) {
        BRTransactionFree(tx);
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction.");
    }

    std::string txid = to_uint256(tx->txHash).ToString();
    OnSendRawTx(tx, nullptr);

    return txid;
}


TBytes CreateScriptForAddress(char const * address)
{
    TBytes script(BRAddressScriptPubKey(NULL, 0, address));
    BRAddressScriptPubKey(script.data(), script.size(), address);
    return script;
}

bool IsAnchorTx(BRTransaction *tx, CAnchor & anchor)
{
    if (!tx)
        return false;

    /// @todo check amounts here if it will be some additional anchoring fee
    if (tx->outCount < 2 || strcmp(tx->outputs[0].address, Params().GetConsensus().spv.anchors_address.c_str()) != 0) {
        return false;
    }

    CScript const & memo = CScript(tx->outputs[1].script, tx->outputs[1].script+tx->outputs[1].scriptLen);
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    spv::TBytes opReturnData;
    if (!memo.GetOp(pc, opcode, opReturnData) ||
        (opcode > OP_PUSHDATA1 && opcode != OP_PUSHDATA2 && opcode != OP_PUSHDATA4) ||
        memcmp(&opReturnData[0], &BtcAnchorMarker[0], BtcAnchorMarker.size()) != 0 ||
        opReturnData.size() < BtcAnchorMarker.size() + 2) { // 80?
        return false;
    }
    CDataStream ss(opReturnData, SER_NETWORK, PROTOCOL_VERSION);
    ss.ignore(static_cast<int>(BtcAnchorMarker.size()));
    uint32_t dataSize;
    ss >> dataSize;
    if (dataSize < ss.size() || (dataSize - ss.size() > (tx->outCount - 2) * 32)) {
        return false;
    }
    for (size_t i = 2; i < tx->outCount && ss.size() < dataSize; ++i) {
        if (tx->outputs[i].scriptLen != 34 || tx->outputs[i].script[0] != OP_0 || tx->outputs[i].script[1] != 32) { // p2wsh script
            LogPrint(BCLog::SPV, "not a p2wsh output #%d\n", i);
            return false;
        }
        auto script = (const char*)(tx->outputs[i].script);
        ss.insert(ss.end(), script + 2, script + 34);
    }
    try {
        ss >> anchor;
    } catch (std::ios_base::failure const & e) {
        LogPrint(BCLog::SPV, "can't deserialize anchor from tx %s\n", to_uint256(tx->txHash).ToString());
        return false;
    }
    return true;
}

void CFakeSpvWrapper::Connect()
{
    isConnected = true;
}

void CFakeSpvWrapper::Disconnect()
{
    AssertLockNotHeld(cs_main); /// @attention due to calling txStatusUpdate() (OnTxUpdated()), savePeers(), syncStopped()
    isConnected = false;
}

bool CFakeSpvWrapper::IsConnected() const
{
    return isConnected;
}

void CFakeSpvWrapper::CancelPendingTxs()
{
}


}


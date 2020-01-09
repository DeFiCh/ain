// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <spv/spv_wrapper.h>

#include <chainparams.h>

#include <masternodes/anchors.h>
#include <net_processing.h>

#include <spv/support/BRKey.h>
#include <spv/support/BRAddress.h>
#include <spv/support/BRBIP39Mnemonic.h>
#include <spv/support/BRBIP32Sequence.h>
#include <spv/bitcoin/BRPeerManager.h>
#include <spv/bitcoin/BRChainParams.h>
#include <spv/bcash/BRBCashParams.h>

#include <sync.h>
#include <util/strencodings.h>
#include <wallet/wallet.h>

#include <string.h>
#include <inttypes.h>
#include <errno.h>

extern RecursiveMutex cs_main;

namespace spv
{

std::unique_ptr<CSpvWrapper> pspv;

using namespace std;

// Prefixes to the masternodes database (masternodes/)
static const char DB_SPVBLOCKS = 'B';     // spv "blocks" table
static const char DB_SPVPEERS  = 'P';     // spv "peers" table
static const char DB_SPVTXS    = 'T';     // spv "tx2msg" table

uint256 to_uint256(const UInt256 & i) {
    return uint256(TBytes(&i.u8[0], &i.u8[32]));
}

/// spv wallet manager's callbacks wrappers:
void balanceChanged(void *info, uint64_t balance)
{
    static_cast<CSpvWrapper *>(info)->OnBalanceChanged(balance);
}
void txAdded(void *info, BRTransaction *tx)
{
    static_cast<CSpvWrapper *>(info)->OnTxAdded(tx);
}
void txUpdated(void *info, const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp)
{
    static_cast<CSpvWrapper *>(info)->OnTxUpdated(txHashes, txCount, blockHeight, timestamp);
}
void txDeleted(void *info, UInt256 txHash, int notifyUser, int recommendRescan)
{
    static_cast<CSpvWrapper *>(info)->OnTxDeleted(txHash, notifyUser, recommendRescan);
}

/// spv peer manager's callbacks wrappers:
void syncStarted(void *info)
{
    static_cast<CSpvWrapper *>(info)->OnSyncStarted();
}
void syncStopped(void *info, int error)
{
    static_cast<CSpvWrapper *>(info)->OnSyncStopped(error);
}
void txStatusUpdate(void *info)
{
    static_cast<CSpvWrapper *>(info)->OnTxStatusUpdate();
}
void saveBlocks(void *info, int replace, BRMerkleBlock *blocks[], size_t blocksCount)
{
    static_cast<CSpvWrapper *>(info)->OnSaveBlocks(replace, blocks, blocksCount);
}
void savePeers(void *info, int replace, const BRPeer peers[], size_t peersCount)
{
    static_cast<CSpvWrapper *>(info)->OnSavePeers(replace, peers, peersCount);
}
void threadCleanup(void *info)
{
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
    /// @attention don't forget to increase both 'n' in BRTestNetCheckpoints[n]
}

CSpvWrapper::CSpvWrapper(bool isMainnet, size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / (isMainnet ?  "spv" : "spv_testnet"), nCacheSize, fMemory, fWipe))
{
    SetCheckpoints();

    // Configuring spv logs:
    // (we need intermediate persistent storage for filename here)
    spv_internal_logfilename = (LogInstance().m_file_path.remove_filename() / "spv.log").string();
    spv_logfilename = spv_internal_logfilename.c_str();
    LogPrintf("spv: internal logs set to %s\n", spv_logfilename);
    spv_log2console = 0;
    spv_mainnet = isMainnet ? 1 : 0;

    BRMasterPubKey mpk = BR_MASTER_PUBKEY_NONE;

    // test:
//    UInt512 seed = UINT512_ZERO;
//    BRBIP39DeriveKey(seed.u8, "axis husband project any sea patch drip tip spirit tide bring belt", NULL);
//    mpk = BRBIP32MasterPubKey(&seed, sizeof(seed));

    mpk = BRBIP32ParseMasterPubKey(Params().GetConsensus().spv.wallet_xpub.c_str());

    char xpub_buf[BRBIP32SerializeMasterPubKey(NULL, 0, mpk)];
    BRBIP32SerializeMasterPubKey(xpub_buf, sizeof(xpub_buf), mpk);
    LogPrintf("spv: debug xpub: %s\n", &xpub_buf[0]);

    std::vector<BRTransaction *> txs;
    // load txs
    {
        std::function<void (uint256 const &, db_tx_rec &)> onLoadTx = [&txs, this] (uint256 const & hash, db_tx_rec & rec) {
            BRTransaction *tx = BRTransactionParse(rec.first.data(), rec.first.size());
            tx->blockHeight = rec.second.first;
            tx->timestamp = rec.second.second;
            txs.push_back(tx);

            LogPrintf("spv: load tx: %s, height: %d\n", to_uint256(tx->txHash).ToString(), tx->blockHeight);

            CAnchor anchor;
            if (IsAnchorTx(tx, anchor)) {
                LogPrintf("spv: LOAD POSSIBLE ANCHOR TX, tx: %s, blockHash: %s, height: %d, btc height: %d\n", to_uint256(tx->txHash).ToString(), anchor.blockHash.ToString(), anchor.height, tx->blockHeight);
            }
        };
        // can't deduce lambda here:
        IterateTable(DB_SPVTXS, onLoadTx);
    }

    wallet = BRWalletNew(txs.data(), txs.size(), mpk, 0);
    BRWalletSetCallbacks(wallet, this, balanceChanged, txAdded, txUpdated, txDeleted);
    LogPrintf("spv: wallet created with first receive address: %s\n", BRWalletLegacyAddress(wallet).s);

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
    manager = BRPeerManagerNew(BRGetChainParams(), wallet, 1572480000, blocks.data(), blocks.size(), NULL, 0); // date is 31/10/2019 - first block (1584558) in testnet with regtest anchoring address

    // can't wrap member function as static "C" function here:
    BRPeerManagerSetCallbacks(manager, this, syncStarted, syncStopped, txStatusUpdate,
                              saveBlocks, savePeers, nullptr /*networkIsReachable*/, threadCleanup);

}

CSpvWrapper::~CSpvWrapper()
{
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
    BRPeerManagerDisconnect(manager);
    uint64_t balance = BRWalletBalance(wallet);
    LogPrintf("spv: balance on disconnect: %lu\n", balance);
}

bool CSpvWrapper::IsConnected() const
{
    return BRPeerManagerConnectStatus(manager) == BRPeerStatusConnected;
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
    LogPrintf("spv: trying to rescan from block %d, current block %u\n", height, curHeight);
    BRPeerManagerRescanFromBlockNumber(manager, static_cast<uint32_t>(height));
    LogPrintf("spv: actual new current block %u\n", BRPeerManagerLastBlockHeight(manager));
    return true;
}

BRPeerManager const * CSpvWrapper::GetPeerManager() const
{
    return manager;
}

// we cant return the whole params itself due to conflicts in include files
uint8_t CSpvWrapper::GetPKHashPrefix() const
{
    return BRPeerManagerChainParams(manager)->base58_p2pkh;
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
    LogPrintf("spv: balance changed: %lu\n", balance);
}

std::vector<BRTransaction *> CSpvWrapper::GetWalletTxs() const
{
    std::vector<BRTransaction *> txs;
    txs.resize(BRWalletTransactions(wallet, nullptr, 0));
    size_t count = BRWalletTransactions(wallet, txs.data(), txs.size());

    LogPrintf("spv: wallet txs count: %lu\n", count);

    return txs;
}

void CSpvWrapper::OnTxAdded(BRTransaction * tx)
{
    uint256 const txHash{to_uint256(tx->txHash)};
    WriteTx(tx);
    LogPrintf("spv: tx added %s, at block %d, timestamp %d\n", txHash.ToString(), tx->blockHeight, tx->timestamp);
//    LogPrintf("spv: current wallet tx count %lu\n", GetWalletTxs().size());

    CAnchor anchor;
    if (IsAnchorTx(tx, anchor)) {

        LogPrintf("spv: IsAnchorTx(): %s\n", txHash.ToString());

        LOCK(cs_main);


            auto const topAnchor = panchors->GetActiveAnchor();
            auto const topTeam = panchors->GetCurrentTeam(topAnchor);

            if (ValidateAnchor(anchor, true)) {
            LogPrintf("spv: valid anchor tx: %s\n", txHash.ToString());

            if (panchors->AddAnchor(anchor, txHash, tx->blockHeight)) {
                LogPrintf("spv: adding anchor %s\n", txHash.ToString());
                // do not try to activate cause tx unconfirmed yet
//    panchors->ActivateBestAnchor();
                    auto myIDs = pmasternodesview->AmIOperator();
                    if (myIDs && pmasternodesview->ExistMasternode(myIDs->id)->IsActive() && topTeam.find(myIDs->operatorAuthAddress) != topTeam.end()) {
                        std::vector<std::shared_ptr<CWallet>> wallets = GetWallets();
                        CKey masternodeKey{};
                        for (auto const wallet : wallets) {
                            if (wallet->GetKey(myIDs->operatorAuthAddress, masternodeKey)) {
                                break;
                            }
                            masternodeKey = CKey{};
                        }

                        if (!masternodeKey.IsValid()) {
                            LogPrintf("spv: ***FAILED*** %s: Can't read masternode operator private key", __func__);
                            return;
                        }

                        auto confirmMessage = CAnchorConfirmMessage::Create(anchor, masternodeKey);
                        RelayAnchorConfirm(confirmMessage.GetHash(), *g_connman);
                    }
            }
        }
    }
    else {
        LogPrintf("spv: not an anchor tx %s\n", txHash.ToString());
    }

}

void CSpvWrapper::OnTxUpdated(const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp)
{
    bool needReActivation{false};
    for (size_t i = 0; i < txCount; ++i) {
        uint256 const txHash{to_uint256(txHashes[i])};
        UpdateTx(txHash, blockHeight, timestamp);
        LogPrintf("spv: tx updated, hash: %s, blockHeight: %d, timestamp: %d\n", txHash.ToString(), blockHeight, timestamp);

        LOCK(cs_main);

        // update index. no any checks nor validations
        auto exist = panchors->GetAnchorByBtcTx(txHash);
        if (exist) {
            LogPrintf("spv: updating anchor %s\n", txHash.ToString());
            CAnchor oldAnchor{exist->anchor};
            if (panchors->AddAnchor(oldAnchor, txHash, blockHeight)) {
                LogPrintf("spv: updated anchor %s\n", txHash.ToString());
                needReActivation = true;
            }
        }
    }
    if (needReActivation) {
        LOCK(cs_main);
        panchors->ActivateBestAnchor();
    }
}

void CSpvWrapper::OnTxDeleted(UInt256 txHash, int notifyUser, int recommendRescan)
{
    uint256 const hash(to_uint256(txHash));
    EraseTx(hash);

    LOCK(cs_main);
    panchors->DeleteAnchorByBtcTx(hash);

    LogPrintf("spv: tx deleted: %s; notifyUser: %d, recommendRescan: %d\n", hash.ToString(), notifyUser, recommendRescan);
}

void CSpvWrapper::OnSyncStarted()
{
    LogPrintf("spv: sync started!\n");
}

void CSpvWrapper::OnSyncStopped(int error)
{
    initialSync = false;
    LogPrintf("spv: sync stopped!\n");
}

void CSpvWrapper::OnTxStatusUpdate()
{
    LogPrintf("spv: tx status update\n");
}

void CSpvWrapper::OnSaveBlocks(int replace, BRMerkleBlock * blocks[], size_t blocksCount)
{
    if (replace)
    {
        LogPrintf("spv: BLOCK: 'replace' requested, deleting...\n");
        DeleteTable<uint256>(DB_SPVBLOCKS);
    }
    for (size_t i = 0; i < blocksCount; ++i) {
        WriteBlock(blocks[i]);
        LogPrintf("spv: BLOCK: %u, %s saved\n", blocks[i]->height, to_uint256(blocks[i]->blockHash).ToString());
    }
    CommitBatch();
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
    db->Write(make_pair(DB_SPVTXS, to_uint256(tx->txHash)), std::make_pair(buf, std::make_pair(tx->blockHeight, tx->timestamp)) );
}

void CSpvWrapper::UpdateTx(uint256 const & hash, uint32_t blockHeight, uint32_t timestamp)
{
    auto const key{std::make_pair(DB_SPVTXS, hash)};
    std::pair<TBytes, std::pair<uint32_t, uint32_t> > txrec;
    if (db->Read(key, txrec)) {
        txrec.second.first = blockHeight;
        txrec.second.second = timestamp;
        db->Write(key, txrec);
    }
}

void CSpvWrapper::EraseTx(uint256 const & hash)
{
    db->Erase(make_pair(DB_SPVTXS, hash));
}

void CSpvWrapper::WriteBlock(const BRMerkleBlock * block)
{
    static TBytes buf;
    size_t blockSize = BRMerkleBlockSerialize(block, NULL, 0);
    buf.resize(blockSize);
    BRMerkleBlockSerialize(block, buf.data(), blockSize);

    BatchWrite(make_pair(DB_SPVBLOCKS, to_uint256(block->blockHash)), make_pair(buf, block->height));
}

void publishedTxCallback(void *info, int error)
{
    LogPrintf("spv: publishedTxCallback: tx 2: %s, %s\n", to_uint256(*(UInt256 *)info).ToString(), error == 0 ? "SUCCESS!" : "error: " + std::to_string(errno));
}

TBytes CreateRawTx(std::vector<TxInput> const & inputs, std::vector<TxOutput> const & outputs)
{
    BRTransaction *tx = BRTransactionNew();
    for (auto input : inputs) {
        BRTransactionAddInput(tx, input.txHash, input.index, input.amount, input.script.data(), input.script.size(), NULL, 0, NULL, 0, TXIN_SEQUENCE);
    }
    for (auto output : outputs) {
        BRTransactionAddOutput(tx, output.amount, output.script.data(), output.script.size());
    }
    size_t len = BRTransactionSerialize(tx, NULL, 0);
    TBytes buf(len);
    len = BRTransactionSerialize(tx, buf.data(), buf.size());
    BRTransactionFree(tx);
    return len ? buf : TBytes{};
}

TBytes CreateAnchorTx(std::string const & hash, int32_t index, uint64_t inputAmount, std::string const & privkey_wif, TBytes const & meta)
{
    /// @todo calculate minimum fee
    uint64_t fee = 10000;
    /// @todo test this amount of dust for p2wsh due to spv is very dumb and checks only for 546 (p2pkh)
    uint64_t const p2wsh_dust = 330; /// 546 p2pkh & 294 p2wpkh (330 p2wsh calc'ed manually)
    uint64_t const p2pkh_dust = 546;
    UInt256 inHash = UInt256Reverse(toUInt256(hash.c_str()));

    CDataStream ss(spv::BtcAnchorMarker, SER_NETWORK, PROTOCOL_VERSION);
    ss << static_cast<uint32_t>(meta.size());
    ss += CDataStream (meta, SER_NETWORK, PROTOCOL_VERSION);;

    // creating key(priv/pub) from WIF priv
    BRKey inputKey;
    BRKeySetPrivKey(&inputKey, privkey_wif.c_str());
    BRKeyPubKey(&inputKey, NULL, 0);

    BRAddress address = BR_ADDRESS_NONE;
    BRKeyLegacyAddr(&inputKey, address.s, sizeof(address));
    size_t inputScriptLen = BRAddressScriptPubKey(NULL, 0, address.s);
    TBytes inputScript(inputScriptLen);
    BRAddressScriptPubKey(inputScript.data(), inputScript.size(), address.s);

    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;

    // create single input (current restriction)
    inputs.push_back({ inHash, index, inputAmount, inputScript});

    auto consensus = Params().GetConsensus();

    // for the case of unused new anchor address from the wallet:
//    BRWallet * wallet = pspv->GetWallet();
//    auto anchorAddr = BRWalletLegacyAddress(wallet);
    BRAddress anchorAddr = BR_ADDRESS_NONE;
    consensus.spv.anchors_address.copy(anchorAddr.s, consensus.spv.anchors_address.size());

    size_t anchorScriptLen = BRAddressScriptPubKey(NULL, 0, anchorAddr.s);
    TBytes anchorScript(anchorScriptLen);
    BRAddressScriptPubKey(anchorScript.data(), anchorScript.size(), anchorAddr.s);

    // output[0] - anchor address with creation fee
    outputs.push_back({ (uint64_t) consensus.spv.creationFee, anchorScript });
    // output[1] - metadata (first part with OP_RETURN)
    size_t opReturnSize = std::min(ss.size(), 80ul);
    CScript opReturnScript = CScript() << OP_RETURN << TBytes(ss.begin(), ss.begin() + opReturnSize);
    outputs.push_back({ 0, ToByteVector(opReturnScript)});

    // output[2..n-1] - metadata (rest of the data in p2wsh keys)
    for (auto it = ss.begin() + opReturnSize; it != ss.end(); ) {
        auto chunkSize = std::min(ss.end() - it, 32l);
        TBytes chunk(it, it + chunkSize);
        if (chunkSize < 32) {
            chunk.resize(32);
        }
        CScript p2wsh = CScript() << OP_0 << chunk;
        outputs.push_back({ p2wsh_dust, ToByteVector(p2wsh) });
        it += chunkSize;
    }

    auto rawtx = CreateRawTx(inputs, outputs);
    LogPrintf("spv: TXunsigned: %s\n", HexStr(rawtx));

    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());

    if (! tx || tx->inCount != inputs.size() || tx->outCount != outputs.size())
        LogPrintf("spv: ***FAILED*** %s: BRTransactionParse(): tx->inCount: %lu tx->outCount %lu\n", __func__, tx ? tx->inCount : 0, tx ? tx->outCount: 0);
    else {
        LogPrintf("spv: ***OK*** %s: BRTransactionParse(): tx->inCount: %lu tx->outCount %lu\n", __func__, tx->inCount, tx->outCount);
    }

    // output[n] (optional) - change
    uint64_t const minFee = BRTransactionStandardFee(tx);

    auto change = inputAmount - consensus.spv.creationFee - (p2wsh_dust * (outputs.size()-2)) - minFee - 3*34; // (3*34) is an estimated cost of change output itself
    if (change > p2pkh_dust) {
        BRTransactionAddOutput(tx, change, inputScript.data(), inputScript.size());
    }

    BRTransactionSign(tx, 0, &inputKey, 1);
    {   // just check
        BRAddress addr;
        BRAddressFromScriptSig(addr.s, sizeof(addr), tx->inputs[0].signature, tx->inputs[0].sigLen);
        if (!BRTransactionIsSigned(tx) || !BRAddressEq(&address, &addr)) {
            LogPrintf("spv: ***FAILED*** %s: BRTransactionSign()\n", __func__);
            BRTransactionFree(tx);
            return {};
        }
    }
    TBytes signedTx(BRTransactionSerialize(tx, NULL, 0));
    BRTransactionSerialize(tx, signedTx.data(), signedTx.size());

    LogPrintf("spv: TXsigned: %s\n", HexStr(signedTx));
    BRTransactionFree(tx);

    return signedTx;
}

// just for tests & experiments
TBytes CreateSplitTx(std::string const & hash, int32_t index, uint64_t inputAmount, std::string const & privkey_wif, int parts, int amount)
{
    /// @todo calculate minimum fee
//    uint64_t fee = 10000;
    uint64_t const p2pkh_dust = 546;
    UInt256 inHash = UInt256Reverse(toUInt256(hash.c_str()));

    // creating key(priv/pub) from WIF priv
    BRKey inputKey;
    BRKeySetPrivKey(&inputKey, privkey_wif.c_str());
    BRKeyPubKey(&inputKey, NULL, 0);

    BRAddress address = BR_ADDRESS_NONE;
    BRKeyLegacyAddr(&inputKey, address.s, sizeof(address));
    size_t inputScriptLen = BRAddressScriptPubKey(NULL, 0, address.s);
    TBytes inputScript(inputScriptLen);
    BRAddressScriptPubKey(inputScript.data(), inputScript.size(), address.s);

    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;

    // create single input (current restriction)
    inputs.push_back({ inHash, index, inputAmount, inputScript});

    BRWallet * wallet = pspv->GetWallet();

    uint64_t const valuePerOutput = (amount > 0) && (parts*(amount + 34*3) + 148*3 < inputAmount) ? amount : (inputAmount - 148*3) / parts - 34*3; // 34*3 is min estimated fee per p2pkh output
    uint64_t sum = 0;
    for (int i = 0; i < parts; ++i) {
        auto addr = BRWalletLegacyAddress(wallet);

        TBytes script(BRAddressScriptPubKey(NULL, 0, addr.s));
        BRAddressScriptPubKey(script.data(), script.size(), addr.s);

        // output[0] - anchor address with creation fee
        outputs.push_back({ valuePerOutput, script });
        sum += valuePerOutput;
    }

    auto rawtx = CreateRawTx(inputs, outputs);
    LogPrintf("spv: TXunsigned: %s\n", HexStr(rawtx));

    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());

    if (! tx)
        LogPrintf("spv: ***FAILED*** %s: BRTransactionParse(): tx->inCount: %lu tx->outCount %lu\n", __func__, tx ? tx->inCount : 0, tx ? tx->outCount: 0);
    else {
        LogPrintf("spv: ***OK*** %s: BRTransactionParse(): tx->inCount: %lu tx->outCount %lu\n", __func__, tx->inCount, tx->outCount);
    }

    // output[n] (optional) - change
    uint64_t const minFee = BRTransactionStandardFee(tx);

    auto change = inputAmount - sum - minFee - 3*34; // (3*34) is an estimated cost of change output itself
    if (change > p2pkh_dust) {
        BRTransactionAddOutput(tx, change, inputScript.data(), inputScript.size());
    }

    BRTransactionSign(tx, 0, &inputKey, 1);
    {   // just check
        BRAddress addr;
        BRAddressFromScriptSig(addr.s, sizeof(addr), tx->inputs[0].signature, tx->inputs[0].sigLen);
        if (!BRTransactionIsSigned(tx) || !BRAddressEq(&address, &addr)) {
            LogPrintf("spv: ***FAILED*** %s: BRTransactionSign()\n", __func__);
            BRTransactionFree(tx);
            return {};
        }
    }
    TBytes signedTx(BRTransactionSerialize(tx, NULL, 0));
    BRTransactionSerialize(tx, signedTx.data(), signedTx.size());

    LogPrintf("spv: TXsigned: %s\n", HexStr(signedTx));
    BRTransactionFree(tx);

    return signedTx;
}

bool CSpvWrapper::SendRawTx(TBytes rawtx)
{
    if (BRPeerManagerPeerCount(manager) == 0) {
        return false; // not connected
    }

    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());
    if (tx) {
        if (BRTransactionIsSigned(tx)) {
            BRPeerManagerPublishTx(manager, tx, &tx->txHash, publishedTxCallback);
        }
        else {
            BRTransactionFree(tx);
            tx = nullptr;
        }
    }
    return tx != nullptr;
}

TBytes CreateScriptForAddress(std::string const & address)
{
    TBytes script;
    script.resize(BRAddressScriptPubKey(NULL, 0, address.c_str()));
    BRAddressScriptPubKey(script.data(), script.size(), address.c_str());
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
            LogPrintf("spv: not a p2wsh output #%d\n", i);
            return false;
        }
        auto script = (const char*)(tx->outputs[i].script);
        ss.insert(ss.end(), script + 2, script + 34);
    }
    try {
        ss >> anchor;
    } catch (std::ios_base::failure const & e) {
        LogPrintf("spv: can't deserialize anchor from tx %s\n", to_uint256(tx->txHash).ToString());
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
    isConnected = false;
}

bool CFakeSpvWrapper::IsConnected() const
{
    return isConnected;
}

bool CFakeSpvWrapper::SendRawTx(TBytes rawtx)
{
    if (!IsConnected()) {
        return false;
    }

    BRTransaction *tx = BRTransactionParse(rawtx.data(), rawtx.size());
    if (tx /*&& BRTransactionIsSigned(tx)*/) {
//        BRPeerManagerPublishTx(manager, tx, &tx->txHash, publishedTxCallback); // just note that txHash is wrong here (need to be reversed)
        LogPrintf("fakespv: adding anchor tx %s\n", to_uint256(tx->txHash).ToString());
        OnTxAdded(tx);
        OnTxUpdated(&tx->txHash, 1, lastBlockHeight, 666);
        BRTransactionFree(tx);
    }
    else {
        tx = NULL;
    }
    return tx;
}

}


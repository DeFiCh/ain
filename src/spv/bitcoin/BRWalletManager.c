//
//  BRWalletManager.c
//  BRCore
//
//  Created by Ed Gamble on 11/21/18.
//  Copyright © 2018 breadwallet. All rights reserved.
//

#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "BRArray.h"
#include "BRSet.h"
#include "BRWalletManager.h"
#include "BRPeerManager.h"
#include "BRMerkleBlock.h"
#include "BRBase58.h"
#include "BRChainParams.h"
#include "bcash/BRBCashParams.h"

#include "../support/BRFileService.h"

/* Forward Declarations */

static void _BRWalletManagerBalanceChanged (void *info, uint64_t balanceInSatoshi);
static void _BRWalletManagerTxAdded   (void *info, BRTransaction *tx);
static void _BRWalletManagerTxUpdated (void *info, const UInt256 *hashes, size_t count, uint32_t blockHeight, uint32_t timestamp);
static void _BRWalletManagerTxDeleted (void *info, UInt256 hash, int notifyUser, int recommendRescan);

static void _BRWalletManagerSyncStarted (void *info);
static void _BRWalletManagerSyncStopped (void *info, int reason);
static void _BRWalletManagerTxStatusUpdate (void *info);
static void _BRWalletManagerSaveBlocks (void *info, int replace, BRMerkleBlock **blocks, size_t count);
static void _BRWalletManagerSavePeers  (void *info, int replace, const BRPeer *peers, size_t count);
static int  _BRWalletManagerNetworkIsReachabele (void *info);
static void _BRWalletManagerThreadCleanup (void *info);

static const char *
getNetworkName (const BRChainParams *params) {
    if (params->magicNumber == BRMainNetParams->magicNumber ||
        params->magicNumber == BRBCashParams->magicNumber)
        return "mainnet";

    if (params->magicNumber == BRTestNetParams->magicNumber ||
        params->magicNumber == BRBCashTestNetParams->magicNumber)
        return "testnet";

    return NULL;
}

static const char *
getCurrencyName (const BRChainParams *params) {
    if (params->magicNumber == BRMainNetParams->magicNumber ||
        params->magicNumber == BRTestNetParams->magicNumber)
        return "btc";

    if (params->magicNumber == BRBCashParams->magicNumber ||
        params->magicNumber == BRBCashTestNetParams->magicNumber)
        return "bch";

    return NULL;
}

static BRWalletForkId
getForkId (const BRChainParams *params) {
    if (params->magicNumber == BRMainNetParams->magicNumber ||
        params->magicNumber == BRTestNetParams->magicNumber)
        return WALLET_FORKID_BITCOIN;

    if (params->magicNumber == BRBCashParams->magicNumber ||
        params->magicNumber == BRBCashTestNetParams->magicNumber)
        return WALLET_FORKID_BITCASH;

    return (BRWalletForkId) -1;
}

/// MARK: - BRWalletManager

struct BRWalletManagerStruct {
    //BRWalletForkId walletForkId;
    BRFileService fileService;
    BRWallet *wallet;
    BRPeerManager  *peerManager;
    BRWalletManagerClient client;
};

/// MARK: - Transaction File Service

static const char *fileServiceTypeTransactions = "transactions";

enum {
    WALLET_MANAGER_TRANSACTION_VERSION_1
};

static UInt256
fileServiceTypeTransactionV1Identifier (BRFileServiceContext context,
                                  BRFileService fs,
                                  const void *entity) {
    const BRTransaction *transaction = entity;
    return transaction->txHash;
}

static uint8_t *
fileServiceTypeTransactionV1Writer (BRFileServiceContext context,
                              BRFileService fs,
                              const void* entity,
                              uint32_t *bytesCount) {
    const BRTransaction *transaction = entity;

    size_t txTimestampSize  = sizeof (uint32_t);
    size_t txBlockHeightSize = sizeof (uint32_t);
    size_t txSize = BRTransactionSerialize (transaction, NULL, 0);

    assert (txTimestampSize   == sizeof(transaction->timestamp));
    assert (txBlockHeightSize == sizeof(transaction->blockHeight));

    *bytesCount = (uint32_t) (txSize + txBlockHeightSize + txTimestampSize);

    uint8_t *bytes = calloc (*bytesCount, 1);

    size_t bytesOffset = 0;

    BRTransactionSerialize (transaction, &bytes[bytesOffset], txSize);
    bytesOffset += txSize;

    UInt32SetLE (&bytes[bytesOffset], transaction->blockHeight);
    bytesOffset += txBlockHeightSize;

    UInt32SetLE(&bytes[bytesOffset], transaction->timestamp);

    return bytes;
}

static void *
fileServiceTypeTransactionV1Reader (BRFileServiceContext context,
                                    BRFileService fs,
                                    uint8_t *bytes,
                                    uint32_t bytesCount) {
    size_t txTimestampSize  = sizeof (uint32_t);
    size_t txBlockHeightSize = sizeof (uint32_t);

    BRTransaction *transaction = BRTransactionParse (bytes, bytesCount);
    if (NULL == transaction) return NULL;

    transaction->blockHeight = UInt32GetLE (&bytes[bytesCount - txTimestampSize - txBlockHeightSize]);
    transaction->timestamp   = UInt32GetLE (&bytes[bytesCount - txTimestampSize]);

    return transaction;
}

static BRArrayOf(BRTransaction*)
initialTransactionsLoad (BRWalletManager manager) {
    BRSetOf(BRTransaction*) transactionSet = BRSetNew(BRTransactionHash, BRTransactionEq, 100);
    if (1 != fileServiceLoad (manager->fileService, transactionSet, fileServiceTypeTransactions, 1)) {
        BRSetFree(transactionSet);
        return NULL;
    }

    size_t transactionsCount = BRSetCount(transactionSet);

    BRArrayOf(BRTransaction*) transactions;
    array_new (transactions, transactionsCount);
    array_set_count(transactions, transactionsCount);

    BRSetAll(transactionSet, (void**) transactions, transactionsCount);
    BRSetFree(transactionSet);

    return transactions;
}

/// MARK: - Block File Service

static const char *fileServiceTypeBlocks = "blocks";
enum {
    WALLET_MANAGER_BLOCK_VERSION_1
};

static UInt256
fileServiceTypeBlockV1Identifier (BRFileServiceContext context,
                                  BRFileService fs,
                                  const void *entity) {
    const BRMerkleBlock *block = (BRMerkleBlock*) entity;
    return block->blockHash;
}

static uint8_t *
fileServiceTypeBlockV1Writer (BRFileServiceContext context,
                              BRFileService fs,
                              const void* entity,
                              uint32_t *bytesCount) {
    const BRMerkleBlock *block = entity;

    // The serialization of a block does not include the block height.  Thus, we'll need to
    // append the height.

    // These are serialization sizes
    size_t blockHeightSize = sizeof (uint32_t);
    size_t blockSize = BRMerkleBlockSerialize(block, NULL, 0);

    // Confirm.
    assert (blockHeightSize == sizeof (block->height));

    // Update bytesCound with the total of what is written.
    *bytesCount = (uint32_t) (blockSize + blockHeightSize);

    // Get our bytes
    uint8_t *bytes = calloc (*bytesCount, 1);

    // We'll serialize the block itself first
    BRMerkleBlockSerialize(block, bytes, blockSize);

    // And then the height.
    UInt32SetLE(&bytes[blockSize], block->height);

    return bytes;
}

static void *
fileServiceTypeBlockV1Reader (BRFileServiceContext context,
                              BRFileService fs,
                              uint8_t *bytes,
                              uint32_t bytesCount) {
    size_t blockHeightSize = sizeof (uint32_t);

    BRMerkleBlock *block = BRMerkleBlockParse (bytes, bytesCount);
    if (NULL == block) return NULL;
    
    block->height = UInt32GetLE(&bytes[bytesCount - blockHeightSize]);

    return block;
}

static BRArrayOf(BRMerkleBlock*)
initialBlocksLoad (BRWalletManager manager) {
    BRSetOf(BRTransaction*) blockSet = BRSetNew(BRMerkleBlockHash, BRMerkleBlockEq, 100);
    if (1 != fileServiceLoad (manager->fileService, blockSet, fileServiceTypeBlocks, 1)) {
        BRSetFree (blockSet);
        return NULL;
    }

    size_t blocksCount = BRSetCount(blockSet);

    BRArrayOf(BRMerkleBlock*) blocks;
    array_new (blocks, blocksCount);
    array_set_count(blocks, blocksCount);

    BRSetAll(blockSet, (void**) blocks, blocksCount);
    BRSetFree(blockSet);

    return blocks;
}

/// MARK: - Peer File Service

static const char *fileServiceTypePeers = "peers";
enum {
    WALLET_MANAGER_PEER_VERSION_1
};

static UInt256
fileServiceTypePeerV1Identifier (BRFileServiceContext context,
                                  BRFileService fs,
                                  const void *entity) {
    const BRPeer *peer = entity;

    UInt256 hash;
    BRSHA256 (&hash, peer, sizeof(BRPeer));

    return hash;
}

static uint8_t *
fileServiceTypePeerV1Writer (BRFileServiceContext context,
                              BRFileService fs,
                              const void* entity,
                              uint32_t *bytesCount) {
    const BRPeer *peer = entity;

    // long term, this is wrong
    *bytesCount = sizeof (BRPeer);
    uint8_t *bytes = malloc (*bytesCount);
    memcpy (bytes, peer, *bytesCount);

    return bytes;
}

static void *
fileServiceTypePeerV1Reader (BRFileServiceContext context,
                              BRFileService fs,
                              uint8_t *bytes,
                              uint32_t bytesCount) {
    assert (bytesCount == sizeof (BRPeer));

    BRPeer *peer = malloc (bytesCount);;
    memcpy (peer, bytes, bytesCount);

    return peer;
}

static BRArrayOf(BRPeer)
initialPeersLoad (BRWalletManager manager) {
    /// Load peers for the wallet manager.
    BRSetOf(BRPeer*) peerSet = BRSetNew(BRPeerHash, BRPeerEq, 100);
    if (1 != fileServiceLoad (manager->fileService, peerSet, fileServiceTypePeers, 1)) {
        BRSetFree(peerSet);
        return NULL;
    }

    size_t peersCount = BRSetCount(peerSet);
    BRPeer *peersRefs[peersCount];

    BRSetAll(peerSet, (void**) peersRefs, peersCount);
    BRSetClear(peerSet);
    BRSetFree(peerSet);

    BRArrayOf(BRPeer) peers;
    array_new (peers, peersCount);

    for (size_t index = 0; index < peersCount; index++)
        array_add (peers, *peersRefs[index]);

    return peers;
}

static void
bwmFileServiceErrorHandler (BRFileServiceContext context,
                            BRFileService fs,
                            BRFileServiceError error) {
    BRWalletManager bwm = (BRWalletManager) context;

    switch (error.type) {
        case FILE_SERVICE_IMPL:
            // This actually a FATAL - an unresolvable coding error.
            _peer_log ("bread: FileService Error: IMPL: %s", error.u.impl.reason);
            break;
        case FILE_SERVICE_UNIX:
            _peer_log ("bread: FileService Error: UNIX: %s", strerror(error.u.unixerror.error));
            break;
        case FILE_SERVICE_ENTITY:
            // This is likely a coding error too.
            _peer_log ("bread: FileService Error: ENTITY (%s); %s",
                     error.u.entity.type,
                     error.u.entity.reason);
            break;
    }
    _peer_log ("bread: FileService Error: FORCED SYNC%s", "");

    if (NULL != bwm->peerManager)
        BRPeerManagerRescan (bwm->peerManager);
}

/// MARK: - Wallet Manager

static BRWalletManager
bwmCreateErrorHandler (BRWalletManager bwm, int fileService, const char* reason) {
    if (NULL != bwm) free (bwm);
    if (fileService)
        _peer_log ("bread: on ewmCreate: FileService Error: %s", reason);
    else
        _peer_log ("bread: on ewmCreate: Error: %s", reason);

    return NULL;
}

extern BRWalletManager
BRWalletManagerNew (BRWalletManagerClient client,
                    BRMasterPubKey mpk,
                    const BRChainParams *params,
                    uint32_t earliestKeyTime,
                    const char *baseStoragePath) {
    BRWalletManager manager = malloc (sizeof (struct BRWalletManagerStruct));
    if (NULL == manager) return bwmCreateErrorHandler (NULL, 0, "allocate");

//    manager->walletForkId = fork;
    manager->client = client;

    BRWalletForkId fork = getForkId (params);
    const char *networkName  = getNetworkName  (params);
    const char *currencyName = getCurrencyName (params);

    //
    // Create the File Service w/ associated types.
    //
    manager->fileService = fileServiceCreate (baseStoragePath, currencyName, networkName, 
                                              manager,
                                              bwmFileServiceErrorHandler);
    if (NULL == manager->fileService) return bwmCreateErrorHandler (manager, 1, "create");

    /// Transaction
    if (1 != fileServiceDefineType (manager->fileService, fileServiceTypeTransactions, WALLET_MANAGER_TRANSACTION_VERSION_1,
                                    (BRFileServiceContext) manager,
                                    fileServiceTypeTransactionV1Identifier,
                                    fileServiceTypeTransactionV1Reader,
                                    fileServiceTypeTransactionV1Writer) ||
        1 != fileServiceDefineCurrentVersion (manager->fileService, fileServiceTypeTransactions,
                                              WALLET_MANAGER_TRANSACTION_VERSION_1))
        return bwmCreateErrorHandler (manager, 1, fileServiceTypeTransactions);

    /// Block
    if (1 != fileServiceDefineType (manager->fileService, fileServiceTypeBlocks, WALLET_MANAGER_BLOCK_VERSION_1,
                                    (BRFileServiceContext) manager,
                                    fileServiceTypeBlockV1Identifier,
                                    fileServiceTypeBlockV1Reader,
                                    fileServiceTypeBlockV1Writer) ||
        1 != fileServiceDefineCurrentVersion (manager->fileService, fileServiceTypeBlocks,
                                              WALLET_MANAGER_BLOCK_VERSION_1))
        return bwmCreateErrorHandler (manager, 1, fileServiceTypeBlocks);

    /// Peer
    if (1 != fileServiceDefineType (manager->fileService, fileServiceTypePeers, WALLET_MANAGER_PEER_VERSION_1,
                                    (BRFileServiceContext) manager,
                                    fileServiceTypePeerV1Identifier,
                                    fileServiceTypePeerV1Reader,
                                    fileServiceTypePeerV1Writer) ||
        1 != fileServiceDefineCurrentVersion (manager->fileService, fileServiceTypePeers,
                                              WALLET_MANAGER_PEER_VERSION_1))
        return bwmCreateErrorHandler (manager, 1, fileServiceTypePeers);

    /// Load transactions for the wallet manager.
    BRArrayOf(BRTransaction*) transactions = initialTransactionsLoad(manager);
    /// Load blocks and peers for the peer manager.
    BRArrayOf(BRMerkleBlock*) blocks = initialBlocksLoad(manager);
    BRArrayOf(BRPeer) peers = initialPeersLoad(manager);

    // If any of these are NULL, then there was a failure; on a failure they all need to be cleared
    // which will cause a *FULL SYNC*
    if (NULL == transactions || NULL == blocks || NULL == peers) {
        if (NULL == transactions) array_new (transactions, 1);
        else array_clear(transactions);

        if (NULL == blocks) array_new (blocks, 1);
        else array_clear(blocks);

        if (NULL == peers) array_new (peers, 1);
        else array_clear(peers);
    }

    manager->wallet = BRWalletNew (transactions, array_count(transactions), mpk, fork);
    BRWalletSetCallbacks (manager->wallet, manager,
                          _BRWalletManagerBalanceChanged,
                          _BRWalletManagerTxAdded,
                          _BRWalletManagerTxUpdated,
                          _BRWalletManagerTxDeleted);
    

    client.funcWalletEvent (manager,
                            manager->wallet,
                            (BRWalletEvent) {
                                BITCOIN_WALLET_CREATED
                            });

    manager->peerManager = BRPeerManagerNew (params, manager->wallet, earliestKeyTime,
                                             blocks, array_count(blocks),
                                             peers,  array_count(peers));

    BRPeerManagerSetCallbacks (manager->peerManager, manager,
                               _BRWalletManagerSyncStarted,
                               _BRWalletManagerSyncStopped,
                               _BRWalletManagerTxStatusUpdate,
                               _BRWalletManagerSaveBlocks,
                               _BRWalletManagerSavePeers,
                               _BRWalletManagerNetworkIsReachabele,
                               _BRWalletManagerThreadCleanup);

    array_free(transactions); array_free(blocks); array_free(peers);

    return manager;
}

extern void
BRWalletManagerFree (BRWalletManager manager) {
    fileServiceRelease(manager->fileService);
    BRPeerManagerFree(manager->peerManager);
    BRWalletFree(manager->wallet);
    free (manager);
}

extern BRWallet *
BRWalletManagerGetWallet (BRWalletManager manager) {
    return manager->wallet;
}

extern BRPeerManager *
BRWalletManagerGetPeerManager (BRWalletManager manager) {
    return manager->peerManager;
}

extern void
BRWalletManagerConnect (BRWalletManager manager) {
    BRPeerManagerConnect(manager->peerManager);
    manager->client.funcWalletManagerEvent (manager,
                                            (BRWalletManagerEvent) {
                                                BITCOIN_WALLET_MANAGER_CONNECTED
                                            });
}

extern void
BRWalletManagerDisconnect (BRWalletManager manager) {
    BRPeerManagerDisconnect(manager->peerManager);
    manager->client.funcWalletManagerEvent (manager,
                                            (BRWalletManagerEvent) {
                                                BITCOIN_WALLET_MANAGER_DISCONNECTED
                                            });
}

/// MARK: - Wallet Callbacks

static void
_BRWalletManagerBalanceChanged (void *info, uint64_t balanceInSatoshi) {
    BRWalletManager manager = (BRWalletManager) info;
    manager->client.funcWalletEvent (manager,
                                     manager->wallet,
                                     (BRWalletEvent) {
                                         BITCOIN_WALLET_BALANCE_UPDATED,
                                         { .balance = { balanceInSatoshi }}
                                     });
}

static void
_BRWalletManagerTxAdded   (void *info, BRTransaction *tx) {
    BRWalletManager manager = (BRWalletManager) info;
    fileServiceSave(manager->fileService, fileServiceTypeTransactions, tx);
    manager->client.funcTransactionEvent (manager,
                                          manager->wallet,
                                          tx,
                                          (BRTransactionEvent) {
                                              BITCOIN_TRANSACTION_ADDED
                                          });
}

static void
_BRWalletManagerTxUpdated (void *info, const UInt256 *hashes, size_t count, uint32_t blockHeight, uint32_t timestamp) {
    BRWalletManager manager = (BRWalletManager) info;

    for (size_t index = 0; index < count; index++) {
        UInt256 hash = hashes[index];
        BRTransaction *transaction = BRWalletTransactionForHash(manager->wallet, hash);

        // assert timestamp and blockHeight in transaction
        fileServiceSave (manager->fileService, fileServiceTypeTransactions, transaction);

        manager->client.funcTransactionEvent (manager,
                                              manager->wallet,
                                              transaction,
                                              (BRTransactionEvent) {
                                                  BITCOIN_TRANSACTION_UPDATED,
                                                  { .updated = { blockHeight, timestamp }}
                                              });
    }
}

static void
_BRWalletManagerTxDeleted (void *info, UInt256 hash, int notifyUser, int recommendRescan) {
    BRWalletManager manager = (BRWalletManager) info;
    fileServiceRemove(manager->fileService, fileServiceTypeTransactions, hash);

    BRTransaction *transaction = BRWalletTransactionForHash(manager->wallet, hash);
    manager->client.funcTransactionEvent (manager,
                                          manager->wallet,
                                          transaction,
                                          (BRTransactionEvent) {
                                              BITCOIN_TRANSACTION_DELETED
                                          });
}

/// MARK: - Peer Manager Callbacks

static void
_BRWalletManagerSaveBlocks (void *info, int replace, BRMerkleBlock **blocks, size_t count) {
    BRWalletManager manager = (BRWalletManager) info;

    if (replace) fileServiceClear(manager->fileService, fileServiceTypeBlocks);
    for (size_t index = 0; index < count; index++)
        fileServiceSave (manager->fileService, fileServiceTypeBlocks, blocks[index]);
}

static void
_BRWalletManagerSavePeers  (void *info, int replace, const BRPeer *peers, size_t count) {
    BRWalletManager manager = (BRWalletManager) info;

    if (replace) fileServiceClear(manager->fileService, fileServiceTypePeers);
    for (size_t index = 0; index < count; index++)
        fileServiceSave (manager->fileService, fileServiceTypePeers, &peers[index]);
}

static void
_BRWalletManagerSyncStarted (void *info) {
    BRWalletManager manager = (BRWalletManager) info;
    manager->client.funcWalletManagerEvent (manager,
                                            (BRWalletManagerEvent) {
                                                BITCOIN_WALLET_MANAGER_SYNC_STARTED
                                            });
}

static void
_BRWalletManagerSyncStopped (void *info, int reason) {
    BRWalletManager manager = (BRWalletManager) info;
    manager->client.funcWalletManagerEvent (manager,
                                            (BRWalletManagerEvent) {
                                                BITCOIN_WALLET_MANAGER_SYNC_STOPPED,
                                                { .syncStopped = { reason }}
                                            });
}

static void
_BRWalletManagerTxStatusUpdate (void *info) {
//    BRWalletManager manager = (BRWalletManager) info;

    // event

}

static int
_BRWalletManagerNetworkIsReachabele (void *info) {
//    BRWalletManager manager = (BRWalletManager) info;

    // event
   return 1;
}

static void
_BRWalletManagerThreadCleanup (void *info) {
//    BRWalletManager manager = (BRWalletManager) info;

    // event
}

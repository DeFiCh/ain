//
//  BRWalletManager.h
//  BRCore
//
//  Created by Ed Gamble on 11/21/18.
//  Copyright (c) 2018 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef BRWalletManager_h
#define BRWalletManager_h

#include <stdio.h>
#include "BRBIP32Sequence.h"        // BRMasterPubKey
#include "BRChainParams.h"          // BRChainParams (*NOT THE STATIC DECLARATIONS*)

#include "BRTransaction.h"
#include "BRWallet.h"
#include "BRPeerManager.h"          // Unneeded, if we shadow some functions (connect,disconnect,scan)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BRWalletManagerStruct *BRWalletManager;

// Likely unneeded.
typedef enum {
    WALLET_FORKID_BITCOIN = 0x00,
    WALLET_FORKID_BITCASH = 0x40,
    WALLET_FORKID_BITGOLD = 0x4f
} BRWalletForkId;

///
/// Transaction Event
///
typedef enum {
    BITCOIN_TRANSACTION_ADDED,
    BITCOIN_TRANSACTION_UPDATED,
    BITCOIN_TRANSACTION_DELETED,
} BRTransactionEventType;

typedef struct {
    BRTransactionEventType type;
    union {
        struct {
            uint32_t blockHeight;
            uint32_t timestamp;
        } updated;
    } u;
} BRTransactionEvent;

typedef void
(*BRTransactionEventCallback) (BRWalletManager manager,
                               BRWallet *wallet,
                               BRTransaction *transaction,
                               BRTransactionEvent event);

///
/// Wallet Event
///
typedef enum {
    BITCOIN_WALLET_CREATED,
    BITCOIN_WALLET_BALANCE_UPDATED,
    BITCOIN_WALLET_DELETED
} BRWalletEventType;

typedef struct {
    BRWalletEventType type;
    union {
        struct {
            uint64_t satoshi;
        } balance;
    } u;
} BRWalletEvent;

typedef void
(*BRWalletEventCallback) (BRWalletManager manager,
                          BRWallet *wallet,
                          BRWalletEvent event);

///
/// WalletManager Event
///
typedef enum {
    BITCOIN_WALLET_MANAGER_CONNECTED,
    BITCOIN_WALLET_MANAGER_DISCONNECTED,
    BITCOIN_WALLET_MANAGER_SYNC_STARTED,
    BITCOIN_WALLET_MANAGER_SYNC_STOPPED
} BRWalletManagerEventType;

typedef struct {
    BRWalletManagerEventType type;
    union {
        struct {
            int error;
        } syncStopped;
    } u;
} BRWalletManagerEvent;

typedef void
(*BRWalletManagerEventCallback) (BRWalletManager manager,
                                 BRWalletManagerEvent event);

typedef struct {
    BRTransactionEventCallback funcTransactionEvent;
    BRWalletEventCallback  funcWalletEvent;
    BRWalletManagerEventCallback funcWalletManagerEvent;
} BRWalletManagerClient;

extern BRWalletManager
BRWalletManagerNew (BRWalletManagerClient client,
                    BRMasterPubKey mpk,
                    const BRChainParams *params,
                    uint32_t earliestKeyTime,
                    const char *storagePath);

extern void
BRWalletManagerFree (BRWalletManager manager);

extern void
BRWalletManagerConnect (BRWalletManager manager);

extern void
BRWalletManagerDisconnect (BRWalletManager manager);

//
// These should not be needed if the events are sufficient
//
extern BRWallet *
BRWalletManagerGetWallet (BRWalletManager manager);

extern BRPeerManager *
BRWalletManagerGetPeerManager (BRWalletManager manager);

#ifdef __cplusplus
}
#endif


#endif /* BRWalletManager_h */

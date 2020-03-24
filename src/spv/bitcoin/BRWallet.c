//
//  BRWallet.c
//
//  Created by Aaron Voisine on 9/1/15.
//  Copyright (c) 2015 breadwallet LLC
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

#include "BRWallet.h"
#include "BRSet.h"
#include "BRAddress.h"
#include "BRArray.h"
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <float.h>
#include <pthread.h>
#include <assert.h>

inline static size_t _pkhHash(const void *pkh)
{
    return (size_t)UInt32GetLE(pkh);
}

inline static int _pkhEq(const void *pkh, const void *otherPkh)
{
    return UInt160Eq(UInt160Get(pkh), UInt160Get(otherPkh));
}

inline static uint64_t _txFee(uint64_t feePerKb, size_t size)
{
    uint64_t standardFee = size*TX_FEE_PER_KB/1000,       // standard fee based on tx size
             fee = (((size*feePerKb/1000) + 99)/100)*100; // fee using feePerKb, rounded up to nearest 100 satoshi
    
    return (fee > standardFee) ? fee : standardFee;
}

// chain position of first tx output address that appears in chain
inline static size_t _txChainIndex(const BRTransaction *tx, const UInt160 *chain)
{
    const uint8_t *pkh;
    
    for (size_t i = array_count(chain); i > 0; i--) {
        for (size_t j = 0; j < tx->outCount; j++) {
            pkh = BRScriptPKH(tx->outputs[j].script, tx->outputs[j].scriptLen);
            if (pkh && _pkhEq(pkh, &chain[i - 1])) return i - 1;
        }
    }
    
    return -1;
}

struct BRWalletStruct {
    uint64_t balance, totalSent, totalReceived, feePerKb, *balanceHist;
    uint32_t blockHeight;
    BRUTXO *utxos;
    BRTransaction **transactions;
    BRMasterPubKey masterPubKey;
    int forkId;
    UInt160 *internalChain, *externalChain;
    BRSet *allTx, *invalidTx, *pendingTx, *spentOutputs, *usedPKH, *allPKH;
    void *callbackInfo;
    void (*balanceChanged)(void *info, uint64_t balance);
    void (*txAdded)(void *info, BRTransaction *tx);
    void (*txUpdated)(void *info, const UInt256 txHashes[], size_t txCount, uint32_t blockHeight, uint32_t timestamp);
    void (*txDeleted)(void *info, UInt256 txHash, int notifyUser, int recommendRescan);
    pthread_mutex_t lock;
};

inline static void _BRWalletAddressFromHash160(BRWallet *wallet, char *addr, size_t addrLen, UInt160 h)
{
    if (wallet->forkId != 0) {
        const uint8_t script[] = { OP_DUP, OP_HASH160, 20, h.u8[0], h.u8[1], h.u8[2], h.u8[3], h.u8[4], h.u8[5],
                                   h.u8[6], h.u8[7], h.u8[8], h.u8[9], h.u8[10], h.u8[11], h.u8[12], h.u8[13], h.u8[14],
                                   h.u8[15], h.u8[16], h.u8[17], h.u8[18], h.u8[19], OP_EQUALVERIFY, OP_CHECKSIG };
        
        BRAddressFromScriptPubKey(addr, addrLen, script, sizeof(script));
    }
    else BRAddressFromHash160(addr, addrLen, &h);
}

inline static int _BRWalletTxIsAscending(BRWallet *wallet, const BRTransaction *tx1, const BRTransaction *tx2)
{
    if (! tx1 || ! tx2) return 0;
    if (tx1->blockHeight > tx2->blockHeight) return 1;
    if (tx1->blockHeight < tx2->blockHeight) return 0;
    
    for (size_t i = 0; i < tx1->inCount; i++) {
        if (UInt256Eq(tx1->inputs[i].txHash, tx2->txHash)) return 1;
    }
    
    for (size_t i = 0; i < tx2->inCount; i++) {
        if (UInt256Eq(tx2->inputs[i].txHash, tx1->txHash)) return 0;
    }

    for (size_t i = 0; i < tx1->inCount; i++) {
        if (_BRWalletTxIsAscending(wallet, BRSetGet(wallet->allTx, &(tx1->inputs[i].txHash)), tx2)) return 1;
    }

    return 0;
}

inline static int _BRWalletTxCompare(BRWallet *wallet, const BRTransaction *tx1, const BRTransaction *tx2)
{
    size_t i = -1, j = -1;

    if (_BRWalletTxIsAscending(wallet, tx1, tx2)) return 1;
    if (_BRWalletTxIsAscending(wallet, tx2, tx1)) return -1;
    if ((i = _txChainIndex(tx1, wallet->internalChain)) != -1) j = _txChainIndex(tx2, wallet->internalChain);
    if (j == -1 && (i = _txChainIndex(tx1, wallet->externalChain)) != -1) j = _txChainIndex(tx2, wallet->externalChain);
    if (i != -1 && j != -1 && i != j) return (i > j) ? 1 : -1;
    return 0;
}

// inserts tx into wallet->transactions, keeping wallet->transactions sorted by date, oldest first (insertion sort)
inline static void _BRWalletInsertTx(BRWallet *wallet, BRTransaction *tx)
{
    size_t i = array_count(wallet->transactions);
    
    array_set_count(wallet->transactions, i + 1);
    
    while (i > 0 && _BRWalletTxCompare(wallet, wallet->transactions[i - 1], tx) > 0) {
        wallet->transactions[i] = wallet->transactions[i - 1];
        i--;
    }
    
    wallet->transactions[i] = tx;
}

// non-threadsafe version of BRWalletContainsTransaction()
static int _BRWalletContainsTx(BRWallet *wallet, const BRTransaction *tx)
{
    int r = 0;
    const uint8_t *pkh;
    
    for (size_t i = 0; ! r && i < tx->outCount; i++) {
        pkh = BRScriptPKH(tx->outputs[i].script, tx->outputs[i].scriptLen);
        if (pkh && BRSetContains(wallet->allPKH, pkh)) r = 1;
    }
    
    for (size_t i = 0; ! r && i < tx->inCount; i++) {
        BRTransaction *t = BRSetGet(wallet->allTx, &tx->inputs[i].txHash);
        uint32_t n = tx->inputs[i].index;
        
        pkh = (t && n < t->outCount) ? BRScriptPKH(t->outputs[n].script, t->outputs[n].scriptLen) : NULL;
        if (pkh && BRSetContains(wallet->allPKH, pkh)) r = 1;
    }
    
    return r;
}

static void _BRWalletUpdateBalance(BRWallet *wallet)
{
    int isInvalid, isPending;
    uint64_t balance = 0, prevBalance = 0;
    time_t now = time(NULL);
    size_t i, j;
    BRTransaction *tx, *t;
    const uint8_t *pkh;
    
    array_clear(wallet->utxos);
    array_clear(wallet->balanceHist);
    BRSetClear(wallet->spentOutputs);
    BRSetClear(wallet->invalidTx);
    BRSetClear(wallet->pendingTx);
    BRSetClear(wallet->usedPKH);
    wallet->totalSent = 0;
    wallet->totalReceived = 0;

    for (i = 0; i < array_count(wallet->transactions); i++) {
        tx = wallet->transactions[i];

        // check if any inputs are invalid or already spent
        if (tx->blockHeight == TX_UNCONFIRMED) {
            for (j = 0, isInvalid = 0; ! isInvalid && j < tx->inCount; j++) {
                if (BRSetContains(wallet->spentOutputs, &tx->inputs[j]) ||
                    BRSetContains(wallet->invalidTx, &tx->inputs[j].txHash)) isInvalid = 1;
            }
        
            if (isInvalid) {
                BRSetAdd(wallet->invalidTx, tx);
                array_add(wallet->balanceHist, balance);
                continue;
            }
        }

        // add inputs to spent output set
        for (j = 0; j < tx->inCount; j++) {
            BRSetAdd(wallet->spentOutputs, &tx->inputs[j]);
        }

        // check if tx is pending
        if (tx->blockHeight == TX_UNCONFIRMED) {
            isPending = (BRTransactionVSize(tx) > TX_MAX_SIZE) ? 1 : 0; // check tx size is under TX_MAX_SIZE
            
            for (j = 0; ! isPending && j < tx->outCount; j++) {
                if (tx->outputs[j].amount < TX_MIN_OUTPUT_AMOUNT) isPending = 1; // check that no outputs are dust
            }

            for (j = 0; ! isPending && j < tx->inCount; j++) {
                if (tx->inputs[j].sequence < UINT32_MAX - 1) isPending = 1; // check for replace-by-fee
                if (tx->inputs[j].sequence < UINT32_MAX && tx->lockTime < TX_MAX_LOCK_HEIGHT &&
                    tx->lockTime > wallet->blockHeight + 1) isPending = 1; // future lockTime
                if (tx->inputs[j].sequence < UINT32_MAX && tx->lockTime > now) isPending = 1; // future lockTime
                if (BRSetContains(wallet->pendingTx, &tx->inputs[j].txHash)) isPending = 1; // check for pending inputs
                // TODO: XXX handle BIP68 check lock time verify rules
            }
            
            if (isPending) {
                BRSetAdd(wallet->pendingTx, tx);
                array_add(wallet->balanceHist, balance);
                continue;
            }
        }

        // add outputs to UTXO set
        // TODO: don't add outputs below TX_MIN_OUTPUT_AMOUNT
        // TODO: don't add coin generation outputs < 100 blocks deep
        // NOTE: balance/UTXOs will then need to be recalculated when last block changes
        for (j = 0; j < tx->outCount; j++) {
            if (tx->outputs[j].address[0] != '\0') {
                pkh = BRScriptPKH(tx->outputs[j].script, tx->outputs[j].scriptLen);

                if (pkh && BRSetContains(wallet->allPKH, pkh)) {
                    BRSetAdd(wallet->usedPKH, (void *)pkh);
                    array_add(wallet->utxos, ((const BRUTXO) { tx->txHash, (uint32_t)j }));
                    balance += tx->outputs[j].amount;
                }
            }
        }

        // transaction ordering is not guaranteed, so check the entire UTXO set against the entire spent output set
        for (j = array_count(wallet->utxos); j > 0; j--) {
            if (! BRSetContains(wallet->spentOutputs, &wallet->utxos[j - 1])) continue;
            t = BRSetGet(wallet->allTx, &wallet->utxos[j - 1].hash);
            balance -= t->outputs[wallet->utxos[j - 1].n].amount;
            array_rm(wallet->utxos, j - 1);
        }
        
        if (prevBalance < balance) wallet->totalReceived += balance - prevBalance;
        if (balance < prevBalance) wallet->totalSent += prevBalance - balance;
        array_add(wallet->balanceHist, balance);
        prevBalance = balance;
    }

    assert(array_count(wallet->balanceHist) == array_count(wallet->transactions));
    wallet->balance = balance;
}

// allocates and populates a BRWallet struct which must be freed by calling BRWalletFree()
// forkId is 0 for bitcoin, 0x40 for b-cash
BRWallet *BRWalletNew(BRTransaction *transactions[], size_t txCount, BRMasterPubKey mpk, int forkId)
{
    BRWallet *wallet = NULL;
    BRTransaction *tx;
    const uint8_t *pkh;

    assert(transactions != NULL || txCount == 0);
    wallet = calloc(1, sizeof(*wallet));
    assert(wallet != NULL);
    array_new(wallet->utxos, 100);
    array_new(wallet->transactions, txCount + 100);
    wallet->feePerKb = DEFAULT_FEE_PER_KB;
    wallet->masterPubKey = mpk;
    wallet->forkId = forkId;
    array_new(wallet->internalChain, 100);
    array_new(wallet->externalChain, 100);
    array_new(wallet->balanceHist, txCount + 100);
    wallet->allTx = BRSetNew(BRTransactionHash, BRTransactionEq, txCount + 100);
    wallet->invalidTx = BRSetNew(BRTransactionHash, BRTransactionEq, 10);
    wallet->pendingTx = BRSetNew(BRTransactionHash, BRTransactionEq, 10);
    wallet->spentOutputs = BRSetNew(BRUTXOHash, BRUTXOEq, txCount + 100);
    wallet->usedPKH = BRSetNew(_pkhHash, _pkhEq, txCount + 100);
    wallet->allPKH = BRSetNew(_pkhHash, _pkhEq, txCount + 100);
    pthread_mutex_init(&wallet->lock, NULL);

    for (size_t i = 0; transactions && i < txCount; i++) {
        tx = transactions[i];
        if (! BRTransactionIsSigned(tx) || BRSetContains(wallet->allTx, tx)) continue;
        BRSetAdd(wallet->allTx, tx);
        _BRWalletInsertTx(wallet, tx);

        for (size_t j = 0; j < tx->outCount; j++) {
            pkh = BRScriptPKH(tx->outputs[j].script, tx->outputs[j].scriptLen);
            if (pkh) BRSetAdd(wallet->usedPKH, (void *)pkh);
        }
    }
    
    BRWalletUnusedAddrs(wallet, NULL, SEQUENCE_GAP_LIMIT_EXTERNAL, SEQUENCE_EXTERNAL_CHAIN);
    BRWalletUnusedAddrs(wallet, NULL, SEQUENCE_GAP_LIMIT_INTERNAL, SEQUENCE_INTERNAL_CHAIN);

    _BRWalletUpdateBalance(wallet);

    if (txCount > 0 && ! _BRWalletContainsTx(wallet, transactions[0])) { // verify transactions match master pubKey
        BRWalletFree(wallet);
        wallet = NULL;
    }
    
    return wallet;
}

// not thread-safe, set callbacks once after BRWalletNew(), before calling other BRWallet functions
// info is a void pointer that will be passed along with each callback call
// void balanceChanged(void *, uint64_t) - called when the wallet balance changes
// void txAdded(void *, BRTransaction *) - called when transaction is added to the wallet
// void txUpdated(void *, const UInt256[], size_t, uint32_t, uint32_t)
//   - called when the blockHeight or timestamp of previously added transactions are updated
// void txDeleted(void *, UInt256) - called when a previously added transaction is removed from the wallet
// NOTE: if a transaction is deleted, and BRWalletAmountSentByTx() is greater than 0, recommend the user do a rescan
void BRWalletSetCallbacks(BRWallet *wallet, void *info,
                          void (*balanceChanged)(void *info, uint64_t balance),
                          void (*txAdded)(void *info, BRTransaction *tx),
                          void (*txUpdated)(void *info, const UInt256 txHashes[], size_t txCount, uint32_t blockHeight,
                                            uint32_t timestamp),
                          void (*txDeleted)(void *info, UInt256 txHash, int notifyUser, int recommendRescan))
{
    assert(wallet != NULL);
    wallet->callbackInfo = info;
    wallet->balanceChanged = balanceChanged;
    wallet->txAdded = txAdded;
    wallet->txUpdated = txUpdated;
    wallet->txDeleted = txDeleted;
}

// wallets are composed of chains of addresses
// each chain is traversed until a gap of a number of addresses is found that haven't been used in any transactions
// this function writes to addrs an array of <gapLimit> unused addresses following the last used address in the chain
// the internal chain is used for change addresses and the external chain for receive addresses
// addrs may be NULL to only generate addresses for BRWalletContainsAddress()
// returns the number addresses written to addrs
size_t BRWalletUnusedAddrs(BRWallet *wallet, BRAddress addrs[], uint32_t gapLimit, uint32_t internal)
{
    UInt160 *chain = NULL, *origChain;
    size_t i, j = 0, count, startCount;

    assert(wallet != NULL);
    assert(gapLimit > 0);
    pthread_mutex_lock(&wallet->lock);
    if (internal == SEQUENCE_EXTERNAL_CHAIN) chain = wallet->externalChain;
    if (internal == SEQUENCE_INTERNAL_CHAIN) chain = wallet->internalChain;
    assert(chain != NULL);
    origChain = chain;
    i = count = startCount = array_count(chain);
    
    // keep only the trailing contiguous block of addresses with no transactions
    while (i > 0 && ! BRSetContains(wallet->usedPKH, &chain[i - 1])) i--;
    
    while (i + gapLimit > count) { // generate new addresses up to gapLimit
        BRKey key;
        uint8_t pubKey[BRBIP32PubKey(NULL, 0, wallet->masterPubKey, internal, count)];
        size_t len = BRBIP32PubKey(pubKey, sizeof(pubKey), wallet->masterPubKey, internal, (uint32_t)count);
        
        if (! BRKeySetPubKey(&key, pubKey, len)) break;
        array_add(chain, BRKeyHash160(&key));
        count++;
        if (BRSetContains(wallet->usedPKH, &chain[array_count(chain) - 1])) i = count;
    }

    if (addrs && i + gapLimit <= count) {
        for (j = 0; j < gapLimit; j++) {
            _BRWalletAddressFromHash160(wallet, addrs[j].s, sizeof(*addrs), chain[i + j]);
        }
    }
    
    // was chain moved to a new memory location?
    if (chain == origChain) {
        for (i = startCount; i < count; i++) {
            BRSetAdd(wallet->allPKH, &chain[i]);
        }
    }
    else {
        if (internal == SEQUENCE_EXTERNAL_CHAIN) wallet->externalChain = chain;
        if (internal == SEQUENCE_INTERNAL_CHAIN) wallet->internalChain = chain;

        BRSetClear(wallet->allPKH); // clear and rebuild allAddrs

        for (i = array_count(wallet->internalChain); i > 0; i--) {
            BRSetAdd(wallet->allPKH, &wallet->internalChain[i - 1]);
        }
        
        for (i = array_count(wallet->externalChain); i > 0; i--) {
            BRSetAdd(wallet->allPKH, &wallet->externalChain[i - 1]);
        }
    }

    pthread_mutex_unlock(&wallet->lock);
    return j;
}

// current wallet balance, not including transactions known to be invalid
uint64_t BRWalletBalance(BRWallet *wallet)
{
    uint64_t balance;

    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    balance = wallet->balance;
    pthread_mutex_unlock(&wallet->lock);
    return balance;
}

// writes unspent outputs to utxos and returns the number of outputs written, or total number available if utxos is NULL
size_t BRWalletUTXOs(BRWallet *wallet, BRUTXO *utxos, size_t utxosCount)
{
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    if (! utxos || array_count(wallet->utxos) < utxosCount) utxosCount = array_count(wallet->utxos);

    for (size_t i = 0; utxos && i < utxosCount; i++) {
        utxos[i] = wallet->utxos[i];
    }

    pthread_mutex_unlock(&wallet->lock);
    return utxosCount;
}

// writes transactions registered in the wallet, sorted by date, oldest first, to the given transactions array
// returns the number of transactions written, or total number available if transactions is NULL
size_t BRWalletTransactions(BRWallet *wallet, BRTransaction *transactions[], size_t txCount)
{
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    if (! transactions || array_count(wallet->transactions) < txCount) txCount = array_count(wallet->transactions);

    for (size_t i = 0; transactions && i < txCount; i++) {
        transactions[i] = wallet->transactions[i];
    }
    
    pthread_mutex_unlock(&wallet->lock);
    return txCount;
}

// writes transactions registered in the wallet, and that were unconfirmed before blockHeight, to the transactions array
// returns the number of transactions written, or total number available if transactions is NULL
size_t BRWalletTxUnconfirmedBefore(BRWallet *wallet, BRTransaction *transactions[], size_t txCount,
                                   uint32_t blockHeight)
{
    size_t total, n = 0;

    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    total = array_count(wallet->transactions);
    while (n < total && wallet->transactions[(total - n) - 1]->blockHeight >= blockHeight) n++;
    if (! transactions || n < txCount) txCount = n;

    for (size_t i = 0; transactions && i < txCount; i++) {
        transactions[i] = wallet->transactions[(total - n) + i];
    }

    pthread_mutex_unlock(&wallet->lock);
    return txCount;
}

// total amount spent from the wallet (exluding change)
uint64_t BRWalletTotalSent(BRWallet *wallet)
{
    uint64_t totalSent;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    totalSent = wallet->totalSent;
    pthread_mutex_unlock(&wallet->lock);
    return totalSent;
}

// total amount received by the wallet (exluding change)
uint64_t BRWalletTotalReceived(BRWallet *wallet)
{
    uint64_t totalReceived;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    totalReceived = wallet->totalReceived;
    pthread_mutex_unlock(&wallet->lock);
    return totalReceived;
}

// fee-per-kb of transaction size to use when creating a transaction
uint64_t BRWalletFeePerKb(BRWallet *wallet)
{
    uint64_t feePerKb;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    feePerKb = wallet->feePerKb;
    pthread_mutex_unlock(&wallet->lock);
    return feePerKb;
}

void BRWalletSetFeePerKb(BRWallet *wallet, uint64_t feePerKb)
{
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    wallet->feePerKb = feePerKb;
    pthread_mutex_unlock(&wallet->lock);
}

// returns the first unused external address (bech32 pay-to-witness-pubkey-hash)
BRAddress BRWalletReceiveAddress(BRWallet *wallet)
{
    BRAddress addr = BR_ADDRESS_NONE;
    
    BRWalletUnusedAddrs(wallet, &addr, 1, 0);
    return addr;
}

// returns the first unused external address (legacy pay-to-pubkey-hash)
BRAddress BRWalletLegacyAddress(BRWallet *wallet)
{
    BRAddress addr = BR_ADDRESS_NONE;
    uint8_t script[] = { OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG };
    
    BRWalletUnusedAddrs(wallet, &addr, 1, 0);
    if (BRAddressHash160(&script[3], addr.s)) BRAddressFromScriptPubKey(addr.s, sizeof(addr), script, sizeof(script));
    return addr;
}

// writes all addresses previously genereated with BRWalletUnusedAddrs() to addrs
// returns the number addresses written, or total number available if addrs is NULL
size_t BRWalletAllAddrs(BRWallet *wallet, BRAddress addrs[], size_t addrsCount)
{
    size_t i, internalCount = 0, externalCount = 0;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    internalCount = (! addrs || array_count(wallet->internalChain) < addrsCount) ?
                    array_count(wallet->internalChain) : addrsCount;

    for (i = 0; addrs && i < internalCount; i++) {
        _BRWalletAddressFromHash160(wallet, addrs[i].s, sizeof(*addrs), wallet->internalChain[i]);
    }

    externalCount = (! addrs || array_count(wallet->externalChain) < addrsCount - internalCount) ?
                    array_count(wallet->externalChain) : addrsCount - internalCount;

    for (i = 0; addrs && i < externalCount; i++) {
        _BRWalletAddressFromHash160(wallet, addrs[internalCount + i].s, sizeof(*addrs), wallet->externalChain[i]);
    }

    pthread_mutex_unlock(&wallet->lock);
    return internalCount + externalCount;
}

// true if the address was previously generated by BRWalletUnusedAddrs() (even if it's now used)
int BRWalletContainsAddress(BRWallet *wallet, const char *addr)
{
    int r = 0;
    UInt160 pkh = UINT160_ZERO;
    
    assert(wallet != NULL);
    assert(addr != NULL);
    pthread_mutex_lock(&wallet->lock);
    if (addr) BRAddressHash160(&pkh, addr);
    r = BRSetContains(wallet->allPKH, &pkh);
    pthread_mutex_unlock(&wallet->lock);
    return r;
}

// true if the address was previously used as an output in any wallet transaction
int BRWalletAddressIsUsed(BRWallet *wallet, const char *addr)
{
    int r = 0;
    UInt160 pkh = UINT160_ZERO;
    
    assert(wallet != NULL);
    assert(addr != NULL);
    pthread_mutex_lock(&wallet->lock);
    if (addr) BRAddressHash160(&pkh, addr);
    r = BRSetContains(wallet->usedPKH, &pkh);
    pthread_mutex_unlock(&wallet->lock);
    return r;
}

// returns an unsigned transaction that sends the specified amount from the wallet to the given address
// result must be freed by calling BRTransactionFree()
BRTransaction *BRWalletCreateTransaction(BRWallet *wallet, uint64_t amount, const char *addr)
{
    BRTxOutput o = BR_TX_OUTPUT_NONE;
    
    assert(wallet != NULL);
    assert(amount > 0);
    assert(addr != NULL && BRAddressIsValid(addr));
    o.amount = amount;
    BRTxOutputSetAddress(&o, addr);
    return BRWalletCreateTxForOutputs(wallet, &o, 1);
}

// returns an unsigned transaction that satisifes the given transaction outputs
// result must be freed by calling BRTransactionFree()
BRTransaction *BRWalletCreateTxForOutputs(BRWallet *wallet, const BRTxOutput outputs[], size_t outCount)
{
    BRTransaction *tx, *transaction = BRTransactionNew();
    uint64_t feeAmount, amount = 0, balance = 0, minAmount;
    size_t i, j, cpfpSize = 0;
    BRUTXO *o;
    BRAddress addr = BR_ADDRESS_NONE;
    
    assert(wallet != NULL);
    assert(outputs != NULL && outCount > 0);

    for (i = 0; outputs && i < outCount; i++) {
        assert(outputs[i].script != NULL && outputs[i].scriptLen > 0);
        BRTransactionAddOutput(transaction, outputs[i].amount, outputs[i].script, outputs[i].scriptLen);
        amount += outputs[i].amount;
    }
    
    minAmount = BRWalletMinOutputAmount(wallet);
    pthread_mutex_lock(&wallet->lock);
    feeAmount = _txFee(wallet->feePerKb, BRTransactionVSize(transaction) + TX_OUTPUT_SIZE);
    
    // TODO: use up all UTXOs for all used addresses to avoid leaving funds in addresses whose public key is revealed
    // TODO: avoid combining addresses in a single transaction when possible to reduce information leakage
    // TODO: use up UTXOs received from any of the output scripts that this transaction sends funds to, to mitigate an
    //       attacker double spending and requesting a refund
    for (i = 0; i < array_count(wallet->utxos); i++) {
        o = &wallet->utxos[i];
        tx = BRSetGet(wallet->allTx, o);
        if (! tx || o->n >= tx->outCount) continue;
        BRTransactionAddInput(transaction, tx->txHash, o->n, tx->outputs[o->n].amount,
                              tx->outputs[o->n].script, tx->outputs[o->n].scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
        
        if (BRTransactionVSize(transaction) + TX_OUTPUT_SIZE > TX_MAX_SIZE) { // transaction size-in-bytes too large
            BRTransactionFree(transaction);
            transaction = NULL;
        
            // check for sufficient total funds before building a smaller transaction
            if (wallet->balance < amount + _txFee(wallet->feePerKb, 10 + array_count(wallet->utxos)*TX_INPUT_SIZE +
                                                  (outCount + 1)*TX_OUTPUT_SIZE + cpfpSize)) break;
            pthread_mutex_unlock(&wallet->lock);

            if (outputs[outCount - 1].amount > amount + feeAmount + minAmount - balance) {
                BRTxOutput newOutputs[outCount];
                
                for (j = 0; j < outCount; j++) {
                    newOutputs[j] = outputs[j];
                }
                
                newOutputs[outCount - 1].amount -= amount + feeAmount - balance; // reduce last output amount
                transaction = BRWalletCreateTxForOutputs(wallet, newOutputs, outCount);
            }
            else transaction = BRWalletCreateTxForOutputs(wallet, outputs, outCount - 1); // remove last output

            balance = amount = feeAmount = 0;
            pthread_mutex_lock(&wallet->lock);
            break;
        }
        
        balance += tx->outputs[o->n].amount;
        
//        // size of unconfirmed, non-change inputs for child-pays-for-parent fee
//        // don't include parent tx with more than 10 inputs or 10 outputs
//        if (tx->blockHeight == TX_UNCONFIRMED && tx->inCount <= 10 && tx->outCount <= 10 &&
//            ! _BRWalletTxIsSend(wallet, tx)) cpfpSize += BRTransactionVSize(tx);

        // fee amount after adding a change output
        feeAmount = _txFee(wallet->feePerKb, BRTransactionVSize(transaction) + TX_OUTPUT_SIZE + cpfpSize);

        // increase fee to round off remaining wallet balance to nearest 100 satoshi
        if (wallet->balance > amount + feeAmount) feeAmount += (wallet->balance - (amount + feeAmount)) % 100;
        
        if (balance == amount + feeAmount || balance >= amount + feeAmount + minAmount) break;
    }
    
    pthread_mutex_unlock(&wallet->lock);
    
    if (transaction && (outCount < 1 || balance < amount + feeAmount)) { // no outputs/insufficient funds
        BRTransactionFree(transaction);
        transaction = NULL;
    }
    else if (transaction && balance - (amount + feeAmount) > minAmount) { // add change output
        BRWalletUnusedAddrs(wallet, &addr, 1, 1);
        uint8_t script[BRAddressScriptPubKey(NULL, 0, addr.s)];
        size_t scriptLen = BRAddressScriptPubKey(script, sizeof(script), addr.s);
    
        BRTransactionAddOutput(transaction, balance - (amount + feeAmount), script, scriptLen);
        BRTransactionShuffleOutputs(transaction);
    }
    
    return transaction;
}

// signs any inputs in tx that can be signed using private keys from the wallet
// seed is the master private key (wallet seed) corresponding to the master public key given when the wallet was created
// returns true if all inputs were signed, or false if there was an error or not all inputs were able to be signed
int BRWalletSignTransaction(BRWallet *wallet, BRTransaction *tx, const void *seed, size_t seedLen)
{
    uint32_t j, internalIdx[tx->inCount], externalIdx[tx->inCount];
    size_t i, internalCount = 0, externalCount = 0;
    int forkId, r = 0;
    
    assert(wallet != NULL);
    assert(tx != NULL);
    pthread_mutex_lock(&wallet->lock);
    forkId = wallet->forkId;
    
    for (i = 0; tx && i < tx->inCount; i++) {
        const uint8_t *pkh = BRScriptPKH(tx->inputs[i].script, tx->inputs[i].scriptLen);
        
        for (j = (uint32_t)array_count(wallet->internalChain); pkh && j > 0; j--) {
            if (UInt160Eq(UInt160Get(pkh), wallet->internalChain[j - 1])) internalIdx[internalCount++] = j - 1;
        }

        for (j = (uint32_t)array_count(wallet->externalChain); pkh && j > 0; j--) {
            if (UInt160Eq(UInt160Get(pkh), wallet->externalChain[j - 1])) externalIdx[externalCount++] = j - 1;
        }
    }

    pthread_mutex_unlock(&wallet->lock);

    BRKey keys[internalCount + externalCount];

    if (seed) {
        BRBIP32PrivKeyList(keys, internalCount, seed, seedLen, SEQUENCE_INTERNAL_CHAIN, internalIdx);
        BRBIP32PrivKeyList(&keys[internalCount], externalCount, seed, seedLen, SEQUENCE_EXTERNAL_CHAIN, externalIdx);
        // TODO: XXX wipe seed callback
        seed = NULL;
        if (tx) r = BRTransactionSign(tx, forkId, keys, internalCount + externalCount);
        for (i = 0; i < internalCount + externalCount; i++) BRKeyClean(&keys[i]);
    }
    else r = -1; // user canceled authentication
    
    return r;
}

// true if the given transaction is associated with the wallet (even if it hasn't been registered)
int BRWalletContainsTransaction(BRWallet *wallet, const BRTransaction *tx)
{
    int r = 0;
    
    assert(wallet != NULL);
    assert(tx != NULL);
    pthread_mutex_lock(&wallet->lock);
    if (tx) r = _BRWalletContainsTx(wallet, tx);
    pthread_mutex_unlock(&wallet->lock);
    return r;
}

// adds a transaction to the wallet, or returns false if it isn't associated with the wallet
int BRWalletRegisterTransaction(BRWallet *wallet, BRTransaction *tx)
{
    int wasAdded = 0, r = 1;
    
    assert(wallet != NULL);
    assert(tx != NULL && BRTransactionIsSigned(tx));
    
    if (tx && BRTransactionIsSigned(tx)) {
        pthread_mutex_lock(&wallet->lock);

        if (! BRSetContains(wallet->allTx, tx)) {
            if (_BRWalletContainsTx(wallet, tx)) {
                // TODO: verify signatures when possible
                // TODO: handle tx replacement with input sequence numbers
                //       (for now, replacements appear invalid until confirmation)
                BRSetAdd(wallet->allTx, tx);
                _BRWalletInsertTx(wallet, tx);
                _BRWalletUpdateBalance(wallet);
                wasAdded = 1;
            }
            else { // keep track of unconfirmed non-wallet tx for invalid tx checks and child-pays-for-parent fees
                   // BUG: limit total non-wallet unconfirmed tx to avoid memory exhaustion attack
                if (tx->blockHeight == TX_UNCONFIRMED) BRSetAdd(wallet->allTx, tx);
                r = 0;
                // BUG: XXX memory leak if tx is not added to wallet->allTx, and we can't just free it
            }
        }
    
        pthread_mutex_unlock(&wallet->lock);
    }
    else r = 0;

    if (wasAdded) {
        // when a wallet address is used in a transaction, generate a new address to replace it
        BRWalletUnusedAddrs(wallet, NULL, SEQUENCE_GAP_LIMIT_EXTERNAL, 0);
        BRWalletUnusedAddrs(wallet, NULL, SEQUENCE_GAP_LIMIT_INTERNAL, 1);
        if (wallet->balanceChanged) wallet->balanceChanged(wallet->callbackInfo, wallet->balance);
        if (wallet->txAdded) wallet->txAdded(wallet->callbackInfo, tx);
    }

    return r;
}

// removes a tx from the wallet, along with any tx that depend on its outputs
void BRWalletRemoveTransaction(BRWallet *wallet, UInt256 txHash)
{
    BRTransaction *tx, *t;
    UInt256 *hashes = NULL;
    int notifyUser = 0, recommendRescan = 0;

    assert(wallet != NULL);
    assert(! UInt256IsZero(txHash));
    pthread_mutex_lock(&wallet->lock);
    tx = BRSetGet(wallet->allTx, &txHash);

    if (tx) {
        array_new(hashes, 0);

        for (size_t i = array_count(wallet->transactions); i > 0; i--) { // find depedent transactions
            t = wallet->transactions[i - 1];
            if (t->blockHeight < tx->blockHeight) break;
            if (BRTransactionEq(tx, t)) continue;
            
            for (size_t j = 0; j < t->inCount; j++) {
                if (! UInt256Eq(t->inputs[j].txHash, txHash)) continue;
                array_add(hashes, t->txHash);
                break;
            }
        }
        
        if (array_count(hashes) > 0) {
            pthread_mutex_unlock(&wallet->lock);
            
            for (size_t i = array_count(hashes); i > 0; i--) {
                BRWalletRemoveTransaction(wallet, hashes[i - 1]);
            }
            
            BRWalletRemoveTransaction(wallet, txHash);
        }
        else {
            for (size_t i = array_count(wallet->transactions); i > 0; i--) {
                if (! BRTransactionEq(wallet->transactions[i - 1], tx)) continue;
                array_rm(wallet->transactions, i - 1);
                break;
            }
            
            _BRWalletUpdateBalance(wallet);
            pthread_mutex_unlock(&wallet->lock);
            
            // if this is for a transaction we sent, and it wasn't already known to be invalid, notify user
            if (BRWalletAmountSentByTx(wallet, tx) > 0 && BRWalletTransactionIsValid(wallet, tx)) {
                recommendRescan = notifyUser = 1;
                
                for (size_t i = 0; i < tx->inCount; i++) { // only recommend a rescan if all inputs are confirmed
                    t = BRWalletTransactionForHash(wallet, tx->inputs[i].txHash);
                    if (t && t->blockHeight != TX_UNCONFIRMED) continue;
                    recommendRescan = 0;
                    break;
                }
            }

            if (wallet->balanceChanged) wallet->balanceChanged(wallet->callbackInfo, wallet->balance);
            if (wallet->txDeleted) wallet->txDeleted(wallet->callbackInfo, txHash, notifyUser, recommendRescan);
        }
        
        array_free(hashes);
    }
    else pthread_mutex_unlock(&wallet->lock);
}

// returns the transaction with the given hash if it's been registered in the wallet
BRTransaction *BRWalletTransactionForHash(BRWallet *wallet, UInt256 txHash)
{
    BRTransaction *tx;
    
    assert(wallet != NULL);
    assert(! UInt256IsZero(txHash));
    pthread_mutex_lock(&wallet->lock);
    tx = BRSetGet(wallet->allTx, &txHash);
    pthread_mutex_unlock(&wallet->lock);
    return tx;
}

// true if no previous wallet transaction spends any of the given transaction's inputs, and no inputs are invalid
int BRWalletTransactionIsValid(BRWallet *wallet, const BRTransaction *tx)
{
    BRTransaction *t;
    int r = 1;

    assert(wallet != NULL);
    assert(tx != NULL && BRTransactionIsSigned(tx));
    
    // TODO: XXX attempted double spends should cause conflicted tx to remain unverified until they're confirmed
    // TODO: XXX conflicted tx with the same wallet outputs should be presented as the same tx to the user

    if (tx && tx->blockHeight == TX_UNCONFIRMED) { // only unconfirmed transactions can be invalid
        pthread_mutex_lock(&wallet->lock);

        if (! BRSetContains(wallet->allTx, tx)) {
            for (size_t i = 0; r && i < tx->inCount; i++) {
                if (BRSetContains(wallet->spentOutputs, &tx->inputs[i])) r = 0;
            }
        }
        else if (BRSetContains(wallet->invalidTx, tx)) r = 0;

        pthread_mutex_unlock(&wallet->lock);

        for (size_t i = 0; r && i < tx->inCount; i++) {
            t = BRWalletTransactionForHash(wallet, tx->inputs[i].txHash);
            if (t && ! BRWalletTransactionIsValid(wallet, t)) r = 0;
        }
    }
    
    return r;
}

// true if tx cannot be immediately spent (i.e. if it or an input tx can be replaced-by-fee)
int BRWalletTransactionIsPending(BRWallet *wallet, const BRTransaction *tx)
{
    BRTransaction *t;
    time_t now = time(NULL);
    uint32_t blockHeight;
    int r = 0;
    
    assert(wallet != NULL);
    assert(tx != NULL && BRTransactionIsSigned(tx));
    pthread_mutex_lock(&wallet->lock);
    blockHeight = wallet->blockHeight;
    pthread_mutex_unlock(&wallet->lock);

    if (tx && tx->blockHeight == TX_UNCONFIRMED) { // only unconfirmed transactions can be postdated
        if (BRTransactionVSize(tx) > TX_MAX_SIZE) r = 1; // check transaction size is under TX_MAX_SIZE
        
        for (size_t i = 0; ! r && i < tx->inCount; i++) {
            if (tx->inputs[i].sequence < UINT32_MAX - 1) r = 1; // check for replace-by-fee
            if (tx->inputs[i].sequence < UINT32_MAX && tx->lockTime < TX_MAX_LOCK_HEIGHT &&
                tx->lockTime > blockHeight + 1) r = 1; // future lockTime
            if (tx->inputs[i].sequence < UINT32_MAX && tx->lockTime > now) r = 1; // future lockTime
        }
        
        for (size_t i = 0; ! r && i < tx->outCount; i++) { // check that no outputs are dust
            if (tx->outputs[i].amount < TX_MIN_OUTPUT_AMOUNT) r = 1;
        }
        
        for (size_t i = 0; ! r && i < tx->inCount; i++) { // check if any inputs are known to be pending
            t = BRWalletTransactionForHash(wallet, tx->inputs[i].txHash);
            if (t && BRWalletTransactionIsPending(wallet, t)) r = 1;
        }
    }
    
    return r;
}

// true if tx is considered 0-conf safe (valid and not pending, timestamp is greater than 0, and no unverified inputs)
int BRWalletTransactionIsVerified(BRWallet *wallet, const BRTransaction *tx)
{
    BRTransaction *t;
    int r = 1;

    assert(wallet != NULL);
    assert(tx != NULL && BRTransactionIsSigned(tx));

    if (tx && tx->blockHeight == TX_UNCONFIRMED) { // only unconfirmed transactions can be unverified
        if (tx->timestamp == 0 || ! BRWalletTransactionIsValid(wallet, tx) ||
            BRWalletTransactionIsPending(wallet, tx)) r = 0;
            
        for (size_t i = 0; r && i < tx->inCount; i++) { // check if any inputs are known to be unverified
            t = BRWalletTransactionForHash(wallet, tx->inputs[i].txHash);
            if (t && ! BRWalletTransactionIsVerified(wallet, t)) r = 0;
        }
    }
    
    return r;
}

// set the block heights and timestamps for the given transactions
// use height TX_UNCONFIRMED and timestamp 0 to indicate a tx should remain marked as unverified (not 0-conf safe)
void BRWalletUpdateTransactions(BRWallet *wallet, const UInt256 txHashes[], size_t txCount, uint32_t blockHeight,
                                uint32_t timestamp)
{
    BRTransaction *tx;
    UInt256 hashes[txCount];
    int needsUpdate = 0;
    size_t i, j, k;
    
    assert(wallet != NULL);
    assert(txHashes != NULL || txCount == 0);
    pthread_mutex_lock(&wallet->lock);
    if (blockHeight > wallet->blockHeight) wallet->blockHeight = blockHeight;
    
    for (i = 0, j = 0; txHashes && i < txCount; i++) {
        tx = BRSetGet(wallet->allTx, &txHashes[i]);
        if (! tx || (tx->blockHeight == blockHeight && tx->timestamp == timestamp)) continue;
        tx->timestamp = timestamp;
        tx->blockHeight = blockHeight;
        
        if (_BRWalletContainsTx(wallet, tx)) {
            for (k = array_count(wallet->transactions); k > 0; k--) { // remove and re-insert tx to keep wallet sorted
                if (! BRTransactionEq(wallet->transactions[k - 1], tx)) continue;
                array_rm(wallet->transactions, k - 1);
                _BRWalletInsertTx(wallet, tx);
                break;
            }
            
            hashes[j++] = txHashes[i];
            if (BRSetContains(wallet->pendingTx, tx) || BRSetContains(wallet->invalidTx, tx)) needsUpdate = 1;
        }
        else if (blockHeight != TX_UNCONFIRMED) { // remove and free confirmed non-wallet tx
            BRSetRemove(wallet->allTx, tx);
            BRTransactionFree(tx);
        }
    }
    
    if (needsUpdate) _BRWalletUpdateBalance(wallet);
    pthread_mutex_unlock(&wallet->lock);
    if (j > 0 && wallet->txUpdated) wallet->txUpdated(wallet->callbackInfo, hashes, j, blockHeight, timestamp);
}

// marks all transactions confirmed after blockHeight as unconfirmed (useful for chain re-orgs)
void BRWalletSetTxUnconfirmedAfter(BRWallet *wallet, uint32_t blockHeight)
{
    size_t i, j, count;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    wallet->blockHeight = blockHeight;
    count = i = array_count(wallet->transactions);
    while (i > 0 && wallet->transactions[i - 1]->blockHeight > blockHeight) i--;
    count -= i;

    UInt256 hashes[count];

    for (j = 0; j < count; j++) {
        wallet->transactions[i + j]->blockHeight = TX_UNCONFIRMED;
        hashes[j] = wallet->transactions[i + j]->txHash;
    }
    
    if (count > 0) _BRWalletUpdateBalance(wallet);
    pthread_mutex_unlock(&wallet->lock);
    if (count > 0 && wallet->txUpdated) wallet->txUpdated(wallet->callbackInfo, hashes, count, TX_UNCONFIRMED, 0);
}

// returns the amount received by the wallet from the transaction (total outputs to change and/or receive addresses)
uint64_t BRWalletAmountReceivedFromTx(BRWallet *wallet, const BRTransaction *tx)
{
    uint64_t amount = 0;
    const uint8_t *pkh;
    
    assert(wallet != NULL);
    assert(tx != NULL);
    pthread_mutex_lock(&wallet->lock);
    
    // TODO: don't include outputs below TX_MIN_OUTPUT_AMOUNT
    for (size_t i = 0; tx && i < tx->outCount; i++) {
        pkh = BRScriptPKH(tx->outputs[i].script, tx->outputs[i].scriptLen);
        if (pkh && BRSetContains(wallet->allPKH, pkh)) amount += tx->outputs[i].amount;
    }
    
    pthread_mutex_unlock(&wallet->lock);
    return amount;
}

// returns the amount sent from the wallet by the trasaction (total wallet outputs consumed, change and fee included)
uint64_t BRWalletAmountSentByTx(BRWallet *wallet, const BRTransaction *tx)
{
    uint64_t amount = 0;
    
    assert(wallet != NULL);
    assert(tx != NULL);
    pthread_mutex_lock(&wallet->lock);
    
    for (size_t i = 0; tx && i < tx->inCount; i++) {
        BRTransaction *t = BRSetGet(wallet->allTx, &tx->inputs[i].txHash);
        uint32_t n = tx->inputs[i].index;
        const uint8_t *pkh;

        if (t && n < t->outCount) {
            pkh = BRScriptPKH(t->outputs[n].script, t->outputs[n].scriptLen);
            if (pkh && BRSetContains(wallet->allPKH, pkh)) amount += t->outputs[n].amount;
        }
    }
    
    pthread_mutex_unlock(&wallet->lock);
    return amount;
}

// returns the fee for the given transaction if all its inputs are from wallet transactions, UINT64_MAX otherwise
uint64_t BRWalletFeeForTx(BRWallet *wallet, const BRTransaction *tx)
{
    uint64_t amount = 0;
    
    assert(wallet != NULL);
    assert(tx != NULL);
    pthread_mutex_lock(&wallet->lock);
    
    for (size_t i = 0; tx && i < tx->inCount && amount != UINT64_MAX; i++) {
        BRTransaction *t = BRSetGet(wallet->allTx, &tx->inputs[i].txHash);
        uint32_t n = tx->inputs[i].index;
        
        if (t && n < t->outCount) {
            amount += t->outputs[n].amount;
        }
        else amount = UINT64_MAX;
    }
    
    pthread_mutex_unlock(&wallet->lock);
    
    for (size_t i = 0; tx && i < tx->outCount && amount != UINT64_MAX; i++) {
        amount -= tx->outputs[i].amount;
    }
    
    return amount;
}

// historical wallet balance after the given transaction, or current balance if transaction is not registered in wallet
uint64_t BRWalletBalanceAfterTx(BRWallet *wallet, const BRTransaction *tx)
{
    uint64_t balance;
    
    assert(wallet != NULL);
    assert(tx != NULL && BRTransactionIsSigned(tx));
    pthread_mutex_lock(&wallet->lock);
    balance = wallet->balance;
    
    for (size_t i = array_count(wallet->transactions); tx && i > 0; i--) {
        if (! BRTransactionEq(tx, wallet->transactions[i - 1])) continue;
        balance = wallet->balanceHist[i - 1];
        break;
    }

    pthread_mutex_unlock(&wallet->lock);
    return balance;
}

// fee that will be added for a transaction of the given size in bytes
uint64_t BRWalletFeeForTxSize(BRWallet *wallet, size_t size)
{
    uint64_t fee;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    fee = _txFee(wallet->feePerKb, size);
    pthread_mutex_unlock(&wallet->lock);
    return fee;
}

// fee that will be added for a transaction of the given amount
uint64_t BRWalletFeeForTxAmount(BRWallet *wallet, uint64_t amount)
{
    static const uint8_t dummyScript[] = { OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG };
    BRTxOutput o = BR_TX_OUTPUT_NONE;
    BRTransaction *tx;
    uint64_t fee = 0, maxAmount = 0;
    
    assert(wallet != NULL);
    assert(amount > 0);
    maxAmount = BRWalletMaxOutputAmount(wallet);
    o.amount = (amount < maxAmount) ? amount : maxAmount;
    BRTxOutputSetScript(&o, dummyScript, sizeof(dummyScript)); // unspendable dummy scriptPubKey
    tx = BRWalletCreateTxForOutputs(wallet, &o, 1);

    if (tx) {
        fee = BRWalletFeeForTx(wallet, tx);
        BRTransactionFree(tx);
    }
    
    return fee;
}

// outputs below this amount are uneconomical due to fees (TX_MIN_OUTPUT_AMOUNT is the absolute minimum output amount)
uint64_t BRWalletMinOutputAmount(BRWallet *wallet)
{
    uint64_t amount;
    
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    amount = (TX_MIN_OUTPUT_AMOUNT*wallet->feePerKb + MIN_FEE_PER_KB - 1)/MIN_FEE_PER_KB;
    pthread_mutex_unlock(&wallet->lock);
    return (amount > TX_MIN_OUTPUT_AMOUNT) ? amount : TX_MIN_OUTPUT_AMOUNT;
}

// maximum amount that can be sent from the wallet to a single address after fees
uint64_t BRWalletMaxOutputAmount(BRWallet *wallet)
{
    BRTransaction *tx;
    BRUTXO *o;
    uint64_t fee, amount = 0;
    size_t i, txSize, cpfpSize = 0, inCount = 0;

    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);

    for (i = array_count(wallet->utxos); i > 0; i--) {
        o = &wallet->utxos[i - 1];
        tx = BRSetGet(wallet->allTx, &o->hash);
        if (! tx || o->n >= tx->outCount) continue;
        inCount++;
        amount += tx->outputs[o->n].amount;
        
//        // size of unconfirmed, non-change inputs for child-pays-for-parent fee
//        // don't include parent tx with more than 10 inputs or 10 outputs
//        if (tx->blockHeight == TX_UNCONFIRMED && tx->inCount <= 10 && tx->outCount <= 10 &&
//            ! _BRWalletTxIsSend(wallet, tx)) cpfpSize += BRTransactionVSize(tx);
    }

    txSize = 8 + BRVarIntSize(inCount) + TX_INPUT_SIZE*inCount + BRVarIntSize(2) + TX_OUTPUT_SIZE*2;
    fee = _txFee(wallet->feePerKb, txSize + cpfpSize);
    pthread_mutex_unlock(&wallet->lock);
    
    return (amount > fee) ? amount - fee : 0;
}

static void _setApplyFreeTx(void *info, void *tx)
{
    BRTransactionFree(tx);
}

// frees memory allocated for wallet, and calls BRTransactionFree() for all registered transactions
void BRWalletFree(BRWallet *wallet)
{
    assert(wallet != NULL);
    pthread_mutex_lock(&wallet->lock);
    BRSetFree(wallet->allPKH);
    BRSetFree(wallet->usedPKH);
    BRSetFree(wallet->invalidTx);
    BRSetFree(wallet->pendingTx);
    BRSetApply(wallet->allTx, NULL, _setApplyFreeTx);
    BRSetFree(wallet->allTx);
    BRSetFree(wallet->spentOutputs);
    array_free(wallet->internalChain);
    array_free(wallet->externalChain);
    array_free(wallet->balanceHist);
    array_free(wallet->transactions);
    array_free(wallet->utxos);
    pthread_mutex_unlock(&wallet->lock);
    pthread_mutex_destroy(&wallet->lock);
    free(wallet);
}

// returns the given amount (in satoshis) in local currency units (i.e. pennies, pence)
// price is local currency units per bitcoin
int64_t BRLocalAmount(int64_t amount, double price)
{
    int64_t localAmount = llabs(amount)*price/SATOSHIS;
    
    // if amount is not 0, but is too small to be represented in local currency, return minimum non-zero localAmount
    if (localAmount == 0 && amount != 0) localAmount = 1;
    return (amount < 0) ? -localAmount : localAmount;
}

// returns the given local currency amount in satoshis
// price is local currency units (i.e. pennies, pence) per bitcoin
int64_t BRBitcoinAmount(int64_t localAmount, double price)
{
    int overflowbits = 0;
    int64_t p = 10, min, max, amount = 0, lamt = llabs(localAmount);

    if (lamt != 0 && price > 0) {
        while (lamt + 1 > INT64_MAX/SATOSHIS) lamt /= 2, overflowbits++; // make sure we won't overflow an int64_t
        min = lamt*SATOSHIS/price; // minimum amount that safely matches localAmount
        max = (lamt + 1)*SATOSHIS/price - 1; // maximum amount that safely matches localAmount
        amount = (min + max)/2; // average min and max
        while (overflowbits > 0) lamt *= 2, min *= 2, max *= 2, amount *= 2, overflowbits--;
        
        if (amount >= MAX_MONEY) return (localAmount < 0) ? -MAX_MONEY : MAX_MONEY;
        while ((amount/p)*p >= min && p <= INT64_MAX/10) p *= 10; // lowest decimal precision matching localAmount
        p /= 10;
        amount = (amount/p)*p;
    }
    
    return (localAmount < 0) ? -amount : amount;
}

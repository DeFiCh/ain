//
//  BRPeer.c
//
//  Created by Aaron Voisine on 9/2/15.
//  Copyright (c) 2015 breadwallet LLC.
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

#include "BRPeer.h"
#include "BRMerkleBlock.h"
#include "BRAddress.h"
#include "BRSet.h"
#include "BRArray.h"
#include "BRCrypto.h"
#include "BRInt.h"
#include <logging.h>

#include <stdlib.h>
#include <float.h>
#include <inttypes.h>
#include <string.h>

#include <compat.h>

//#include <pthread.h>
//#include <unistd.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <netdb.h>
//#include <sys/socket.h>
//#include <sys/time.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>

#define HEADER_LENGTH      24
#define MAX_MSG_LENGTH     0x02000000
#define MAX_GETDATA_HASHES 50000
#define ENABLED_SERVICES   0ULL  // we don't provide full blocks to remote nodes
#define PROTOCOL_VERSION   70013
#define MIN_PROTO_VERSION  70002 // peers earlier than this protocol version not supported (need v0.9 txFee relay rules)
#define LOCAL_HOST         (UInt128) {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x01 }}
#define CONNECT_TIMEOUT    3.0
#define MESSAGE_TIMEOUT    10.0
#define WITNESS_FLAG       0x40000000

#define PTHREAD_STACK_SIZE  (512 * 1024)


// the standard blockchain download protocol works as follows (for SPV mode):
// - local peer sends getblocks
// - remote peer reponds with inv containing up to 500 block hashes
// - local peer sends getdata with the block hashes
// - remote peer responds with multiple merkleblock and tx messages
// - remote peer sends inv containg 1 hash, of the most recent block
// - local peer sends getdata with the most recent block hash
// - remote peer responds with merkleblock
// - if local peer can't connect the most recent block to the chain (because it started more than 500 blocks behind), go
//   back to first step and repeat until entire chain is downloaded
//
// we modify this sequence to improve sync performance and handle adding bip32 addresses to the bloom filter as needed:
// - local peer sends getheaders
// - remote peer responds with up to 2000 headers
// - local peer immediately sends getheaders again and then processes the headers
// - previous two steps repeat until a header within a week of earliestKeyTime is reached (further headers are ignored)
// - local peer sends getblocks
// - remote peer responds with inv containing up to 500 block hashes
// - local peer sends getdata with the block hashes
// - if there were 500 hashes, local peer sends getblocks again without waiting for remote peer
// - remote peer responds with multiple merkleblock and tx messages, followed by inv containing up to 500 block hashes
// - previous two steps repeat until an inv with fewer than 500 block hashes is received
// - local peer sends just getdata for the final set of fewer than 500 block hashes
// - remote peer responds with multiple merkleblock and tx messages
// - if at any point tx messages consume enough wallet addresses to drop below the bip32 chain gap limit, more addresses
//   are generated and local peer sends filterload with an updated bloom filter
// - after filterload is sent, getdata is sent to re-request recent blocks that may contain new tx matching the filter

char const * spv_logfilename = NULL;
int spv_log2console = 1;
boost::mutex log_mutex;

typedef enum {
    inv_undefined = 0,
    inv_tx = 1,
    inv_block = 2,
    inv_filtered_block = 3,
    inv_witness_block = inv_block | WITNESS_FLAG,
    inv_witness_tx = inv_tx | WITNESS_FLAG,
    inv_filtered_witness_block = inv_filtered_block | WITNESS_FLAG
} inv_type;

typedef struct {
    BRPeer peer; // superstruct on top of BRPeer
    uint32_t magicNumber;
    char host[INET6_ADDRSTRLEN];
    BRPeerStatus status;
    int waitingForNetwork;
    volatile int needsFilterUpdate;
    uint64_t nonce, feePerKb;
    char *useragent;
    uint32_t version, lastblock, earliestKeyTime, currentBlockHeight;
    double startTime, pingTime;
    volatile double disconnectTime, mempoolTime;
    int sentVerack, gotVerack, sentGetaddr, sentFilter, sentGetdata, sentMempool, sentGetblocks;
    UInt256 lastBlockHash;
    BRMerkleBlock *currentBlock;
    UInt256 *currentBlockTxHashes, *knownBlockHashes, *knownTxHashes;
    BRSet *knownTxHashSet;
    volatile SOCKET socket;
    void *info;
    void (*connected)(void *info);
    void (*disconnected)(void *info, int error);
    void (*relayedPeers)(void *info, const BRPeer peers[], size_t peersCount);
    void (*relayedTx)(void *info, BRTransaction *tx);
    void (*hasTx)(void *info, UInt256 txHash);
    void (*rejectedTx)(void *info, UInt256 txHash, uint8_t code);
    void (*relayedBlock)(void *info, BRMerkleBlock *block);
    void (*notfound)(void *info, const UInt256 txHashes[], size_t txCount, const UInt256 blockHashes[],
                     size_t blockCount);
    void (*setFeePerKb)(void *info, uint64_t feePerKb);
    BRTransaction *(*requestedTx)(void *info, UInt256 txHash);
    int (*networkIsReachable)(void *info);
    void (*threadCleanup)(void *info);
    void **volatile pongInfo;
    void (**volatile pongCallback)(void *info, int success);
    void *volatile mempoolInfo;
    void (*volatile mempoolCallback)(void *info, int success);
    std::unique_ptr<boost::thread> thread;
    boost::mutex lock;
} BRPeerContext;

void BRPeerSendVersionMessage(BRPeer *peer);
void BRPeerSendVerackMessage(BRPeer *peer);
void BRPeerSendAddr(BRPeer *peer);

inline static int _BRPeerIsIPv4(const BRPeer *peer)
{
    return (peer->address.u64[0] == 0 && peer->address.u16[4] == 0 && peer->address.u16[5] == 0xffff);
}

static void _BRPeerAddKnownTxHashes(const BRPeer *peer, const UInt256 txHashes[], size_t txCount)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    UInt256 *knownTxHashes = ctx->knownTxHashes;
    size_t i, j;
    
    for (i = 0; i < txCount; i++) {
        if (! BRSetContains(ctx->knownTxHashSet, &txHashes[i])) {
            array_add(knownTxHashes, txHashes[i]);
            
            if (ctx->knownTxHashes != knownTxHashes) { // check if knownTxHashes was moved to a new memory location
                ctx->knownTxHashes = knownTxHashes;
                BRSetClear(ctx->knownTxHashSet);
                for (j = array_count(knownTxHashes); j > 0; j--) BRSetAdd(ctx->knownTxHashSet, &knownTxHashes[j - 1]);
            }
            else BRSetAdd(ctx->knownTxHashSet, &knownTxHashes[array_count(knownTxHashes) - 1]);
        }
    }
}

static void _BRPeerDidConnect(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;

    ctx->lock.lock();
    if (ctx->status == BRPeerStatusConnecting && ctx->sentVerack && ctx->gotVerack) {
        peer_log(peer, "handshake completed");
        ctx->disconnectTime = DBL_MAX;
        ctx->status = BRPeerStatusConnected;
        peer_log(peer, "connected with lastblock: %" PRIu32, ctx->lastblock);
        ctx->lock.unlock();
        if (ctx->connected) ctx->connected(ctx->info);
    }
    else {
        ctx->lock.unlock();
    }
}

static int _BRPeerAcceptVersionMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, strLen = 0, len = 0;
    uint64_t recvServices, fromServices, nonce;
    UInt128 recvAddr, fromAddr;
    uint16_t recvPort, fromPort;
    int r = 1;
    
    if (85 > msgLen) {
        peer_log(peer, "malformed version message, length is %zu, should be >= 85", msgLen);
        r = 0;
    }
    else {
        ctx->version = UInt32GetLE(&msg[off]);
        off += sizeof(uint32_t);
        peer->services = UInt64GetLE(&msg[off]);
        off += sizeof(uint64_t);
        peer->timestamp = UInt64GetLE(&msg[off]);
        off += sizeof(uint64_t);
        recvServices = UInt64GetLE(&msg[off]);
        off += sizeof(uint64_t);
        recvAddr = UInt128Get(&msg[off]);
        off += sizeof(UInt128);
        recvPort = UInt16GetBE(&msg[off]);
        off += sizeof(uint16_t);
        fromServices = UInt64GetLE(&msg[off]);
        off += sizeof(uint64_t);
        fromAddr = UInt128Get(&msg[off]);
        off += sizeof(UInt128);
        fromPort = UInt16GetBE(&msg[off]);
        off += sizeof(uint16_t);
        nonce = UInt64GetLE(&msg[off]);
        off += sizeof(uint64_t);
        strLen = (size_t)BRVarInt(&msg[off], (off <= msgLen ? msgLen - off : 0), &len);
        off += len;

        if (off + strLen + sizeof(uint32_t) > msgLen) {
            peer_log(peer, "malformed version message, length is %zu, should be %zu", msgLen,
                     off + strLen + sizeof(uint32_t));
            r = 0;
        }
        else if (ctx->version < MIN_PROTO_VERSION) {
            peer_log(peer, "protocol version %" PRIu32 " not supported", ctx->version);
            r = 0;
        }
        else {
            array_clear(ctx->useragent);
            array_add_array(ctx->useragent, &msg[off], strLen);
            array_add(ctx->useragent, '\0');
            off += strLen;
            ctx->lastblock = UInt32GetLE(&msg[off]);
            off += sizeof(uint32_t);
            peer_log(peer, "got version %" PRIu32 ", services %" PRIx64 ", useragent:\"%s\"", ctx->version, peer->services,
                     ctx->useragent);
            BRPeerSendVerackMessage(peer);
        }
    }
    
    return r;
}

static int _BRPeerAcceptVerackMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    struct timeval tv;
    int r = 1;
    
    if (ctx->gotVerack) {
        peer_log(peer, "got unexpected verack");
    }
    else {
        gettimeofday(&tv, NULL);
        ctx->pingTime = tv.tv_sec + (double)tv.tv_usec/1000000 - ctx->startTime; // use verack time as initial ping time
        ctx->startTime = 0;
        peer_log(peer, "got verack in %fs", ctx->pingTime);
        ctx->gotVerack = 1;
        _BRPeerDidConnect(peer);
    }
    
    return r;
}

// TODO: relay addresses
static int _BRPeerAcceptAddrMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, count = (size_t)BRVarInt(msg, msgLen, &off);
    int r = 1;
    
    if (off == 0 || off + count*30 > msgLen) {
        peer_log(peer, "malformed addr message, length is %zu, should be %zu for %zu address(es)", msgLen,
                 BRVarIntSize(count) + 30*count, count);
        r = 0;
    }
    else if (count > 1000) {
        peer_log(peer, "dropping addr message, %zu is too many addresses, max is 1000", count);
    }
    else if (ctx->sentGetaddr) { // simple anti-tarpitting tactic, don't accept unsolicited addresses
        BRPeer peers[count], p;
        size_t peersCount = 0;
        time_t now = time(NULL);
        
        peer_log(peer, "got addr with %zu address(es)", count);

        for (size_t i = 0; i < count; i++) {
            p.timestamp = UInt32GetLE(&msg[off]);
            off += sizeof(uint32_t);
            p.services = UInt64GetLE(&msg[off]);
            off += sizeof(uint64_t);
            p.address = UInt128Get(&msg[off]);
            off += sizeof(UInt128);
            p.port = UInt16GetBE(&msg[off]);
            off += sizeof(uint16_t);

            if (! (p.services & SERVICES_NODE_NETWORK)) continue; // skip peers that don't carry full blocks
            if (! _BRPeerIsIPv4(&p)) continue; // ignore IPv6 for now
        
            // if address time is more than 10 min in the future or unknown, set to 5 days old
            if (p.timestamp > now + 10*60 || p.timestamp == 0) p.timestamp = now - 5*24*60*60;
            p.timestamp -= 2*60*60; // subtract two hours
            peers[peersCount++] = p; // add it to the list
        }

        if (peersCount > 0 && ctx->relayedPeers) ctx->relayedPeers(ctx->info, peers, peersCount);
    }

    return r;
}

static int _BRPeerAcceptInvMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, count = (size_t)BRVarInt(msg, msgLen, &off);
    int r = 1;
    
    if (off == 0 || off + count*36 > msgLen) {
        peer_log(peer, "malformed inv message, length is %zu, should be %zu for %zu item(s)", msgLen,
                 BRVarIntSize(count) + 36*count, count);
        r = 0;
    }
    else if (count > MAX_GETDATA_HASHES) {
        peer_log(peer, "dropping inv message, %zu is too many items, max is %d", count, MAX_GETDATA_HASHES);
    }
    else {
        inv_type type;
        const uint8_t *transactions[count], *blocks[count];
        size_t i, j, txCount = 0, blockCount = 0;
        
        peer_log(peer, "got inv with %zu item(s)", count);

        for (i = 0; i < count; i++) {
            type = (inv_type)UInt32GetLE(&msg[off]);
            
            switch (type) { // inv messages only use inv_tx or inv_block
                case inv_tx: transactions[txCount++] = &msg[off + sizeof(uint32_t)]; break;
                case inv_block: blocks[blockCount++] = &msg[off + sizeof(uint32_t)]; break;
                default: break;
            }

            off += 36;
        }

        if (txCount > 0 && ! ctx->sentFilter && ! ctx->sentMempool && ! ctx->sentGetblocks) {
            peer_log(peer, "got inv message before loading a filter");
            r = 0;
        }
        else if (txCount > 10000) { // sanity check
            peer_log(peer, "too many transactions, disconnecting");
            r = 0;
        }
        else if (ctx->currentBlockHeight > 0 && blockCount > 2 && blockCount < 500 &&
                 ctx->currentBlockHeight + array_count(ctx->knownBlockHashes) + blockCount < ctx->lastblock) {
            peer_log(peer, "non-standard inv, %zu is fewer block hash(es) than expected", blockCount);
            r = 0;
        }
        else {
            if (! ctx->sentFilter && ! ctx->sentGetblocks) blockCount = 0;
            if (blockCount == 1 && UInt256Eq(ctx->lastBlockHash, UInt256Get(blocks[0]))) blockCount = 0;
            if (blockCount == 1) ctx->lastBlockHash = UInt256Get(blocks[0]);

            UInt256 hash, blockHashes[blockCount], txHashes[txCount];

            for (i = 0; i < blockCount; i++) {
                blockHashes[i] = UInt256Get(blocks[i]);
                // remember blockHashes in case we need to re-request them with an updated bloom filter
                array_add(ctx->knownBlockHashes, blockHashes[i]);
            }
        
            while (array_count(ctx->knownBlockHashes) > MAX_GETDATA_HASHES) {
                array_rm_range(ctx->knownBlockHashes, 0, array_count(ctx->knownBlockHashes)/3);
            }
        
            if (ctx->needsFilterUpdate) blockCount = 0;
        
            for (i = 0, j = 0; i < txCount; i++) {
                hash = UInt256Get(transactions[i]);
                
                if (BRSetContains(ctx->knownTxHashSet, &hash)) {
                    if (ctx->hasTx) ctx->hasTx(ctx->info, hash);
                }
                else txHashes[j++] = hash;
            }
            
            _BRPeerAddKnownTxHashes(peer, txHashes, j);
            if (j > 0 || blockCount > 0) BRPeerSendGetdata(peer, txHashes, j, blockHashes, blockCount);
    
            // to improve chain download performance, if we received 500 block hashes, request the next 500 block hashes
            if (blockCount >= 500) {
                UInt256 locators[] = { blockHashes[blockCount - 1], blockHashes[0] };
            
                BRPeerSendGetblocks(peer, locators, 2, UINT256_ZERO);
            }
            
            if (txCount > 0 && ctx->mempoolCallback) {
                peer_log(peer, "got initial mempool response");
                BRPeerSendPing(peer, ctx->mempoolInfo, ctx->mempoolCallback);
                ctx->mempoolCallback = NULL;

                ctx->lock.lock();
                ctx->mempoolTime = DBL_MAX;
                ctx->lock.unlock();

            }
        }
    }
    
    return r;
}

static int _BRPeerAcceptTxMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    BRTransaction *tx = BRTransactionParse(msg, msgLen);
    UInt256 txHash;
    int r = 1;

    if (! tx) {
        peer_log(peer, "malformed tx message with length: %zu", msgLen);
        r = 0;
    }
    else if (! ctx->sentFilter && ! ctx->sentGetdata) {
        peer_log(peer, "got tx message before loading filter");
        BRTransactionFree(tx);
        r = 0;
    }
    else {
        txHash = tx->txHash;
        peer_log(peer, "got tx: %s", u256hex(txHash).c_str());

        if (ctx->relayedTx) {
            ctx->relayedTx(ctx->info, tx);
        }
        else BRTransactionFree(tx);

        if (ctx->currentBlock) { // we're collecting tx messages for a merkleblock
            for (size_t i = array_count(ctx->currentBlockTxHashes); i > 0; i--) {
                if (! UInt256Eq(txHash, ctx->currentBlockTxHashes[i - 1])) continue;
                array_rm(ctx->currentBlockTxHashes, i - 1);
                break;
            }
        
            if (array_count(ctx->currentBlockTxHashes) == 0) { // we received the entire block including all matched tx
                BRMerkleBlock *block = ctx->currentBlock;
            
                ctx->currentBlock = NULL;
                if (ctx->relayedBlock) ctx->relayedBlock(ctx->info, block);
            }
        }
    }
    
    return r;
}

static int _BRPeerAcceptHeadersMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, count = (size_t)BRVarInt(msg, msgLen, &off);
    int r = 1;

    if (off == 0 || off + 81*count > msgLen) {
        peer_log(peer, "malformed headers message, length is %zu, should be %zu for %zu header(s)", msgLen,
                 BRVarIntSize(count) + 81*count, count);
        r = 0;
    }
    else {
        peer_log(peer, "got %zu header(s)", count);
    
        // To improve chain download performance, if this message contains 2000 headers then request the next 2000
        // headers immediately, and switch to requesting blocks when we receive a header newer than earliestKeyTime
        uint32_t timestamp = (count > 0) ? UInt32GetLE(&msg[off + 81*(count - 1) + 68]) : 0;
    
        if (count >= 2000 || (timestamp > 0 && timestamp + 7*24*60*60 + BLOCK_MAX_TIME_DRIFT >= ctx->earliestKeyTime)) {
            size_t last = 0;
            time_t now = time(NULL);
            UInt256 locators[2];
            
            BRSHA256_2(&locators[0], &msg[off + 81*(count - 1)], 80);
            BRSHA256_2(&locators[1], &msg[off], 80);

            if (timestamp > 0 && timestamp + 7*24*60*60 + BLOCK_MAX_TIME_DRIFT >= ctx->earliestKeyTime) {
                // request blocks for the remainder of the chain
                timestamp = (++last < count) ? UInt32GetLE(&msg[off + 81*last + 68]) : 0;

                while (timestamp > 0 && timestamp + 7*24*60*60 + BLOCK_MAX_TIME_DRIFT < ctx->earliestKeyTime) {
                    timestamp = (++last < count) ? UInt32GetLE(&msg[off + 81*last + 68]) : 0;
                }
                
                BRSHA256_2(&locators[0], &msg[off + 81*(last - 1)], 80);
                BRPeerSendGetblocks(peer, locators, 2, UINT256_ZERO);
            }
            else BRPeerSendGetheaders(peer, locators, 2, UINT256_ZERO);

            for (size_t i = 0; r && i < count; i++) {
                BRMerkleBlock *block = BRMerkleBlockParse(&msg[off + 81*i], 81);
                
                if (! block) {
                    peer_log(peer, "malformed headers message with length: %zu", msgLen);
                    r = 0;
                }
                else if (! BRMerkleBlockIsValid(block, (uint32_t)now)) {
                    peer_log(peer, "invalid block header: %s", u256hex(block->blockHash).c_str());
                    BRMerkleBlockFree(block);
                    r = 0;
                }
                else if (ctx->relayedBlock) {
                    ctx->relayedBlock(ctx->info, block);
                }
                else BRMerkleBlockFree(block);
            }
        }
        else {
            peer_log(peer, "non-standard headers message, %zu is fewer header(s) than expected", count);
            r = 0;
        }
    }
    
    return r;
}

static int _BRPeerAcceptGetaddrMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    peer_log(peer, "got getaddr");
    BRPeerSendAddr(peer);
    return 1;
}

static int _BRPeerAcceptGetdataMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, count = (size_t)BRVarInt(msg, msgLen, &off);
    int r = 1;
    
    if (off == 0 || off + 36*count > msgLen) {
        peer_log(peer, "malformed getdata message, length is %zu, should %zu for %zu item(s)", msgLen,
                 BRVarIntSize(count) + 36*count, count);
        r = 0;
    }
    else if (count > MAX_GETDATA_HASHES) {
        peer_log(peer, "dropping getdata message, %zu is too many items, max is %d", count, MAX_GETDATA_HASHES);
    }
    else {
        struct inv_item { uint8_t item[36]; } *notfound = NULL;
        BRTransaction *tx = NULL;
        
        peer_log(peer, "got getdata with %zu item(s)", count);
        
        for (size_t i = 0; i < count; i++) {
            inv_type type = (inv_type)UInt32GetLE(&msg[off]);
            UInt256 hash = UInt256Get(&msg[off + sizeof(uint32_t)]);
            
            switch (type) {
                case inv_witness_tx: // drop through
                case inv_tx:
                    if (ctx->requestedTx) tx = ctx->requestedTx(ctx->info, hash);

                    if (tx && BRTransactionVSize(tx) < TX_MAX_SIZE) {
                        uint8_t buf[BRTransactionSerialize(tx, NULL, 0)];
                        size_t bufLen = BRTransactionSerialize(tx, buf, sizeof(buf));
                        char txHex[bufLen*2 + 1];
                        
                        for (size_t j = 0; j < bufLen; j++) {
                            sprintf(&txHex[j*2], "%02x", buf[j]);
                        }
                        
                        peer_log(peer, "publishing tx: %s", txHex);
                        BRPeerSendMessage(peer, buf, bufLen, MSG_TX);
                        break;
                    }
                    
                    // fall through
                default:
                    if (! notfound) array_new(notfound, 1);
                    array_add(notfound, *(struct inv_item *)&msg[off]);
                    peer_log(peer, "not found: type = %i, data= %s", type, u256hex(UInt256Reverse(hash)).c_str());
                    break;
            }
            
            off += 36;
        }

        if (notfound) {
            size_t bufLen = BRVarIntSize(array_count(notfound)) + 36*array_count(notfound), o = 0;
            uint8_t *buf = (uint8_t *)malloc(bufLen);
            
            assert(buf != NULL);
            o += BRVarIntSet(&buf[o], (o <= bufLen ? bufLen - o : 0), array_count(notfound));
            memcpy(&buf[o], notfound, 36*array_count(notfound));
            o += 36*array_count(notfound);
            array_free(notfound);
            BRPeerSendMessage(peer, buf, o, MSG_NOTFOUND);
            free(buf);
        }
    }

    return r;
}

static int _BRPeerAcceptNotfoundMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, count = (size_t)BRVarInt(msg, msgLen, &off);
    int r = 1;

    if (off == 0 || off + 36*count > msgLen) {
        peer_log(peer, "malformed notfound message, length is %zu, should be %zu for %zu item(s)", msgLen,
                 BRVarIntSize(count) + 36*count, count);
        r = 0;
    }
    else if (count > MAX_GETDATA_HASHES) {
        peer_log(peer, "dropping notfound message, %zu is too many items, max is %d", count, MAX_GETDATA_HASHES);
    }
    else {
        inv_type type;
        UInt256 *txHashes, *blockHashes, hash;
        
        peer_log(peer, "got notfound with %zu item(s)", count);
        array_new(txHashes, 1);
        array_new(blockHashes, 1);
        
        for (size_t i = 0; i < count; i++) {
            type = (inv_type)UInt32GetLE(&msg[off]);
            hash = UInt256Get(&msg[off + sizeof(uint32_t)]);
            
            switch (type) {
                case inv_witness_tx: // drop through
                case inv_tx: array_add(txHashes, hash); break;
                case inv_filtered_witness_block: // drop through
                case inv_witness_block: // drop through
                case inv_filtered_block: // drop through
                case inv_block: array_add(blockHashes, hash); break;
                default: break;
            }
            
            off += 36;
        }
        
        if (ctx->notfound) {
            ctx->notfound(ctx->info, txHashes, array_count(txHashes), blockHashes, array_count(blockHashes));
        }
        
        array_free(txHashes);
        array_free(blockHashes);
    }
    
    return r;
}

static int _BRPeerAcceptPingMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    int r = 1;
    
    if (sizeof(uint64_t) > msgLen) {
        peer_log(peer, "malformed ping message, length is %zu, should be %zu", msgLen, sizeof(uint64_t));
        r = 0;
    }
    else {
        peer_log(peer, "got ping");
        BRPeerSendMessage(peer, msg, msgLen, MSG_PONG);
    }

    return r;
}

static int _BRPeerAcceptPongMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    struct timeval tv;
    double pingTime;
    int r = 1;
    
    if (sizeof(uint64_t) > msgLen) {
        peer_log(peer, "malformed pong message, length is %zu, should be %zu", msgLen, sizeof(uint64_t));
        r = 0;
    }
    else if (UInt64GetLE(msg) != ctx->nonce) {
        peer_log(peer, "pong message has wrong nonce: %" PRIu64 ", expected: %" PRIu64, UInt64GetLE(msg), ctx->nonce);
        r = 0;
    }
    else if (array_count(ctx->pongCallback) == 0) {
        peer_log(peer, "got unexpected pong");
        r = 0;
    }
    else {
        if (ctx->startTime > 1) {
            gettimeofday(&tv, NULL);
            pingTime = tv.tv_sec + (double)tv.tv_usec/1000000 - ctx->startTime;

            // 50% low pass filter on current ping time
            ctx->pingTime = ctx->pingTime*0.5 + pingTime*0.5;
            ctx->startTime = 0;
            peer_log(peer, "got pong in %fs", pingTime);
        }
        else peer_log(peer, "got pong");

        if (array_count(ctx->pongCallback) > 0) {
            void (*pongCallback)(void *, int) = ctx->pongCallback[0];
            void *pongInfo = ctx->pongInfo[0];

            array_rm(ctx->pongCallback, 0);
            array_rm(ctx->pongInfo, 0);
            if (pongCallback) pongCallback(pongInfo, 1);
        }
    }
    
    return r;
}

static int _BRPeerAcceptMerkleblockMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    // Bitcoin nodes don't support querying arbitrary transactions, only transactions not yet accepted in a block. After
    // a merkleblock message, the remote node is expected to send tx messages for the tx referenced in the block. When a
    // non-tx message is received we should have all the tx in the merkleblock.
    BRPeerContext *ctx = (BRPeerContext *)peer;
    BRMerkleBlock *block = BRMerkleBlockParse(msg, msgLen);
    int r = 1;
  
    if (! block) {
        peer_log(peer, "malformed merkleblock message with length: %zu", msgLen);
        r = 0;
    }
    else if (! BRMerkleBlockIsValid(block, (uint32_t)time(NULL))) {
        peer_log(peer, "invalid merkleblock: %s", u256hex(block->blockHash).c_str());
        BRMerkleBlockFree(block);
        block = NULL;
        r = 0;
    }
    else if (! ctx->sentFilter && ! ctx->sentGetdata) {
        peer_log(peer, "got merkleblock message before loading a filter");
        BRMerkleBlockFree(block);
        block = NULL;
        r = 0;
    }
    else {
        size_t count = BRMerkleBlockTxHashes(block, NULL, 0);
        UInt256 _hashes[128], *hashes = (count <= 128) ? _hashes : (UInt256 *)malloc(count*sizeof(UInt256));
        
        assert(hashes != NULL);
        count = BRMerkleBlockTxHashes(block, hashes, count);

        for (size_t i = count; i > 0; i--) { // reverse order for more efficient removal as tx arrive
            if (BRSetContains(ctx->knownTxHashSet, &hashes[i - 1])) continue;
            array_add(ctx->currentBlockTxHashes, hashes[i - 1]);
        }

        if (hashes != _hashes) free(hashes);
    }

    if (block) {
        if (array_count(ctx->currentBlockTxHashes) > 0) { // wait til we get all tx messages before processing the block
            ctx->currentBlock = block;
        }
        else if (ctx->relayedBlock) {
            ctx->relayedBlock(ctx->info, block);
        }
        else BRMerkleBlockFree(block);
    }

    return r;
}

// described in BIP61: https://github.com/bitcoin/bips/blob/master/bip-0061.mediawiki
static int _BRPeerAcceptRejectMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, strLen = (size_t)BRVarInt(msg, msgLen, &off);
    int r = 1;
    
    if (off + strLen + sizeof(uint8_t) > msgLen) {
        peer_log(peer, "malformed reject message, length is %zu, should be >= %zu", msgLen,
                 off + strLen + sizeof(uint8_t));
        r = 0;
    }
    else {
        char type[(strLen < 0x1000) ? strLen + 1 : 0x1000];
        uint8_t code;
        size_t len = 0, hashLen = 0;

        strncpy(type, (const char *)&msg[off], sizeof(type) - 1);
        type[sizeof(type) - 1] = '\0';
        off += strLen;
        code = msg[off++];
        strLen = (size_t)BRVarInt(&msg[off], (off <= msgLen ? msgLen - off : 0), &len);
        off += len;
        if (strncmp(type, MSG_TX, sizeof(type)) == 0) hashLen = sizeof(UInt256);
        
        if (off + strLen + hashLen > msgLen) {
            peer_log(peer, "malformed reject message, length is %zu, should be >= %zu", msgLen, off + strLen + hashLen);
            r = 0;
        }
        else {
            char reason[(strLen < 0x1000) ? strLen + 1 : 0x1000];
            UInt256 txHash = UINT256_ZERO;
            
            strncpy(reason, (const char *)&msg[off], sizeof(reason) - 1);
            reason[sizeof(reason) - 1] = '\0';
            off += strLen;
            if (hashLen == sizeof(UInt256)) txHash = UInt256Get(&msg[off]);
            off += hashLen;

            if (! UInt256IsZero(txHash)) {
                peer_log(peer, "rejected %s code: 0x%x reason: \"%s\" txid: %s", type, code, reason, u256hex(txHash).c_str());
                if (ctx->rejectedTx) ctx->rejectedTx(ctx->info, txHash, code);
            }
            else peer_log(peer, "rejected %s code: 0x%x reason: \"%s\"", type, code, reason);
        }
    }

    return r;
}

// BIP133: https://github.com/bitcoin/bips/blob/master/bip-0133.mediawiki
static int _BRPeerAcceptFeeFilterMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    int r = 1;
    
    if (sizeof(uint64_t) > msgLen) {
        peer_log(peer, "malformed feefilter message, length is %zu, should be >= %zu", msgLen, sizeof(uint64_t));
        r = 0;
    }
    else {
        ctx->lock.lock();
        ctx->feePerKb = UInt64GetLE(msg);
        ctx->lock.unlock();
        
        peer_log(peer, "got feefilter with rate %" PRIu64, ctx->feePerKb);
        if (ctx->setFeePerKb) ctx->setFeePerKb(ctx->info, ctx->feePerKb);
    }
    
    return r;
}

static int _BRPeerAcceptMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen, const char *type)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    int r = 1;
    
    if (ctx->currentBlock && strncmp(MSG_TX, type, 12) != 0) { // if we receive a non-tx message, merkleblock is done
        peer_log(peer, "incomplete merkleblock %s, expected %zu more tx, got %s", u256hex(ctx->currentBlock->blockHash).c_str(),
                 array_count(ctx->currentBlockTxHashes), type);
        array_clear(ctx->currentBlockTxHashes);
        ctx->currentBlock = NULL;
        r = 0;
    }
    else if (strncmp(MSG_VERSION, type, 12) == 0) r = _BRPeerAcceptVersionMessage(peer, msg, msgLen);
    else if (strncmp(MSG_VERACK, type, 12) == 0) r = _BRPeerAcceptVerackMessage(peer, msg, msgLen);
    else if (strncmp(MSG_ADDR, type, 12) == 0) r = _BRPeerAcceptAddrMessage(peer, msg, msgLen);
    else if (strncmp(MSG_INV, type, 12) == 0) r = _BRPeerAcceptInvMessage(peer, msg, msgLen);
    else if (strncmp(MSG_TX, type, 12) == 0) r = _BRPeerAcceptTxMessage(peer, msg, msgLen);
    else if (strncmp(MSG_HEADERS, type, 12) == 0) r = _BRPeerAcceptHeadersMessage(peer, msg, msgLen);
    else if (strncmp(MSG_GETADDR, type, 12) == 0) r = _BRPeerAcceptGetaddrMessage(peer, msg, msgLen);
    else if (strncmp(MSG_GETDATA, type, 12) == 0) r = _BRPeerAcceptGetdataMessage(peer, msg, msgLen);
    else if (strncmp(MSG_NOTFOUND, type, 12) == 0) r = _BRPeerAcceptNotfoundMessage(peer, msg, msgLen);
    else if (strncmp(MSG_PING, type, 12) == 0) r = _BRPeerAcceptPingMessage(peer, msg, msgLen);
    else if (strncmp(MSG_PONG, type, 12) == 0) r = _BRPeerAcceptPongMessage(peer, msg, msgLen);
    else if (strncmp(MSG_MERKLEBLOCK, type, 12) == 0) r = _BRPeerAcceptMerkleblockMessage(peer, msg, msgLen);
    else if (strncmp(MSG_REJECT, type, 12) == 0) r = _BRPeerAcceptRejectMessage(peer, msg, msgLen);
    else if (strncmp(MSG_FEEFILTER, type, 12) == 0) r = _BRPeerAcceptFeeFilterMessage(peer, msg, msgLen);
    else peer_log(peer, "dropping %s, length %zu, not implemented", type, msgLen);

    return r;
}

static int _BRPeerOpenSocket(BRPeer *peer, int domain, double timeout, int *error)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    struct sockaddr_storage addr;
    struct timeval tv;
    fd_set fds;
    socklen_t addrLen, optLen;
    int count, arg = 0, err = 0, on = 1, r = 1;
    SOCKET sock = INVALID_SOCKET;

    ctx->lock.lock();
    sock = ctx->socket = socket(domain, SOCK_STREAM, IPPROTO_TCP);
    ctx->lock.unlock();

    if (sock == INVALID_SOCKET) {
        err = WSAGetLastError();
        r = 0;
    }
    else {
#ifdef WIN32
        int set = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else
        tv.tv_sec = 1; // one second timeout for send/receive, so thread doesn't block for too long
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (sockopt_arg_type) &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (sockopt_arg_type) &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (sockopt_arg_type) &on, sizeof(on));
#ifdef SO_NOSIGPIPE // BSD based systems have a SO_NOSIGPIPE socket option to supress SIGPIPE signals
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
#endif

#ifdef WIN32
        u_long nBlock = 1; // Non-Blocking
        if (ioctlsocket(sock, FIONBIO, &nBlock) == SOCKET_ERROR) {
            r = 0;
        }
#else
        arg = fcntl(sock, F_GETFL, NULL);
        if (arg < 0 || fcntl(sock, F_SETFL, arg | O_NONBLOCK) == SOCKET_ERROR) r = 0; // temporarily set socket non-blocking
#endif
        if (! r) err = WSAGetLastError();
    }

    if (r) {
        memset(&addr, 0, sizeof(addr));
        
        if (domain == PF_INET6) {
            ((struct sockaddr_in6 *)&addr)->sin6_family = AF_INET6;
            ((struct sockaddr_in6 *)&addr)->sin6_addr = *(struct in6_addr *)&peer->address;
            ((struct sockaddr_in6 *)&addr)->sin6_port = htons(peer->port);
            addrLen = sizeof(struct sockaddr_in6);
        }
        else {
            ((struct sockaddr_in *)&addr)->sin_family = AF_INET;
            ((struct sockaddr_in *)&addr)->sin_addr = *(struct in_addr *)&peer->address.u32[3];
            ((struct sockaddr_in *)&addr)->sin_port = htons(peer->port);
            addrLen = sizeof(struct sockaddr_in);
        }

#ifdef WIN32
        if (connect(sock, (struct sockaddr *)&addr, addrLen) == SOCKET_ERROR) err = WSAGetLastError();
#else
        if (connect(sock, (struct sockaddr *)&addr, addrLen) < 0) err = errno;
#endif

        // Connection in progress.
#ifdef WIN32
        if (err == WSAEINPROGRESS || err == WSAEWOULDBLOCK || err == WSAEINVAL) {
#else
        if (err == EINPROGRESS) {
#endif
            err = 0;
            optLen = sizeof(err);
            tv.tv_sec = timeout;
            tv.tv_usec = (long)(timeout*1000000) % 1000000;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            count = select(sock + 1, NULL, &fds, NULL, &tv);

            if (count <= 0 || getsockopt(sock, SOL_SOCKET, SO_ERROR, (sockopt_arg_type)&err, &optLen) == SOCKET_ERROR || err) {
                if (count == 0) err = ETIMEDOUT;
                if (count < 0 || ! err) err = WSAGetLastError();
                r = 0;
            }
        }
        else if (err) {
            r = 0;
        }

        if (r) peer_log(peer, "socket connected");

#ifdef WIN32
    u_long nZero = 0;
    ioctlsocket(sock, FIONBIO, &nZero);
#else
    fcntl(sock, F_SETFL, arg);
#endif
    }
    if (! r && err) peer_log(peer, "connect error: %d: %s", err, strerror(err));
    if (error && err) *error = err;
    return r;
}

static int _peerCheckAndGetSocket (BRPeerContext *ctx, SOCKET *socket) {
    int exists;

    ctx->lock.lock();
    exists = ctx->socket != INVALID_SOCKET;
    if (NULL != socket) *socket = ctx->socket;
    ctx->lock.unlock();

    return exists;
}

static SOCKET _peerGetSocket (BRPeerContext *ctx) {
    SOCKET socket;

    ctx->lock.lock();
    socket = ctx->socket;
    ctx->lock.unlock();

    return socket;
}

static double _peerGetDisconnectTime (BRPeerContext *ctx) {
    double value;

    ctx->lock.lock();
    value = ctx->disconnectTime;
    ctx->lock.unlock();

    return value;
}

static double _peerGetMempoolTime (BRPeerContext *ctx) {
    double value;

    ctx->lock.lock();
    value = ctx->mempoolTime;
    ctx->lock.unlock();

    return value;
}

static void *_peerThreadRoutine(void *arg)
{
    BRPeer *peer = (BRPeer *)arg;
    BRPeerContext *ctx = (BRPeerContext *)arg;
    int error = 0;
    SOCKET socket;

    threadCleanup guard(ctx->threadCleanup, ctx->info);

    int domain{PF_INET6};
    if (_BRPeerIsIPv4(peer)) {
        domain = PF_INET;
    }
    
    if (_BRPeerOpenSocket(peer, domain, CONNECT_TIMEOUT, &error)) {
        struct timeval tv;
        double time = 0, msgTimeout;
        uint8_t header[HEADER_LENGTH], *payload = (uint8_t *)malloc(0x1000);
        size_t len = 0, payloadLen = 0x1000;
        ssize_t n = 0;

        assert(payload != NULL);
        gettimeofday(&tv, NULL);
        ctx->startTime = tv.tv_sec + (double)tv.tv_usec/1000000;
        BRPeerSendVersionMessage(peer);

        while (_peerCheckAndGetSocket(ctx, &socket) && ! error) {
            len = 0;

            while (socket != INVALID_SOCKET && ! error && len < HEADER_LENGTH) {
//                n = read(socket, &header[len], sizeof(header) - len);
                n = recv(socket, (char*)&header[len], sizeof(header) - len, 0);
                if (n > 0) len += n;
                if (n == 0) error = ECONNRESET;
                if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK) error = WSAGetLastError();
                if (error == WSAENOTSOCK) {
                    ctx->lock.lock();
                    socket = ctx->socket = INVALID_SOCKET;
                    ctx->lock.unlock();
                    continue;
                }
                gettimeofday(&tv, NULL);
                time = tv.tv_sec + (double)tv.tv_usec/1000000;
                if (! error && time >= _peerGetDisconnectTime(ctx)) error = ETIMEDOUT;

                if (! error && time >= _peerGetMempoolTime(ctx)) {
                    peer_log(peer, "done waiting for mempool response");
                    BRPeerSendPing(peer, ctx->mempoolInfo, ctx->mempoolCallback);
                    ctx->mempoolCallback = NULL;

                    ctx->lock.lock();
                    ctx->mempoolTime = DBL_MAX;
                    ctx->lock.unlock();
                }

                while (sizeof(uint32_t) <= len && UInt32GetLE(header) != ctx->magicNumber) {
                    memmove(header, &header[1], --len); // consume one byte at a time until we find the magic number
                }

                socket = _peerGetSocket(ctx);
            }
            
            if (error) {
                peer_log(peer, "peer error: %d: %s", error, strerror(error));
            }
            else if (header[15] != 0) { // verify header type field is NULL terminated
                peer_log(peer, "malformed message header: type not NULL terminated");
                error = EPROTO;
            }
            else if (len == HEADER_LENGTH) {
                const char *type = (const char *)(&header[4]);
                uint32_t msgLen = UInt32GetLE(&header[16]);
                uint32_t checksum = UInt32GetLE(&header[20]);
                UInt256 hash;
                
                if (msgLen > MAX_MSG_LENGTH) { // check message length
                    peer_log(peer, "error reading %s, message length %" PRIu32 " is too long", type, msgLen);
                    error = EPROTO;
                }
                else {
                    if (msgLen > payloadLen) payload = (uint8_t *)realloc(payload, (payloadLen = msgLen));
                    assert(payload != NULL);
                    len = 0;
                    socket = _peerGetSocket(ctx);
                    msgTimeout = time + MESSAGE_TIMEOUT;
                    
                    while (socket != INVALID_SOCKET && ! error && len < msgLen) {
//                        n = read(socket, &payload[len], msgLen - len);
                        n = recv(socket, (char *) &payload[len], msgLen - len, 0);
                        if (n > 0) len += n;
                        if (n == 0) error = ECONNRESET;
                        if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK) error = WSAGetLastError();
                        if (error == WSAENOTSOCK) {
                            ctx->lock.lock();
                            socket = ctx->socket = INVALID_SOCKET;
                            ctx->lock.unlock();
                            continue;
                        }
                        gettimeofday(&tv, NULL);
                        time = tv.tv_sec + (double)tv.tv_usec/1000000;
                        if (n > 0) msgTimeout = time + MESSAGE_TIMEOUT;
                        if (! error && time >= msgTimeout) error = ETIMEDOUT;
                        socket = _peerGetSocket(ctx);
                    }
                    
                    if (error) {
                        peer_log(peer, "peer error: %d: %s", error, strerror(error));
                    }
                    else if (len == msgLen) {
                        BRSHA256_2(&hash, payload, msgLen);
                        
                        if (UInt32GetLE(&hash) != checksum) { // verify checksum
                            peer_log(peer, "error reading %s, invalid checksum %x, expected %x, payload length:%" PRIu32
                                     ", SHA256_2:%s", type, UInt32GetLE(&hash), checksum, msgLen, u256hex(hash).c_str());
                            error = EPROTO;
                        }
                        else if (! _BRPeerAcceptMessage(peer, payload, msgLen, type)) error = EPROTO;
                    }
                }
            }
        }
        
        free(payload);
    }

    ctx->lock.lock();
    socket = ctx->socket;
    ctx->status = BRPeerStatusDisconnected;
    ctx->lock.unlock();

    if (socket != INVALID_SOCKET) {
#ifdef WIN32
        closesocket(socket);
#else
        close(socket);
#endif
    }

    peer_log(peer, "disconnected");
    
    while (array_count(ctx->pongCallback) > 0) {
        void (*pongCallback)(void *, int) = ctx->pongCallback[0];
        void *pongInfo = ctx->pongInfo[0];
        
        array_rm(ctx->pongCallback, 0);
        array_rm(ctx->pongInfo, 0);
        if (pongCallback) pongCallback(pongInfo, 0);
    }

    if (ctx->mempoolCallback) ctx->mempoolCallback(ctx->mempoolInfo, 0);
    ctx->mempoolCallback = NULL;
    if (ctx->disconnected) ctx->disconnected(ctx->info, error);
    /// @todo spv: debug for proper thread cleanup!
//    guard.commit();
    return NULL; // detached threads don't need to return a value
}

static void _dummyThreadCleanup(void *info)
{
}

// returns a newly allocated BRPeer struct that must be freed by calling BRPeerFree()
BRPeer *BRPeerNew(uint32_t magicNumber)
{
    BRPeerContext *ctx = new BRPeerContext{};

    assert(ctx != NULL);
    ctx->magicNumber = magicNumber;
    array_new(ctx->useragent, 40);
    array_new(ctx->knownBlockHashes, 10);
    array_new(ctx->currentBlockTxHashes, 10);
    array_new(ctx->knownTxHashes, 10);
    ctx->knownTxHashSet = BRSetNew(BRTransactionHash, BRTransactionEq, 10);
    array_new(ctx->pongInfo, 10);
    array_new(ctx->pongCallback, 10);
    ctx->pingTime = DBL_MAX;
    ctx->mempoolTime = DBL_MAX;
    ctx->disconnectTime = DBL_MAX;
    ctx->socket = INVALID_SOCKET;
    ctx->threadCleanup = _dummyThreadCleanup;
    ctx->thread = 0;

    return &ctx->peer;
}

// info is a void pointer that will be passed along with each callback call
// void connected(void *) - called when peer handshake completes successfully
// void disconnected(void *, int) - called when peer connection is closed, error is an errno.h code
// void relayedPeers(void *, const BRPeer[], size_t) - called when an "addr" message is received from peer
// void relayedTx(void *, BRTransaction *) - called when a "tx" message is received from peer
// void hasTx(void *, UInt256 txHash) - called when an "inv" message with an already-known tx hash is received from peer
// void rejectedTx(void *, UInt256 txHash, uint8_t) - called when a "reject" message is received from peer
// void relayedBlock(void *, BRMerkleBlock *) - called when a "merkleblock" or "headers" message is received from peer
// void notfound(void *, const UInt256[], size_t, const UInt256[], size_t) - called when "notfound" message is received
// BRTransaction *requestedTx(void *, UInt256) - called when "getdata" message with a tx hash is received from peer
// int networkIsReachable(void *) - must return true when networking is available, false otherwise
// void threadCleanup(void *) - called before a thread terminates to faciliate any needed cleanup
void BRPeerSetCallbacks(BRPeer *peer, void *info,
                        void (*connected)(void *info),
                        void (*disconnected)(void *info, int error),
                        void (*relayedPeers)(void *info, const BRPeer peers[], size_t peersCount),
                        void (*relayedTx)(void *info, BRTransaction *tx),
                        void (*hasTx)(void *info, UInt256 txHash),
                        void (*rejectedTx)(void *info, UInt256 txHash, uint8_t code),
                        void (*relayedBlock)(void *info, BRMerkleBlock *block),
                        void (*notfound)(void *info, const UInt256 txHashes[], size_t txCount,
                                         const UInt256 blockHashes[], size_t blockCount),
                        void (*setFeePerKb)(void *info, uint64_t feePerKb),
                        BRTransaction *(*requestedTx)(void *info, UInt256 txHash),
                        int (*networkIsReachable)(void *info),
                        void (*threadCleanup)(void *info))
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    
    ctx->info = info;
    ctx->connected = connected;
    ctx->disconnected = disconnected;
    ctx->relayedPeers = relayedPeers;
    ctx->relayedTx = relayedTx;
    ctx->hasTx = hasTx;
    ctx->rejectedTx = rejectedTx;
    ctx->relayedBlock = relayedBlock;
    ctx->notfound = notfound;
    ctx->setFeePerKb = setFeePerKb;
    ctx->requestedTx = requestedTx;
    ctx->networkIsReachable = networkIsReachable;
    ctx->threadCleanup = (threadCleanup) ? threadCleanup : _dummyThreadCleanup;
}

// set earliestKeyTime to wallet creation time in order to speed up initial sync
void BRPeerSetEarliestKeyTime(BRPeer *peer, uint32_t earliestKeyTime)
{
    ((BRPeerContext *)peer)->earliestKeyTime = earliestKeyTime;
}

// call this when local block height changes (helps detect tarpit nodes)
void BRPeerSetCurrentBlockHeight(BRPeer *peer, uint32_t currentBlockHeight)
{
    ((BRPeerContext *)peer)->currentBlockHeight = currentBlockHeight;
}

// current connection status
BRPeerStatus BRPeerConnectStatus(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    BRPeerStatus status = BRPeerStatusDisconnected;

    ctx->lock.lock();
    status = ctx->status;
    ctx->lock.unlock();

    return status;
}

// open connection to peer and perform handshake
int BRPeerConnect(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    struct timeval tv;

    ctx->lock.lock();
    if (ctx->status == BRPeerStatusDisconnected || ctx->waitingForNetwork) { /// @todo check all places with "->waitingForNetwork"
        ctx->status = BRPeerStatusConnecting;
    
        if (ctx->networkIsReachable && ! ctx->networkIsReachable(ctx->info)) { // delay until network is reachable
            if (! ctx->waitingForNetwork) peer_log(peer, "waiting for network reachability");
            ctx->waitingForNetwork = 1;
        }
        else {
            peer_log(peer, "connecting");
            ctx->waitingForNetwork = 0;
            gettimeofday(&tv, NULL);

            // No race - set before the thread starts.
            ctx->disconnectTime = tv.tv_sec + (double)tv.tv_usec/1000000 + CONNECT_TIMEOUT;
            {
                boost::thread::attributes attrs;
                attrs.set_stack_size(PTHREAD_STACK_SIZE);
                ctx->thread.reset(new boost::thread(attrs, boost::bind(_peerThreadRoutine, peer)));
                ctx->thread->detach();
                ctx->lock.unlock();
                return 1; // thread successful run
            }
        }
    }
    ctx->lock.unlock();
    return 0;
}

// close connection to peer
void BRPeerDisconnect(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    SOCKET socket = INVALID_SOCKET;

    if (_peerCheckAndGetSocket(ctx, &socket)) {
        ctx->lock.lock();
        ctx->status = BRPeerStatusDisconnected;
        ctx->lock.unlock();

#ifdef WIN32
        if (shutdown(socket, SD_BOTH) == SOCKET_ERROR) peer_log(peer, "%s: %s", __func__, strerror(WSAGetLastError()));
        closesocket(socket);
#else
        if (shutdown(socket, SHUT_RDWR) == SOCKET_ERROR) peer_log(peer, "%s: %s", __func__, strerror(WSAGetLastError()));
        close(socket);
#endif
    }
}

// call this to (re)schedule a disconnect in the given number of seconds, or < 0 to cancel (useful for sync timeout)
void BRPeerScheduleDisconnect(BRPeer *peer, double seconds)
{
    BRPeerContext *ctx = ((BRPeerContext *)peer);
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    ctx->lock.lock();
    ctx->disconnectTime = (seconds < 0) ? DBL_MAX : tv.tv_sec + (double)tv.tv_usec/1000000 + seconds;
    ctx->lock.unlock();
}

// call this when wallet addresses need to be added to bloom filter
void BRPeerSetNeedsFilterUpdate(BRPeer *peer, int needsFilterUpdate)
{
    ((BRPeerContext *)peer)->needsFilterUpdate = needsFilterUpdate;
}

// display name of peer address
const char *BRPeerHost(BRPeer *peer)
{
    /// @attention it looks like 'cast' doesn't working!
    BRPeerContext *ctx = (BRPeerContext *)peer;
    if (!ctx)
        return "";

    if (ctx->host[0] == '\0') {
        if (_BRPeerIsIPv4(peer)) {
            inet_ntop(AF_INET, &peer->address.u32[3], ctx->host, sizeof(ctx->host));
        }
        else inet_ntop(AF_INET6, &peer->address, ctx->host, sizeof(ctx->host));
    }
    
    return ctx->host;
}

void BRPeerHostSafe(BRPeer const *peer, char *host)
{
    if (!peer) {
        host[0] = '\0';
        return;
    }

    if (_BRPeerIsIPv4(peer)) {
        inet_ntop(AF_INET, &peer->address.u32[3], host, INET6_ADDRSTRLEN);
    }
    else inet_ntop(AF_INET6, &peer->address, host, INET6_ADDRSTRLEN);
}

std::string BRPeerHostString(BRPeer *peer) {
    char host[INET6_ADDRSTRLEN];
    BRPeerHostSafe(peer, host);

    return std::string{host};
}

// connected peer version number
uint32_t BRPeerVersion(BRPeer *peer)
{
    return ((BRPeerContext *)peer)->version;
}

// connected peer user agent string
const char *BRPeerUserAgent(BRPeer *peer)
{
    return ((BRPeerContext *)peer)->useragent;
}

// best block height reported by connected peer
uint32_t BRPeerLastBlock(BRPeer *peer)
{
    return ((BRPeerContext *)peer)->lastblock;
}

// average ping time for connected peer
double BRPeerPingTime(BRPeer *peer)
{
    return ((BRPeerContext *)peer)->pingTime;
}

// minimum tx fee rate peer will accept
uint64_t BRPeerFeePerKb(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    uint64_t feePerKb;

    ctx->lock.lock();
    feePerKb = ctx->feePerKb;
    ctx->lock.unlock();
    return feePerKb;
}

#ifndef MSG_NOSIGNAL   // linux based systems have a MSG_NOSIGNAL send flag, useful for supressing SIGPIPE signals
#define MSG_NOSIGNAL 0 // set to 0 if undefined (BSD has the SO_NOSIGPIPE sockopt, and windows has no signals at all)
#endif

// sends a bitcoin protocol message to peer
void BRPeerSendMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen, const char *type)
{
    if (msgLen > MAX_MSG_LENGTH) {
        peer_log(peer, "failed to send %s, length %zu is too long", type, msgLen);
    }
    else {
        BRPeerContext *ctx = (BRPeerContext *)peer;
        uint8_t buf[HEADER_LENGTH + msgLen], hash[32];
        size_t off = 0;
        ssize_t n = 0;
        struct timeval tv;
        int error = 0;
        SOCKET socket = INVALID_SOCKET;
        
        UInt32SetLE(&buf[off], ctx->magicNumber);
        off += sizeof(uint32_t);
        strncpy((char *)&buf[off], type, 12);
        off += 12;
        UInt32SetLE(&buf[off], (uint32_t)msgLen);
        off += sizeof(uint32_t);
        BRSHA256_2(hash, msg, msgLen);
        memcpy(&buf[off], hash, sizeof(uint32_t));
        off += sizeof(uint32_t);
        memcpy(&buf[off], msg, msgLen);
        peer_log(peer, "sending %s", type);
        msgLen = 0;
        socket = _peerGetSocket(ctx);
        if (socket == INVALID_SOCKET) error = ENOTCONN;
        
        while (socket != INVALID_SOCKET && ! error && msgLen < sizeof(buf)) {
            n = send(socket, &buf[msgLen], sizeof(buf) - msgLen, MSG_NOSIGNAL);
            if (n >= 0) msgLen += n;
            if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK) error = WSAGetLastError();
            gettimeofday(&tv, NULL);
            if (! error && tv.tv_sec + (double)tv.tv_usec/1000000 >= _peerGetDisconnectTime(ctx)) error = ETIMEDOUT;
            socket = _peerGetSocket(ctx);
        }
        
        if (error) {
            peer_log(peer, "%s", strerror(error));
            BRPeerDisconnect(peer);
        }
    }
}

void BRPeerSendVersionMessage(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t off = 0, userAgentLen = strlen(USER_AGENT);
    uint8_t msg[80 + BRVarIntSize(userAgentLen) + userAgentLen + 5];
    
    UInt32SetLE(&msg[off], PROTOCOL_VERSION); // version
    off += sizeof(uint32_t);
    UInt64SetLE(&msg[off], ENABLED_SERVICES); // services
    off += sizeof(uint64_t);
    UInt64SetLE(&msg[off], time(NULL)); // timestamp
    off += sizeof(uint64_t);
    UInt64SetLE(&msg[off], peer->services); // services of remote peer
    off += sizeof(uint64_t);
    UInt128Set(&msg[off], peer->address); // IPv6 address of remote peer
    off += sizeof(UInt128);
    UInt16SetBE(&msg[off], peer->port); // port of remote peer
    off += sizeof(uint16_t);
    UInt64SetLE(&msg[off], ENABLED_SERVICES); // services
    off += sizeof(uint64_t);
    UInt128Set(&msg[off], LOCAL_HOST); // IPv4 mapped IPv6 header
    off += sizeof(UInt128);
    UInt16SetBE(&msg[off], peer->port);
    off += sizeof(uint16_t);
    ctx->nonce = ((uint64_t)BRRand(0) << 32) | (uint64_t)BRRand(0); // random nonce
    UInt64SetLE(&msg[off], ctx->nonce);
    off += sizeof(uint64_t);
    off += BRVarIntSet(&msg[off], (off <= sizeof(msg) ? sizeof(msg) - off : 0), userAgentLen);
    strncpy((char *)&msg[off], USER_AGENT, userAgentLen); // user agent string
    off += userAgentLen;
    UInt32SetLE(&msg[off], 0); // last block received
    off += sizeof(uint32_t);
    msg[off++] = 0; // relay transactions (0 for SPV bloom filter mode)
    BRPeerSendMessage(peer, msg, sizeof(msg), MSG_VERSION);
}

void BRPeerSendVerackMessage(BRPeer *peer)
{
    BRPeerSendMessage(peer, NULL, 0, MSG_VERACK);
    ((BRPeerContext *)peer)->sentVerack = 1;
}

void BRPeerSendAddr(BRPeer *peer)
{
    uint8_t msg[BRVarIntSize(0)];
    size_t msgLen = BRVarIntSet(msg, sizeof(msg), 0);
    
    //TODO: send peer addresses we know about
    BRPeerSendMessage(peer, msg, msgLen, MSG_ADDR);
}

void BRPeerSendFilterload(BRPeer *peer, const uint8_t *filter, size_t filterLen)
{
    ((BRPeerContext *)peer)->sentFilter = 1;
    ((BRPeerContext *)peer)->sentMempool = 0;
    BRPeerSendMessage(peer, filter, filterLen, MSG_FILTERLOAD);
}

void BRPeerSendMempool(BRPeer *peer, const UInt256 knownTxHashes[], size_t knownTxCount, void *info,
                       void (*completionCallback)(void *info, int success))
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    struct timeval tv;
    int sentMempool = ctx->sentMempool;
    
    ctx->sentMempool = 1;
    
    if (! sentMempool && ! ctx->mempoolCallback) {
        _BRPeerAddKnownTxHashes(peer, knownTxHashes, knownTxCount);
        
        if (completionCallback) {
            gettimeofday(&tv, NULL);

            ctx->lock.lock();
            ctx->mempoolTime = tv.tv_sec + (double)tv.tv_usec/1000000 + 10.0;
            ctx->lock.unlock();

            ctx->mempoolInfo = info;
            ctx->mempoolCallback = completionCallback;
        }
        
        BRPeerSendMessage(peer, NULL, 0, MSG_MEMPOOL);
    }
    else {
        peer_log(peer, "mempool request already sent");
        if (completionCallback) completionCallback(info, 0);
    }
}

void BRPeerSendGetheaders(BRPeer *peer, const UInt256 locators[], size_t locatorsCount, UInt256 hashStop)
{
    size_t i, off = 0;
    size_t msgLen = sizeof(uint32_t) + BRVarIntSize(locatorsCount) + sizeof(*locators)*locatorsCount + sizeof(hashStop);
    uint8_t msg[msgLen];
    
    UInt32SetLE(&msg[off], PROTOCOL_VERSION);
    off += sizeof(uint32_t);
    off += BRVarIntSet(&msg[off], (off <= msgLen ? msgLen - off : 0), locatorsCount);

    for (i = 0; i < locatorsCount; i++) {
        UInt256Set(&msg[off], locators[i]);
        off += sizeof(UInt256);
    }

    UInt256Set(&msg[off], hashStop);
    off += sizeof(UInt256);

    if (locatorsCount > 0) {
        peer_log(peer, "calling getheaders with %zu locators: [%s,%s %s]", locatorsCount, u256hex(locators[0]).c_str(),
                 (locatorsCount > 2 ? " ...," : ""), (locatorsCount > 1 ? u256hex(locators[locatorsCount - 1]).c_str() : ""));
        BRPeerSendMessage(peer, msg, off, MSG_GETHEADERS);
    }
}

void BRPeerSendGetblocks(BRPeer *peer, const UInt256 locators[], size_t locatorsCount, UInt256 hashStop)
{
    size_t i, off = 0;
    size_t msgLen = sizeof(uint32_t) + BRVarIntSize(locatorsCount) + sizeof(*locators)*locatorsCount + sizeof(hashStop);
    uint8_t msg[msgLen];
    
    UInt32SetLE(&msg[off], PROTOCOL_VERSION);
    off += sizeof(uint32_t);
    off += BRVarIntSet(&msg[off], (off <= msgLen ? msgLen - off : 0), locatorsCount);
    
    for (i = 0; i < locatorsCount; i++) {
        UInt256Set(&msg[off], locators[i]);
        off += sizeof(UInt256);
    }
    
    UInt256Set(&msg[off], hashStop);
    off += sizeof(UInt256);
    
    if (locatorsCount > 0) {
        peer_log(peer, "calling getblocks with %zu locators: [%s,%s %s]", locatorsCount, u256hex(locators[0]).c_str(),
                 (locatorsCount > 2 ? " ...," : ""), (locatorsCount > 1 ? u256hex(locators[locatorsCount - 1]).c_str() : ""));
        BRPeerSendMessage(peer, msg, off, MSG_GETBLOCKS);
    }
}

void BRPeerSendInv(BRPeer *peer, const UInt256 txHashes[], size_t txCount)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t knownCount = array_count(ctx->knownTxHashes);

    _BRPeerAddKnownTxHashes(peer, txHashes, txCount);
    txCount = array_count(ctx->knownTxHashes) - knownCount;

    if (txCount > 0) {
        size_t i, off = 0, msgLen = BRVarIntSize(txCount) + (sizeof(uint32_t) + sizeof(*txHashes))*txCount;
        uint8_t msg[msgLen];
        
        off += BRVarIntSet(&msg[off], (off <= msgLen ? msgLen - off : 0), txCount);
        
        for (i = 0; i < txCount; i++) {
            UInt32SetLE(&msg[off], inv_tx);
            off += sizeof(uint32_t);
            UInt256Set(&msg[off], ctx->knownTxHashes[knownCount + i]);
            off += sizeof(UInt256);
        }

        BRPeerSendMessage(peer, msg, off, MSG_INV);
    }
}

void BRPeerSendGetdata(BRPeer *peer, const UInt256 txHashes[], size_t txCount, const UInt256 blockHashes[],
                       size_t blockCount)
{
    size_t i, off = 0, count = txCount + blockCount;
    
    if (count > MAX_GETDATA_HASHES) { // limit total hash count to MAX_GETDATA_HASHES
        peer_log(peer, "couldn't send getdata, %zu is too many items, max is %d", count, MAX_GETDATA_HASHES);
    }
    else if (count > 0) {
        size_t msgLen = BRVarIntSize(count) + (sizeof(uint32_t) + sizeof(UInt256))*(count);
        uint8_t msg[msgLen];

        off += BRVarIntSet(&msg[off], (off <= msgLen ? msgLen - off : 0), count);
        
        for (i = 0; i < txCount; i++) {
            UInt32SetLE(&msg[off], inv_witness_tx);
            off += sizeof(uint32_t);
            UInt256Set(&msg[off], txHashes[i]);
            off += sizeof(UInt256);
        }
        
        for (i = 0; i < blockCount; i++) {
            UInt32SetLE(&msg[off], inv_filtered_block);
            off += sizeof(uint32_t);
            UInt256Set(&msg[off], blockHashes[i]);
            off += sizeof(UInt256);
        }
        
        ((BRPeerContext *)peer)->sentGetdata = 1;
        BRPeerSendMessage(peer, msg, off, MSG_GETDATA);
    }
}

void BRPeerSendGetaddr(BRPeer *peer)
{
    ((BRPeerContext *)peer)->sentGetaddr = 1;
    BRPeerSendMessage(peer, NULL, 0, MSG_GETADDR);
}

void BRPeerSendPing(BRPeer *peer, void *info, void (*pongCallback)(void *info, int success))
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    uint8_t msg[sizeof(uint64_t)];
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    ctx->startTime = tv.tv_sec + (double)tv.tv_usec/1000000;
    array_add(ctx->pongInfo, info);
    array_add(ctx->pongCallback, pongCallback);
    UInt64SetLE(msg, ctx->nonce);
    BRPeerSendMessage(peer, msg, sizeof(msg), MSG_PING);
}

// useful to get additional tx after a bloom filter update
void BRPeerRerequestBlocks(BRPeer *peer, UInt256 fromBlock)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    size_t i = array_count(ctx->knownBlockHashes);
    
    while (i > 0 && ! UInt256Eq(ctx->knownBlockHashes[i - 1], fromBlock)) i--;
   
    if (i > 0) {
        array_rm_range(ctx->knownBlockHashes, 0, i - 1);
        peer_log(peer, "re-requesting %zu block(s)", array_count(ctx->knownBlockHashes));
        BRPeerSendGetdata(peer, NULL, 0, ctx->knownBlockHashes, array_count(ctx->knownBlockHashes));
    }
}

void BRPeerFree(BRPeer *peer)
{
    BRPeerContext *ctx = (BRPeerContext *)peer;
    
    if (ctx->useragent) array_free(ctx->useragent);
    if (ctx->currentBlockTxHashes) array_free(ctx->currentBlockTxHashes);
    if (ctx->knownBlockHashes) array_free(ctx->knownBlockHashes);
    if (ctx->knownTxHashes) array_free(ctx->knownTxHashes);
    if (ctx->knownTxHashSet) BRSetFree(ctx->knownTxHashSet);
    if (ctx->pongCallback) array_free(ctx->pongCallback);
    if (ctx->pongInfo) array_free(ctx->pongInfo);
    
    delete ctx;
}

void BRPeerAcceptMessageTest(BRPeer *peer, const uint8_t *msg, size_t msgLen, const char *type)
{
    _BRPeerAcceptMessage(peer, msg, msgLen, type);
}

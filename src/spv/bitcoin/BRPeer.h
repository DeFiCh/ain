//
//  BRPeer.h
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

#ifndef BRPeer_h
#define BRPeer_h

#include <stddef.h>
#include <inttypes.h>
#include <mutex>

#include <spv/support/BRAddress.h>
#include <spv/support/BRInt.h>
#include <spv/bitcoin/BRTransaction.h>
#include <spv/bitcoin/BRMerkleBlock.h>

/// define logs
#define console_peer_log(peer, ...) { \
    if (spv_log2console) { \
        log_mutex.lock(); \
        if (peer) { \
            char host[INET6_ADDRSTRLEN]; \
            BRPeerHostSafe((BRPeer const*)peer, host); \
            _peer_log("%s:%" PRIu16 " " _va_first(__VA_ARGS__, NULL) "\n", host, ((BRPeer const*)peer)->port, _va_rest(__VA_ARGS__, NULL)); \
        } else { \
            _peer_log(_va_first(__VA_ARGS__, NULL) "\n", _va_rest(__VA_ARGS__, NULL)); \
        } \
        log_mutex.unlock(); \
    } \
}

#define peer_log(peer, ...) { console_peer_log(peer, __VA_ARGS__); file_peer_log(peer, __VA_ARGS__); }

#define _va_first(first, ...) first
#define _va_rest(first, ...) __VA_ARGS__

#define file_peer_log(peer, ...) { \
    log_mutex.lock(); \
    FILE* logfile = NULL; \
    if (spv_logfilename && (logfile = fopen(spv_logfilename, "a"))) { \
        setbuf(logfile, NULL); \
        if (peer) { \
            char host[INET6_ADDRSTRLEN]; \
            BRPeerHostSafe((BRPeer const*)peer, host); \
            fprintf(logfile, "%s:%" PRIu16 " " _va_first(__VA_ARGS__, NULL) "\n", host, ((BRPeer const*)peer)->port, _va_rest(__VA_ARGS__, NULL)); \
        } else { \
            fprintf(logfile, _va_first(__VA_ARGS__, NULL) "\n", _va_rest(__VA_ARGS__, NULL)); \
        } \
        fclose(logfile); \
    } \
    log_mutex.unlock(); \
}
//#endif

#if defined(TARGET_OS_MAC)
#include <Foundation/Foundation.h>
#define _peer_log(...) NSLog(__VA_ARGS__)
#elif defined(__ANDROID__)
#include <android/log.h>
#define _peer_log(...) __android_log_print(ANDROID_LOG_INFO, "bread", __VA_ARGS__)
#else
#include <stdio.h>
#define _peer_log(...) printf(__VA_ARGS__)
#endif

extern char const * spv_logfilename;
extern int spv_log2console;
extern std::mutex log_mutex;

#ifndef INET6_ADDRSTRLEN // defined in netinet/in.h
#define INET6_ADDRSTRLEN 46
#endif

#define SERVICES_NODE_NETWORK 0x01 // services value indicating a node carries full blocks, not just headers
#define SERVICES_NODE_BLOOM   0x04 // BIP111: https://github.com/bitcoin/bips/blob/master/bip-0111.mediawiki
#define SERVICES_NODE_WITNESS 0x08 // BIP144: https://github.com/bitcoin/bips/blob/master/bip-0144.mediawiki
#define SERVICES_NODE_BCASH   0x20 // https://github.com/Bitcoin-UAHF/spec/blob/master/uahf-technical-spec.md
    
#define BR_VERSION "2.1"
#define USER_AGENT "/bread:" BR_VERSION "/"

#define REJECT_INVALID     0x10 // transaction is invalid for some reason (invalid signature, output value > input, etc)
#define REJECT_SPENT       0x12 // an input is already spent
#define REJECT_NONSTANDARD 0x40 // not mined/relayed because it is "non-standard" (type or version unknown by server)
#define REJECT_DUST        0x41 // one or more output amounts are below the 'dust' threshold
#define REJECT_LOWFEE      0x42 // transaction does not have enough fee/priority to be relayed or mined

#define BR_PROTOCOL_VERSION   70013
#define BR_MIN_PROTO_VERSION  70002 // peers earlier than this protocol version not supported (need v0.9 txFee relay rules)

typedef enum {
    BRPeerStatusDisconnected = 0,
    BRPeerStatusConnecting,
    BRPeerStatusConnected
} BRPeerStatus;

struct BRPeerStruct {
    UInt128 address; // IPv6 address of peer
    uint16_t port; // port number for peer connection
    uint64_t services; // bitcoin network services supported by peer
    uint64_t timestamp; // timestamp reported by peer
    uint8_t flags; // scratch variable
};
typedef struct BRPeerStruct BRPeer;

struct threadCleanup {
    threadCleanup(void (*cleanup)(void *), void *info) : m_cleanup(cleanup), m_info(info), m_commit( false ) {}
    ~threadCleanup()
    {
        if (!m_commit && m_cleanup)
            m_cleanup(m_info);
    }
    void commit() { m_commit = true; }

private:
    void (*m_cleanup)(void *info);
    void *m_info;
    bool m_commit;
};

constexpr BRPeer BR_PEER_NONE = { UINT128_ZERO, 0, 0, 0, 0 };

// NOTE: BRPeer functions are not thread-safe

// returns a newly allocated BRPeer struct that must be freed by calling BRPeerFree()
BRPeer *BRPeerNew(uint32_t magicNumber);

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
                        void (*threadCleanup)(void *info));

// set earliestKeyTime to wallet creation time in order to speed up initial sync
void BRPeerSetEarliestKeyTime(BRPeer *peer, uint32_t earliestKeyTime);

// call this when local best block height changes (helps detect tarpit nodes)
void BRPeerSetCurrentBlockHeight(BRPeer *peer, uint32_t currentBlockHeight);

// current connection status
BRPeerStatus BRPeerConnectStatus(BRPeer *peer);

// open connection to peer and perform handshake
int BRPeerConnect(BRPeer *peer);

// close connection to peer
void BRPeerDisconnect(BRPeer *peer);

// call this to (re)schedule a disconnect in the given number of seconds, or < 0 to cancel (useful for sync timeout)
void BRPeerScheduleDisconnect(BRPeer *peer, double seconds);

// set this to true when wallet addresses need to be added to bloom filter
void BRPeerSetNeedsFilterUpdate(BRPeer *peer, int needsFilterUpdate);

// display name of peer address
const char *BRPeerHost(BRPeer *peer);
void BRPeerHostSafe(BRPeer const *peer, char *host);
std::string BRPeerHostString(BRPeer *peer);

// connected peer version number
uint32_t BRPeerVersion(BRPeer *peer);

// connected peer user agent string
const char *BRPeerUserAgent(BRPeer *peer);

// best block height reported by connected peer
uint32_t BRPeerLastBlock(BRPeer *peer);

// minimum tx fee rate peer will accept
uint64_t BRPeerFeePerKb(BRPeer *peer);

// average ping time for connected peer
double BRPeerPingTime(BRPeer *peer);

// sends a bitcoin protocol message to peer
void BRPeerSendMessage(BRPeer *peer, const uint8_t *msg, size_t msgLen, const char *type);
void BRPeerSendFilterload(BRPeer *peer, const uint8_t *filter, size_t filterLen);
void BRPeerSendMempool(BRPeer *peer, const UInt256 knownTxHashes[], size_t knownTxCount, void *info,
                       void (*completionCallback)(void *info, int success));
void BRPeerSendGetheaders(BRPeer *peer, const UInt256 locators[], size_t locatorsCount, UInt256 hashStop);
void BRPeerSendGetblocks(BRPeer *peer, const UInt256 locators[], size_t locatorsCount, UInt256 hashStop);
void BRPeerSendInv(BRPeer *peer, const UInt256 txHashes[], size_t txCount);
void BRPeerSendGetdata(BRPeer *peer, const UInt256 txHashes[], size_t txCount, const UInt256 blockHashes[],
                       size_t blockCount);
void BRPeerSendGetaddr(BRPeer *peer);
void BRPeerSendPing(BRPeer *peer, void *info, void (*pongCallback)(void *info, int success));

// useful to get additional tx after a bloom filter update
void BRPeerRerequestBlocks(BRPeer *peer, UInt256 fromBlock);

// returns a hash value for peer suitable for use in a hashtable
inline static size_t BRPeerHash(const void *peer)
{
    uint32_t address = ((const BRPeer *)peer)->address.u32[3], port = ((const BRPeer *)peer)->port;
 
    // (((FNV_OFFSET xor address)*FNV_PRIME) xor port)*FNV_PRIME
    return (size_t)((((0x811C9dc5 ^ address)*0x01000193) ^ port)*0x01000193);
}

// true if a and b have the same address and port
inline static int BRPeerEq(const void *peer, const void *otherPeer)
{
    return (peer == otherPeer ||
            (UInt128Eq(((const BRPeer *)peer)->address, ((const BRPeer *)otherPeer)->address) &&
             ((const BRPeer *)peer)->port == ((const BRPeer *)otherPeer)->port));
}

// frees memory allocated for peer
void BRPeerFree(BRPeer *peer);

#endif // BRPeer_h

// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_NET_PROCESSING_H
#define DEFI_NET_PROCESSING_H

#include <net.h>
#include <validationinterface.h>
#include <consensus/params.h>
#include <sync.h>

extern CCriticalSection cs_main;

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
static const bool DEFAULT_PEERBLOOMFILTERS = false;

/** The maximum rate of address records we're willing to process on average. Can be bypassed using
 *  the NetPermissionFlags::Addr permission. */
static constexpr double MAX_ADDR_RATE_PER_SECOND{0.1};

/** The maximum rate of address records we're willing to process on average. Can be bypassed using
 *  the NetPermissionFlags::Addr permission. */
static constexpr double MAX_ADDR_RATE_PER_SECOND_REGTEST{100000};

/** The soft limit of the address processing token bucket (the regular MAX_ADDR_RATE_PER_SECOND
 *  based increments won't go above this, but the MAX_ADDR_TO_SEND increment following GETADDR
 *  is exempt from this limit. */
static constexpr size_t MAX_ADDR_PROCESSING_TOKEN_BUCKET{MAX_ADDR_TO_SEND};

extern double maxAddrRatePerSecond;
extern size_t maxAddrProcessingTokenBucket;

class PeerLogicValidation final : public CValidationInterface, public NetEventsInterface {
private:
    CConnman* const connman;
    BanMan* const m_banman;

    bool CheckIfBanned(CNode* pnode) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
public:
    PeerLogicValidation(CConnman* connman, BanMan* banman, CScheduler &scheduler);

    /**
     * Overridden from CValidationInterface.
     */
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexConnected, const std::vector<CTransactionRef>& vtxConflicted) override;
    /**
     * Overridden from CValidationInterface.
     */
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;
    /**
     * Overridden from CValidationInterface.
     */
    void BlockChecked(const CBlock& block, const CValidationState& state) override;
    /**
     * Overridden from CValidationInterface.
     */
    void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock>& pblock) override;

    /** Initialize a peer by adding it to mapNodeState and pushing a message requesting its version */
    void InitializeNode(CNode* pnode) override;
    /** Handle removal of a peer by updating various state and removing it from mapNodeState */
    void FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime) override;
    /**
    * Process protocol messages received from a given node
    *
    * @param[in]   pfrom           The node which we have received messages from.
    * @param[in]   interrupt       Interrupt condition for processing threads
    */
    bool ProcessMessages(CNode* pfrom, std::atomic<bool>& interrupt) override;
    /**
    * Send queued protocol messages to be sent to a give node.
    *
    * @param[in]   pto             The node which we are sending messages to.
    * @return                      True if there is more work to be done
    */
    bool SendMessages(CNode* pto) override EXCLUSIVE_LOCKS_REQUIRED(pto->cs_sendProcessing);

    /** Consider evicting an outbound peer based on the amount of time they've been behind our tip */
    void ConsiderEviction(CNode *pto, int64_t time_in_seconds) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound */
    void CheckForStaleTipAndEvictPeers(const Consensus::Params &consensusParams);
    /** If we have extra outbound peers, try to disconnect the one with the oldest block announcement */
    void EvictExtraOutboundPeers(int64_t time_in_seconds) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    int64_t m_stale_tip_check_time; //!< Next time to check for stale tip
};

struct CNodeStateStats {
    int nMisbehavior = 0;
    int nSyncHeight = -1;
    int nCommonHeight = -1;
    std::vector<int> vHeightInFlight;
};

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

/** Relay transaction to every node */
void RelayTransaction(const uint256&, const CConnman& connman);

/** Relay getauths to every node */
void RelayGetAnchorAuths(const uint256& lowHash, const uint256& highHash, CConnman& connman);

/** Relay anchor auth to every node, possible skipping the source (rebroadcasting) */
void RelayAnchorAuths(std::vector<CInv> const & vInv, CConnman& connman, CNode* skipNode = nullptr);

void RelayAnchorConfirm(const uint256&, CConnman& connman, CNode* skipNode = nullptr);

void ResyncHeaders(CConnman& connman);

#endif // DEFI_NET_PROCESSING_H

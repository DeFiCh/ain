// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_CONSENSUS_PARAMS_H
#define DEFI_CONSENSUS_PARAMS_H

#include <amount.h>
#include <masternodes/communityaccounttypes.h>
#include <script/standard.h>
#include <uint256.h>
#include <limits>
#include <map>
#include <string>
#include <arith_uint256.h>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;

    /** Constant for nTimeout very far in the future. */
    static constexpr int64_t NO_TIMEOUT = std::numeric_limits<int64_t>::max();

    /** Special value for nStartTime indicating that the deployment is always active.
     *  This is useful for testing, as it means tests don't need to deal with the activation
     *  process (which takes at least 3 BIP9 intervals). Only tests that specifically test the
     *  behaviour during activation cannot use this. */
    static constexpr int64_t ALWAYS_ACTIVE = -1;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    CAmount baseBlockSubsidy;
    CScript foundationShareScript;
    uint32_t foundationShare;
    std::set<CScript> foundationMembers;
    /* Block hash that is excepted from BIP16 enforcement */
    uint256 BIP16Exception;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block height at which CSV (BIP68, BIP112 and BIP113) becomes active */
    int CSVHeight;
    /** Block height at which Segwit (BIP141, BIP143 and BIP147) becomes active.
     * Note that segwit v0 script rules are enforced on all blocks except the
     * BIP 16 exception blocks. */
    int SegwitHeight;
    /** Block height at which tokens, liquidity pools and new block rewards becomes active */
    int AMKHeight;
    /** What exactly? Changes to mint DAT, new updatetokens? */
    int BayfrontHeight;
    int BayfrontMarinaHeight;
    int BayfrontGardensHeight;
    /** Third major fork. */
    int ClarkeQuayHeight;
    /** Fourth major fork **/
    int DakotaHeight;
    /** Foundation share after AMK, normalized to COIN = 100% */
    CAmount foundationShareDFIP1;

    /** Proof of stake parameters */
    struct PoS {
        uint256 diffLimit;
        int64_t nTargetTimespan;
        int64_t nTargetSpacing;
        bool fAllowMinDifficultyBlocks;
        bool fNoRetargeting;

        int64_t DifficultyAdjustmentInterval() const { return nTargetTimespan / nTargetSpacing; }

        arith_uint256 interestAtoms = arith_uint256{10000000000000000ull};
        bool allowMintingWithoutPeers;
        int coinstakeMaturity = 500;
    };
    PoS pos;

    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nTargetTimespan / nTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];

    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;

    struct MnParams {
        CAmount creationFee;
        CAmount collateralAmount;
        int activationDelay;
        int resignDelay; // same delay for criminal ban
        int historyFrame;
        int anchoringTeamSize;
        int anchoringFrequency; // create every Nth block
        int anchoringLag;       // older than Tip() by
    };
    MnParams mn;

    struct TokenParams {
        CAmount creationFee;
        CAmount collateralAmount;
    };
    TokenParams token;

    struct SpvParams {
        CAmount creationFee;
        CAmount anchorSubsidy;
        int subsidyIncreasePeriod;
        CAmount subsidyIncreaseValue;
        std::string wallet_xpub;
        std::string anchors_address;
        int minConfirmations;
    };
    SpvParams spv;

    std::map<CommunityAccountType, CAmount> nonUtxoBlockSubsidies;
};
} // namespace Consensus

#endif // DEFI_CONSENSUS_PARAMS_H

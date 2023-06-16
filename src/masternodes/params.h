// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_PARAMS_H
#define DEFI_MASTERNODES_PARAMS_H

#include <amount.h>
#include <masternodes/communityaccounttypes.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>

#include <map>
#include <string>

const auto SMART_CONTRACT_DFIP_2201 = "DFIP2201";
const auto SMART_CONTRACT_DFIP_2203 = "DFIP2203";
const auto SMART_CONTRACT_DFIP2206F = "DFIP2206F";

namespace DeFiConsensus {
    struct Params {
        uint32_t emissionReductionPeriod;
        uint32_t emissionReductionAmount;
        CScript foundationShareScript;
        uint32_t foundationShare;
        CAmount foundationShareDFIP1; // Foundation share after AMK, normalized to COIN = 100%
        std::set<CScript> foundationMembers;
        std::set<CScript> accountDestruction;

        /** Previous burn address to transfer tokens from */
        CScript retiredBurnAddress;

        /** Address to hold unused emission */
        CScript unusedEmission;

        /** Proof of stake parameters */
        struct PoS {
            uint256 diffLimit;
            int64_t nTargetTimespan;
            int64_t nTargetTimespanV2;
            int64_t nTargetSpacing;
            int64_t nStakeMinAge;
            int64_t nStakeMaxAge;
            bool fAllowMinDifficultyBlocks;
            bool fNoRetargeting;

            [[nodiscard]] int64_t DifficultyAdjustmentInterval() const { return nTargetTimespan / nTargetSpacing; }
            [[nodiscard]] int64_t DifficultyAdjustmentIntervalV2() const { return nTargetTimespanV2 / nTargetSpacing; }

            arith_uint256 interestAtoms = arith_uint256{10000000000000000ull};
            bool allowMintingWithoutPeers;
        };
        PoS pos;

        [[nodiscard]] uint32_t blocksPerDay() const {
            static const uint32_t blocks = 60 * 60 * 24 / pos.nTargetSpacing;
            return blocks;
        }

        [[nodiscard]] uint32_t blocksCollateralizationRatioCalculation() const {
            static const uint32_t blocks = 15 * 60 / pos.nTargetSpacing;
            return blocks;
        }

        [[nodiscard]] uint32_t blocksCollateralAuction() const {
            static const uint32_t blocks = 6 * 60 * 60 / pos.nTargetSpacing;
            return blocks;
        }

        struct CPropsParams {
            struct CPropsSpecs {
                CAmount fee;
                CAmount minimumFee;
                CAmount emergencyFee;
                CAmount approvalThreshold;
            } cfp, brp, voc;
            uint32_t votingPeriod;
            uint32_t emergencyPeriod;
            CAmount quorum;
            CAmount feeBurnPct;
        };
        CPropsParams props;

        /** Struct to hold percentages for coinbase distribution.
         *  Percentages are calculated out of 10000 */
        struct CoinbaseDistribution {
            uint32_t masternode; // Mining reward
            uint32_t community; // Community fund
            uint32_t anchor; // Anchor reward
            uint32_t liquidity; // Liquidity mining
            uint32_t loan; // Loans
            uint32_t options; // Options
            uint32_t unallocated; // Reserved
        };
        CoinbaseDistribution dist;

        std::map<std::string, CScript> smartContracts;

        std::map<CommunityAccountType, CAmount> nonUtxoBlockSubsidies;
        std::map<CommunityAccountType, uint32_t> newNonUTXOSubsidies;

        struct MnParams {
            CAmount creationFee;
            CAmount collateralAmount;
            CAmount collateralAmountDakota;
            int activationDelay;
            int resignDelay;
            int newActivationDelay;
            int newResignDelay;
            int anchoringTeamSize;
            int anchoringFrequency; // create every Nth block

            int anchoringTimeDepth; // Min age of anchored blocks
            int anchoringAdditionalTimeDepth; // Additional min age of anchored blocks
            int anchoringTeamChange; // How many blocks before team is changed
        };
        MnParams mn;

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

        struct TokenParams {
            CAmount creationFee;
            CAmount collateralAmount;
        };
        TokenParams token;
    };
}

class CDeFiParams
{
public:
    [[nodiscard]] const DeFiConsensus::Params& GetConsensus() const { return consensus; }

    /** Whether it is possible to mine blocks on demand (no retargeting) */
    [[nodiscard]] bool MineBlocksOnDemand() const { return consensus.pos.fNoRetargeting; }
    [[nodiscard]] const std::set<CKeyID>& GetGenesisTeam() const { return genesisTeam; }

protected:
    DeFiConsensus::Params consensus;

    std::set<CKeyID> genesisTeam;
};

/**
 * Creates and returns a std::unique_ptr<CDeFiParams> of the chosen chain.
 * @returns a CDeFiParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CDeFiParams> CreateDeFiChainParams(const std::string& chain);

/**
 * Return the currently selected parameters.
 */
const CDeFiParams &DeFiParams();

/**
 * Sets the params returned by DeFiParams().
 */
void SelectDeFiParams(const std::string &chain);

#endif  // DEFI_MASTERNODES_PARAMS_H

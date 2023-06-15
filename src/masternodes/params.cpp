// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/params.h>

#include <chainparamsbase.h>
#include <consensus/tx_check.h>
#include <key_io.h>
#include <masternodes/mn_checks.h>

#include <vector>

class CMainnetDeFiParams : public CDeFiParams {
public:
    CMainnetDeFiParams() {
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("dZcHjYhKtEM88TtZLjp314H2xZjkztXtRc", Params()));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        consensus.foundationMembers.clear();
        consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination("dJEbxbfufyPF14SC93yxiquECEfq4YSd9L", Params())));
        consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination("8bL7jZe2Nk5EhqFA6yuf8HPre3M6eewkqj", Params())));
        consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination("8UhqhhiwtUuEqCD7HsekUsgYRuz115eLiQ", Params())));

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("dJEbxbfufyPF14SC93yxiquECEfq4YSd9L", Params())));
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("8UAhRuUFCyFUHEPD7qvtj8Zy2HxF5HH5nb", Params())));

        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("8defichainDSTBurnAddressXXXXaCAuTq", Params()));

        // Destination for unused emission
        consensus.unusedEmission = GetScriptForDestination(DecodeDestination("df1qlwvtdrh4a4zln3k56rqnx8chu8t0sqx36syaea", Params()));

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30; // seconds
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false; // only for regtest
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = false; // don't mint if no peers connected

        consensus.props.cfp.fee = COIN / 100; // 1%
        consensus.props.cfp.minimumFee = 10 * COIN; // 10 DFI
        consensus.props.cfp.approvalThreshold = COIN / 2; // vote pass with over 50% majority
        consensus.props.voc.fee = 100 * COIN;
        consensus.props.voc.emergencyFee = 10000 * COIN;
        consensus.props.voc.approvalThreshold = 66670000; // vote pass with over 66.67% majority
        consensus.props.quorum = COIN / 100; // 1% of the masternodes must vote
        consensus.props.votingPeriod = 130000; // tally votes every 130K blocks
        consensus.props.emergencyPeriod = 8640;
        consensus.props.feeBurnPct = COIN / 2;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI of 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN /10 / 200);       // 0.1 DFI of 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.loan = 2468; // 24.68%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Loan, consensus.dist.loan);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::CommunityDevFunds, consensus.dist.community);

        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));

        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.wallet_xpub = "xpub68vVWYqkpwYT8ZxBhN2buFMTPNFzrJQV19QZmhuwQqKQZHxcXVg36GZCrwPhb7KPpivsGXxvd7g82sJXYnKNqi2ZuHJvhqcwF418YEfGMrv";
        consensus.spv.anchors_address = "1FtZwEZKknoquUb6DyQHFZ6g6oomXJYEcb";
        consensus.spv.minConfirmations = 6;

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("8J846CKFF83Jcj5m4EReJmxiaJ6Jy1Y6Ea", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("8bzHwhaF2MaVs4owRvpWtZQVug3mKuJji2", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("8cHaEaqRsz7fgW1eAjeroB5Bau5NfJNbtk", Params())));

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;

        UpdateActivationParametersFromArgs();
    }

    void UpdateActivationParametersFromArgs();
};

class CTestnetDeFiParams : public CDeFiParams {
public:
    CTestnetDeFiParams() {
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("7Q2nZCcKnxiRiHSNQtLB27RA5efxm2cE7w", Params()));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        consensus.foundationMembers.clear();
        consensus.foundationMembers.insert(consensus.foundationShareScript);

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("trnZD2qPU1c3WryBi8sWX16mEaq9WkGHeg", Params()))); // cVUZfDj1B1o7eVhxuZr8FQLh626KceiGQhZ8G6YCUdeW3CAV49ti
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("75jrurn8tkDLhZ3YPyzhk6D9kc1a4hBrmM", Params()))); // cSmsVpoR6dSW5hPNKeGwC561gXHXcksdQb2yAFQdjbSp5MUyzZqr

        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("7DefichainDSTBurnAddressXXXXXzS4Hi", Params()));

        // Destination for unused emission
        consensus.unusedEmission = GetScriptForDestination(DecodeDestination("7HYC4WVAjJ5BGVobwbGTEzWJU8tzY3Kcjq", Params()));

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30;
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false;
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true;

        consensus.props.cfp.fee = COIN / 100; // 1%
        consensus.props.cfp.minimumFee = 10 * COIN; // 10 DFI
        consensus.props.cfp.approvalThreshold = COIN / 2; // vote pass with over 50%
        consensus.props.voc.fee = 50 * COIN;
        consensus.props.voc.emergencyFee = 10000 * COIN;
        consensus.props.voc.approvalThreshold = 66670000; // vote pass with over 66.67%
        consensus.props.quorum = COIN / 100; // 1% of the masternodes must vote
        consensus.props.votingPeriod = 70000; // tally votes every 70K blocks
        consensus.props.emergencyPeriod = 8640;
        consensus.props.feeBurnPct = COIN / 2;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI @ 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 200);       // 0.1 DFI @ 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.loan = 2468; // 24.68%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Loan, consensus.dist.loan);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::CommunityDevFunds, consensus.dist.community);

        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));

        consensus.spv.wallet_xpub = "tpubD9RkyYW1ixvD9vXVpYB1ka8rPZJaEQoKraYN7YnxbBxxsRYEMZgRTDRGEo1MzQd7r5KWxH8eRaQDVDaDuT4GnWgGd17xbk6An6JMdN4dwsY";
        consensus.spv.anchors_address = "mpAkq2LyaUvKrJm2agbswrkn3QG9febnqL";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7KEu9JMKCx6aJ9wyg138W3p42rjg19DR5D", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("78MWNEcAAJxihddCw1UnZD8T7fMWmUuBro", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7MYdTGv3bv3z65ai6y5J1NFiARg8PYu4hK", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7GULFtS6LuJfJEikByKKg8psscg84jnfHs", Params())));

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;
    }
};


class CChangiDeFiParams : public CDeFiParams {
public:
    CChangiDeFiParams() {
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("7Q2nZCcKnxiRiHSNQtLB27RA5efxm2cE7w", Params()));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        consensus.foundationMembers.clear();
        consensus.foundationMembers.insert(consensus.foundationShareScript);

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("trnZD2qPU1c3WryBi8sWX16mEaq9WkGHeg", Params()))); // cVUZfDj1B1o7eVhxuZr8FQLh626KceiGQhZ8G6YCUdeW3CAV49ti
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("75jrurn8tkDLhZ3YPyzhk6D9kc1a4hBrmM", Params()))); // cSmsVpoR6dSW5hPNKeGwC561gXHXcksdQb2yAFQdjbSp5MUyzZqr

        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("7DefichainDSTBurnAddressXXXXXzS4Hi", Params()));

        // Destination for unused emission
        consensus.unusedEmission = GetScriptForDestination(DecodeDestination("7HYC4WVAjJ5BGVobwbGTEzWJU8tzY3Kcjq", Params()));

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30;
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false;
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true;

        consensus.props.cfp.fee = COIN / 100; // 1%
        consensus.props.cfp.minimumFee = 10 * COIN; // 10 DFI
        consensus.props.cfp.approvalThreshold = COIN / 2; // vote pass with over 50%
        consensus.props.voc.fee = 50 * COIN;
        consensus.props.voc.emergencyFee = 10000 * COIN;
        consensus.props.voc.approvalThreshold = 66670000; // vote pass with over 66.67%
        consensus.props.quorum = COIN / 100; // 1% of the masternodes must vote
        consensus.props.votingPeriod = 70000; // tally votes every 70K blocks
        consensus.props.emergencyPeriod = 8640;
        consensus.props.feeBurnPct = COIN / 2;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI @ 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 200);       // 0.1 DFI @ 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.loan = 2468; // 24.68%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Loan, consensus.dist.loan);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::CommunityDevFunds, consensus.dist.community);

        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));

        consensus.spv.wallet_xpub = "tpubD9RkyYW1ixvD9vXVpYB1ka8rPZJaEQoKraYN7YnxbBxxsRYEMZgRTDRGEo1MzQd7r5KWxH8eRaQDVDaDuT4GnWgGd17xbk6An6JMdN4dwsY";
        consensus.spv.anchors_address = "mpAkq2LyaUvKrJm2agbswrkn3QG9febnqL";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7KEu9JMKCx6aJ9wyg138W3p42rjg19DR5D", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("78MWNEcAAJxihddCw1UnZD8T7fMWmUuBro", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7MYdTGv3bv3z65ai6y5J1NFiARg8PYu4hK", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7GULFtS6LuJfJEikByKKg8psscg84jnfHs", Params())));

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;
    }
};

class CDevnetDeFiParams : public CDeFiParams {
public:
    CDevnetDeFiParams() {
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("7Q2nZCcKnxiRiHSNQtLB27RA5efxm2cE7w", Params()));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        consensus.foundationMembers.clear();
        consensus.foundationMembers.insert(consensus.foundationShareScript);

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("trnZD2qPU1c3WryBi8sWX16mEaq9WkGHeg", Params()))); // cVUZfDj1B1o7eVhxuZr8FQLh626KceiGQhZ8G6YCUdeW3CAV49ti
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("75jrurn8tkDLhZ3YPyzhk6D9kc1a4hBrmM", Params()))); // cSmsVpoR6dSW5hPNKeGwC561gXHXcksdQb2yAFQdjbSp5MUyzZqr

        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("7DefichainDSTBurnAddressXXXXXzS4Hi", Params()));

        // Destination for unused emission
        consensus.unusedEmission = GetScriptForDestination(DecodeDestination("7HYC4WVAjJ5BGVobwbGTEzWJU8tzY3Kcjq", Params()));

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30;
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false;
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true;

        consensus.props.cfp.fee = COIN / 100; // 1%
        consensus.props.cfp.minimumFee = 10 * COIN; // 10 DFI
        consensus.props.cfp.approvalThreshold = COIN / 2; // vote pass with over 50%
        consensus.props.voc.fee = 50 * COIN;
        consensus.props.voc.emergencyFee = 10000 * COIN;
        consensus.props.voc.approvalThreshold = 66670000; // vote pass with over 66.67%
        consensus.props.quorum = COIN / 100; // 1% of the masternodes must vote
        consensus.props.votingPeriod = 70000; // tally votes every 70K blocks
        consensus.props.emergencyPeriod = 8640;
        consensus.props.feeBurnPct = COIN / 2;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI @ 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 200);       // 0.1 DFI @ 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.loan = 2468; // 24.68%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Loan, consensus.dist.loan);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::CommunityDevFunds, consensus.dist.community);

        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));

        consensus.spv.wallet_xpub = "tpubD9RkyYW1ixvD9vXVpYB1ka8rPZJaEQoKraYN7YnxbBxxsRYEMZgRTDRGEo1MzQd7r5KWxH8eRaQDVDaDuT4GnWgGd17xbk6An6JMdN4dwsY"; /// @note devnet matter
        consensus.spv.anchors_address = "mpAkq2LyaUvKrJm2agbswrkn3QG9febnqL"; /// @note devnet matter
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7KEu9JMKCx6aJ9wyg138W3p42rjg19DR5D", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("78MWNEcAAJxihddCw1UnZD8T7fMWmUuBro", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7MYdTGv3bv3z65ai6y5J1NFiARg8PYu4hK", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("7GULFtS6LuJfJEikByKKg8psscg84jnfHs", Params())));

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;
    }
};

class CRegtestDeFiParams : public CDeFiParams {
public:
    explicit CRegtestDeFiParams() {
        consensus.emissionReductionPeriod = gArgs.GetBoolArg("-jellyfish_regtest", false) ? 32690 : 150;
        consensus.emissionReductionAmount = 1658; // 1.658%

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS", Params())); // cMv1JaaZ9Mbb3M3oNmcFvko8p7EcHJ8XD7RCQjzNaMs7BWRVZTyR
        consensus.foundationShare = 0; // old style - just percents // stil zero here to not broke old tests
        consensus.foundationShareDFIP1 = 19 * COIN / 10 / 50; // 1.9 DFI @ 50 per block (rate normalized to (COIN == 100%)

        // now it is for devnet and regtest only, 2 first and 2 last of genesis MNs acts as foundation members
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU", Params())));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7", Params())));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny", Params())));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu", Params())));

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("2MxJf6Ak8MGrLoGdekrU6AusW29szZUFphH", Params())));
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("mxiaFfAnCoXEUy4RW8NgsQM7yU5YRCiFSh", Params())));

        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("mfdefichainDSTBurnAddressXXXZcE1vs", Params()));

        // Destination for unused emission
        consensus.unusedEmission = GetScriptForDestination(DecodeDestination("mkzZWPwBVgdnwLSmXKW5SuUFMpm6C5ZPcJ", Params())); // cUUj4d9tkgJGwGBF7VwFvCpcFMuEpC8tYbduaCDexKMx8A8ntL7C

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.pos.nTargetTimespanV2 = 14 * 24 * 60 * 60; // two weeks
        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = true; // only for regtest
        consensus.pos.fNoRetargeting = true; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true; // don't mint if no peers connected

        consensus.props.cfp.fee = COIN / 100; // 1%
        consensus.props.cfp.minimumFee = 10 * COIN; // 10 DFI
        consensus.props.cfp.approvalThreshold = COIN / 2; // vote pass with over 50% majority
        consensus.props.voc.fee = 5 * COIN;
        consensus.props.voc.emergencyFee = 10000 * COIN;
        consensus.props.voc.approvalThreshold = 66670000; // vote pass with over 66.67% majority
        consensus.props.quorum = COIN / 100; // 1% of the masternodes must vote
        consensus.props.votingPeriod = 70; // tally votes every 70 blocks
        consensus.props.emergencyPeriod = 50;
        consensus.props.feeBurnPct = COIN / 2;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 10 * COIN / 50); // normalized to (COIN == 100%) // 10 per block
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 50);       // 0.1 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.loan = 2468; // 24.68%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Loan, consensus.dist.loan);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::CommunityDevFunds, consensus.dist.community);

        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));

        consensus.spv.wallet_xpub = "tpubDA2Mn6LMJ35tYaA1Noxirw2WDzmgKEDKLRbSs2nwF8TTsm2iB6hBJmNjAAEbDqYzZLdThLykWDcytGzKDrjUzR9ZxdmSbFz7rt18vFRYjt9";
        consensus.spv.anchors_address = "n1h1kShnyiw3qRR6MM1FnwShaNVoVwBTnF";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 6;

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 20;
        consensus.mn.resignDelay = 10;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 1 * COIN;
        consensus.mn.collateralAmount = 10 * COIN;
        consensus.mn.collateralAmountDakota = 2 * COIN;
        consensus.mn.anchoringTeamSize = 3;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60;
        consensus.mn.anchoringAdditionalTimeDepth = 15 * 60; // 15 minutes
        consensus.mn.anchoringTeamChange = 15; // Number of blocks

        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("mps7BdmwEF2vQ9DREDyNPibqsuSRZ8LuwQ", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("mtbWisYQmw9wcaecvmExeuixG7rYGqKEU4", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("n1n6Z5Zdoku4oUnrXeQ2feLz3t7jmVLG9t", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("mzqdipBJcKX9rXXxcxw2kTHC3Xjzd3siKg", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("mk5DkY4qcV6CUpuxDVyD3AHzRq5XK9kbRN", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("bcrt1qmfvw3dp3u6fdvqkdc0y3lr0e596le9cf22vtsv", Params())));
        genesisTeam.insert(GetCKeyIDFromDestination(DecodeDestination("bcrt1qurwyhta75n2g75u2u5nds9p6w9v62y8wr40d2r", Params())));

        consensus.token.creationFee = 1 * COIN;
        consensus.token.collateralAmount = 10 * COIN;

        UpdateActivationParametersFromArgs();
    }

    void UpdateActivationParametersFromArgs();
};

void CMainnetDeFiParams::UpdateActivationParametersFromArgs() {
    if (fMockNetwork) {
        auto sMockFoundationPubKey = gArgs.GetArg("-mocknet-key", "");
        auto nMockBlockTimeSecs = gArgs.GetArg("-mocknet-blocktime", 30);

        // Add additional foundation members here for testing
        if (!sMockFoundationPubKey.empty()) {
            consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination(sMockFoundationPubKey, Params())));
            LogPrintf("mocknet: key: %s\n", sMockFoundationPubKey);
        }

        // End of args. Perform sane set below.
        consensus.pos.nTargetSpacing = nMockBlockTimeSecs;
        consensus.pos.nTargetTimespanV2 = 10 * consensus.pos.nTargetSpacing;
        consensus.pos.allowMintingWithoutPeers = true;

        LogPrintf("mocknet: block-time: %s secs\n", consensus.pos.nTargetSpacing);
    }
}

void CRegtestDeFiParams::UpdateActivationParametersFromArgs()
{
    if (gArgs.GetBoolArg("-simulatemainnet", false)) {
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30; // seconds
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        LogPrintf("conf: simulatemainnet: true (Re-adjusted: blocktime=%ds, difficultytimespan=%ds)\n",
                  consensus.pos.nTargetSpacing, consensus.pos.nTargetTimespanV2);
    }
}

static std::unique_ptr<const CDeFiParams> defiChainParams;

std::unique_ptr<CDeFiParams> CreateDeFiChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CDeFiParams>(new CMainnetDeFiParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CDeFiParams>(new CTestnetDeFiParams());
    else if (chain == CBaseChainParams::CHANGI)
        return std::unique_ptr<CDeFiParams>(new CChangiDeFiParams());
    else if (chain == CBaseChainParams::DEVNET)
        return std::unique_ptr<CDeFiParams>(new CDevnetDeFiParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CDeFiParams>(new CRegtestDeFiParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

const CDeFiParams &DeFiParams() {
    assert(defiChainParams);
    return *defiChainParams;
}

void SelectDeFiParams(const std::string& network) {
    defiChainParams = CreateDeFiChainParams(network);
}

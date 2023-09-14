// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <ain_rs_exports.h>
#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <ffi/cxx.h>
#include <masternodes/anchors.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/validation.h>
#include <memory.h>
#include <net.h>
#include <node/transaction.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pos.h>
#include <pos_kernel.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/validation.h>
#include <wallet/wallet.h>
#include <ffi/ffihelpers.h>

#include <algorithm>
#include <random>
#include <utility>

struct EvmPackageContext {
    // Used to track EVM TX fee and TX hash by sender and nonce.
    std::map<EvmAddressWithNonce, std::pair<uint64_t, uint256>> evmFeeMap;
    // Quick lookup for EVM fee map
    using EVMFeeMapIterator = std::map<EvmAddressWithNonce, std::pair<uint64_t, uint256>>::iterator;
    std::map<uint256, EVMFeeMapIterator> feeMapLookup{};
    // Quick lookup for mempool time order map
    using TimeMapIterator = std::multimap<int64_t, CTxMemPool::txiter>::iterator;
    std::map<uint256, TimeMapIterator> timeOrderLookup{};
    // Keep track of EVM entries that failed nonce check
    std::multimap<uint64_t, CTxMemPool::txiter> failedNonces;
    // Used for replacement Eth TXs ordered by time and nonce
    std::map<std::pair<uint64_t, uint64_t>, CTxMemPool::txiter> replaceByFee;
    // Variable to tally total gas used in the block
    uint64_t totalGas{};
    // Used to track gas fees of EVM TXs
    std::map<uint256, uint64_t> evmGasFees;
};

struct EvmTxPreApplyContext {
    CTxMemPool::txiter& txIter;
    std::vector<unsigned char>& txMetadata;
    CustomTxType txType;
    int height;
    uint64_t evmQueueId;
    EvmPackageContext& pkgCtx;
    std::set<uint256>& checkedTXEntries;
    CTxMemPool::setEntries& failedTxEntries;
};

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.pos.fAllowMinDifficultyBlocks)
        pblock->nBits = pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options()
{
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT - 4000, options.nBlockMaxWeight));
}

static BlockAssembler::Options DefaultOptions()
{
    // Block resource limits
    // If -blockmaxweight is not given, limit to DEFAULT_BLOCK_MAX_WEIGHT
    BlockAssembler::Options options;
    options.nBlockMaxWeight = gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
    CAmount n = 0;
    if (gArgs.IsArgSet("-blockmintxfee") && ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n)) {
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
    return options;
}

BlockAssembler::BlockAssembler(const CChainParams& params) : BlockAssembler(params, DefaultOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

ResVal<std::unique_ptr<CBlockTemplate>> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, int64_t blockTime, const EvmAddressData& evmBeneficiary)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate = std::make_unique<CBlockTemplate>();

    if (!pblocktemplate)
        return Res::Err("Failed to create block template");
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1);       // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    const auto myIDs = pcustomcsview->AmIOperator();
    if (!myIDs) {
        return Res::Err("Node has no operators");
    }
    const auto nodePtr = pcustomcsview->GetMasternode(myIDs->second);
    if (!nodePtr || !nodePtr->IsActive(nHeight, *pcustomcsview)) {
        return Res::Err("Node is not active");
    }

    auto consensus = chainparams.GetConsensus();
    pblock->nVersion = ComputeBlockVersion(pindexPrev, consensus);
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = blockTime;
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization).
    // Note that the mempool would accept transactions with witness data before
    // IsWitnessEnabled, but we would only ever mine blocks after IsWitnessEnabled
    // unless there is a massive block reorganization with the witness softfork
    // not activated.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = IsWitnessEnabled(pindexPrev, consensus);

    const auto txVersion = GetTransactionVersion(nHeight);

    // Skip on main as fix to avoid merkle root error. Allow on other networks for testing.
    if (Params().NetworkIDString() != CBaseChainParams::MAIN ||
        (Params().NetworkIDString() == CBaseChainParams::MAIN && nHeight >= chainparams.GetConsensus().EunosKampungHeight)) {
        CTeamView::CTeam currentTeam;
        if (const auto team = pcustomcsview->GetConfirmTeam(pindexPrev->nHeight)) {
            currentTeam = *team;
        }

        auto confirms = panchorAwaitingConfirms->GetQuorumFor(currentTeam);

        bool createAnchorReward{false};

        // No new anchors until we hit fork height, no new confirms should be found before fork.
        if (pindexPrev->nHeight >= consensus.DakotaHeight && confirms.size() > 0) {
            // Make sure anchor block height and hash exist in chain.
            CBlockIndex* anchorIndex = ::ChainActive()[confirms[0].anchorHeight];
            if (anchorIndex && anchorIndex->GetBlockHash() == confirms[0].dfiBlockHash) {
                createAnchorReward = true;
            }
        }

        if (createAnchorReward) {
            CAnchorFinalizationMessagePlus finMsg{confirms[0]};

            for (auto const& msg : confirms) {
                finMsg.sigs.push_back(msg.signature);
            }

            CDataStream metadata(DfAnchorFinalizeTxMarkerPlus, SER_NETWORK, PROTOCOL_VERSION);
            metadata << finMsg;

            CTxDestination destination;
            if (nHeight < consensus.NextNetworkUpgradeHeight) {
                destination = FromOrDefaultKeyIDToDestination(finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNOwnerKeyType);
            } else {
                destination = FromOrDefaultKeyIDToDestination(finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNRewardKeyType);
            }

            if (IsValidDestination(destination)) {
                CMutableTransaction mTx(txVersion);
                mTx.vin.resize(1);
                mTx.vin[0].prevout.SetNull();
                mTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
                mTx.vout.resize(2);
                mTx.vout[0].scriptPubKey = CScript() << OP_RETURN << ToByteVector(metadata);
                mTx.vout[0].nValue = 0;
                mTx.vout[1].scriptPubKey = GetScriptForDestination(destination);
                mTx.vout[1].nValue = pcustomcsview->GetCommunityBalance(
                    CommunityAccountType::AnchorReward); // do not reset it, so it will occur on connectblock

                auto rewardTx = pcustomcsview->GetRewardForAnchor(finMsg.btcTxHash);
                if (!rewardTx) {
                    pblock->vtx.push_back(MakeTransactionRef(std::move(mTx)));
                    pblocktemplate->vTxFees.push_back(0);
                    pblocktemplate->vTxSigOpsCost.push_back(
                        WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx.back()));
                }
            }
        }
    }

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    CCustomCSView mnview(*pcustomcsview);
    if (!blockTime) {
        UpdateTime(pblock, consensus, pindexPrev); // update time before tx packaging
    }
    auto isEvmEnabledForBlock = IsEVMEnabled(nHeight, mnview, consensus);

    bool timeOrdering{false};
    if (txOrdering == MIXED_ORDERING) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<unsigned long long> dis;

        if (dis(rd) % 2 == 0)
            timeOrdering = false;
        else
            timeOrdering = true;
    } else if (txOrdering == ENTRYTIME_ORDERING) {
        timeOrdering = true;
    } else if (txOrdering == FEE_ORDERING) {
        timeOrdering = false;
    }

    uint64_t evmQueueId{};
    if (isEvmEnabledForBlock) {
        auto r = XResultValueLogged(evm_unsafe_try_create_queue(result));
        if (!r) return Res::Err("Failed to create queue");
        evmQueueId = *r;
    }

    std::map<uint256, CAmount> txFees;

    if (timeOrdering) {
        addPackageTxs<entry_time>(nPackagesSelected, nDescendantsUpdated, nHeight, mnview, evmQueueId, txFees, isEvmEnabledForBlock);
    } else {
        addPackageTxs<ancestor_score>(nPackagesSelected, nDescendantsUpdated, nHeight, mnview, evmQueueId, txFees, isEvmEnabledForBlock);
    }

    XVM xvm{};
    if (isEvmEnabledForBlock) {
        auto res = XResultValueLogged(evm_unsafe_try_construct_block_in_q(result, evmQueueId, pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensus), evmBeneficiary, blockTime, nHeight, static_cast<std::size_t>(reinterpret_cast<uintptr_t>(&mnview))));

        auto r = XResultStatusLogged(evm_unsafe_try_remove_queue(result, evmQueueId));
        if (!r) return Res::Err("Failed to remove queue");

        if (!res) return Res::Err("Failed to construct block");
        auto blockResult = *res;

        xvm = XVM{0, {0, std::string(blockResult.block_hash.data(), blockResult.block_hash.length()).substr(2), blockResult.total_burnt_fees, blockResult.total_priority_fees, evmBeneficiary}};
    }

    // TXs for the creationTx field in new tokens created via token split
    if (nHeight >= chainparams.GetConsensus().FortCanningCrunchHeight) {
        const auto attributes = mnview.GetAttributes();
        if (attributes) {
            CDataStructureV0 splitKey{AttributeTypes::Oracles, OracleIDs::Splits, static_cast<uint32_t>(nHeight)};
            const auto splits = attributes->GetValue(splitKey, OracleSplits{});

            for (const auto& [id, multiplier] : splits) {
                uint32_t entries{1};
                mnview.ForEachPoolPair([&, id = id](DCT_ID const& poolId, const CPoolPair& pool) {
                    if (pool.idTokenA.v == id || pool.idTokenB.v == id) {
                        const auto tokenA = mnview.GetToken(pool.idTokenA);
                        const auto tokenB = mnview.GetToken(pool.idTokenB);
                        assert(tokenA);
                        assert(tokenB);
                        if ((tokenA->destructionHeight == -1 && tokenA->destructionTx == uint256{}) &&
                            (tokenB->destructionHeight == -1 && tokenB->destructionTx == uint256{})) {
                            ++entries;
                        }
                    }
                    return true;
                });

                for (uint32_t i{0}; i < entries; ++i) {
                    CDataStream metadata(DfTokenSplitMarker, SER_NETWORK, PROTOCOL_VERSION);
                    metadata << i << id << multiplier;

                    CMutableTransaction mTx(txVersion);
                    mTx.vin.resize(1);
                    mTx.vin[0].prevout.SetNull();
                    mTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
                    mTx.vout.resize(1);
                    mTx.vout[0].scriptPubKey = CScript() << OP_RETURN << ToByteVector(metadata);
                    mTx.vout[0].nValue = 0;
                    pblock->vtx.push_back(MakeTransactionRef(std::move(mTx)));
                    pblocktemplate->vTxFees.push_back(0);
                    pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx.back()));
                }
            }
        }
    }

    int64_t nTime1 = GetTimeMicros();

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx(txVersion);
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    CAmount blockReward = GetBlockSubsidy(nHeight, consensus);
    coinbaseTx.vout[0].nValue = nFees + blockReward;

    if (nHeight >= consensus.EunosHeight) {
        auto foundationValue = CalculateCoinbaseReward(blockReward, consensus.dist.community);
        if (nHeight < consensus.GrandCentralHeight) {
            coinbaseTx.vout.resize(2);
            // Community payment always expected
            coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
            coinbaseTx.vout[1].nValue = foundationValue;
        }

        // Explicitly set miner reward
        if (nHeight >= consensus.FortCanningHeight) {
            coinbaseTx.vout[0].nValue = nFees + CalculateCoinbaseReward(blockReward, consensus.dist.masternode);
        } else {
            coinbaseTx.vout[0].nValue = CalculateCoinbaseReward(blockReward, consensus.dist.masternode);
        }

        if (isEvmEnabledForBlock) {
            if (xvm.evm.blockHash.empty()) {
                return Res::Err("EVM block hash is null");
            }
            const auto headerIndex = coinbaseTx.vout.size();
            coinbaseTx.vout.resize(headerIndex + 1);
            coinbaseTx.vout[headerIndex].nValue = 0;
            coinbaseTx.vout[headerIndex].scriptPubKey = xvm.ToScript();
        }

        LogPrint(BCLog::STAKING, "%s: post Eunos logic. Block reward %d Miner share %d foundation share %d\n",
            __func__, blockReward, coinbaseTx.vout[0].nValue, foundationValue);
    } else if (nHeight >= consensus.AMKHeight) {
        // assume community non-utxo funding:
        for (const auto& kv : consensus.blockTokenRewardsLegacy) {
            coinbaseTx.vout[0].nValue -= blockReward * kv.second / COIN;
        }
        // Pinch off foundation share
        if (!consensus.foundationShareScript.empty() && consensus.foundationShareDFIP1 != 0) {
            coinbaseTx.vout.resize(2);
            coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
            coinbaseTx.vout[1].nValue = blockReward * consensus.foundationShareDFIP1 / COIN; // the main difference is that new FS is a %% from "base" block reward and no fees involved
            coinbaseTx.vout[0].nValue -= coinbaseTx.vout[1].nValue;

            LogPrint(BCLog::STAKING, "%s: post AMK logic, foundation share %d\n", __func__, coinbaseTx.vout[1].nValue);
        }
    } else { // pre-AMK logic:
        // Pinch off foundation share
        CAmount foundationsReward = coinbaseTx.vout[0].nValue * consensus.foundationShare / 100;
        if (!consensus.foundationShareScript.empty() && consensus.foundationShare != 0) {
            if (pcustomcsview->GetFoundationsDebt() < foundationsReward) {
                coinbaseTx.vout.resize(2);
                coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
                coinbaseTx.vout[1].nValue = foundationsReward - pcustomcsview->GetFoundationsDebt();
                coinbaseTx.vout[0].nValue -= coinbaseTx.vout[1].nValue;

                LogPrint(BCLog::STAKING, "%s: pre AMK logic, foundation share %d\n", __func__, coinbaseTx.vout[1].nValue);
            } else {
                pcustomcsview->SetFoundationsDebt(pcustomcsview->GetFoundationsDebt() - foundationsReward);
            }
        }
    }

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));

    pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, consensus);
    pblocktemplate->vTxFees[0] = -nFees;

    LogPrint(BCLog::STAKING, "%s: block weight: %u txs: %u fees: %ld sigops %d\n", __func__, GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    pblock->deprecatedHeight = pindexPrev->nHeight + 1;
    pblock->nBits = pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensus);
    if (!blockTime) {
        pblock->stakeModifier = pos::ComputeStakeModifier(pindexPrev->stakeModifier, myIDs->first);
    }

    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    if (nHeight >= chainparams.GetConsensus().EunosHeight && nHeight < chainparams.GetConsensus().EunosKampungHeight) {
        // includes coinbase account changes
        ApplyGeneralCoinbaseTx(mnview, *(pblock->vtx[0]), nHeight, nFees, chainparams.GetConsensus());
        pblock->hashMerkleRoot = Hash2(pblock->hashMerkleRoot, mnview.MerkleRoot());
    }

    LogPrint(BCLog::BENCH, "%s packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", __func__, 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return {std::move(pblocktemplate), Res::Ok()};
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end();) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
            CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
            iter->GetTx().GetHash().ToString());
    }
}

void BlockAssembler::RemoveFromBlock(CTxMemPool::txiter iter)
{
    const auto& tx = iter->GetTx();
    for (auto blockIt = pblock->vtx.begin(); blockIt != pblock->vtx.end(); ++blockIt) {
        auto current = *blockIt;
        if (!current || current->GetHash() != tx.GetHash()) {
            continue;
        }

        pblock->vtx.erase(blockIt);
        if (pblocktemplate) {
            auto& vTxFees = pblocktemplate->vTxFees;
            for (auto it = vTxFees.begin(); it != vTxFees.end(); ++it) {
                if (*it == iter->GetFee()) {
                    vTxFees.erase(it);
                    break;
                }
            }
            auto& vTxSigOpsCost = pblocktemplate->vTxSigOpsCost;
            for (auto it = vTxSigOpsCost.begin(); it != vTxSigOpsCost.end(); ++it) {
                if (*it == iter->GetSigOpCost()) {
                    vTxSigOpsCost.erase(it);
                    break;
                }
            }
        }
        nBlockWeight -= iter->GetTxWeight();
        --nBlockTx;
        nBlockSigOpsCost -= iter->GetSigOpCost();
        nFees -= iter->GetFee();
        inBlock.erase(iter);
        break;
    }
}

void BlockAssembler::RemoveFromBlock(const CTxMemPool::setEntries& txIterSet, bool removeDescendants)
{
    if (txIterSet.empty()) {
        return;
    }
    std::set<uint256> txHashes;
    for (auto iter : txIterSet) {
        RemoveFromBlock(iter);
        txHashes.insert(iter->GetTx().GetHash());
    }
    if (!removeDescendants) return;
    CTxMemPool::setEntries descendantTxsToErase;
    for (const auto& txIter : inBlock) {
        auto& tx = txIter->GetTx();
        for (const auto& vin : tx.vin) {
            if (txHashes.count(vin.prevout.hash)) {
                descendantTxsToErase.insert(txIter);
            }
        }
    }
    RemoveFromBlock(descendantTxsToErase, true);
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
    indexed_modified_transaction_set& mapModifiedTxSet)
{
    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTxSet.find(desc);
            if (mit == mapModifiedTxSet.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTxSet.insert(modEntry);
            } else {
                mapModifiedTxSet.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTxSet (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTxSet and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTxSet and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set& mapModifiedTxSet, CTxMemPool::setEntries& failedTxSet)
{
    assert(it != mempool.mapTx.end());
    return mapModifiedTxSet.count(it) || inBlock.count(it) || failedTxSet.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByEntryTime());
}

bool BlockAssembler::EvmTxPreapply(const EvmTxPreApplyContext& ctx)
{
    auto& txType = ctx.txType;
    auto txMessage = customTypeToMessage(txType);
    auto& txIter = ctx.txIter;
    auto& evmQueueId = ctx.evmQueueId;
    auto& metadata = ctx.txMetadata;
    auto& height = ctx.height;
    auto& checkedDfTxHashSet = ctx.checkedTXEntries;
    auto& failedTxSet = ctx.failedTxEntries;
    auto& pkgCtx = ctx.pkgCtx;
    auto& failedNonces = pkgCtx.failedNonces;
    auto& replaceByFee = pkgCtx.replaceByFee;
    auto& totalGas = pkgCtx.totalGas;
    auto& timeOrderLookup = pkgCtx.timeOrderLookup;

    if (!CustomMetadataParse(height, Params().GetConsensus(), metadata, txMessage)) {
        return false;
    }

    CrossBoundaryResult result;
    ValidateTxMiner txResult;
    if (ctx.txType == CustomTxType::EvmTx) {
        const auto obj = std::get<CEvmTxMessage>(txMessage);
        txResult = evm_unsafe_try_validate_raw_tx_in_q(result, evmQueueId, HexStr(obj.evmTx), true);
        if (!result.ok) {
            return false;
        }
        if (txResult.higher_nonce) {
            if (!failedTxSet.count(txIter)) {
                failedNonces.emplace(txResult.nonce, txIter);
            }
            return false;
        }
    } else if (ctx.txType == CustomTxType::TransferDomain) {
        const auto obj = std::get<CTransferDomainMessage>(txMessage);
        if (obj.transfers.size() != 1) {
            return false;
        }

        std::string evmTx;
        if (obj.transfers[0].first.domain == static_cast<uint8_t>(VMDomain::DVM) && obj.transfers[0].second.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            evmTx = HexStr(obj.transfers[0].second.data);
        }
        else if (obj.transfers[0].first.domain == static_cast<uint8_t>(VMDomain::EVM) && obj.transfers[0].second.domain == static_cast<uint8_t>(VMDomain::DVM)) {
            evmTx = HexStr(obj.transfers[0].first.data);
        }
        auto senderInfo = evm_try_get_tx_sender_info_from_raw_tx(result, evmTx);
        if (!result.ok) {
            return false;
        }
        const auto expectedNonce = evm_unsafe_try_get_next_valid_nonce_in_q(result, evmQueueId, senderInfo.address);
        if (!result.ok) {
            return false;
        }
        if (senderInfo.nonce < expectedNonce) { // Pays 0 so cannot be RBF
            return false;
        } else if (senderInfo.nonce > expectedNonce) {
            if (!failedTxSet.count(txIter)) {
                failedNonces.emplace(senderInfo.nonce, txIter);
            }
            return false;
        }
        txResult.nonce = senderInfo.nonce;
        txResult.sender = senderInfo.address;
        txResult.prepay_fee = 0;
        txResult.higher_nonce = false;
        txResult.lower_nonce = false;
    } else {
        return false;
    }

    auto& evmFeeMap = pkgCtx.evmFeeMap;
    auto& feeMapLookup = pkgCtx.feeMapLookup;

    const std::string txResultSender{txResult.sender.data(), txResult.sender.length()};
    const EvmAddressWithNonce addrKey{txResult.nonce, txResultSender};

    if (const auto feeEntry = evmFeeMap.find(addrKey); feeEntry != evmFeeMap.end()) {
        // Key already exists. We check to see if we need to prioritize higher fee tx
        const auto& [lastFee, prevHash] = feeEntry->second;
        if (txResult.prepay_fee > lastFee) {
            // Higher paying fee. Remove all TXs above the TX to be replaced.
            auto hashes = evm_unsafe_try_remove_txs_above_hash_in_q(result, evmQueueId, prevHash.ToString());
            if (!result.ok) {
                return Res::Err("%s: Unable to remove TXs from queue\n", __func__);
            }

            // Set of TXs to remove from the block
            CTxMemPool::setEntries txsForRemoval;

            // Loop through hashes of removed TXs and remove from block.
            for (const auto &rustStr : hashes) {
                const auto txHash = uint256S(std::string{rustStr.data(), rustStr.length()});

                // Check fee map entry and get nonce
                assert(feeMapLookup.count(txHash));
                auto it = feeMapLookup.at(txHash);
                auto &[nonce, hash] = it->first;

                // Check TX time order entry and get time and TX
                assert(timeOrderLookup.count(txHash));
                const auto &timeIt = timeOrderLookup.at(txHash);
                const auto& [txTime, transaction] = *timeIt;

                // Add entry to removal set
                txsForRemoval.insert(transaction);

                // Reduce fee
                totalGas -= transaction->GetFee();

                // This is the entry to be replaced
                if (prevHash == txHash) {
                    replaceByFee.emplace(std::make_pair(txTime, nonce), txIter);
                } else { // Add in previous mempool entry
                    replaceByFee.emplace(std::make_pair(txTime, nonce), transaction);
                }

                // Remove from checked entries
                checkedDfTxHashSet.erase(txHash);

                // Remove replace fee entries
                evmFeeMap.erase(feeMapLookup.at(txHash));
                feeMapLookup.erase(txHash);
            }

            RemoveFromBlock(txsForRemoval, true);
        }

        return false;
    }

    // If not RBF for a mempool TX then return false
    if (txResult.lower_nonce) {
        return false;
    }

    auto feeMapRes = evmFeeMap.emplace(addrKey, std::make_pair(txResult.prepay_fee, txIter->GetTx().GetHash()));
    feeMapLookup.emplace(txIter->GetTx().GetHash(), feeMapRes.first);

    return true;
}


// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
template <class T>
void BlockAssembler::addPackageTxs(int& nPackagesSelected, int& nDescendantsUpdated, int nHeight, CCustomCSView& view, const uint64_t evmQueueId, std::map<uint256, CAmount>& txFees, const bool isEvmEnabledForBlock)
{
    // mapModifiedTxSet will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTxSet;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTxSet;
    // Checked DfTxs hashes for tracking
    std::set<uint256> checkedDfTxHashSet;

    // Start by adding all descendants of previously added txs to mapModifiedTxSet
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTxSet);

    auto mi = mempool.mapTx.get<T>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    // Copy of the view
    CCoinsViewCache coinsView(&::ChainstateActive().CoinsTip());

    // Track mempool time order
    std::multimap<int64_t, CTxMemPool::txiter> mempoolTimeOrder{};

    // EVM related context for this package
    EvmPackageContext evmPackageContext;

    while (mi != mempool.mapTx.get<T>().end() || !mapModifiedTxSet.empty() || !evmPackageContext.failedNonces.empty() || !evmPackageContext.replaceByFee.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<T>().end() &&
            SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTxSet, failedTxSet)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTxSet?
        bool fUsingModified = false;

        auto& replaceByFee = evmPackageContext.replaceByFee;

        // Check whether we are using Eth replaceByFee
        bool usingReplaceByFee = !replaceByFee.empty();

        modtxscoreiter modit = mapModifiedTxSet.get<ancestor_score>().begin();
        if (!replaceByFee.empty()) {
            const auto it = replaceByFee.begin();
            iter = it->second;
            replaceByFee.erase(it);
            usingReplaceByFee = true;
        } else if (mi == mempool.mapTx.get<T>().end() && mapModifiedTxSet.empty()) {
            auto& failedNonces = evmPackageContext.failedNonces;
            const auto it = failedNonces.begin();
            iter = it->second;
            failedNonces.erase(it);
        } else if (mi == mempool.mapTx.get<T>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTxSet
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTxSet entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTxSet.get<ancestor_score>().end() &&
                CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTxSet has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTxSet, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
            auto timeIt = mempoolTimeOrder.emplace(iter->GetTime(), iter);
            evmPackageContext.timeOrderLookup.emplace(iter->GetTx().GetHash(), timeIt);
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTxSet shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (!IsEVMTx(iter->GetTx()) && packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            break;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTxSet,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTxSet.get<ancestor_score>().erase(modit);
                failedTxSet.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                                                                     nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTxSet.get<ancestor_score>().erase(modit);
                failedTxSet.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        // Account check
        bool customTxPassed{true};

        // Apply and check custom TXs in order
        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            const CTransaction& tx = sortedEntries[i]->GetTx();

            // Do not double check already checked custom TX. This will be an ancestor of current TX.
            if (checkedDfTxHashSet.find(tx.GetHash()) != checkedDfTxHashSet.end()) {
                continue;
            }

            // temporary view to ensure failed tx
            // to not be kept in parent view
            CCoinsViewCache coins(&coinsView);

            // allow coin override, tx with same inputs
            // will be removed for block while we connect it
            AddCoins(coins, tx, nHeight, false); // do not check

            std::vector<unsigned char> metadata;
            CustomTxType txType = GuessCustomTxType(tx, metadata, true);

            // Only check custom TXs
            if (txType != CustomTxType::None) {
                if (txType == CustomTxType::EvmTx || txType == CustomTxType::TransferDomain) {
                    if (!isEvmEnabledForBlock) {
                        customTxPassed = false;
                        break;
                    }
                    auto evmTxCtx = EvmTxPreApplyContext{
                        sortedEntries[i],
                        metadata,
                        txType,
                        nHeight,
                        evmQueueId,
                        evmPackageContext,
                        checkedDfTxHashSet,
                        failedTxSet,
                    };
                    auto res = EvmTxPreapply(evmTxCtx);
                    if (res) {
                        customTxPassed = true;
                    } else {
                        failedTxSet.insert(sortedEntries[i]);
                        customTxPassed = false;
                        break;
                    }
                }

                uint64_t gasUsed{};
                const auto res = ApplyCustomTx(view, coins, tx, chainparams.GetConsensus(), nHeight, gasUsed, pblock->nTime, nullptr, 0, evmQueueId, isEvmEnabledForBlock, false);
                // Not okay invalidate, undo and skip
                if (!res.ok) {
                    customTxPassed = false;
                    LogPrintf("%s: Failed %s TX %s: %s\n", __func__, ToString(txType), tx.GetHash().GetHex(), res.msg);
                    break;
                }

                evmPackageContext.evmGasFees.emplace(tx.GetHash(), gasUsed);

                if (evmPackageContext.totalGas + gasUsed > MAX_BLOCK_GAS_LIMIT) {
                    customTxPassed = false;
                    break;
                }
                evmPackageContext.totalGas += gasUsed;

                // Track checked TXs to avoid double applying
                checkedDfTxHashSet.insert(tx.GetHash());
            }
            coins.Flush();
        }

        // Failed, let's move on!
        if (!customTxPassed) {
            if (fUsingModified) {
                mapModifiedTxSet.get<ancestor_score>().erase(modit);
            }
            if (usingReplaceByFee) {
                // TX failed in chain so remove the rest of the items
                replaceByFee.clear();
            }
            failedTxSet.insert(iter);
            continue;
        }

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            txFees.emplace(sortedEntries[i]->GetTx().GetHash(), sortedEntries[i]->GetFee());
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTxSet.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTxSet);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

namespace pos {

// initialize static variables here
std::map<uint256, int64_t> Staker::mapMNLastBlockCreationAttemptTs;
AtomicMutex cs_MNLastBlockCreationAttemptTs;
int64_t Staker::nLastCoinStakeSearchTime{0};
int64_t Staker::nFutureTime{0};
uint256 Staker::lastBlockSeen{};

Staker::Status Staker::init(const CChainParams& chainparams)
{
    if (!chainparams.GetConsensus().pos.allowMintingWithoutPeers) {
        if (!g_connman)
            throw std::runtime_error("Error: Peer-to-peer functionality missing or disabled");

        if (!chainparams.GetConsensus().pos.allowMintingWithoutPeers && g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
            return Status::initWaiting;

        if (::ChainstateActive().IsInitialBlockDownload())
            return Status::initWaiting;

        if (::ChainstateActive().IsDisconnectingTip())
            return Status::stakeWaiting;
    }
    return Status::stakeReady;
}

Staker::Status Staker::stake(const CChainParams& chainparams, const ThreadStaker::Args& args)
{
    bool found = false;

    // this part of code stay valid until tip got changed

    uint32_t mintedBlocks(0);
    uint256 masternodeID{};
    int64_t creationHeight;
    CScript scriptPubKey;
    int64_t blockTime;
    CBlockIndex* tip;
    int64_t blockHeight;
    std::vector<int64_t> subNodesBlockTime;
    uint16_t timelock;
    std::optional<CMasternode> nodePtr;

    {
        LOCK(cs_main);
        auto optMasternodeID = pcustomcsview->GetMasternodeIdByOperator(args.operatorID);
        if (!optMasternodeID) {
            return Status::initWaiting;
        }
        tip = ::ChainActive().Tip();
        masternodeID = *optMasternodeID;
        nodePtr = pcustomcsview->GetMasternode(masternodeID);
        if (!nodePtr || !nodePtr->IsActive(tip->nHeight + 1, *pcustomcsview)) {
            /// @todo may be new status for not activated (or already resigned) MN??
            return Status::initWaiting;
        }
        mintedBlocks = nodePtr->mintedBlocks;
        if (args.coinbaseScript.empty()) {
            // this is safe because MN was found
            if (tip->nHeight >= chainparams.GetConsensus().FortCanningHeight && nodePtr->rewardAddressType != 0) {
                scriptPubKey = GetScriptForDestination(FromOrDefaultKeyIDToDestination(nodePtr->rewardAddress, TxDestTypeToKeyType(nodePtr->rewardAddressType), KeyType::MNRewardKeyType));
            } else {
                scriptPubKey = GetScriptForDestination(FromOrDefaultKeyIDToDestination(nodePtr->ownerAuthAddress, TxDestTypeToKeyType(nodePtr->ownerType), KeyType::MNOwnerKeyType));
            }
        } else {
            scriptPubKey = args.coinbaseScript;
        }

        blockHeight = tip->nHeight + 1;
        creationHeight = int64_t(nodePtr->creationHeight);
        blockTime = std::max(tip->GetMedianTimePast() + 1, GetAdjustedTime());
        const auto optTimeLock = pcustomcsview->GetTimelock(masternodeID, *nodePtr, blockHeight);
        if (!optTimeLock)
            return Status::stakeWaiting;

        timelock = *optTimeLock;

        // Get block times
        subNodesBlockTime = pcustomcsview->GetBlockTimes(args.operatorID, blockHeight, creationHeight, timelock);
    }

    auto nBits = pos::GetNextWorkRequired(tip, blockTime, chainparams.GetConsensus());
    auto stakeModifier = pos::ComputeStakeModifier(tip->stakeModifier, args.minterKey.GetPubKey().GetID());

    // Set search time if null or last block has changed
    if (!nLastCoinStakeSearchTime || lastBlockSeen != tip->GetBlockHash()) {
        if (Params().NetworkIDString() == CBaseChainParams::REGTEST) {
            // For regtest use previous oldest time
            nLastCoinStakeSearchTime = GetAdjustedTime() - 60;
            if (nLastCoinStakeSearchTime <= tip->GetMedianTimePast()) {
                nLastCoinStakeSearchTime = tip->GetMedianTimePast() + 1;
            }
        } else {
            // Plus one to avoid time-too-old error on exact median time.
            nLastCoinStakeSearchTime = tip->GetMedianTimePast() + 1;
        }

        lastBlockSeen = tip->GetBlockHash();
    }

    withSearchInterval([&](const int64_t currentTime, const int64_t lastSearchTime, const int64_t futureTime) {
        // update last block creation attempt ts for the master node here
        {
            std::unique_lock l{pos::cs_MNLastBlockCreationAttemptTs};
            pos::Staker::mapMNLastBlockCreationAttemptTs[masternodeID] = GetTime();
        }
        CheckContextState ctxState;
        // Search backwards in time first
        if (currentTime > lastSearchTime) {
            for (uint32_t t = 0; t < currentTime - lastSearchTime; ++t) {
                if (ShutdownRequested()) break;

                blockTime = ((uint32_t)currentTime - t);

                if (pos::CheckKernelHash(stakeModifier, nBits, creationHeight, blockTime, blockHeight, masternodeID, chainparams.GetConsensus(),
                        subNodesBlockTime, timelock, ctxState)) {
                    LogPrintf("MakeStake: kernel found\n");

                    found = true;
                    break;
                }

                std::this_thread::yield(); // give a slot to other threads
            }
        }

        if (!found) {
            // Search from current time or lastSearchTime set in the future
            int64_t searchTime = lastSearchTime > currentTime ? lastSearchTime : currentTime;

            // Search forwards in time
            for (uint32_t t = 1; t <= futureTime - searchTime; ++t) {
                if (ShutdownRequested()) break;

                blockTime = ((uint32_t)searchTime + t);

                if (pos::CheckKernelHash(stakeModifier, nBits, creationHeight, blockTime, blockHeight, masternodeID, chainparams.GetConsensus(),
                        subNodesBlockTime, timelock, ctxState)) {
                    LogPrint(BCLog::STAKING, "MakeStake: kernel found\n");

                    found = true;
                    break;
                }

                std::this_thread::yield(); // give a slot to other threads
            }
        }
    },
        blockHeight);

    if (!found) {
        return Status::stakeWaiting;
    }

    //
    // Create block template
    //
    auto pubKey = args.minterKey.GetPubKey();
    if (pubKey.IsCompressed()) {
        pubKey.Decompress();
    }
    const auto evmBeneficiary = pubKey.GetEthID().GetHex();
    auto res = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, blockTime, evmBeneficiary);
    if (!res) {
        LogPrintf("Error: WalletStaker: %s\n", res.msg);
        return Status::stakeWaiting;
    }

    auto& pblocktemplate = *res;
    auto pblock = std::make_shared<CBlock>(pblocktemplate->block);

    pblock->nBits = nBits;
    pblock->mintedBlocks = mintedBlocks + 1;
    pblock->stakeModifier = std::move(stakeModifier);

    LogPrint(BCLog::STAKING, "Running Staker with %u common transactions in block (%u bytes)\n", pblock->vtx.size() - 1,
        ::GetSerializeSize(*pblock, PROTOCOL_VERSION));

    //
    // Trying to sign a block
    //
    auto err = pos::SignPosBlock(pblock, args.minterKey);
    if (err) {
        LogPrint(BCLog::STAKING, "SignPosBlock(): %s \n", *err);
        return Status::stakeWaiting;
    }

    //
    // Final checks
    //
    {
        LOCK(cs_main);
        err = pos::CheckSignedBlock(pblock, tip, chainparams);
        if (err) {
            LogPrint(BCLog::STAKING, "CheckSignedBlock(): %s \n", *err);
            return Status::stakeWaiting;
        }
    }

    if (!ProcessNewBlock(chainparams, pblock, true, nullptr)) {
        LogPrintf("PoS block was checked, but wasn't accepted by ProcessNewBlock\n");
        return Status::stakeWaiting;
    }

    return Status::minted;
}

template <typename F>
void Staker::withSearchInterval(F&& f, int64_t height)
{
    if (height >= Params().GetConsensus().EunosPayaHeight) {
        // Mine up to max future minus 1 second buffer
        nFutureTime = GetAdjustedTime() + (MAX_FUTURE_BLOCK_TIME_EUNOSPAYA - 1); // 29 seconds
    } else {
        // Mine up to max future minus 5 second buffer
        nFutureTime = GetAdjustedTime() + (MAX_FUTURE_BLOCK_TIME_DAKOTACRESCENT - 5); // 295 seconds
    }

    if (nFutureTime > nLastCoinStakeSearchTime) {
        f(GetAdjustedTime(), nLastCoinStakeSearchTime, nFutureTime);
    }
}

void ThreadStaker::operator()(std::vector<ThreadStaker::Args> args, CChainParams chainparams)
{
    std::map<CKeyID, int32_t> nMinted;
    std::map<CKeyID, int32_t> nTried;

    auto wallets = GetWallets();

    for (auto& arg : args) {
        while (true) {
            if (ShutdownRequested()) break;

            bool found = false;
            for (auto wallet : wallets) {
                if (wallet->GetKey(arg.operatorID, arg.minterKey)) {
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
            static std::atomic<uint64_t> time{0};
            if (GetSystemTimeInSeconds() - time > 120) {
                LogPrintf("ThreadStaker: unlock wallet to start minting...\n");
                time = GetSystemTimeInSeconds();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    LogPrintf("ThreadStaker: started.\n");

    while (!args.empty()) {
        if (ShutdownRequested()) break;

        while (fImporting || fReindex) {
            if (ShutdownRequested()) break;

            LogPrintf("ThreadStaker: waiting reindex...\n");

            std::this_thread::sleep_for(std::chrono::milliseconds(900));
        }

        for (auto it = args.begin(); it != args.end();) {
            const auto& arg = *it;
            const auto operatorName = arg.operatorID.GetHex();

            if (ShutdownRequested()) break;

            pos::Staker staker;

            try {
                auto status = staker.init(chainparams);
                if (status == Staker::Status::stakeReady) {
                    status = staker.stake(chainparams, arg);
                }
                if (status == Staker::Status::error) {
                    LogPrintf("ThreadStaker: (%s) terminated due to a staking error!\n", operatorName);
                    it = args.erase(it);
                    continue;
                } else if (status == Staker::Status::minted) {
                    LogPrintf("ThreadStaker: (%s) minted a block!\n", operatorName);
                    nMinted[arg.operatorID]++;
                } else if (status == Staker::Status::initWaiting) {
                    LogPrintCategoryOrThreadThrottled(BCLog::STAKING, "init_waiting", 1000 * 60 * 10, "ThreadStaker: (%s) waiting init...\n", operatorName);
                } else if (status == Staker::Status::stakeWaiting) {
                    LogPrintCategoryOrThreadThrottled(BCLog::STAKING, "no_kernel_found", 1000 * 60 * 10, "ThreadStaker: (%s) Staked, but no kernel found yet.\n", operatorName);
                }
            } catch (const std::runtime_error& e) {
                LogPrintf("ThreadStaker: (%s) runtime error: %s\n", e.what(), operatorName);

                // Could be failed TX in mempool, wipe mempool and allow loop to continue.
                LOCK(cs_main);
                mempool.clear();
            }

            auto& tried = nTried[arg.operatorID];
            tried++;

            if ((arg.nMaxTries != -1 && tried >= arg.nMaxTries) || (arg.nMint != -1 && nMinted[arg.operatorID] >= arg.nMint)) {
                it = args.erase(it);
                continue;
            }

            ++it;
        }

        // Set search period to last time set
        Staker::nLastCoinStakeSearchTime = Staker::nFutureTime;

        std::this_thread::sleep_for(std::chrono::milliseconds(900));
    }
}

} // namespace pos

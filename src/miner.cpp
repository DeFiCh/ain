// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/tx_check.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <masternodes/anchors.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <net.h>
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

#include <algorithm>
#include <queue>
#include <utility>

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.pos.fAllowMinDifficultyBlocks)
        pblock->nBits = pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
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

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, int64_t blockTime)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = ::ChainActive().Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;
    // in fact, this may be redundant cause it was checked upthere in the miner
    std::optional<std::pair<CKeyID, uint256>> myIDs;
    if (!blockTime) {
        myIDs = pcustomcsview->AmIOperator();
        if (!myIDs)
            return nullptr;
        auto nodePtr = pcustomcsview->GetMasternode(myIDs->second);
        if (!nodePtr || !nodePtr->IsActive(nHeight))
            return nullptr;
    }

    auto consensus = chainparams.GetConsensus();
    pblock->nVersion = ComputeBlockVersion(pindexPrev, consensus);
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = blockTime;
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

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
            CBlockIndex *anchorIndex = ::ChainActive()[confirms[0].anchorHeight];
            if (anchorIndex && anchorIndex->GetBlockHash() == confirms[0].dfiBlockHash) {
                createAnchorReward = true;
            }
        }

        if (createAnchorReward) {
            CAnchorFinalizationMessagePlus finMsg{confirms[0]};

            for (auto const &msg : confirms) {
                finMsg.sigs.push_back(msg.signature);
            }

            CDataStream metadata(DfAnchorFinalizeTxMarkerPlus, SER_NETWORK, PROTOCOL_VERSION);
            metadata << finMsg;

            CTxDestination destination =
                    finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID)) : CTxDestination(
                            WitnessV0KeyHash(finMsg.rewardKeyID));

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

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    CCustomCSView mnview(*pcustomcsview);
    if (!blockTime) {
        UpdateTime(pblock, consensus, pindexPrev); // update time before tx packaging
    }
    addPackageTxs(nPackagesSelected, nDescendantsUpdated, nHeight, mnview);

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

    if (nHeight >= consensus.EunosHeight)
    {
        coinbaseTx.vout.resize(2);

        // Explicitly set miner reward
        if (nHeight >= consensus.FortCanningHeight) {
            coinbaseTx.vout[0].nValue = nFees + CalculateCoinbaseReward(blockReward, consensus.dist.masternode);
        } else {
            coinbaseTx.vout[0].nValue = CalculateCoinbaseReward(blockReward, consensus.dist.masternode);
        }

        // Community payment always expected
        coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
        coinbaseTx.vout[1].nValue = CalculateCoinbaseReward(blockReward, consensus.dist.community);

        LogPrint(BCLog::STAKING, "%s: post Eunos logic. Block reward %d Miner share %d foundation share %d\n",
                 __func__, blockReward, coinbaseTx.vout[0].nValue, coinbaseTx.vout[1].nValue);
    }
    else if (nHeight >= consensus.AMKHeight) {
        // assume community non-utxo funding:
        for (const auto& kv : consensus.nonUtxoBlockSubsidies) {
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
    }
    else { // pre-AMK logic:
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

    LogPrint(BCLog::STAKING, "CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    pblock->deprecatedHeight = pindexPrev->nHeight + 1;
    pblock->nBits          = pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensus);
    if (myIDs) {
        pblock->stakeModifier  = pos::ComputeStakeModifier(pindexPrev->stakeModifier, myIDs->first);
    }

    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    if (nHeight >= chainparams.GetConsensus().EunosHeight
    && nHeight < chainparams.GetConsensus().EunosKampungHeight) {
        // includes coinbase account changes
        ApplyGeneralCoinbaseTx(mnview, *(pblock->vtx[0]), nHeight, nFees, chainparams.GetConsensus());
        pblock->hashMerkleRoot = Hash2(pblock->hashMerkleRoot, mnview.MerkleRoot());
    }

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
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

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
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
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
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
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated, int nHeight, CCustomCSView &view)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    // Custom TXs to undo at the end
    std::set<uint256> checkedTX;

    // Copy of the view
    CCoinsViewCache coinsView(&::ChainstateActive().CoinsTip());

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
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

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            break;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
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
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
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
            if (checkedTX.find(tx.GetHash()) != checkedTX.end()) {
                continue;
            }

            // temporary view to ensure failed tx
            // to not be kept in parent view
            CCoinsViewCache coins(&coinsView);

            // allow coin override, tx with same inputs
            // will be removed for block while we connect it
            AddCoins(coins, tx, nHeight, false); // do not check

            std::vector<unsigned char> metadata;
            CustomTxType txType = GuessCustomTxType(tx, metadata);

            // Only check custom TXs
            if (txType != CustomTxType::None) {
                auto res = ApplyCustomTx(view, coins, tx, chainparams.GetConsensus(), nHeight, pblock->nTime);

                // Not okay invalidate, undo and skip
                if (!res.ok) {
                    customTxPassed = false;

                    LogPrintf("%s: Failed %s TX %s: %s\n", __func__, ToString(txType), tx.GetHash().GetHex(), res.msg);

                    break;
                }

                // Track checked TXs to avoid double applying
                checkedTX.insert(tx.GetHash());
            }
            coins.Flush();
        }

        // Failed, let's move on!
        if (!customTxPassed) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
            }
            failedTx.insert(iter);
            continue;
        }

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

namespace pos {

    //initialize static variables here
    std::map<uint256, int64_t> Staker::mapMNLastBlockCreationAttemptTs;
    std::atomic_bool Staker::cs_MNLastBlockCreationAttemptTs(false);
    int64_t Staker::nLastCoinStakeSearchTime{0};
    int64_t Staker::nFutureTime{0};
    uint256 Staker::lastBlockSeen{};

    Staker::Status Staker::init(const CChainParams& chainparams) {
        if (!chainparams.GetConsensus().pos.allowMintingWithoutPeers) {
            if(!g_connman)
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

    // This is an internal only method to ignore some mints, even though it's valid. 
    // This is done to workaround https://github.com/DeFiCh/ain/issues/693, so on specific bug condition, 
    // i.e, when subnode 1 stakes before subnode 0 on Eunos Paya+ (in particular to target pre-Eunos Paya masternodes that's carried over), it's ignored.
    // in order to give time for subnode 0 to first succeed and create a record. 
    // 
    // This is done to ensure that we can workaround the bug, without waiting for consensus change in next update.
    // This shouldn't have any effect on the mining, even though we ignore some successful hashes since the
    // Coinages are retained and keep growing. Once subnode 0 mines, subnode 1 should stake again shortly with 
    // higher coinage / TM.
    //
    bool shouldIgnoreMint(uint8_t subNode, int64_t blockHeight, int64_t creationHeight, 
        const std::vector<int64_t>& subNodesBlockTime, const CChainParams& chainParams) {
            
        auto eunosPayaHeight = chainParams.GetConsensus().EunosPayaHeight;
        if (blockHeight < eunosPayaHeight || creationHeight >= eunosPayaHeight) {
            return false;
        }

        if (subNode == 1 && subNodesBlockTime[0] == subNodesBlockTime[1]) {
            LogPrint(BCLog::STAKING, "MakeStake: kernel ignored\n");
            return true;
        }
        return false;
    }

    Staker::Status Staker::stake(const CChainParams& chainparams, const ThreadStaker::Args& args) {

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

        {
            LOCK(cs_main);
            auto optMasternodeID = pcustomcsview->GetMasternodeIdByOperator(args.operatorID);
            if (!optMasternodeID) {
                return Status::initWaiting;
            }
            tip = ::ChainActive().Tip();
            masternodeID = *optMasternodeID;
            auto nodePtr = pcustomcsview->GetMasternode(masternodeID);
            if (!nodePtr || !nodePtr->IsActive(tip->nHeight + 1)) {
                /// @todo may be new status for not activated (or already resigned) MN??
                return Status::initWaiting;
            }
            mintedBlocks = nodePtr->mintedBlocks;
            if (args.coinbaseScript.empty()) {
                // this is safe cause MN was found
                if (tip->nHeight >= chainparams.GetConsensus().FortCanningHeight && nodePtr->rewardAddressType != 0) {
                    scriptPubKey = GetScriptForDestination(nodePtr->rewardAddressType == PKHashType ?
                        CTxDestination(PKHash(nodePtr->rewardAddress)) :
                        CTxDestination(WitnessV0KeyHash(nodePtr->rewardAddress))
                    );
                }
                else {
                    scriptPubKey = GetScriptForDestination(nodePtr->ownerType == PKHashType ?
                        CTxDestination(PKHash(nodePtr->ownerAuthAddress)) :
                        CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress))
                    );
                }
            } else {
                scriptPubKey = args.coinbaseScript;
            }

            blockHeight = tip->nHeight + 1;
            creationHeight = int64_t(nodePtr->creationHeight);
            blockTime = std::max(tip->GetMedianTimePast() + 1, GetAdjustedTime());
            timelock = pcustomcsview->GetTimelock(masternodeID, *nodePtr, blockHeight);

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
                CLockFreeGuard lock(pos::Staker::cs_MNLastBlockCreationAttemptTs);
                pos::Staker::mapMNLastBlockCreationAttemptTs[masternodeID] = GetTime();
            }
            CheckContextState ctxState;
            // Search backwards in time first
            if (currentTime > lastSearchTime) {
                for (uint32_t t = 0; t < currentTime - lastSearchTime; ++t) {
                    if (ShutdownRequested()) break;

                    blockTime = ((uint32_t)currentTime - t);

                    if (pos::CheckKernelHash(stakeModifier, nBits, creationHeight, blockTime, blockHeight, masternodeID, chainparams.GetConsensus(),
                                             subNodesBlockTime, timelock, ctxState))
                    {
                        if (shouldIgnoreMint(ctxState.subNode, blockHeight, creationHeight, subNodesBlockTime, chainparams)) 
                            break;
                        LogPrint(BCLog::STAKING, "MakeStake: kernel found\n");

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
                                             subNodesBlockTime, timelock, ctxState))
                    {
                        if (shouldIgnoreMint(ctxState.subNode, blockHeight, creationHeight, subNodesBlockTime, chainparams)) 
                            break;
                        LogPrint(BCLog::STAKING, "MakeStake: kernel found\n");

                        found = true;
                        break;
                    }

                    std::this_thread::yield(); // give a slot to other threads
                }
            }
        }, blockHeight);

        if (!found) {
            return Status::stakeWaiting;
        }

        //
        // Create block template
        //
        auto pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, blockTime);
        if (!pblocktemplate) {
            throw std::runtime_error("Error in WalletStaker: Keypool ran out, please call keypoolrefill before restarting the staking thread");
        }

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
    void Staker::withSearchInterval(F&& f, int64_t height) {
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

void ThreadStaker::operator()(std::vector<ThreadStaker::Args> args, CChainParams chainparams) {

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

        for (auto it = args.begin(); it != args.end(); ) {
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
                }
                else if (status == Staker::Status::minted) {
                    LogPrintf("ThreadStaker: (%s) minted a block!\n", operatorName);
                    nMinted[arg.operatorID]++;
                }
                else if (status == Staker::Status::initWaiting) {
                    LogPrintf("ThreadStaker: (%s) waiting init...\n", operatorName);
                }
                else if (status == Staker::Status::stakeWaiting) {
                    LogPrint(BCLog::STAKING, "ThreadStaker: (%s) Staked, but no kernel found yet.\n", operatorName);
                }
            }
            catch (const std::runtime_error &e) {
                LogPrintf("ThreadStaker: (%s) runtime error: %s\n", e.what(), operatorName);

                // Could be failed TX in mempool, wipe mempool and allow loop to continue.
                LOCK(cs_main);
                mempool.clear();
            }

            auto& tried = nTried[arg.operatorID];
            tried++;

            if ((arg.nMaxTries != -1 && tried >= arg.nMaxTries)
            || (arg.nMint != -1 && nMinted[arg.operatorID] >= arg.nMint)) {
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

}

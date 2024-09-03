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
#include <dfi/anchors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>
#include <dfi/validation.h>
#include <ffi/cxx.h>
#include <ffi/ffiexports.h>
#include <ffi/ffihelpers.h>
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

#include <algorithm>
#include <random>
#include <utility>

#include <boost/lockfree/queue.hpp>

using SplitMap = std::map<uint32_t, std::pair<int32_t, uint256>>;

struct EvmTxPreApplyContext {
    const CTxMemPool::txiter &txIter;
    const std::shared_ptr<CScopedTemplate> &evmTemplate;
    std::multimap<uint64_t, CTxMemPool::txiter> &failedNonces;
    std::map<uint256, CTxMemPool::FailedNonceIterator> &failedNoncesLookup;
    CTxMemPool::setEntries &failedTxEntries;
};

int64_t UpdateTime(CBlockHeader *pblock, const Consensus::Params &consensusParams, const CBlockIndex *pindexPrev) {
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    // Updating time can change work required on testnet:
    if (consensusParams.pos.fAllowMinDifficultyBlocks) {
        pblock->nBits = pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensusParams);
    }

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
}

BlockAssembler::BlockAssembler(const CChainParams &params, const Options &options)
    : chainparams(params) {
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT - 4000, options.nBlockMaxWeight));
}

static BlockAssembler::Options DefaultOptions() {
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

BlockAssembler::BlockAssembler(const CChainParams &params)
    : BlockAssembler(params, DefaultOptions()) {}

void BlockAssembler::resetBlock() {
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

static void AddSplitEVMTxs(BlockContext &blockCtx, const SplitMap &splitMap) {
    const auto evmEnabled = blockCtx.GetEVMEnabledForBlock();
    const auto &evmTemplate = blockCtx.GetEVMTemplate();

    if (!evmEnabled || !evmTemplate) {
        return;
    }

    auto &mnview = blockCtx.GetView();
    const auto attributes = mnview.GetAttributes();

    DCT_ID newId{};
    mnview.ForEachToken(
        [&](DCT_ID const &currentId, CLazySerialize<CTokenImplementation>) {
            if (currentId < CTokensView::DCT_ID_START) {
                newId.v = currentId.v + 1;
            }
            return currentId < CTokensView::DCT_ID_START;
        },
        newId);

    for (const auto &[id, splitData] : splitMap) {
        const auto &[multiplier, creationTx] = splitData;

        auto oldToken = mnview.GetToken(DCT_ID{id});
        if (!oldToken) {
            continue;
        }

        std::string newTokenSuffix = "/v";
        auto res = GetTokenSuffix(mnview, *attributes, id, newTokenSuffix);
        if (!res) {
            continue;
        }

        if (newId == CTokensView::DCT_ID_START) {
            newId = mnview.IncrementLastDctId();
        }

        auto tokenSymbol = oldToken->symbol;
        oldToken->symbol += newTokenSuffix;

        uint256 hash{};
        CrossBoundaryResult result;
        evm_try_unsafe_rename_dst20(result,
                                    evmTemplate->GetTemplate(),
                                    hash.GetByteArray(),
                                    DST20TokenInfo{
                                        id,
                                        oldToken->name,
                                        oldToken->symbol,
                                    });
        if (!result.ok) {
            LogPrintf("AddSplitEVMTxs evm_try_unsafe_rename_dst20 error: %s\n", result.reason.c_str());
            continue;
        }

        evm_try_unsafe_create_dst20(result,
                                    evmTemplate->GetTemplate(),
                                    creationTx.GetByteArray(),
                                    DST20TokenInfo{
                                        newId.v,
                                        oldToken->name,
                                        tokenSymbol,
                                    });
        if (!result.ok) {
            LogPrintf("AddSplitEVMTxs evm_try_unsafe_create_dst20 error: %s\n", result.reason.c_str());
            continue;
        }

        newId.v++;
    }
}

template <typename T>
static void AddSplitDVMTxs(CCustomCSView &mnview,
                           CBlock *pblock,
                           std::unique_ptr<CBlockTemplate> &pblocktemplate,
                           const int height,
                           const T &splits,
                           const int txVersion,
                           SplitMap &splitMap) {
    for (const auto &[id, multiplier] : splits) {
        uint32_t entries{1};
        mnview.ForEachPoolPair([&, id = id](DCT_ID const &poolId, const CPoolPair &pool) {
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
            mTx.vin[0].scriptSig = CScript() << height << OP_0;
            mTx.vout.resize(1);
            mTx.vout[0].scriptPubKey = CScript() << OP_RETURN << ToByteVector(metadata);
            mTx.vout[0].nValue = 0;
            auto tx = MakeTransactionRef(std::move(mTx));
            if (!i) {
                splitMap[id] = std::make_pair(multiplier, tx->GetHash());
            }
            pblock->vtx.push_back(tx);
            pblocktemplate->vTxFees.push_back(0);
            pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx.back()));
        }
    }
}

ResVal<std::unique_ptr<CBlockTemplate>> BlockAssembler::CreateNewBlock(const CScript &scriptPubKeyIn,
                                                                       int64_t blockTime,
                                                                       const std::string &evmBeneficiary) {
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate = std::make_unique<CBlockTemplate>();

    if (!pblocktemplate) {
        return Res::Err("Failed to create block template");
    }
    pblock = &pblocktemplate->block;  // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1);        // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1);  // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex *pindexPrev = ::ChainActive().Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    std::optional<std::pair<CKeyID, uint256>> myIDs;
    if (!blockTime) {
        myIDs = pcustomcsview->AmIOperator();
        if (!myIDs) {
            return Res::Err("Node has no operators");
        }
        const auto nodePtr = pcustomcsview->GetMasternode(myIDs->second);
        if (!nodePtr || !nodePtr->IsActive(nHeight, *pcustomcsview)) {
            return Res::Err("Node is not active");
        }
    }

    auto consensus = chainparams.GetConsensus();
    pblock->nVersion = ComputeBlockVersion(pindexPrev, consensus);
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = blockTime;
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff =
        (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : pblock->GetBlockTime();

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
        (Params().NetworkIDString() == CBaseChainParams::MAIN &&
         nHeight >= chainparams.GetConsensus().DF9EunosKampungHeight)) {
        CTeamView::CTeam currentTeam;
        if (const auto team = pcustomcsview->GetConfirmTeam(pindexPrev->nHeight)) {
            currentTeam = *team;
        }

        auto confirms = panchorAwaitingConfirms->GetQuorumFor(currentTeam);

        bool createAnchorReward{false};

        // No new anchors until we hit fork height, no new confirms should be found before fork.
        if (pindexPrev->nHeight >= consensus.DF6DakotaHeight && confirms.size() > 0) {
            // Make sure anchor block height and hash exist in chain.
            CBlockIndex *anchorIndex = ::ChainActive()[confirms[0].anchorHeight];
            if (anchorIndex && anchorIndex->GetBlockHash() == confirms[0].dfiBlockHash) {
                createAnchorReward = true;
            }
        }

        if (createAnchorReward) {
            CAnchorFinalizationMessagePlus finMsg{confirms[0]};

            for (const auto &msg : confirms) {
                finMsg.sigs.push_back(msg.signature);
            }

            CDataStream metadata(DfAnchorFinalizeTxMarkerPlus, SER_NETWORK, PROTOCOL_VERSION);
            metadata << finMsg;

            CTxDestination destination;
            if (nHeight < consensus.DF22MetachainHeight) {
                destination = FromOrDefaultKeyIDToDestination(
                    finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNOwnerKeyType);
            } else {
                destination = FromOrDefaultKeyIDToDestination(
                    finMsg.rewardKeyID, TxDestTypeToKeyType(finMsg.rewardKeyType), KeyType::MNRewardKeyType);
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
                    CommunityAccountType::AnchorReward);  // do not reset it, so it will occur on connectblock

                auto rewardTx = pcustomcsview->GetRewardForAnchor(finMsg.btcTxHash);
                if (!rewardTx) {
                    pblock->vtx.push_back(MakeTransactionRef(std::move(mTx)));
                    pblocktemplate->vTxFees.push_back(0);
                    pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR *
                                                            GetLegacySigOpCount(*pblock->vtx.back()));
                }
            }
        }
    }

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    BlockContext blockCtx(nHeight, pblock->nTime, chainparams.GetConsensus());
    auto &mnview = blockCtx.GetView();
    if (!blockTime) {
        UpdateTime(pblock, consensus, pindexPrev);  // update time before tx packaging
    }

    bool timeOrdering{false};
    if (txOrdering == MIXED_ORDERING) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<unsigned long long> dis;

        if (dis(rd) % 2 == 0) {
            timeOrdering = false;
        } else {
            timeOrdering = true;
        }
    } else if (txOrdering == ENTRYTIME_ORDERING) {
        timeOrdering = true;
    } else if (txOrdering == FEE_ORDERING) {
        timeOrdering = false;
    }

    const auto attributes = mnview.GetAttributes();
    const auto isEvmEnabledForBlock = blockCtx.GetEVMEnabledForBlock();
    const auto &evmTemplate = blockCtx.GetEVMTemplate();

    if (isEvmEnabledForBlock) {
        blockCtx.SetEVMTemplate(
            CScopedTemplate::Create(nHeight,
                                    evmBeneficiary,
                                    pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensus),
                                    blockTime,
                                    static_cast<std::size_t>(reinterpret_cast<uintptr_t>(&mnview))));
        if (!evmTemplate) {
            return Res::Err("Failed to create block template");
        }
        XResultThrowOnErr(evm_try_unsafe_update_state_in_template(result, evmTemplate->GetTemplate()));
    }

    std::map<uint256, CAmount> txFees;

    if (timeOrdering) {
        addPackageTxs<entry_time>(nPackagesSelected, nDescendantsUpdated, nHeight, txFees, blockCtx);
    } else {
        addPackageTxs<ancestor_score>(nPackagesSelected, nDescendantsUpdated, nHeight, txFees, blockCtx);
    }

    SplitMap splitMap;

    // TXs for the creationTx field in new tokens created via token split
    if (nHeight >= chainparams.GetConsensus().DF16FortCanningCrunchHeight) {
        CDataStructureV0 splitKey{AttributeTypes::Oracles, OracleIDs::Splits, static_cast<uint32_t>(nHeight)};
        if (const auto splits32 = attributes->GetValue(splitKey, OracleSplits{}); !splits32.empty()) {
            AddSplitDVMTxs(mnview, pblock, pblocktemplate, nHeight, splits32, txVersion, splitMap);
        } else if (const auto splits64 = attributes->GetValue(splitKey, OracleSplits64{}); !splits64.empty()) {
            AddSplitDVMTxs(mnview, pblock, pblocktemplate, nHeight, splits64, txVersion, splitMap);
        }
    }

    if (nHeight >= chainparams.GetConsensus().DF23Height) {
        // Add token split TXs
        AddSplitEVMTxs(blockCtx, splitMap);
    }

    if (nHeight >= chainparams.GetConsensus().DF24Height) {
        // Add token lock creations TXs: duplicate code from AddSplitDVMTxs.
        // TODO: refactor

        CDataStructureV0 lockedTokenKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::LockedTokens};
        CDataStructureV0 lockKey{AttributeTypes::Param, ParamIDs::dTokenRestart, static_cast<uint32_t>(nHeight)};
        const auto lockedTokens = attributes->GetValue(lockedTokenKey, CBalances{});
        const auto lockRatio = attributes->GetValue(lockKey, CAmount{});

        // Check all collaterals are currently valid
        auto tokenPricesValid{true};

        auto checkLivePrice = [&](const CTokenCurrencyPair &currencyPair) {
            if (auto fixedIntervalPrice = mnview.GetFixedIntervalPrice(currencyPair)) {
                if (!fixedIntervalPrice.val->isLive(mnview.GetPriceDeviation())) {
                    tokenPricesValid = false;
                    return false;
                }
            }
            return true;
        };

        attributes->ForEach(
            [&](const CDataStructureV0 &attr, const CAttributeValue &) {
                if (attr.type != AttributeTypes::Token) {
                    return false;
                }
                if (attr.key == TokenKeys::LoanCollateralEnabled) {
                    if (auto collateralToken = mnview.GetCollateralTokenFromAttributes({attr.typeId})) {
                        return checkLivePrice(collateralToken->fixedIntervalPriceId);
                    }
                } else if (attr.key == TokenKeys::LoanMintingEnabled) {
                    if (auto loanToken = mnview.GetLoanTokenFromAttributes({attr.typeId})) {
                        return checkLivePrice(loanToken->fixedIntervalPriceId);
                    }
                }
                return true;
            },
            CDataStructureV0{AttributeTypes::Token});

        if (lockedTokens.balances.empty() && lockRatio && tokenPricesValid) {
            SplitMap lockSplitMapEVM;
            auto createTokenLockSplitTx = [&](const uint32_t id, const bool isToken) {
                CDataStream metadata(DfTokenSplitMarker, SER_NETWORK, PROTOCOL_VERSION);
                int64_t multiplier = COIN;
                metadata << (isToken ? 0 : 1) << id << multiplier;

                CMutableTransaction mTx(txVersion);
                mTx.vin.resize(1);
                mTx.vin[0].prevout.SetNull();
                mTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
                mTx.vout.resize(1);
                mTx.vout[0].scriptPubKey = CScript() << OP_RETURN << ToByteVector(metadata);
                mTx.vout[0].nValue = 0;
                auto tx = MakeTransactionRef(std::move(mTx));
                if (isToken) {
                    lockSplitMapEVM[id] = std::make_pair(multiplier, tx->GetHash());
                }
                pblock->vtx.push_back(tx);
                pblocktemplate->vTxFees.push_back(0);
                pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR *
                                                        GetLegacySigOpCount(*pblock->vtx.back()));
                LogPrintf("Add creation TX ID: %d isToken: %d Hash: %s\n", id, isToken, tx->GetHash().GetHex());
            };
            ForEachLockTokenAndPool(
                [&](const DCT_ID &id, const CLoanSetLoanTokenImplementation &token) {
                    createTokenLockSplitTx(id.v, true);
                    return true;
                },
                [&](const DCT_ID &id, const CPoolPair &token) {
                    createTokenLockSplitTx(id.v, false);
                    return true;
                },
                mnview);
            AddSplitEVMTxs(blockCtx, lockSplitMapEVM);
        }
    }

    XVM xvm{};
    if (isEvmEnabledForBlock) {
        auto res =
            XResultValueLogged(evm_try_unsafe_construct_block_in_template(result, evmTemplate->GetTemplate(), true));
        if (!res) {
            return Res::Err("Failed to construct block");
        }
        auto blockResult = *res;
        auto blockHash = uint256::FromByteArray(blockResult.block_hash).GetHex();
        xvm = XVM{
            0, {0, blockHash, blockResult.total_burnt_fees, blockResult.total_priority_fees, evmBeneficiary}
        };
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

    if (nHeight >= consensus.DF8EunosHeight) {
        auto foundationValue = CalculateCoinbaseReward(blockReward, consensus.dist.community);
        if (nHeight < consensus.DF20GrandCentralHeight) {
            coinbaseTx.vout.resize(2);
            // Community payment always expected
            coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
            coinbaseTx.vout[1].nValue = foundationValue;
        }

        // Explicitly set miner reward
        if (nHeight >= consensus.DF11FortCanningHeight) {
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

        LogPrint(BCLog::STAKING,
                 "%s: post Eunos logic. Block reward %d Miner share %d foundation share %d\n",
                 __func__,
                 blockReward,
                 coinbaseTx.vout[0].nValue,
                 foundationValue);
    } else if (nHeight >= consensus.DF1AMKHeight) {
        // assume community non-utxo funding:
        for (const auto &kv : consensus.blockTokenRewardsLegacy) {
            coinbaseTx.vout[0].nValue -= blockReward * kv.second / COIN;
        }
        // Pinch off foundation share
        if (!consensus.foundationShareScript.empty() && consensus.foundationShareDFIP1 != 0) {
            coinbaseTx.vout.resize(2);
            coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
            coinbaseTx.vout[1].nValue =
                blockReward * consensus.foundationShareDFIP1 /
                COIN;  // the main difference is that new FS is a %% from "base" block reward and no fees involved
            coinbaseTx.vout[0].nValue -= coinbaseTx.vout[1].nValue;

            LogPrint(BCLog::STAKING, "%s: post AMK logic, foundation share %d\n", __func__, coinbaseTx.vout[1].nValue);
        }
    } else {  // pre-AMK logic:
        // Pinch off foundation share
        CAmount foundationsReward = coinbaseTx.vout[0].nValue * consensus.foundationShare / 100;
        if (!consensus.foundationShareScript.empty() && consensus.foundationShare != 0) {
            if (pcustomcsview->GetFoundationsDebt() < foundationsReward) {
                coinbaseTx.vout.resize(2);
                coinbaseTx.vout[1].scriptPubKey = consensus.foundationShareScript;
                coinbaseTx.vout[1].nValue = foundationsReward - pcustomcsview->GetFoundationsDebt();
                coinbaseTx.vout[0].nValue -= coinbaseTx.vout[1].nValue;

                LogPrint(
                    BCLog::STAKING, "%s: pre AMK logic, foundation share %d\n", __func__, coinbaseTx.vout[1].nValue);
            } else {
                pcustomcsview->SetFoundationsDebt(pcustomcsview->GetFoundationsDebt() - foundationsReward);
            }
        }
    }

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));

    pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, consensus);
    pblocktemplate->vTxFees[0] = -nFees;

    LogPrint(BCLog::STAKING,
             "%s: block weight: %u txs: %u fees: %ld sigops %d\n",
             __func__,
             GetBlockWeight(*pblock),
             nBlockTx,
             nFees,
             nBlockSigOpsCost);

    // Fill in header
    pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    pblock->deprecatedHeight = pindexPrev->nHeight + 1;
    pblock->nBits = pos::GetNextWorkRequired(pindexPrev, pblock->nTime, consensus);
    if (myIDs) {
        pblock->stakeModifier = pos::ComputeStakeModifier(pindexPrev->stakeModifier, myIDs->first);
    }

    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    if (nHeight >= chainparams.GetConsensus().DF8EunosHeight &&
        nHeight < chainparams.GetConsensus().DF9EunosKampungHeight) {
        // includes coinbase account changes
        ApplyGeneralCoinbaseTx(mnview, *(pblock->vtx[0]), nHeight, nFees, chainparams.GetConsensus());
        pblock->hashMerkleRoot = Hash2(pblock->hashMerkleRoot, mnview.MerkleRoot());
    }

    LogPrint(BCLog::BENCH,
             "%s packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
             __func__,
             0.001 * (nTime1 - nTimeStart),
             nPackagesSelected,
             nDescendantsUpdated,
             0.001 * (nTime2 - nTime1),
             0.001 * (nTime2 - nTimeStart));

    return {std::move(pblocktemplate), Res::Ok()};
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries &testSet) {
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end();) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const {
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight) {
        return false;
    }
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries &package) {
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff)) {
            return false;
        }
        if (!fIncludeWitness && it->GetTx().HasWitness()) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter) {
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

void BlockAssembler::RemoveFromBlock(CTxMemPool::txiter iter) {
    const auto &tx = iter->GetTx();
    for (auto blockIt = pblock->vtx.begin(); blockIt != pblock->vtx.end(); ++blockIt) {
        auto current = *blockIt;
        if (!current || current->GetHash() != tx.GetHash()) {
            continue;
        }

        pblock->vtx.erase(blockIt);
        if (pblocktemplate) {
            auto &vTxFees = pblocktemplate->vTxFees;
            for (auto it = vTxFees.begin(); it != vTxFees.end(); ++it) {
                if (*it == iter->GetFee()) {
                    vTxFees.erase(it);
                    break;
                }
            }
            auto &vTxSigOpsCost = pblocktemplate->vTxSigOpsCost;
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

void BlockAssembler::RemoveFromBlock(const CTxMemPool::setEntries &txIterSet, bool removeDescendants) {
    if (txIterSet.empty()) {
        return;
    }
    std::set<uint256> txHashes;
    for (auto iter : txIterSet) {
        RemoveFromBlock(iter);
        txHashes.insert(iter->GetTx().GetHash());
    }
    if (!removeDescendants) {
        return;
    }
    CTxMemPool::setEntries descendantTxsToErase;
    for (const auto &txIter : inBlock) {
        auto &tx = txIter->GetTx();
        for (const auto &vin : tx.vin) {
            if (txHashes.count(vin.prevout.hash)) {
                descendantTxsToErase.insert(txIter);
            }
        }
    }
    RemoveFromBlock(descendantTxsToErase, true);
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries &alreadyAdded,
                                           indexed_modified_transaction_set &mapModifiedTxSet) {
    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc)) {
                continue;
            }
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
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it,
                                    indexed_modified_transaction_set &mapModifiedTxSet,
                                    CTxMemPool::setEntries &failedTxSet) {
    assert(it != mempool.mapTx.end());
    return mapModifiedTxSet.count(it) || inBlock.count(it) || failedTxSet.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries &package,
                                  std::vector<CTxMemPool::txiter> &sortedEntries) {
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByEntryTime());
}

bool BlockAssembler::EvmTxPreapply(EvmTxPreApplyContext &ctx) {
    const auto &txIter = ctx.txIter;
    const auto &evmTemplate = ctx.evmTemplate;
    const auto &failedTxSet = ctx.failedTxEntries;
    auto &failedNonces = ctx.failedNonces;
    auto &failedNoncesLookup = ctx.failedNoncesLookup;
    auto &[txNonce, txSender] = txIter->GetEVMAddrAndNonce();

    CrossBoundaryResult result;
    const auto expectedNonce =
        evm_try_unsafe_get_next_valid_nonce_in_template(result, evmTemplate->GetTemplate(), txSender);
    if (!result.ok) {
        return false;
    }

    if (txNonce < expectedNonce) {
        return false;
    } else if (txNonce > expectedNonce) {
        if (!failedTxSet.count(txIter)) {
            auto it = failedNonces.emplace(txNonce, txIter);
            failedNoncesLookup.emplace(txIter->GetTx().GetHash(), it);
        }
        return false;
    }

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
void BlockAssembler::addPackageTxs(int &nPackagesSelected,
                                   int &nDescendantsUpdated,
                                   int nHeight,
                                   std::map<uint256, CAmount> &txFees,
                                   BlockContext &blockCtx) {
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

    // Keep track of EVM entries that failed nonce check
    std::multimap<uint64_t, CTxMemPool::txiter> failedNonces;

    // Quick lookup for failedNonces entries
    std::map<uint256, CTxMemPool::FailedNonceIterator> failedNoncesLookup;

    const auto isEvmEnabledForBlock = blockCtx.GetEVMEnabledForBlock();
    const auto &evmTemplate = blockCtx.GetEVMTemplate();
    auto &view = blockCtx.GetView();

    // Block gas limit
    while (mi != mempool.mapTx.get<T>().end() || !mapModifiedTxSet.empty() || !failedNonces.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<T>().end() &&
            SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTxSet, failedTxSet)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTxSet?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTxSet.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<T>().end() && mapModifiedTxSet.empty()) {
            const auto it = failedNonces.begin();
            iter = it->second;
            failedNonces.erase(it);
            failedNoncesLookup.erase(iter->GetTx().GetHash());
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

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight > nBlockMaxWeight - 4000) {
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

        // Temporary views
        CCoinsViewCache coinsCache(&coinsView);
        CCustomCSView cache(view);

        // Track failed custom TX. Used for removing EVM TXs from the queue.
        uint256 failedCustomTx;

        // Apply and check custom TXs in order
        for (const auto &entry : sortedEntries) {
            const CTransaction &tx = entry->GetTx();

            // Do not double check already checked custom TX. This will be an ancestor of current TX.
            if (checkedDfTxHashSet.find(tx.GetHash()) != checkedDfTxHashSet.end()) {
                continue;
            }

            // temporary view to ensure failed tx
            // to not be kept in parent view
            CCoinsViewCache coins(&coinsCache);

            // allow coin override, tx with same inputs
            // will be removed for block while we connect it
            AddCoins(coins, tx, nHeight, false);  // do not check

            CustomTxType txType = entry->GetCustomTxType();

            // Only check custom TXs
            if (txType != CustomTxType::None) {
                const auto evmType = txType == CustomTxType::EvmTx || txType == CustomTxType::TransferDomain;
                if (evmType) {
                    if (!isEvmEnabledForBlock) {
                        customTxPassed = false;
                        break;
                    }
                    auto evmTxCtx = EvmTxPreApplyContext{
                        entry,
                        evmTemplate,
                        failedNonces,
                        failedNoncesLookup,
                        failedTxSet,
                    };
                    auto res = EvmTxPreapply(evmTxCtx);
                    if (res) {
                        customTxPassed = true;
                    } else {
                        failedTxSet.insert(entry);
                        failedCustomTx = tx.GetHash();
                        customTxPassed = false;
                        break;
                    }
                }

                auto txCtx = TransactionContext{
                    coins,
                    tx,
                    blockCtx,
                };

                // Copy block context and update to cache view
                BlockContext blockCtxTxView{blockCtx, cache};

                const auto res = ApplyCustomTx(blockCtxTxView, txCtx);
                // Not okay invalidate, undo and skip
                if (!res.ok) {
                    failedTxSet.insert(entry);
                    failedCustomTx = tx.GetHash();
                    customTxPassed = false;
                    LogPrintf("%s: Failed %s TX %s: %s\n", __func__, ToString(txType), tx.GetHash().GetHex(), res.msg);
                    break;
                }

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

            // Remove from checked TX set
            for (const auto &entry : sortedEntries) {
                checkedDfTxHashSet.erase(entry->GetTx().GetHash());
            }

            if (sortedEntries.size() <= 1) {
                continue;
            }

            // Remove entries from queue if first EVM TX is not the failed TX.
            for (const auto &entry : sortedEntries) {
                auto entryTxType = entry->GetCustomTxType();
                auto entryHash = entry->GetTx().GetHash();

                if (entryTxType == CustomTxType::EvmTx || entryTxType == CustomTxType::TransferDomain) {
                    // If the first TX in a failed set is not the failed TX
                    // then remove from queue, otherwise it has not been added.
                    if (entryHash != failedCustomTx) {
                        CrossBoundaryResult result;
                        evm_try_unsafe_remove_txs_above_hash_in_template(
                            result, evmTemplate->GetTemplate(), entryHash.GetByteArray());
                        if (!result.ok) {
                            LogPrintf("%s: Unable to remove %s from queue. Will result in a block hash mismatch.\n",
                                      __func__,
                                      entryHash.ToString());
                        }
                    }
                    break;
                } else if (entryHash == failedCustomTx) {
                    // Failed before getting to an EVM TX. Break out.
                    break;
                }
            }

            continue;
        }

        // Flush the views now that add sortedEntries are confirmed successful
        cache.Flush();
        coinsCache.Flush();

        for (const auto &entry : sortedEntries) {
            auto &hash = entry->GetTx().GetHash();
            if (failedNoncesLookup.count(hash)) {
                auto &it = failedNoncesLookup.at(hash);
                failedNonces.erase(it);
                failedNoncesLookup.erase(hash);
            }
            txFees.emplace(hash, entry->GetFee());
            AddToBlock(entry);
            // Erase from the modified set, if present
            mapModifiedTxSet.erase(entry);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTxSet);
    }
}

void IncrementExtraNonce(CBlock *pblock, const CBlockIndex *pindexPrev, unsigned int &nExtraNonce) {
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1;  // Height first in coinbase required for block.version=2
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

    // Only using one item a time to avoid outdata block data
    boost::lockfree::queue<std::vector<ThreadStaker::Args> *> stakersParamsQueue(1);

    Staker::Status Staker::init(const CChainParams &chainparams) {
        if (!chainparams.GetConsensus().pos.allowMintingWithoutPeers) {
            if (!g_connman) {
                throw std::runtime_error("Error: Peer-to-peer functionality missing or disabled");
            }

            if (!chainparams.GetConsensus().pos.allowMintingWithoutPeers &&
                g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) {
                return Status::initWaiting;
            }

            if (::ChainstateActive().IsInitialBlockDownload()) {
                return Status::initWaiting;
            }

            if (::ChainstateActive().IsDisconnectingTip()) {
                return Status::stakeWaiting;
            }
        }
        return Status::stakeReady;
    }

    Staker::Status Staker::stake(const CChainParams &chainparams, const ThreadStaker::Args &args) {
        bool found = false;

        const auto &operatorId = args.operatorID;
        const auto &masternodeID = args.masternode;
        const auto creationHeight = args.creationHeight;
        const auto &scriptPubKey = args.coinbaseScript;
        const auto subNode = args.subNode;
        CBlockIndex *tip{};
        int64_t blockHeight{};
        uint32_t mintedBlocks{};
        int64_t blockTime{};
        int64_t subNodeBlockTime{};

        {
            LOCK(cs_main);
            tip = ::ChainActive().Tip();
            blockHeight = tip->nHeight + 1;
            blockTime = std::max(tip->GetMedianTimePast() + 1, GetAdjustedTime());
            const auto nodePtr = pcustomcsview->GetMasternode(masternodeID);
            if (!nodePtr || !nodePtr->IsActive(blockHeight, *pcustomcsview)) {
                return Status::initWaiting;
            }
            mintedBlocks = nodePtr->mintedBlocks;
            const auto timeLock = pcustomcsview->GetTimelock(masternodeID, *nodePtr, blockHeight);
            if (!timeLock) {
                return Status::initWaiting;
            }
            subNodeBlockTime =
                pcustomcsview->GetBlockTimes(operatorId, blockHeight, creationHeight, *timeLock)[subNode];
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
                if (gArgs.GetBoolArg("-ascendingstaketime", false) ||
                    blockHeight >= static_cast<int64_t>(Params().GetConsensus().DF24Height)) {
                    // Set time to last block time. New blocks must be after the last block.
                    nLastCoinStakeSearchTime = tip->GetBlockTime();
                } else {
                    // Plus one to avoid time-too-old error on exact median time.
                    nLastCoinStakeSearchTime = tip->GetMedianTimePast() + 1;
                }
            }

            lastBlockSeen = tip->GetBlockHash();
        }

        withSearchInterval(
            [&](const int64_t currentTime, const int64_t lastSearchTime, const int64_t futureTime) {
                // update last block creation attempt ts for the master node here
                {
                    std::unique_lock l{pos::cs_MNLastBlockCreationAttemptTs};
                    pos::Staker::mapMNLastBlockCreationAttemptTs[masternodeID] = GetTime();
                }
                CheckContextState ctxState{subNode};
                // Search backwards in time first
                if (currentTime > lastSearchTime) {
                    for (uint32_t t = 0; t < currentTime - lastSearchTime; ++t) {
                        if (ShutdownRequested()) {
                            break;
                        }

                        blockTime = (static_cast<uint32_t>(currentTime) - t);

                        if (pos::CheckKernelHash(stakeModifier,
                                                 nBits,
                                                 creationHeight,
                                                 blockTime,
                                                 blockHeight,
                                                 masternodeID,
                                                 chainparams.GetConsensus(),
                                                 subNodeBlockTime,
                                                 ctxState)) {
                            LogPrint(BCLog::STAKING,
                                     "MakeStake: kernel found. height: %d time: %d\n",
                                     blockHeight,
                                     blockTime);

                            found = true;
                            break;
                        }

                        std::this_thread::yield();  // give a slot to other threads
                    }
                }

                if (!found) {
                    // Search from current time or lastSearchTime set in the future
                    int64_t searchTime = lastSearchTime > currentTime ? lastSearchTime : currentTime;

                    // Search forwards in time
                    for (uint32_t t = 1; t <= futureTime - searchTime; ++t) {
                        if (ShutdownRequested()) {
                            break;
                        }

                        blockTime = (static_cast<uint32_t>(searchTime) + t);

                        if (pos::CheckKernelHash(stakeModifier,
                                                 nBits,
                                                 creationHeight,
                                                 blockTime,
                                                 blockHeight,
                                                 masternodeID,
                                                 chainparams.GetConsensus(),
                                                 subNodeBlockTime,
                                                 ctxState)) {
                            LogPrint(BCLog::STAKING,
                                     "MakeStake: kernel found. height: %d time: %d\n",
                                     blockHeight,
                                     blockTime);

                            found = true;
                            break;
                        }

                        std::this_thread::yield();  // give a slot to other threads
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

        auto &pblocktemplate = *res;
        auto pblock = std::make_shared<CBlock>(pblocktemplate->block);

        pblock->nBits = nBits;
        pblock->mintedBlocks = mintedBlocks + 1;
        pblock->stakeModifier = std::move(stakeModifier);

        LogPrint(BCLog::STAKING,
                 "Running Staker with %u common transactions in block (%u bytes)\n",
                 pblock->vtx.size() - 1,
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
    void Staker::withSearchInterval(F &&f, int64_t height) {
        if (height >= Params().GetConsensus().DF10EunosPayaHeight) {
            // Mine up to max future minus 1 second buffer
            nFutureTime = GetAdjustedTime() + (MAX_FUTURE_BLOCK_TIME_EUNOSPAYA - 1);  // 29 seconds
        } else {
            // Mine up to max future minus 5 second buffer
            nFutureTime = GetAdjustedTime() + (MAX_FUTURE_BLOCK_TIME_DAKOTACRESCENT - 5);  // 295 seconds
        }

        if (nFutureTime > nLastCoinStakeSearchTime) {
            f(GetAdjustedTime(), nLastCoinStakeSearchTime, nFutureTime);
        }
    }

    void ThreadStaker::operator()(CChainParams chainparams) {
        uint32_t nPastFailures{};

        auto wallets = GetWallets();

        LogPrintf("ThreadStaker: started.\n");

        while (!ShutdownRequested()) {
            while (fImporting || fReindex) {
                if (ShutdownRequested()) {
                    return;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(900));
            }

            while (stakersParamsQueue.empty()) {
                if (ShutdownRequested()) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            std::vector<ThreadStaker::Args> *localStakersParams{};
            stakersParamsQueue.pop(localStakersParams);
            if (!localStakersParams) {
                continue;
            }

            for (auto arg : *localStakersParams) {
                if (ShutdownRequested()) {
                    break;
                }

                const auto operatorName = arg.operatorID.GetHex();

                pos::Staker staker;

                try {
                    auto status = staker.init(chainparams);
                    if (status == Staker::Status::stakeReady) {
                        status = staker.stake(chainparams, arg);
                    }
                    if (status == Staker::Status::minted) {
                        LogPrintf("ThreadStaker: (%s) minted a block!\n", operatorName);
                        nPastFailures = 0;
                    } else if (status == Staker::Status::initWaiting) {
                        LogPrintCategoryOrThreadThrottled(BCLog::STAKING,
                                                          "init_waiting",
                                                          1000 * 60 * 10,
                                                          "ThreadStaker: (%s) waiting init...\n",
                                                          operatorName);
                    } else if (status == Staker::Status::stakeWaiting) {
                        LogPrintCategoryOrThreadThrottled(BCLog::STAKING,
                                                          "no_kernel_found",
                                                          1000 * 60 * 10,
                                                          "ThreadStaker: (%s) Staked, but no kernel found yet.\n",
                                                          operatorName);
                    }
                } catch (const std::runtime_error &e) {
                    LogPrintf("ThreadStaker: (%s) runtime error: %s, nPastFailures: %d\n",
                              e.what(),
                              operatorName,
                              nPastFailures);

                    if (!nPastFailures) {
                        LOCK2(cs_main, mempool.cs);
                        mempool.rebuildViews();
                    } else {
                        // Could be failed TX in mempool, wipe mempool and allow loop to continue.
                        LOCK(cs_main);
                        mempool.clear();
                    }

                    ++nPastFailures;
                }
            }

            // Lock free queue does not work with smart pointers. Need to delete manually.
            delete localStakersParams;

            // Set search period to last time set
            Staker::nLastCoinStakeSearchTime = Staker::nFutureTime;
        }
    }

    void stakingManagerThread(std::vector<std::shared_ptr<CWallet>> wallets, const int subnodeCount) {
        auto operators = gArgs.GetArgs("-masternode_operator");

        if (fMockNetwork) {
            auto mocknet_operator = "df1qu04hcpd3untnm453mlkgc0g9mr9ap39lyx4ajc";
            operators.push_back(mocknet_operator);
        }

        std::map<CKeyID, CKey> minterKeyMap;

        while (!ShutdownRequested()) {
            {
                LOCK(cs_main);

                auto newStakersParams = new std::vector<ThreadStaker::Args>;
                std::multimap<uint64_t, ThreadStaker::Args> targetMultiplierMap;
                int totalSubnodes{};
                std::set<std::string> operatorsSet;

                for (const auto &op : operators) {
                    // Do not process duplicate operator
                    if (operatorsSet.count(op)) {
                        continue;
                    }
                    operatorsSet.insert(op);

                    ThreadStaker::Args stakerParams;
                    auto &operatorId = stakerParams.operatorID;
                    auto &coinbaseScript = stakerParams.coinbaseScript;
                    auto &creationHeight = stakerParams.creationHeight;
                    auto &masternode = stakerParams.masternode;

                    CTxDestination destination = DecodeDestination(op);
                    operatorId = CKeyID::FromOrDefaultDestination(destination, KeyType::MNOperatorKeyType);
                    if (operatorId.IsNull()) {
                        continue;
                    }

                    // Load from map to avoid locking wallet
                    if (minterKeyMap.count(operatorId)) {
                        stakerParams.minterKey = minterKeyMap.at(operatorId);
                    } else {
                        bool found{};
                        for (auto wallet : wallets) {
                            LOCK(wallet->cs_wallet);
                            if ((::IsMine(*wallet, destination) & ISMINE_SPENDABLE) &&
                                wallet->GetKey(operatorId, stakerParams.minterKey)) {
                                minterKeyMap[operatorId] = stakerParams.minterKey;
                                found = true;
                                break;
                            }
                        }

                        if (!found) {
                            continue;
                        }
                    }

                    const auto masternodeID = pcustomcsview->GetMasternodeIdByOperator(operatorId);
                    if (!masternodeID) {
                        continue;
                    }

                    masternode = *masternodeID;
                    auto tip = ::ChainActive().Tip();
                    const auto blockHeight = tip->nHeight + 1;

                    const auto nodePtr = pcustomcsview->GetMasternode(masternode);
                    if (!nodePtr || !nodePtr->IsActive(blockHeight, *pcustomcsview)) {
                        continue;
                    }

                    // determine coinbase script for minting thread
                    const auto customRewardAddressStr = gArgs.GetArg("-rewardaddress", "");
                    const auto customRewardDest = customRewardAddressStr.empty()
                                                      ? CNoDestination{}
                                                      : DecodeDestination(customRewardAddressStr, Params());

                    CTxDestination ownerDest = FromOrDefaultKeyIDToDestination(
                        nodePtr->ownerAuthAddress, TxDestTypeToKeyType(nodePtr->ownerType), KeyType::MNOwnerKeyType);

                    CTxDestination rewardDest;
                    if (nodePtr->rewardAddressType != 0) {
                        rewardDest = FromOrDefaultKeyIDToDestination(nodePtr->rewardAddress,
                                                                     TxDestTypeToKeyType(nodePtr->rewardAddressType),
                                                                     KeyType::MNRewardKeyType);
                    }

                    if (IsValidDestination(rewardDest)) {
                        coinbaseScript = GetScriptForDestination(rewardDest);
                    } else if (IsValidDestination(customRewardDest)) {
                        coinbaseScript = GetScriptForDestination(customRewardDest);
                    } else if (IsValidDestination(ownerDest)) {
                        coinbaseScript = GetScriptForDestination(ownerDest);
                    } else {
                        continue;
                    }

                    const auto timeLock = pcustomcsview->GetTimelock(masternode, *nodePtr, blockHeight);
                    if (!timeLock) {
                        continue;
                    }

                    creationHeight = nodePtr->creationHeight;

                    // Get sub node block times
                    const auto subNodesBlockTimes =
                        pcustomcsview->GetBlockTimes(operatorId, blockHeight, creationHeight, *timeLock);

                    auto loops = GetTimelockLoops(*timeLock);
                    if (blockHeight < Params().GetConsensus().DF10EunosPayaHeight) {
                        loops = 1;
                    }

                    for (uint8_t i{}; i < loops; ++i) {
                        const auto targetMultiplier =
                            CalcCoinDayWeight(Params().GetConsensus(), GetTime(), subNodesBlockTimes[i]).GetLow64();

                        stakerParams.subNode = i;

                        targetMultiplierMap.emplace(targetMultiplier, stakerParams);

                        ++totalSubnodes;
                    }
                }

                int maxMultiplier{57};
                auto remainingSubNodes = subnodeCount;
                if (remainingSubNodes > totalSubnodes) {
                    remainingSubNodes = totalSubnodes;
                }

                for (int key{maxMultiplier}; key > 0 && remainingSubNodes > 0; --key) {
                    const auto keyCount = targetMultiplierMap.count(key);
                    if (!keyCount) {
                        continue;
                    }

                    if (keyCount <= remainingSubNodes) {
                        auto range = targetMultiplierMap.equal_range(key);
                        for (auto it = range.first; it != range.second; ++it) {
                            newStakersParams->push_back(it->second);
                            --remainingSubNodes;
                        }
                    } else {
                        // Store elements in a temporary vector
                        std::vector<ThreadStaker::Args> temp;
                        auto range = targetMultiplierMap.equal_range(key);
                        for (auto it = range.first; it != range.second; ++it) {
                            temp.push_back(it->second);
                        }

                        // Shuffle the elements
                        std::random_device rd;
                        std::default_random_engine rng(rd());
                        std::shuffle(temp.begin(), temp.end(), rng);

                        // Select the desired number of elements
                        for (size_t i{}; i < remainingSubNodes; ++i) {
                            newStakersParams->push_back(temp[i]);
                        }
                        break;
                    }
                }

                // Push the new stakersParams onto the queue
                stakersParamsQueue.push(newStakersParams);
            }

            while (!stakersParamsQueue.empty() && !ShutdownRequested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(900));
            }
        }
    }

    bool StartStakingThreads(std::vector<std::thread> &threadGroup) {
        auto wallets = GetWallets();
        if (wallets.empty()) {
            LogPrintf("Warning! wallets not found\n");
            return false;
        }

        const auto minerStrategy = gArgs.GetArg("-minerstrategy", "none");

        auto subnodeCount{std::numeric_limits<int>::max()};
        try {
            subnodeCount = std::stoi(minerStrategy);
        } catch (const std::invalid_argument &) {
            // Expected for "none" value or other future non-numeric strategys
        } catch (const std::out_of_range &) {
            LogPrintf("-minerstrategy out of range: too large to fit in an integer\n");
            return false;
        }

        if (subnodeCount <= 0) {
            LogPrintf("-minerstrategy must be set to more than 0\n");
            return false;
        }

        // Run staking manager thread
        threadGroup.emplace_back(
            TraceThread<std::function<void()>>, "CoinStakerManager", [=, wallets = std::move(wallets)]() {
                stakingManagerThread(std::move(wallets), subnodeCount);
            });

        // Mint proof-of-stake blocks in background
        threadGroup.emplace_back(TraceThread<std::function<void()>>, "CoinStaker", []() {
            ThreadStaker threadStaker;
            threadStaker(Params());
        });

        return true;
    }

}  // namespace pos

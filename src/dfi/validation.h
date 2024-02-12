// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_VALIDATION_H
#define DEFI_DFI_VALIDATION_H

#include <amount.h>

struct CAuctionBatch;
class CBlock;
class CBlockIndex;
class CBurnHistoryStorage;
class CChainParams;
class CCoinsViewCache;
class CCustomCSView;
class CVaultAssets;

using CreationTxs = std::map<uint32_t, std::pair<uint256, std::vector<std::pair<DCT_ID, uint256>>>>;

void ProcessDeFiEvent(const CBlock &block,
                      const CBlockIndex *pindex,
                      const CCoinsViewCache &view,
                      const CreationTxs &creationTxs,
                      BlockContext &blockCtx);

Res ProcessDeFiEventFallible(const CBlock &block,
                             const CBlockIndex *pindex,
                             CCustomCSView &mnview,
                             const CChainParams &chainparams,
                             const std::shared_ptr<CScopedTemplate> &evmTemplate,
                             const bool isEvmEnabledForBlock);

std::vector<CAuctionBatch> CollectAuctionBatches(const CVaultAssets &vaultAssets,
                                                 const TAmounts &collBalances,
                                                 const TAmounts &loanBalances);

#endif  // DEFI_DFI_VALIDATION_H

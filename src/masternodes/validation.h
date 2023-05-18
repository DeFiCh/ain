// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_VALIDATION_H
#define DEFI_MASTERNODES_VALIDATION_H

#include <amount.h>

struct CAuctionBatch;
class CBlock;
class CBlockIndex;
class CChainParams;
class CCoinsViewCache;
class CVaultAssets;
class CCustomCSView;

using CreationTxs = std::map<uint32_t, std::pair<uint256, std::vector<std::pair<DCT_ID, uint256>>>>;

void ProcessDeFiEvent(const CBlock &block, const CBlockIndex* pindex, CCustomCSView& mnview, const CCoinsViewCache& view, const CChainParams& chainparams, const CreationTxs &creationTxs, const uint64_t evmContext);
std::vector<CAuctionBatch> CollectAuctionBatches(const CVaultAssets& vaultAssets, const TAmounts& collBalances, const TAmounts& loanBalances);


#endif  // DEFI_MASTERNODES_VALIDATION_H

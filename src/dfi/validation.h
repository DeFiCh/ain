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
struct TokenAmount;

constexpr CAmount DEFAULT_FS_LIQUIDITY_BLOCK_PERIOD = 28 * 2880;
constexpr CAmount DEFAULT_LIQUIDITY_CALC_SAMPLING_PERIOD = 120;
constexpr CAmount DEFAULT_AVERAGE_LIQUIDITY_PERCENTAGE = COIN / 10;

using CreationTxs = std::map<uint32_t, std::pair<uint256, std::vector<std::pair<DCT_ID, uint256>>>>;

void ProcessDeFiEvent(const CBlock &block,
                      const CBlockIndex *pindex,
                      const CCoinsViewCache &view,
                      const CreationTxs &creationTxs,
                      BlockContext &blockCtx);

Res ProcessDeFiEventFallible(const CBlock &block,
                             const CBlockIndex *pindex,
                             const CChainParams &chainparams,
                             const CreationTxs &creationTxs,
                             BlockContext &blockCtx);

std::vector<CAuctionBatch> CollectAuctionBatches(const CVaultAssets &vaultAssets,
                                                 const TAmounts &collBalances,
                                                 const TAmounts &loanBalances);

Res GetTokenSuffix(const CCustomCSView &view, const ATTRIBUTES &attributes, const uint32_t id, std::string &newSuffix);

bool ExecuteTokenMigrationEVM(std::size_t mnview_ptr, const TokenAmount oldAmount, TokenAmount &newAmount);
Res ExecuteTokenMigrationTransferDomain(CCustomCSView &view, CTokenAmount &amount);

#endif  // DEFI_DFI_VALIDATION_H

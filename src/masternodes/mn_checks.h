// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MN_CHECKS_H
#define DEFI_MASTERNODES_MN_CHECKS_H

#include <consensus/params.h>
#include <consensus/tx_check.h>
#include <masternodes/customtx.h>
#include <masternodes/masternodes.h>
#include <masternodes/poolpairs.h>
#include <masternodes/tokens.h>

#include <cstring>
#include <variant>
#include <vector>

class CTransaction;
class CTxMemPool;
class CCoinsViewCache;

class CCustomCSView;
class CHistoryWriters;
class CHistoryErasers;

struct CCustomTxMessageNone{};

using CCustomTxMessage = std::variant<
    CCustomTxMessageNone,
    CCreateMasterNodeMessage,
    CResignMasterNodeMessage,
    CSetForcedRewardAddressMessage,
    CRemForcedRewardAddressMessage,
    CUpdateMasterNodeMessage,
    CCreateTokenMessage,
    CUpdateTokenPreAMKMessage,
    CUpdateTokenMessage,
    CMintTokensMessage,
    CCreatePoolPairMessage,
    CUpdatePoolPairMessage,
    CPoolSwapMessage,
    CPoolSwapMessageV2,
    CLiquidityMessage,
    CRemoveLiquidityMessage,
    CUtxosToAccountMessage,
    CAccountToUtxosMessage,
    CAccountToAccountMessage,
    CAnyAccountsToAccountsMessage,
    CSmartContractMessage,
    CGovernanceMessage,
    CGovernanceHeightMessage,
    CAppointOracleMessage,
    CRemoveOracleAppointMessage,
    CUpdateOracleAppointMessage,
    CSetOracleDataMessage,
    CICXCreateOrderMessage,
    CICXMakeOfferMessage,
    CICXSubmitDFCHTLCMessage,
    CICXSubmitEXTHTLCMessage,
    CICXClaimDFCHTLCMessage,
    CICXCloseOrderMessage,
    CICXCloseOfferMessage,
    CLoanSetCollateralTokenMessage,
    CLoanSetLoanTokenMessage,
    CLoanUpdateLoanTokenMessage,
    CLoanSchemeMessage,
    CDefaultLoanSchemeMessage,
    CDestroyLoanSchemeMessage,
    CVaultMessage,
    CCloseVaultMessage,
    CUpdateVaultMessage,
    CDepositToVaultMessage,
    CWithdrawFromVaultMessage,
    CLoanTakeLoanMessage,
    CLoanPaybackLoanMessage,
    CAuctionBidMessage
>;

CCustomTxMessage customTypeToMessage(CustomTxType txType);
bool IsMempooledCustomTxCreate(const CTxMemPool& pool, const uint256& txid);
Res RpcInfo(const CTransaction& tx, uint32_t height, CustomTxType& type, UniValue& results);
Res CustomMetadataParse(uint32_t height, const Consensus::Params& consensus, const std::vector<unsigned char>& metadata, CCustomTxMessage& txMessage);
Res ApplyCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint64_t time = 0, uint32_t txn = 0, CHistoryWriters* writers = nullptr);
Res CustomTxVisit(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage, uint64_t time = 0);
ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView& mnview, const CTransaction& tx, int height, const uint256& prevStakeModifier, const std::vector<unsigned char>& metadata, const Consensus::Params& consensusParams);
ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView& mnview, const CTransaction& tx, int height, const std::vector<unsigned char>& metadata, const Consensus::Params& consensusParams);
ResVal<CAmount> GetAggregatePrice(CCustomCSView& view, const std::string& token, const std::string& currency, uint64_t lastBlockTime);
bool IsVaultPriceValid(CCustomCSView& mnview, const CVaultId& vaultId, uint32_t height);
Res SwapToDFIOverUSD(CCustomCSView& mnview, DCT_ID tokenId, CAmount amount, CScript const & from, CScript const & to, uint32_t height);

class CPoolSwap {
    const CPoolSwapMessage& obj;
    uint32_t height;
    CAmount result{0};
    DCT_ID currentID;

public:
    std::vector<std::pair<std::string, std::string>> errors;

    CPoolSwap(const CPoolSwapMessage& obj, uint32_t height)
        : obj(obj), height(height) {}

    std::vector<DCT_ID> CalculateSwaps(CCustomCSView& view, bool testOnly = false);
    Res ExecuteSwap(CCustomCSView& view, std::vector<DCT_ID> poolIDs, bool testOnly = false);
    std::vector<std::vector<DCT_ID>> CalculatePoolPaths(CCustomCSView& view);
    CTokenAmount GetResult() { return CTokenAmount{obj.idTokenTo, result}; };
};

#endif // DEFI_MASTERNODES_MN_CHECKS_H

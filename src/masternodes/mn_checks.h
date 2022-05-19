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
    CUpdateMasterNodeMessage,
    CCreateTokenMessage,
    CUpdateTokenPreAMKMessage,
    CUpdateTokenMessage,
    CMintTokensMessage,
    CBurnTokensMessage,
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
    CFutureSwapMessage,
    CGovernanceMessage,
    CGovernanceUnsetMessage,
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
    CLoanPaybackLoanV2Message,
    CAuctionBidMessage,
    CCreatePropMessage,
    CPropVoteMessage
>;

CCustomTxMessage customTypeToMessage(CustomTxType txType, uint8_t version);
bool IsMempooledCustomTxCreate(const CTxMemPool& pool, const uint256& txid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
Res RpcInfo(const CTransaction& tx, uint32_t height, CustomTxType& type, UniValue& results);
Res CustomMetadataParse(uint32_t height, const Consensus::Params& consensus, const std::vector<unsigned char>& metadata, CCustomTxMessage& txMessage);
Res ApplyCustomTx(CCustomCSView& mnview, CFutureSwapView& futureSwapView, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint64_t time = 0, uint256* canSpend = nullptr, uint32_t* customTxExpiration = nullptr, uint32_t txn = 0, CHistoryWriters* writers = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
Res CustomTxVisit(CCustomCSView& mnview, CFutureSwapView &futureSwapView, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage, uint64_t time = 0, uint32_t txn = 0) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView& mnview, const CTransaction& tx, int height, const std::vector<unsigned char>& metadata, const Consensus::Params& consensusParams) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
ResVal<CAmount> GetAggregatePrice(CCustomCSView& view, const std::string& token, const std::string& currency, uint64_t lastBlockTime);
bool IsVaultPriceValid(CCustomCSView& mnview, const CVaultId& vaultId, uint32_t height);
Res SwapToDFIOverUSD(CCustomCSView& mnview, DCT_ID tokenId, CAmount amount, CScript const & from, CScript const & to, uint32_t height);

inline bool OraclePriceFeed(CCustomCSView& view, const CTokenCurrencyPair& priceFeed) {
    // Allow hard coded DUSD/USD
    if (priceFeed.first == "DUSD" && priceFeed.second == "USD") {
        return true;
    }
    bool found = false;
    view.ForEachOracle([&](const COracleId&, COracle oracle) {
        return !(found = oracle.SupportsPair(priceFeed.first, priceFeed.second));
    });
    return found;
}

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

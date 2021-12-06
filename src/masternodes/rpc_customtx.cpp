
#include <core_io.h>
#include <key_io.h>
#include <masternodes/res.h>
#include <masternodes/mn_checks.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <univalue.h>

extern std::string ScriptToString(CScript const& script);

class CCustomTxRpcVisitor
{
    uint32_t height;
    UniValue& rpcInfo;
    CCustomCSView& mnview;
    const CTransaction& tx;

    void tokenInfo(const CToken& token) const {
        rpcInfo.pushKV("name", token.name);
        rpcInfo.pushKV("symbol", token.symbol);
        rpcInfo.pushKV("isDAT", token.IsDAT());
        rpcInfo.pushKV("mintable", token.IsMintable());
        rpcInfo.pushKV("tradeable", token.IsTradeable());
        rpcInfo.pushKV("finalized", token.IsFinalized());
    }

    void customRewardsInfo(const CBalances& rewards) const {
        UniValue rewardArr(UniValue::VARR);
        for (const auto& reward : rewards.balances) {
            if (reward.second > 0) {
                rewardArr.push_back(CTokenAmount{reward.first, reward.second}.ToString());
            }
        }
        if (!rewardArr.empty()) {
            rpcInfo.pushKV("customRewards", rewardArr);
        }
    }

    UniValue accountsInfo(const CAccounts& accounts) const {
        UniValue info(UniValue::VOBJ);
        for (const auto& account : accounts) {
            info.pushKV(ScriptToString(account.first), account.second.ToString());
        }
        return info;
    }

    void tokenCurrencyPairInfo(const std::set<CTokenCurrencyPair>& pairs) const {
        UniValue availablePairs(UniValue::VARR);
        for (const auto& pair : pairs) {
            UniValue uniPair(UniValue::VOBJ);
            uniPair.pushKV("token", pair.first);
            uniPair.pushKV("currency", pair.second);
            availablePairs.push_back(uniPair);
        }
        rpcInfo.pushKV("availablePairs", availablePairs);
    }

public:
    CCustomTxRpcVisitor(const CTransaction& tx, uint32_t height, CCustomCSView& mnview, UniValue& rpcInfo)
        : height(height), rpcInfo(rpcInfo), mnview(mnview), tx(tx) {
    }

    void operator()(const CCreateMasterNodeMessage& obj) const {
        rpcInfo.pushKV("collateralamount", ValueFromAmount(GetMnCollateralAmount(height)));
        rpcInfo.pushKV("masternodeoperator", EncodeDestination(obj.operatorType == PKHashType ?
                                                CTxDestination(PKHash(obj.operatorAuthAddress)) :
                                                CTxDestination(WitnessV0KeyHash(obj.operatorAuthAddress))));
        rpcInfo.pushKV("timelock", CMasternode::GetTimelockToString(static_cast<CMasternode::TimeLock>(obj.timelock)));
    }

    void operator()(const CResignMasterNodeMessage& obj) const {
        rpcInfo.pushKV("id", obj.GetHex());
    }

    void operator()(const CSetForcedRewardAddressMessage& obj) const {
        rpcInfo.pushKV("mc_id", obj.nodeId.GetHex());
        rpcInfo.pushKV("rewardAddress", EncodeDestination(
                obj.rewardAddressType == 1 ?
                    CTxDestination(PKHash(obj.rewardAddress)) :
                    CTxDestination(WitnessV0KeyHash(obj.rewardAddress)))
        );
    }

    void operator()(const CRemForcedRewardAddressMessage& obj) const {
        rpcInfo.pushKV("mc_id", obj.nodeId.GetHex());
    }

    void operator()(const CUpdateMasterNodeMessage& obj) const {
        rpcInfo.pushKV("id", obj.mnId.GetHex());
        rpcInfo.pushKV("masternodeoperator", EncodeDestination(obj.operatorType == PKHashType ?
                                                CTxDestination(PKHash(obj.operatorAuthAddress)) :
                                                CTxDestination(WitnessV0KeyHash(obj.operatorAuthAddress))));
    }

    void operator()(const CCreateTokenMessage& obj) const {
        rpcInfo.pushKV("creationTx", tx.GetHash().GetHex());
        tokenInfo(obj);
    }

    void operator()(const CUpdateTokenPreAMKMessage& obj) const {
        rpcInfo.pushKV("isDAT", obj.isDAT);
    }

    void operator()(const CUpdateTokenMessage& obj) const {
        tokenInfo(obj.token);
    }

    void operator()(const CMintTokensMessage& obj) const {
        for (auto const & kv : obj.balances) {
            if (auto token = mnview.GetToken(kv.first)) {
                auto tokenImpl = static_cast<CTokenImplementation const&>(*token);
                if (auto tokenPair = mnview.GetTokenByCreationTx(tokenImpl.creationTx)) {
                    rpcInfo.pushKV(tokenPair->first.ToString(), ValueFromAmount(kv.second));
                }
            }
        }
    }

    void operator()(const CLiquidityMessage& obj) const {
        CBalances sumTx = SumAllTransfers(obj.from);
        if (sumTx.balances.size() == 2) {
            auto amountA = *sumTx.balances.begin();
            auto amountB = *(std::next(sumTx.balances.begin(), 1));
            rpcInfo.pushKV(amountA.first.ToString(), ValueFromAmount(amountA.second));
            rpcInfo.pushKV(amountB.first.ToString(), ValueFromAmount(amountB.second));
            rpcInfo.pushKV("shareaddress", ScriptToString(obj.shareAddress));
        }
    }

    void operator()(const CRemoveLiquidityMessage& obj) const {
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CUtxosToAccountMessage& obj) const {
        rpcInfo.pushKVs(accountsInfo(obj.to));
    }

    void operator()(const CAccountToUtxosMessage& obj) const {
        rpcInfo.pushKV("from", ScriptToString(obj.from));

        UniValue dest(UniValue::VOBJ);
        for (uint32_t i = obj.mintingOutputsStart; i <  static_cast<uint32_t>(tx.vout.size()); i++) {
            dest.pushKV(ScriptToString(tx.vout[i].scriptPubKey), tx.vout[i].TokenAmount().ToString());
        }
        rpcInfo.pushKV("to", dest);
    }

    void operator()(const CAccountToAccountMessage& obj) const {
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("to", accountsInfo(obj.to));
    }

    void operator()(const CAnyAccountsToAccountsMessage& obj) const {
        rpcInfo.pushKV("from", accountsInfo(obj.from));
        rpcInfo.pushKV("to", accountsInfo(obj.to));
    }

    void operator()(const CSmartContractMessage& obj) const {
        rpcInfo.pushKV("name", obj.name);
        rpcInfo.pushKV("accounts", accountsInfo(obj.accounts));
    }

    void operator()(const CCreatePoolPairMessage& obj) const {
        rpcInfo.pushKV("creationTx", tx.GetHash().GetHex());
        if (auto tokenPair = mnview.GetTokenByCreationTx(tx.GetHash()))
            tokenInfo(tokenPair->second);
        if (auto tokenA = mnview.GetToken(obj.idTokenA))
            rpcInfo.pushKV("tokenA", tokenA->name);
        if (auto tokenB = mnview.GetToken(obj.idTokenB))
            rpcInfo.pushKV("tokenB", tokenB->name);
        rpcInfo.pushKV("commission", ValueFromAmount(obj.commission));
        rpcInfo.pushKV("status", obj.status);
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        customRewardsInfo(obj.rewards);
    }

    void operator()(const CUpdatePoolPairMessage& obj) const {
        rpcInfo.pushKV("commission", ValueFromAmount(obj.commission));
        rpcInfo.pushKV("status", obj.status);
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));

        // Add rewards here before processing them below to avoid adding current rewards
        if (!obj.rewards.balances.empty()) {
            UniValue rewardArr(UniValue::VARR);
            auto& rewards = obj.rewards;
            // Check for special case to wipe rewards
            if (rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
            && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max()) {
                rpcInfo.pushKV("customRewards", rewardArr);
            } else {
                customRewardsInfo(rewards);
            }
        }
    }

    void operator()(const CPoolSwapMessage& obj) const {
        rpcInfo.pushKV("fromAddress", ScriptToString(obj.from));
        rpcInfo.pushKV("fromToken", obj.idTokenFrom.ToString());
        rpcInfo.pushKV("fromAmount", ValueFromAmount(obj.amountFrom));
        rpcInfo.pushKV("toAddress", ScriptToString(obj.to));
        rpcInfo.pushKV("toToken", obj.idTokenTo.ToString());
        rpcInfo.pushKV("maxPrice", ValueFromAmount((obj.maxPrice.integer * COIN) + obj.maxPrice.fraction));
    }

    void operator()(const CPoolSwapMessageV2& obj) const {
        (*this)(obj.swapInfo);
        std::string str;
        for (auto& id : obj.poolIDs) {
            if (auto token = mnview.GetToken(id)) {
                if (!str.empty()) {
                    str += '/';
                }
                str += token->symbol;
            }
        }
        if (!str.empty()) {
            rpcInfo.pushKV("compositeDex", str);
        }
    }

    void operator()(const CGovernanceMessage& obj) const {
        for (const auto& gov : obj.govs) {
            auto& var = gov.second;
            rpcInfo.pushKV(var->GetName(), var->Export());
        }
    }

    void operator()(const CGovernanceHeightMessage& obj) const {
        rpcInfo.pushKV(obj.govVar->GetName(), obj.govVar->Export());
        rpcInfo.pushKV("startHeight", static_cast<uint64_t>(obj.startHeight));
    }

    void operator()(const CAppointOracleMessage& obj) const {
        rpcInfo.pushKV("oracleAddress", ScriptToString(obj.oracleAddress));
        rpcInfo.pushKV("weightage", obj.weightage);
        tokenCurrencyPairInfo(obj.availablePairs);
    }

    void operator()(const CUpdateOracleAppointMessage& obj) const {
        rpcInfo.pushKV("oracleId", obj.oracleId.ToString());
        (*this)(obj.newOracleAppoint);
    }

    void operator()(const CRemoveOracleAppointMessage& obj) const {
        rpcInfo.pushKV("oracleId", obj.oracleId.ToString());
    }

    void operator()(const CSetOracleDataMessage& obj) const {
        rpcInfo.pushKV("oracleId", obj.oracleId.ToString());
        rpcInfo.pushKV("timestamp", obj.timestamp);

        UniValue tokenPrices(UniValue::VARR);
        for (const auto& tokenPice : obj.tokenPrices) {
            const auto& token = tokenPice.first;
            for (const auto& price : tokenPice.second) {
                const auto& currency = price.first;
                auto amount = price.second;

                UniValue uniPair(UniValue::VOBJ);
                uniPair.pushKV("currency", currency);
                uniPair.pushKV("tokenAmount", strprintf("%s@%s", GetDecimaleString(amount), token));
                tokenPrices.push_back(uniPair);
            }
        }
        rpcInfo.pushKV("tokenPrices", tokenPrices);
    }

    void operator()(const CICXCreateOrderMessage& obj) const {
        if (obj.orderType == CICXOrder::TYPE_INTERNAL)
        {
            rpcInfo.pushKV("type","DFC");
            if (auto token = mnview.GetToken(obj.idToken))
                rpcInfo.pushKV("tokenFrom", token->CreateSymbolKey(obj.idToken));
            rpcInfo.pushKV("chainto", CICXOrder::CHAIN_BTC);
        }
        else if (obj.orderType == CICXOrder::TYPE_EXTERNAL)
        {
            rpcInfo.pushKV("type","EXTERNAL");
            rpcInfo.pushKV("chainFrom", CICXOrder::CHAIN_BTC);
            if (auto token = mnview.GetToken(obj.idToken))
                rpcInfo.pushKV("tokenTo", token->CreateSymbolKey(obj.idToken));
            rpcInfo.pushKV("receivePubkey", HexStr(obj.receivePubkey));
        }

        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        rpcInfo.pushKV("amountFrom", ValueFromAmount(obj.amountFrom));
        rpcInfo.pushKV("amountToFill", ValueFromAmount(obj.amountToFill));
        rpcInfo.pushKV("orderPrice", ValueFromAmount(obj.orderPrice));
        auto calcedAmount = MultiplyAmounts(obj.amountToFill, obj.orderPrice);
        rpcInfo.pushKV("amountToFillInToAsset", ValueFromAmount(calcedAmount));
        rpcInfo.pushKV("expiry", static_cast<int>(obj.expiry));
    }

    void operator()(const CICXMakeOfferMessage& obj) const {
        rpcInfo.pushKV("orderTx", obj.orderTx.GetHex());
        rpcInfo.pushKV("amount", ValueFromAmount(obj.amount));
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        if (obj.receivePubkey.IsFullyValid())
            rpcInfo.pushKV("receivePubkey", HexStr(obj.receivePubkey));
        rpcInfo.pushKV("takerFee", ValueFromAmount(obj.takerFee));
        rpcInfo.pushKV("expiry", static_cast<int>(obj.expiry));
    }

    void operator()(const CICXSubmitDFCHTLCMessage& obj) const {
        rpcInfo.pushKV("type", "DFC");
        rpcInfo.pushKV("offerTx", obj.offerTx.GetHex());
        rpcInfo.pushKV("amount", ValueFromAmount(obj.amount));
        rpcInfo.pushKV("hash", obj.hash.GetHex());
        rpcInfo.pushKV("timeout", static_cast<int>(obj.timeout));
    }

    void operator()(const CICXSubmitEXTHTLCMessage& obj) const {
        rpcInfo.pushKV("type", "EXTERNAL");
        rpcInfo.pushKV("offerTx", obj.offerTx.GetHex());
        rpcInfo.pushKV("amount", ValueFromAmount(obj.amount));
        rpcInfo.pushKV("hash", obj.hash.GetHex());
        rpcInfo.pushKV("htlcScriptAddress", obj.htlcscriptAddress);
        rpcInfo.pushKV("ownerPubkey", HexStr(obj.ownerPubkey));
        rpcInfo.pushKV("timeout", static_cast<int>(obj.timeout));
    }

    void operator()(const CICXClaimDFCHTLCMessage& obj) const {
        rpcInfo.pushKV("type", "CLAIM DFC");
        rpcInfo.pushKV("dfchtlcTx", obj.dfchtlcTx.GetHex());
        rpcInfo.pushKV("seed", HexStr(obj.seed));
    }

    void operator()(const CICXCloseOrderMessage& obj) const {
        rpcInfo.pushKV("orderTx", obj.orderTx.GetHex());
    }

    void operator()(const CICXCloseOfferMessage& obj) const {
        rpcInfo.pushKV("offerTx", obj.offerTx.GetHex());
    }

    void operator()(const CLoanSetCollateralTokenMessage& obj) const {
        if (auto token = mnview.GetToken(obj.idToken))
            rpcInfo.pushKV("token", token->CreateSymbolKey(obj.idToken));
        rpcInfo.pushKV("factor", ValueFromAmount(obj.factor));
        rpcInfo.pushKV("fixedIntervalPriceId", obj.fixedIntervalPriceId.first + "/" + obj.fixedIntervalPriceId.second);
        if (obj.activateAfterBlock)
            rpcInfo.pushKV("activateAfterBlock", static_cast<int>(obj.activateAfterBlock));
    }

    void operator()(const CLoanSetLoanTokenMessage& obj) const {
        rpcInfo.pushKV("symbol", obj.symbol);
        rpcInfo.pushKV("name", obj.name);
        rpcInfo.pushKV("fixedIntervalPriceId", obj.fixedIntervalPriceId.first + "/" + obj.fixedIntervalPriceId.second);
        rpcInfo.pushKV("mintable", obj.mintable);
        rpcInfo.pushKV("interest", ValueFromAmount(obj.interest));
    }

    void operator()(const CLoanUpdateLoanTokenMessage& obj) const {
        rpcInfo.pushKV("id", obj.tokenTx.ToString());
        rpcInfo.pushKV("symbol", obj.symbol);
        rpcInfo.pushKV("name", obj.name);
        rpcInfo.pushKV("fixedIntervalPriceId", obj.fixedIntervalPriceId.first + "/" + obj.fixedIntervalPriceId.second);
        rpcInfo.pushKV("mintable", obj.mintable);
        rpcInfo.pushKV("interest", ValueFromAmount(obj.interest));
    }

    void operator()(const CLoanSchemeMessage& obj) const {
        rpcInfo.pushKV("id", obj.identifier);
        rpcInfo.pushKV("mincolratio", static_cast<uint64_t>(obj.ratio));
        rpcInfo.pushKV("interestrate", ValueFromAmount(obj.rate));
        rpcInfo.pushKV("updateHeight", obj.updateHeight);
    }

    void operator()(const CDefaultLoanSchemeMessage& obj) const {
        rpcInfo.pushKV("id", obj.identifier);
    }

    void operator()(const CDestroyLoanSchemeMessage& obj) const {
        rpcInfo.pushKV("id", obj.identifier);
        rpcInfo.pushKV("destroyHeight", obj.destroyHeight);
    }

    void operator()(const CVaultMessage& obj) const {
        // Add Vault attributes
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        rpcInfo.pushKV("loanSchemeId", obj.schemeId);
    }

    void operator()(const CCloseVaultMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("to", ScriptToString(obj.to));
    }

    void operator()(const CUpdateVaultMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        rpcInfo.pushKV("loanSchemeId", obj.schemeId);
    }

    void operator()(const CDepositToVaultMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CWithdrawFromVaultMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("to", ScriptToString(obj.to));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CLoanTakeLoanMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        if (!obj.to.empty())
            rpcInfo.pushKV("to", ScriptToString(obj.to));
        for (auto const & kv : obj.amounts.balances) {
            if (auto token = mnview.GetToken(kv.first)) {
                auto tokenImpl = static_cast<CTokenImplementation const&>(*token);
                if (auto tokenPair = mnview.GetTokenByCreationTx(tokenImpl.creationTx)) {
                    rpcInfo.pushKV(tokenPair->first.ToString(), ValueFromAmount(kv.second));
                }
            }
        }
    }

    void operator()(const CLoanPaybackLoanMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        for (auto const & kv : obj.amounts.balances) {
            if (auto token = mnview.GetToken(kv.first)) {
                auto tokenImpl = static_cast<CTokenImplementation const&>(*token);
                if (auto tokenPair = mnview.GetTokenByCreationTx(tokenImpl.creationTx)) {
                    rpcInfo.pushKV(tokenPair->first.ToString(), ValueFromAmount(kv.second));
                }
            }
        }
    }

    void operator()(const CAuctionBidMessage& obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("index", int64_t(obj.index));
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CCustomTxMessageNone&) const {
    }
};

Res RpcInfo(const CTransaction& tx, uint32_t height, CustomTxType& txType, UniValue& results) {
    std::vector<unsigned char> metadata;
    txType = GuessCustomTxType(tx, metadata);
    if (txType == CustomTxType::None) {
        return Res::Ok();
    }
    auto txMessage = customTypeToMessage(txType);
    auto res = CustomMetadataParse(height, Params().GetConsensus(), metadata, txMessage);
    if (res) {
        CCustomCSView mnview(*pcustomcsview);
        std::visit(CCustomTxRpcVisitor(tx, height, mnview, results), txMessage);
    }
    return res;
}

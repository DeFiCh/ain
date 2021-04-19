
#include <core_io.h>
#include <key_io.h>
#include <masternodes/res.h>
#include <masternodes/mn_checks.h>
#include <primitives/transaction.h>
#include <univalue.h>

#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

extern std::string ScriptToString(CScript const& script);

class CCustomTxRpcVisitor : public boost::static_visitor<void>
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
            info.pushKV(account.first.GetHex(), account.second.ToString());
        }
        return info;
    }

public:
    CCustomTxRpcVisitor(const CTransaction& tx, uint32_t height, CCustomCSView& mnview, UniValue& rpcInfo)
        : height(height), rpcInfo(rpcInfo), mnview(mnview), tx(tx) {
    }

    void operator()(const CCreateMasterNodeMessage& obj) const {
        rpcInfo.pushKV("collateralamount", ValueFromAmount(GetMnCollateralAmount(height)));
        rpcInfo.pushKV("masternodeoperator", EncodeDestination(obj.operatorType == 1 ?
                                                CTxDestination(PKHash(obj.operatorAuthAddress)) :
                                                CTxDestination(WitnessV0KeyHash(obj.operatorAuthAddress))));
    }

    void operator()(const CResignMasterNodeMessage& obj) const {
        rpcInfo.pushKV("id", obj.GetHex());
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
            rpcInfo.pushKV("shareaddress", obj.shareAddress.GetHex());
        }
    }

    void operator()(const CRemoveLiquidityMessage& obj) const {
        rpcInfo.pushKV("from", obj.from.GetHex());
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CUtxosToAccountMessage& obj) const {
        rpcInfo.pushKVs(accountsInfo(obj.to));
    }

    void operator()(const CAccountToUtxosMessage& obj) const {
        rpcInfo.pushKV("from", obj.from.GetHex());

        UniValue dest(UniValue::VOBJ);
        for (uint32_t i = obj.mintingOutputsStart; i <  static_cast<uint32_t>(tx.vout.size()); i++) {
            dest.pushKV(tx.vout[i].scriptPubKey.GetHex(), tx.vout[i].TokenAmount().ToString());
        }
        rpcInfo.pushKV("to", dest);
    }

    void operator()(CAccountToAccountMessage& obj) const {
        rpcInfo.pushKV("from", obj.from.GetHex());
        rpcInfo.pushKV("to", accountsInfo(obj.to));
    }

    void operator()(const CAnyAccountsToAccountsMessage& obj) const {
        rpcInfo.pushKV("from", accountsInfo(obj.from));
        rpcInfo.pushKV("to", accountsInfo(obj.to));
    }

    void operator()(const CCreatePoolPairMessage& obj) const {
        auto tokenA = mnview.GetToken(obj.poolPair.idTokenA);
        auto tokenB = mnview.GetToken(obj.poolPair.idTokenB);
        auto tokenPair = mnview.GetTokenByCreationTx(tx.GetHash());
        if (!tokenA || !tokenB || !tokenPair) {
            return;
        }
        rpcInfo.pushKV("creationTx", tx.GetHash().GetHex());
        tokenInfo(tokenPair->second);
        rpcInfo.pushKV("tokenA", tokenA->name);
        rpcInfo.pushKV("tokenB", tokenB->name);
        rpcInfo.pushKV("commission", ValueFromAmount(obj.poolPair.commission));
        rpcInfo.pushKV("status", obj.poolPair.status);
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.poolPair.ownerAddress));
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
        rpcInfo.pushKV("fromAddress", obj.from.GetHex());
        rpcInfo.pushKV("fromToken", obj.idTokenFrom.ToString());
        rpcInfo.pushKV("fromAmount", ValueFromAmount(obj.amountFrom));
        rpcInfo.pushKV("toAddress", obj.to.GetHex());
        rpcInfo.pushKV("toToken", obj.idTokenTo.ToString());
        rpcInfo.pushKV("maxPrice", ValueFromAmount((obj.maxPrice.integer * COIN) + obj.maxPrice.fraction));
    }

    void operator()(const CGovernanceMessage& obj) const {
        for (const auto& var : obj.govs) {
            rpcInfo.pushKV(var->GetName(), var->Export());
        }
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
        boost::apply_visitor(CCustomTxRpcVisitor(tx, height, mnview, results), txMessage);
    }
    return res;
}

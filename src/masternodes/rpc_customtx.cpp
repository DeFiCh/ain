
#include <core_io.h>
#include <key_io.h>
#include <masternodes/mn_checks.h>
#include <masternodes/res.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <univalue.h>
#include <arith_uint256.h>

extern std::string ScriptToString(const CScript &script);

class CCustomTxRpcVisitor {
    uint32_t height;
    UniValue &rpcInfo;
    CCustomCSView &mnview;
    const CTransaction &tx;

    void tokenInfo(const CToken &token) const {
        rpcInfo.pushKV("name", token.name);
        rpcInfo.pushKV("symbol", token.symbol);
        rpcInfo.pushKV("isDAT", token.IsDAT());
        rpcInfo.pushKV("mintable", token.IsMintable());
        rpcInfo.pushKV("tradeable", token.IsTradeable());
        rpcInfo.pushKV("finalized", token.IsFinalized());
    }

    void customRewardsInfo(const CBalances &rewards) const {
        UniValue rewardArr(UniValue::VARR);
        for (const auto &reward : rewards.balances) {
            if (reward.second > 0) {
                rewardArr.push_back(CTokenAmount{reward.first, reward.second}.ToString());
            }
        }
        if (!rewardArr.empty()) {
            rpcInfo.pushKV("customRewards", rewardArr);
        }
    }

    UniValue accountsInfo(const CAccounts &accounts) const {
        UniValue info(UniValue::VOBJ);
        for (const auto &account : accounts) {
            info.pushKV(ScriptToString(account.first), account.second.ToString());
        }
        return info;
    }

    void tokenCurrencyPairInfo(const std::set<CTokenCurrencyPair> &pairs) const {
        UniValue availablePairs(UniValue::VARR);
        for (const auto &pair : pairs) {
            UniValue uniPair(UniValue::VOBJ);
            uniPair.pushKV("token", pair.first);
            uniPair.pushKV("currency", pair.second);
            availablePairs.push_back(uniPair);
        }
        rpcInfo.pushKV("availablePairs", availablePairs);
    }

    UniValue tokenBalances(const CBalances &balances) const {
        UniValue info(UniValue::VOBJ);
        for (const auto &kv : balances.balances) {
            info.pushKV(kv.first.ToString(), ValueFromAmount(kv.second));
        }
        return info;
    }

public:
    CCustomTxRpcVisitor(const CTransaction &tx, uint32_t height, CCustomCSView &mnview, UniValue &rpcInfo)
        : height(height),
          rpcInfo(rpcInfo),
          mnview(mnview),
          tx(tx) {}

    void operator()(const CCreateMasterNodeMessage &obj) const {
        rpcInfo.pushKV("collateralamount", ValueFromAmount(GetMnCollateralAmount(height)));
        rpcInfo.pushKV("masternodeoperator",
                       EncodeDestination(obj.operatorType == PKHashType
                                             ? CTxDestination(PKHash(obj.operatorAuthAddress))
                                             : CTxDestination(WitnessV0KeyHash(obj.operatorAuthAddress))));
        rpcInfo.pushKV("timelock", CMasternode::GetTimelockToString(static_cast<CMasternode::TimeLock>(obj.timelock)));
    }

    void operator()(const CResignMasterNodeMessage &obj) const { rpcInfo.pushKV("id", obj.GetHex()); }

    void operator()(const CUpdateMasterNodeMessage &obj) const {
        rpcInfo.pushKV("id", obj.mnId.GetHex());
        for (const auto &[updateType, addressPair] : obj.updates) {
            const auto &[addressType, rawAddress] = addressPair;
            if (updateType == static_cast<uint8_t>(UpdateMasternodeType::OperatorAddress)) {
                rpcInfo.pushKV(
                    "operatorAddress",
                    EncodeDestination(addressType == PKHashType ? CTxDestination(PKHash(rawAddress))
                                                                : CTxDestination(WitnessV0KeyHash(rawAddress))));
            } else if (updateType == static_cast<uint8_t>(UpdateMasternodeType::OwnerAddress)) {
                CTxDestination dest;
                if (tx.vout.size() >= 2 && ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
                    rpcInfo.pushKV("ownerAddress",EncodeDestination(dest));
                }
            }
            if (updateType == static_cast<uint8_t>(UpdateMasternodeType::SetRewardAddress)) {
                rpcInfo.pushKV(
                    "rewardAddress",
                    EncodeDestination(addressType == PKHashType ? CTxDestination(PKHash(rawAddress))
                                                                : CTxDestination(WitnessV0KeyHash(rawAddress))));
            } else if (updateType == static_cast<uint8_t>(UpdateMasternodeType::RemRewardAddress)) {
                rpcInfo.pushKV("rewardAddress", "");
            }
        }
    }

    void operator()(const CCreateTokenMessage &obj) const {
        rpcInfo.pushKV("creationTx", tx.GetHash().GetHex());
        tokenInfo(obj);
    }

    void operator()(const CUpdateTokenPreAMKMessage &obj) const { rpcInfo.pushKV("isDAT", obj.isDAT); }

    void operator()(const CUpdateTokenMessage &obj) const { tokenInfo(obj.token); }

    void operator()(const CMintTokensMessage &obj) const {
        rpcInfo.pushKVs(tokenBalances(obj));
        rpcInfo.pushKV("to", ScriptToString(obj.to));
    }

    void operator()(const CBurnTokensMessage &obj) const {
        rpcInfo.pushKVs(tokenBalances(obj.amounts));
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        std::string type;
        switch (obj.burnType) {
            case CBurnTokensMessage::BurnType::TokenBurn:
                type = "TokenBurn";
                break;
            default:
                type = "Unexpected";
        }
        rpcInfo.pushKV("type", type);

        if (auto addr = std::get_if<CScript>(&obj.context); !addr->empty())
            rpcInfo.pushKV("context", ScriptToString(*addr));
    }

    void operator()(const CLiquidityMessage &obj) const {
        CBalances sumTx = SumAllTransfers(obj.from);
        if (sumTx.balances.size() == 2) {
            auto amountA = *sumTx.balances.begin();
            auto amountB = *(std::next(sumTx.balances.begin(), 1));
            rpcInfo.pushKV(amountA.first.ToString(), ValueFromAmount(amountA.second));
            rpcInfo.pushKV(amountB.first.ToString(), ValueFromAmount(amountB.second));
            rpcInfo.pushKV("shareaddress", ScriptToString(obj.shareAddress));
        }
    }

    void operator()(const CRemoveLiquidityMessage &obj) const {
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CUtxosToAccountMessage &obj) const { rpcInfo.pushKVs(accountsInfo(obj.to)); }

    void operator()(const CAccountToUtxosMessage &obj) const {
        rpcInfo.pushKV("from", ScriptToString(obj.from));

        UniValue dest(UniValue::VOBJ);
        for (uint32_t i = obj.mintingOutputsStart; i < static_cast<uint32_t>(tx.vout.size()); i++) {
            dest.pushKV(ScriptToString(tx.vout[i].scriptPubKey), tx.vout[i].TokenAmount().ToString());
        }
        rpcInfo.pushKV("to", dest);
    }

    void operator()(const CAccountToAccountMessage &obj) const {
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("to", accountsInfo(obj.to));
    }

    void operator()(const CAnyAccountsToAccountsMessage &obj) const {
        rpcInfo.pushKV("from", accountsInfo(obj.from));
        rpcInfo.pushKV("to", accountsInfo(obj.to));
    }

    void operator()(const CSmartContractMessage &obj) const {
        rpcInfo.pushKV("name", obj.name);
        rpcInfo.pushKV("accounts", accountsInfo(obj.accounts));
    }

    void operator()(const CFutureSwapMessage &obj) const {
        CTxDestination dest;
        if (ExtractDestination(obj.owner, dest)) {
            rpcInfo.pushKV("owner", EncodeDestination(dest));
        } else {
            rpcInfo.pushKV("owner", "Invalid destination");
        }

        rpcInfo.pushKV("source", obj.source.ToString());
        rpcInfo.pushKV("destination", std::to_string(obj.destination));
    }

    void operator()(const CCreatePoolPairMessage &obj) const {
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

    void operator()(const CUpdatePoolPairMessage &obj) const {
        rpcInfo.pushKV("commission", ValueFromAmount(obj.commission));
        rpcInfo.pushKV("status", obj.status);
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));

        // Add rewards here before processing them below to avoid adding current rewards
        if (!obj.rewards.balances.empty()) {
            UniValue rewardArr(UniValue::VARR);
            auto &rewards = obj.rewards;
            // Check for special case to wipe rewards
            if (rewards.balances.size() == 1 &&
                rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()} &&
                rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max()) {
                rpcInfo.pushKV("customRewards", rewardArr);
            } else {
                customRewardsInfo(rewards);
            }
        }
    }

    void operator()(const CPoolSwapMessage &obj) const {
        rpcInfo.pushKV("fromAddress", ScriptToString(obj.from));
        rpcInfo.pushKV("fromToken", obj.idTokenFrom.ToString());
        rpcInfo.pushKV("fromAmount", ValueFromAmount(obj.amountFrom));
        rpcInfo.pushKV("toAddress", ScriptToString(obj.to));
        rpcInfo.pushKV("toToken", obj.idTokenTo.ToString());
        
        if (checkMaxPoolPrice(obj.maxPrice)) {
            // get max pool price
            PoolPrice price;
            setMaxPoolPrice(price);
            rpcInfo.pushKV("maxPrice", ValueFromAmount((price.integer * COIN) + price.fraction));
        }
        else {
            rpcInfo.pushKV("maxPrice", ValueFromAmount((obj.maxPrice.integer * COIN) + obj.maxPrice.fraction));
        }
    }

    void operator()(const CPoolSwapMessageV2 &obj) const {
        (*this)(obj.swapInfo);
        std::string str;
        for (auto &id : obj.poolIDs) {
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

    void operator()(const CGovernanceMessage &obj) const {
        for (const auto &gov : obj.govs) {
            auto& var = gov.second;
            rpcInfo.pushKV(var->GetName(), var->Export());
        }
    }

    void operator()(const CGovernanceUnsetMessage &obj) const {
        for (const auto &gov : obj.govs) {
            UniValue keys(UniValue::VARR);
            for (const auto &key : gov.second)
                keys.push_back(key);

            rpcInfo.pushKV(gov.first, keys);
        }
    }

    void operator()(const CGovernanceHeightMessage &obj) const {
        rpcInfo.pushKV(obj.govVar->GetName(), obj.govVar->Export());
        rpcInfo.pushKV("startHeight", static_cast<uint64_t>(obj.startHeight));
    }

    void operator()(const CAppointOracleMessage &obj) const {
        rpcInfo.pushKV("oracleAddress", ScriptToString(obj.oracleAddress));
        rpcInfo.pushKV("weightage", obj.weightage);
        tokenCurrencyPairInfo(obj.availablePairs);
    }

    void operator()(const CUpdateOracleAppointMessage &obj) const {
        rpcInfo.pushKV("oracleId", obj.oracleId.ToString());
        rpcInfo.pushKV("oracleAddress", ScriptToString(obj.newOracleAppoint.oracleAddress));
        rpcInfo.pushKV("weightage", obj.newOracleAppoint.weightage);
        tokenCurrencyPairInfo(obj.newOracleAppoint.availablePairs);
    }

    void operator()(const CRemoveOracleAppointMessage &obj) const {
        rpcInfo.pushKV("oracleId", obj.oracleId.ToString());
    }

    void operator()(const CSetOracleDataMessage &obj) const {
        rpcInfo.pushKV("oracleId", obj.oracleId.ToString());
        rpcInfo.pushKV("timestamp", obj.timestamp);

        UniValue tokenPrices(UniValue::VARR);
        for (const auto &tokenPice : obj.tokenPrices) {
            const auto &token = tokenPice.first;
            for (const auto &price : tokenPice.second) {
                const auto &currency = price.first;
                auto amount          = price.second;

                UniValue uniPair(UniValue::VOBJ);
                uniPair.pushKV("currency", currency);
                uniPair.pushKV("tokenAmount", strprintf("%s@%s", GetDecimalString(amount), token));
                tokenPrices.push_back(uniPair);
            }
        }
        rpcInfo.pushKV("tokenPrices", tokenPrices);
    }

    void operator()(const CICXCreateOrderMessage &obj) const {
        if (obj.orderType == CICXOrder::TYPE_INTERNAL) {
            rpcInfo.pushKV("type", "DFC");
            if (auto token = mnview.GetToken(obj.idToken))
                rpcInfo.pushKV("tokenFrom", token->CreateSymbolKey(obj.idToken));
            rpcInfo.pushKV("chainto", CICXOrder::CHAIN_BTC);
        } else if (obj.orderType == CICXOrder::TYPE_EXTERNAL) {
            rpcInfo.pushKV("type", "EXTERNAL");
            rpcInfo.pushKV("chainFrom", CICXOrder::CHAIN_BTC);
            if (auto token = mnview.GetToken(obj.idToken))
                rpcInfo.pushKV("tokenTo", token->CreateSymbolKey(obj.idToken));
            rpcInfo.pushKV("receivePubkey", HexStr(obj.receivePubkey));
        }

        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        rpcInfo.pushKV("amountFrom", ValueFromAmount(obj.amountFrom));
        rpcInfo.pushKV("amountToFill", ValueFromAmount(obj.amountToFill));
        rpcInfo.pushKV("orderPrice", ValueFromAmount(obj.orderPrice));
        CAmount calcedAmount(static_cast<CAmount>(
            (arith_uint256(obj.amountToFill) * arith_uint256(obj.orderPrice) / arith_uint256(COIN)).GetLow64()));
        rpcInfo.pushKV("amountToFillInToAsset", ValueFromAmount(calcedAmount));
        rpcInfo.pushKV("expiry", static_cast<int>(obj.expiry));
    }

    void operator()(const CICXMakeOfferMessage &obj) const {
        rpcInfo.pushKV("orderTx", obj.orderTx.GetHex());
        rpcInfo.pushKV("amount", ValueFromAmount(obj.amount));
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        if (obj.receivePubkey.IsFullyValid())
            rpcInfo.pushKV("receivePubkey", HexStr(obj.receivePubkey));
        rpcInfo.pushKV("takerFee", ValueFromAmount(obj.takerFee));
        rpcInfo.pushKV("expiry", static_cast<int>(obj.expiry));
    }

    void operator()(const CICXSubmitDFCHTLCMessage &obj) const {
        rpcInfo.pushKV("type", "DFC");
        rpcInfo.pushKV("offerTx", obj.offerTx.GetHex());
        rpcInfo.pushKV("amount", ValueFromAmount(obj.amount));
        rpcInfo.pushKV("hash", obj.hash.GetHex());
        rpcInfo.pushKV("timeout", static_cast<int>(obj.timeout));
    }

    void operator()(const CICXSubmitEXTHTLCMessage &obj) const {
        rpcInfo.pushKV("type", "EXTERNAL");
        rpcInfo.pushKV("offerTx", obj.offerTx.GetHex());
        rpcInfo.pushKV("amount", ValueFromAmount(obj.amount));
        rpcInfo.pushKV("hash", obj.hash.GetHex());
        rpcInfo.pushKV("htlcScriptAddress", obj.htlcscriptAddress);
        rpcInfo.pushKV("ownerPubkey", HexStr(obj.ownerPubkey));
        rpcInfo.pushKV("timeout", static_cast<int>(obj.timeout));
    }

    void operator()(const CICXClaimDFCHTLCMessage &obj) const {
        rpcInfo.pushKV("type", "CLAIM DFC");
        rpcInfo.pushKV("dfchtlcTx", obj.dfchtlcTx.GetHex());
        rpcInfo.pushKV("seed", HexStr(obj.seed));
    }

    void operator()(const CICXCloseOrderMessage &obj) const { rpcInfo.pushKV("orderTx", obj.orderTx.GetHex()); }

    void operator()(const CICXCloseOfferMessage &obj) const { rpcInfo.pushKV("offerTx", obj.offerTx.GetHex()); }

    void operator()(const CLoanSetCollateralTokenMessage &obj) const {
        if (auto token = mnview.GetToken(obj.idToken))
            rpcInfo.pushKV("token", token->CreateSymbolKey(obj.idToken));
        rpcInfo.pushKV("factor", ValueFromAmount(obj.factor));
        rpcInfo.pushKV("fixedIntervalPriceId", obj.fixedIntervalPriceId.first + "/" + obj.fixedIntervalPriceId.second);
        if (obj.activateAfterBlock)
            rpcInfo.pushKV("activateAfterBlock", static_cast<int>(obj.activateAfterBlock));
    }

    void operator()(const CLoanSetLoanTokenMessage &obj) const {
        rpcInfo.pushKV("symbol", obj.symbol);
        rpcInfo.pushKV("name", obj.name);
        rpcInfo.pushKV("fixedIntervalPriceId", obj.fixedIntervalPriceId.first + "/" + obj.fixedIntervalPriceId.second);
        rpcInfo.pushKV("mintable", obj.mintable);
        rpcInfo.pushKV("interest", ValueFromAmount(obj.interest));
    }

    void operator()(const CLoanUpdateLoanTokenMessage &obj) const {
        rpcInfo.pushKV("id", obj.tokenTx.ToString());
        rpcInfo.pushKV("symbol", obj.symbol);
        rpcInfo.pushKV("name", obj.name);
        rpcInfo.pushKV("fixedIntervalPriceId", obj.fixedIntervalPriceId.first + "/" + obj.fixedIntervalPriceId.second);
        rpcInfo.pushKV("mintable", obj.mintable);
        rpcInfo.pushKV("interest", ValueFromAmount(obj.interest));
    }

    void operator()(const CLoanSchemeMessage &obj) const {
        rpcInfo.pushKV("id", obj.identifier);
        rpcInfo.pushKV("mincolratio", static_cast<uint64_t>(obj.ratio));
        rpcInfo.pushKV("interestrate", ValueFromAmount(obj.rate));
        rpcInfo.pushKV("updateHeight", obj.updateHeight);
    }

    void operator()(const CDefaultLoanSchemeMessage &obj) const { rpcInfo.pushKV("id", obj.identifier); }

    void operator()(const CDestroyLoanSchemeMessage &obj) const {
        rpcInfo.pushKV("id", obj.identifier);
        rpcInfo.pushKV("destroyHeight", obj.destroyHeight);
    }

    void operator()(const CVaultMessage &obj) const {
        // Add Vault attributes
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        rpcInfo.pushKV("loanSchemeId", obj.schemeId);
    }

    void operator()(const CCloseVaultMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("to", ScriptToString(obj.to));
    }

    void operator()(const CUpdateVaultMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("ownerAddress", ScriptToString(obj.ownerAddress));
        rpcInfo.pushKV("loanSchemeId", obj.schemeId);
    }

    void operator()(const CDepositToVaultMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CWithdrawFromVaultMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("to", ScriptToString(obj.to));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CPaybackWithCollateralMessage &obj) const { rpcInfo.pushKV("vaultId", obj.vaultId.GetHex()); }

    void operator()(const CLoanTakeLoanMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        if (!obj.to.empty())
            rpcInfo.pushKV("to", ScriptToString(obj.to));
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        for (const auto &kv : obj.amounts.balances) {
            if (auto token = mnview.GetToken(kv.first)) {
                auto tokenImpl = static_cast<const CTokenImplementation &>(*token);
                if (auto tokenPair = mnview.GetTokenByCreationTx(tokenImpl.creationTx)) {
                    rpcInfo.pushKV(tokenPair->first.ToString(), ValueFromAmount(kv.second));
                }
            }
        }
    }

    void operator()(const CLoanPaybackLoanMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        for (const auto &kv : obj.amounts.balances) {
            if (auto token = mnview.GetToken(kv.first)) {
                auto tokenImpl = static_cast<const CTokenImplementation &>(*token);
                if (auto tokenPair = mnview.GetTokenByCreationTx(tokenImpl.creationTx)) {
                    rpcInfo.pushKV(tokenPair->first.ToString(), ValueFromAmount(kv.second));
                }
            }
        }
    }

    void operator()(const CLoanPaybackLoanV2Message &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        UniValue loans{UniValue::VARR};
        for (const auto &idx : obj.loans) {
            UniValue loan{UniValue::VOBJ};
            if (auto dtoken = mnview.GetToken(idx.first)) {
                auto dtokenImpl = static_cast<const CTokenImplementation &>(*dtoken);
                if (auto dtokenPair = mnview.GetTokenByCreationTx(dtokenImpl.creationTx)) {
                    loan.pushKV("dTokens", dtokenPair->first.ToString());
                }
            }
            for (const auto &kv : idx.second.balances) {
                if (auto token = mnview.GetToken(kv.first)) {
                    auto tokenImpl = static_cast<const CTokenImplementation &>(*token);
                    if (auto tokenPair = mnview.GetTokenByCreationTx(tokenImpl.creationTx)) {
                        loan.pushKV(tokenPair->first.ToString(), ValueFromAmount(kv.second));
                    }
                }
            }
            loans.push_back(loan);
        }
        rpcInfo.pushKV("dToken", loans);
    }

    void operator()(const CAuctionBidMessage &obj) const {
        rpcInfo.pushKV("vaultId", obj.vaultId.GetHex());
        rpcInfo.pushKV("index", int64_t(obj.index));
        rpcInfo.pushKV("from", ScriptToString(obj.from));
        rpcInfo.pushKV("amount", obj.amount.ToString());
    }

    void operator()(const CCreateProposalMessage &obj) const {
        auto propId = tx.GetHash();
        rpcInfo.pushKV("proposalId", propId.GetHex());
        auto type = static_cast<CProposalType>(obj.type);
        rpcInfo.pushKV("type", CProposalTypeToString(type));
        rpcInfo.pushKV("title", obj.title);
        rpcInfo.pushKV("context", obj.context);
        rpcInfo.pushKV("amount", ValueFromAmount(obj.nAmount));
        rpcInfo.pushKV("cycles", int(obj.nCycles));
        int64_t proposalEndHeight{};
        if (auto prop = mnview.GetProposal(propId)) {
            proposalEndHeight = prop->proposalEndHeight;
        } else {
            // TX still in mempool. For the most accurate guesstimate use
            // votingPeriod as it would be set when TX is added to the chain.
            const auto votingPeriod = obj.options & CProposalOption::Emergency ?
                                mnview.GetEmergencyPeriodFromAttributes(type) :
                                mnview.GetVotingPeriodFromAttributes();
            proposalEndHeight = height + (votingPeriod - height % votingPeriod);
            for (uint8_t i = 1; i <= obj.nCycles; ++i) {
                proposalEndHeight += votingPeriod;
            }
        }
        rpcInfo.pushKV("proposalEndHeight", proposalEndHeight);
        rpcInfo.pushKV("payoutAddress", ScriptToString(obj.address));
        if (obj.options) {
            UniValue opt = UniValue(UniValue::VARR);
            if (obj.options & CProposalOption::Emergency)
                opt.push_back("emergency");

            rpcInfo.pushKV("options", opt);
        }
    }

    void operator()(const CProposalVoteMessage &obj) const {
        rpcInfo.pushKV("proposalId", obj.propId.GetHex());
        rpcInfo.pushKV("masternodeId", obj.masternodeId.GetHex());
        auto vote = static_cast<CProposalVoteType>(obj.vote);
        rpcInfo.pushKV("vote", CProposalVoteToString(vote));
    }

    void operator()(const CTransferDomainMessage &obj) const {
        UniValue array{UniValue::VARR};

        for (const auto &[src, dst] : obj.transfers) {
            UniValue srcJson{UniValue::VOBJ};
            UniValue dstJson{UniValue::VOBJ};
            std::array<std::pair<UniValue&, const CTransferDomainItem>,
            2> items {
                std::make_pair(std::ref(srcJson), src),
                std::make_pair(std::ref(dstJson), dst)
            };

            for (auto &[j, o]: items) {
                j.pushKV("address", ScriptToString(o.address));
                j.pushKV("amount", o.amount.ToString());
                j.pushKV("domain", CTransferDomainToString(VMDomain(o.domain)));
                if (!o.data.empty()) {
                    j.pushKV("data", std::string(o.data.begin(), o.data.end()));
                }
            }

            UniValue elem{UniValue::VOBJ};
            elem.pushKV("src", srcJson);
            elem.pushKV("dst", dstJson);
            array.push_back(elem);
        }

        rpcInfo.pushKV("transfers", array);
    }

    void operator()(const CEvmTxMessage &obj) const {
        rpcInfo.pushKV("evmTx", HexStr(obj.evmTx.begin(),obj.evmTx.end()));
    }

    void operator()(const CCustomTxMessageNone &) const {}
};

Res RpcInfo(const CTransaction &tx, uint32_t height, CustomTxType &txType, UniValue &results) {
    std::vector<unsigned char> metadata;
    txType = GuessCustomTxType(tx, metadata);
    if (txType == CustomTxType::None) {
        return Res::Ok();
    }
    auto txMessage = customTypeToMessage(txType);
    auto res       = CustomMetadataParse(height, Params().GetConsensus(), metadata, txMessage);
    if (res) {
        CCustomCSView mnview(*pcustomcsview);
        std::visit(CCustomTxRpcVisitor(tx, height, mnview, results), txMessage);
    }
    return res;
}

#include <dfi/accountshistory.h>
#include <dfi/auctionhistory.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/mn_rpc.h>
#include <dfi/vaulthistory.h>

extern UniValue AmountsToJSON(const CCustomCSView &view,
                              const TAmounts &diffs,
                              AmountFormat format = AmountFormat::Symbol);
extern std::string tokenAmountString(const CCustomCSView &view,
                                     const CTokenAmount &amount,
                                     AmountFormat format = AmountFormat::Symbol);

namespace {

    enum class VaultState : uint32_t {
        Unknown = 0,
        Active = (1 << 0),
        InLiquidation = (1 << 1),
        Frozen = (1 << 2),
        MayLiquidate = (1 << 3)
    };

    std::string VaultStateToString(const VaultState &state) {
        switch (state) {
            case VaultState::Active:
                return "active";
            case VaultState::Frozen:
                return "frozen";
            case VaultState::InLiquidation:
                return "inLiquidation";
            case VaultState::MayLiquidate:
                return "mayLiquidate";
            default:
                return "unknown";
        }
    }

    VaultState StringToVaultState(const std::string &stateStr) {
        if (stateStr == "active") {
            return VaultState::Active;
        }
        if (stateStr == "frozen") {
            return VaultState::Frozen;
        }
        if (stateStr == "inLiquidation") {
            return VaultState::InLiquidation;
        }
        if (stateStr == "mayLiquidate") {
            return VaultState::MayLiquidate;
        }
        return VaultState::Unknown;
    }

    bool WillLiquidateNext(CCustomCSView &view, const CVaultId &vaultId, const CVaultData &vault) {
        auto height = view.GetLastHeight();
        int64_t blockTime{};
        {
            LOCK(cs_main);
            blockTime = ::ChainActive()[height]->GetBlockTime();
        }

        auto collaterals = view.GetVaultCollaterals(vaultId);
        if (!collaterals) {
            return false;
        }

        bool useNextPrice = true, requireLivePrice = false;
        auto vaultRate = view.GetVaultAssets(vaultId, *collaterals, height, blockTime, useNextPrice, requireLivePrice);
        if (!vaultRate) {
            return false;
        }

        auto loanScheme = view.GetLoanScheme(vault.schemeId);
        return (vaultRate.val->ratio() < loanScheme->ratio);
    }

    VaultState GetVaultState(CCustomCSView &view, const CVaultId &vaultId, const CVaultData &vault) {
        auto height = view.GetLastHeight();
        auto inLiquidation = vault.isUnderLiquidation;
        auto priceIsValid = IsVaultPriceValid(view, vaultId, height);
        auto willLiquidateNext = WillLiquidateNext(view, vaultId, vault);

        // Can possibly optimize with flags, but provides clarity for now.
        if (!inLiquidation && priceIsValid && !willLiquidateNext) {
            return VaultState::Active;
        }
        if (!inLiquidation && priceIsValid && willLiquidateNext) {
            return VaultState::MayLiquidate;
        }
        if (!inLiquidation && !priceIsValid) {
            return VaultState::Frozen;
        }
        if (inLiquidation && priceIsValid) {
            return VaultState::InLiquidation;
        }
        return VaultState::Unknown;
    }

    UniValue BatchToJSON(CCustomCSView &view, const CVaultId &vaultId, uint32_t batchCount) {
        UniValue batchArray{UniValue::VARR};
        for (uint32_t i = 0; i < batchCount; i++) {
            UniValue batchObj{UniValue::VOBJ};
            auto batch = view.GetAuctionBatch({vaultId, i});
            batchObj.pushKV("index", int(i));
            batchObj.pushKV("collaterals", AmountsToJSON(view, batch->collaterals.balances));
            batchObj.pushKV("loan", tokenAmountString(view, batch->loanAmount));
            if (auto bid = view.GetAuctionBid({vaultId, i})) {
                UniValue bidObj{UniValue::VOBJ};
                bidObj.pushKV("owner", ScriptToString(bid->first));
                bidObj.pushKV("amount", tokenAmountString(view, bid->second));
                batchObj.pushKV("highestBid", bidObj);
            }
            batchArray.push_back(batchObj);
        }
        return batchArray;
    }

    UniValue AuctionToJSON(CCustomCSView &view, const CVaultId &vaultId, const CAuctionData &data) {
        UniValue auctionObj{UniValue::VOBJ};
        auto vault = view.GetVault(vaultId);
        auctionObj.pushKV("vaultId", vaultId.GetHex());
        auctionObj.pushKV("loanSchemeId", vault->schemeId);
        auctionObj.pushKV("ownerAddress", ScriptToString(vault->ownerAddress));
        auctionObj.pushKV("state", VaultStateToString(VaultState::InLiquidation));
        auctionObj.pushKV("liquidationHeight", int64_t(data.liquidationHeight));
        auctionObj.pushKV("batchCount", int64_t(data.batchCount));
        auctionObj.pushKV("liquidationPenalty", ValueFromAmount(data.liquidationPenalty * 100));
        auctionObj.pushKV("batches", BatchToJSON(view, vaultId, data.batchCount));
        return auctionObj;
    }

    UniValue VaultToJSON(CCustomCSView &view,
                         const CVaultId &vaultId,
                         const CVaultData &vault,
                         const bool verbose = false) {
        UniValue result{UniValue::VOBJ};
        auto vaultState = GetVaultState(view, vaultId, vault);
        auto height = view.GetLastHeight();

        const auto scheme = view.GetLoanScheme(vault.schemeId);
        assert(scheme);

        if (vaultState == VaultState::InLiquidation) {
            if (auto data = view.GetAuction(vaultId, height)) {
                result.pushKVs(AuctionToJSON(view, vaultId, *data));
            } else {
                LogPrintf("Warning: Vault in liquidation, but no auctions found\n");
            }
            return result;
        }

        UniValue ratioValue{0}, collValue{0}, loanValue{0}, interestValue{0}, collateralRatio{0},
            nextCollateralRatio{0}, totalInterestsPerBlockValue{0};

        auto collaterals = view.GetVaultCollaterals(vaultId);
        if (!collaterals) {
            collaterals = CBalances{};
        }

        int64_t blockTime{};
        {
            LOCK(cs_main);
            blockTime = ::ChainActive().Tip()->GetBlockTime();
        }
        bool useNextPrice = false, requireLivePrice = vaultState != VaultState::Frozen;

        if (auto rate =
                view.GetVaultAssets(vaultId, *collaterals, height + 1, blockTime, useNextPrice, requireLivePrice)) {
            collValue = ValueFromUint(rate.val->totalCollaterals);
            loanValue = ValueFromUint(rate.val->totalLoans);
            ratioValue = ValueFromAmount(rate.val->precisionRatio());
            collateralRatio = int(rate.val->ratio());
        }

        bool isVaultTokenLocked{false};
        for (const auto &collateral : collaterals->balances) {
            if (view.AreTokensLocked({collateral.first.v})) {
                isVaultTokenLocked = true;
                break;
            }
        }

        UniValue loanBalances{UniValue::VARR};
        UniValue interestAmounts{UniValue::VARR};
        UniValue interestsPerBlockBalances{UniValue::VARR};
        std::map<DCT_ID, CInterestAmount> interestsPerBlockHighPrecission;
        CInterestAmount interestsPerBlockValueHighPrecision;
        TAmounts interestsPerBlock{};
        CAmount totalInterestsPerBlock{0};

        if (const auto loanTokens = view.GetLoanTokens(vaultId)) {
            TAmounts totalBalances{};
            TAmounts interestBalances{};
            CAmount totalInterests{0};

            for (const auto &[tokenId, amount] : loanTokens->balances) {
                auto token = view.GetLoanTokenByID(tokenId);
                if (!token) {
                    continue;
                }
                auto rate = view.GetInterestRate(vaultId, tokenId, height);
                if (!rate) {
                    continue;
                }
                auto totalInterest = TotalInterest(*rate, height + 1);
                auto value = amount + totalInterest;
                if (value > 0) {
                    if (auto priceFeed = view.GetFixedIntervalPrice(token->fixedIntervalPriceId)) {
                        auto price = priceFeed.val->priceRecord[0];
                        if (const auto interestCalculation = MultiplyAmounts(price, totalInterest)) {
                            totalInterests += interestCalculation;
                        }
                        if (verbose) {
                            if (height >= Params().GetConsensus().DF18FortCanningGreatWorldHeight) {
                                interestsPerBlockValueHighPrecision = InterestAddition(
                                    interestsPerBlockValueHighPrecision,
                                    {rate->interestPerBlock.negative,
                                     static_cast<base_uint<128>>(price) * rate->interestPerBlock.amount / COIN});
                                interestsPerBlockHighPrecission[tokenId] = rate->interestPerBlock;
                            } else if (height >= Params().GetConsensus().DF14FortCanningHillHeight) {
                                interestsPerBlockValueHighPrecision.amount +=
                                    static_cast<base_uint<128>>(price) * rate->interestPerBlock.amount / COIN;
                                interestsPerBlockHighPrecission[tokenId] = rate->interestPerBlock;
                            } else {
                                const auto interestPerBlock = rate->interestPerBlock.amount.GetLow64();
                                interestsPerBlock.insert({tokenId, interestPerBlock});
                                totalInterestsPerBlock +=
                                    MultiplyAmounts(price, static_cast<CAmount>(interestPerBlock));
                            }
                        }
                    }

                    totalBalances.insert({tokenId, value});
                    interestBalances.insert({tokenId, totalInterest});
                }
                if (view.AreTokensLocked({tokenId.v})) {
                    isVaultTokenLocked = true;
                }
            }
            interestValue = ValueFromAmount(totalInterests);
            loanBalances = AmountsToJSON(view, totalBalances);
            interestAmounts = AmountsToJSON(view, interestBalances);
        }

        result.pushKV("vaultId", vaultId.GetHex());
        result.pushKV("loanSchemeId", vault.schemeId);
        result.pushKV("ownerAddress", ScriptToString(vault.ownerAddress));
        result.pushKV("state", VaultStateToString(vaultState));
        result.pushKV("collateralAmounts", AmountsToJSON(view, collaterals->balances));
        result.pushKV("loanAmounts", loanBalances);
        result.pushKV("interestAmounts", interestAmounts);
        if (isVaultTokenLocked) {
            collValue = -1;
            loanValue = -1;
            interestValue = -1;
            ratioValue = -1;
            collateralRatio = -1;
            totalInterestsPerBlockValue = -1;
            interestsPerBlockValueHighPrecision.negative =
                true;  // Not really an invalid amount as it could actually be -1
            interestsPerBlockValueHighPrecision.amount = 1;
        }
        result.pushKV("collateralValue", collValue);
        result.pushKV("loanValue", loanValue);
        result.pushKV("interestValue", interestValue);
        result.pushKV("informativeRatio", ratioValue);
        result.pushKV("collateralRatio", collateralRatio);
        if (verbose) {
            useNextPrice = true;
            if (auto rate =
                    view.GetVaultAssets(vaultId, *collaterals, height + 1, blockTime, useNextPrice, requireLivePrice)) {
                nextCollateralRatio = int(rate.val->ratio());
                result.pushKV("nextCollateralRatio", nextCollateralRatio);
            }
            if (height >= Params().GetConsensus().DF14FortCanningHillHeight) {
                if (isVaultTokenLocked) {
                    result.pushKV("interestPerBlockValue", -1);
                } else {
                    result.pushKV("interestPerBlockValue",
                                  GetInterestPerBlockHighPrecisionString(interestsPerBlockValueHighPrecision));
                    for (const auto &[id, interestPerBlock] : interestsPerBlockHighPrecission) {
                        auto tokenId = id;
                        auto amountStr = GetInterestPerBlockHighPrecisionString(interestPerBlock);
                        auto token = view.GetToken(tokenId);
                        assert(token);
                        auto tokenSymbol = token->CreateSymbolKey(tokenId);
                        interestsPerBlockBalances.push_back(amountStr.append("@").append(tokenSymbol));
                    }
                }
            } else {
                interestsPerBlockBalances = AmountsToJSON(view, interestsPerBlock);
                totalInterestsPerBlockValue = ValueFromAmount(totalInterestsPerBlock);
                result.pushKV("interestPerBlockValue", totalInterestsPerBlockValue);
            }
            result.pushKV("interestsPerBlock", interestsPerBlockBalances);
        }
        return result;
    }
}  // namespace

UniValue createvault(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "createvault",
        "Creates a vault transaction.\n" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address"},
                                                    {"loanSchemeId",
             RPCArg::Type::STR,
             RPCArg::Optional::OMITTED,
             "Unique identifier of the loan scheme (8 chars max). If empty, the default loan scheme will be selected "
             "(Optional)"},
                                                    {
                "inputs",
                RPCArg::Type::ARR,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "A json array of json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv") +
                    HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv \"\"") +
                    HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv LOAN0001") +
                    HelpExampleRpc("createvault", R"("2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv")") +
                    HelpExampleRpc("createvault", R"("2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv", "")") +
                    HelpExampleRpc("createvault", R"("2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv", "LOAN0001")")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot createvault while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, true);

    CVaultMessage vault;
    vault.ownerAddress = DecodeScript(request.params[0].getValStr());

    RejectErc55Address(vault.ownerAddress);

    if (request.params.size() > 1) {
        if (!request.params[1].isNull()) {
            vault.schemeId = request.params[1].get_str();
        }
    }

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;
    const auto attributes = view->GetAttributes();
    const CDataStructureV0 creationFeeKey{AttributeTypes::Vaults, VaultIDs::Parameters, VaultKeys::CreationFee};
    const auto vaultCreationFee = attributes->GetValue(creationFeeKey, Params().GetConsensus().vaultCreationFee);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::Vault) << vault;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2], *view, request.metadata.coinSelectOpts);

    rawTx.vout.emplace_back(vaultCreationFee, scriptMeta);

    CCoinControl coinControl;

    // Set change to foundation address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue closevault(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "closevault",
        "Close vault transaction.\n" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault to be closed"},
                                                    {"to",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Any valid address to receive collaterals (if any) and half fee back"},
                                                    {
                "inputs",
                RPCArg::Type::ARR,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "A json array of json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                                                    HelpExampleCli(
                "closevault", "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF") +
            HelpExampleRpc(
                "closevault", R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2", "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF")")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot closevault while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, false);

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    CScript ownerAddress;
    CCloseVaultMessage msg;
    msg.vaultId = ParseHashV(request.params[0], "vaultId");
    {
        // decode vaultId
        auto vault = view->GetVault(msg.vaultId);
        if (!vault) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> not found", msg.vaultId.GetHex()));
        }

        if (vault->isUnderLiquidation) {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");
        }

        ownerAddress = vault->ownerAddress;
    }

    msg.to = DecodeScript(request.params[1].getValStr());

    RejectErc55Address(msg.to);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CloseVault) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerAddress};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2], *view, request.metadata.coinSelectOpts);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Set change to foundation address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listvaults(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listvaults",
        "List all available vaults.\n",
        {
          {
                "options",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {{"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of the vault owner."},
                 {"loanSchemeId", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Vault's loan scheme id"},
                 {"state",
                  RPCArg::Type::STR,
                  RPCArg::Optional::OMITTED,
                  "Wether the vault is under a given state. (default = 'unknown')"},
                 {"verbose",
                  RPCArg::Type::BOOL,
                  RPCArg::Optional::OMITTED,
                  "Flag for verbose list (default = false), otherwise only ids, ownerAddress, loanSchemeIds and state "
                  "are listed"}},
            }, {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::STR_HEX,
                     RPCArg::Optional::OMITTED,
                     "Optional first key to iterate from, in lexicographical order. "
                     "Typically it's set to last ID from previous request."},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of orders to return, 100 by default"},
                },
            }, },
        RPCResult{"[                         (json array of objects)\n"
                  "{...}                 (object) Json object with vault information\n"
                  "]\n"},
        RPCExamples{
          HelpExampleCli("listvaults", "") + HelpExampleCli("listvaults", "'{\"loanSchemeId\": \"LOAN1502\"}'") +
            HelpExampleCli("listvaults",
          "'{\"loanSchemeId\": \"LOAN1502\"}' "
                           "'{\"start\":\"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013\", "
                           "\"including_start\": true, "
                           "\"limit\":100}'") +
            HelpExampleCli("listvaults",
          "{} '{\"start\":\"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013\", "
                           "\"including_start\": true, "
                           "\"limit\":100}'") +
            HelpExampleRpc("listvaults", "") + HelpExampleRpc("listvaults", R"({"loanSchemeId": "LOAN1502"})") +
            HelpExampleRpc(
                "listvaults", R"({"loanSchemeId": "LOAN1502"}, {"start":"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013", "including_start": true, "limit":100})") +
            HelpExampleRpc(
                "listvaults", R"({}, {"start":"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013", "including_start": true, "limit":100})")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    CScript ownerAddress = {};
    std::string loanSchemeId;
    VaultState state{VaultState::Unknown};
    bool verbose{false};
    if (request.params.size() > 0) {
        UniValue optionsObj = request.params[0].get_obj();
        if (!optionsObj["ownerAddress"].isNull()) {
            ownerAddress = DecodeScript(optionsObj["ownerAddress"].getValStr());
        }
        if (!optionsObj["loanSchemeId"].isNull()) {
            loanSchemeId = optionsObj["loanSchemeId"].getValStr();
        }
        if (!optionsObj["state"].isNull()) {
            state = StringToVaultState(optionsObj["state"].getValStr());
        }
        if (!optionsObj["verbose"].isNull()) {
            verbose = optionsObj["verbose"].getBool();
        }
    }

    // parse pagination
    size_t limit = 100;
    CVaultId start = {};
    bool including_start = true;
    {
        if (request.params.size() > 1) {
            UniValue paginationObj = request.params[1].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue valueArr{UniValue::VARR};

    auto [view, accountView, vaultView] = GetSnapshots();

    view->ForEachVault(
        [&, &view = view](const CVaultId &vaultId, const CVaultData &data) {
            if (!including_start) {
                including_start = true;
                return (true);
            }
            if (!ownerAddress.empty() && ownerAddress != data.ownerAddress) {
                return false;
            }
            auto vaultState = GetVaultState(*view, vaultId, data);

            if ((loanSchemeId.empty() || loanSchemeId == data.schemeId) &&
                (state == VaultState::Unknown || state == vaultState)) {
                UniValue vaultObj{UniValue::VOBJ};
                if (!verbose) {
                    vaultObj.pushKV("vaultId", vaultId.GetHex());
                    vaultObj.pushKV("ownerAddress", ScriptToString(data.ownerAddress));
                    vaultObj.pushKV("loanSchemeId", data.schemeId);
                    vaultObj.pushKV("state", VaultStateToString(vaultState));
                } else {
                    vaultObj = VaultToJSON(*view, vaultId, data);
                }
                valueArr.push_back(vaultObj);
                limit--;
            }
            return limit != 0;
        },
        start,
        ownerAddress);

    return GetRPCResultCache().Set(request, valueArr);
}

UniValue getvault(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getvault",
        "Returns information about vault.\n",
        {
          {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id"},
          {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Verbose vault information (default = false)"},
          },
        RPCResult{"\"json\"                  (string) vault data in json form\n"},
        RPCExamples{
          HelpExampleCli("getvault", "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf") +
            HelpExampleRpc("getvault", R"("5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf")")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);
    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    bool verbose{false};
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    auto [view, accountView, vaultView] = GetSnapshots();

    auto vault = view->GetVault(vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Vault <%s> not found", vaultId.GetHex()));
    }

    auto res = VaultToJSON(*view, vaultId, *vault, verbose);
    return GetRPCResultCache().Set(request, res);
}

UniValue updatevault(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "updatevault",
        "\nCreates (and submits to local node and network) an `update vault transaction`, \n"
        "and saves vault updates to database.\n"
        "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault id"},
                          {
                "parameters",
                RPCArg::Type::OBJ,
                RPCArg::Optional::NO,
                "",
                {
                    {"ownerAddress", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Vault's owner address"},
                    {"loanSchemeId", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Vault's loan scheme id"},
                },
            }, {
                "inputs",
                RPCArg::Type::ARR,
                RPCArg::Optional::OMITTED_NAMED_ARG,
                "A json array of json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                          HelpExampleCli(
                "updatevault", R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 '{"ownerAddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanSchemeId": "LOANSCHEME001"}')") +
            HelpExampleRpc(
                "updatevault", R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2", {"ownerAddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanSchemeId": "LOANSCHEME001"})")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot update vault while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 must be non-null");
    }

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    CVaultMessage vault;
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    {
        // decode vaultId
        auto storedVault = view->GetVault(vaultId);
        if (!storedVault) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> not found", vaultId.GetHex()));
        }

        if (storedVault->isUnderLiquidation) {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");
        }

        vault = *storedVault;
    }

    CUpdateVaultMessage msg{vaultId, vault.ownerAddress, vault.schemeId};

    if (request.params[1].isNull()) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Invalid parameters, arguments 2 must be non-null and expected as object at least with one of"
            "{\"ownerAddress\",\"loanSchemeId\"}");
    }
    UniValue params = request.params[1].get_obj();
    if (params["ownerAddress"].isNull() && params["loanSchemeId"].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "At least ownerAddress OR loanSchemeId must be set");
    }

    if (!params["ownerAddress"].isNull()) {
        auto ownerAddress = params["ownerAddress"].getValStr();
        // check address validity
        CTxDestination ownerDest = DecodeDestination(ownerAddress);
        if (!IsValidDestination(ownerDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid owner address");
        }
        msg.ownerAddress = DecodeScript(ownerAddress);
    }

    RejectErc55Address(msg.ownerAddress);

    if (!params["loanSchemeId"].isNull()) {
        auto loanschemeid = params["loanSchemeId"].getValStr();
        msg.schemeId = loanschemeid;
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UpdateVault) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    const UniValue &txInputs = request.params[2];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{vault.ownerAddress};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to auth address if there's only one auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue deposittovault(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "deposittovault",
        "Deposit collateral token amount to vault.\n" + HelpRequiringPassphrase(pwallet) + "\n",
        {{"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault id"},
                                                    {
             "from",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Address containing collateral",
         }, {
             "amount",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Amount of collateral in amount@symbol format",
         }, {
             "inputs",
             RPCArg::Type::ARR,
             RPCArg::Optional::OMITTED_NAMED_ARG,
             "A json array of json objects",
             {
                 {
                     "",
                     RPCArg::Type::OBJ,
                     RPCArg::Optional::OMITTED,
                     "",
                     {
                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                     },
                 },
             },
         }},
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                                                    HelpExampleCli("deposittovault",
                                                    "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i "
                           "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 1@DFI") +
            HelpExampleRpc(
                "deposittovault", R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i", "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "1@DFI")")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot upddeposittovaultate vault while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    if (request.params[0].isNull() || request.params[1].isNull() || request.params[2].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments must be non-null");
    }

    // decode vaultId
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    auto from = DecodeScript(request.params[1].get_str());
    RejectErc55Address(from);
    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[2].get_str(), "amount");

    CDepositToVaultMessage msg{vaultId, from, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::DepositToVault) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    const UniValue &txInputs = request.params[3];

    auto [view, accountView, vaultView] = GetSnapshots();
    CTransactionRef optAuthTx;
    std::set<CScript> auths{from};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue withdrawfromvault(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "withdrawfromvault",
        "Withdraw collateral token amount from vault.\n" + HelpRequiringPassphrase(pwallet) + "\n",
        {{"vaultId", RPCArg::Type::STR, RPCArg::Optional::NO, "Vault id"},
                                                    {
             "to",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Destination address for withdraw of collateral",
         }, {
             "amount",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Amount of collateral in amount@symbol format",
         }, {
             "inputs",
             RPCArg::Type::ARR,
             RPCArg::Optional::OMITTED_NAMED_ARG,
             "A json array of json objects",
             {
                 {
                     "",
                     RPCArg::Type::OBJ,
                     RPCArg::Optional::OMITTED,
                     "",
                     {
                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                     },
                 },
             },
         }},
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                                                    HelpExampleCli("withdrawfromvault",
                                                    "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i "
                           "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 1@DFI") +
            HelpExampleRpc(
                "withdrawfromvault", R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i", "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "1@DFI")")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot withdrawfromvault while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    if (request.params[0].isNull() || request.params[1].isNull() || request.params[2].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments must be non-null");
    }

    // decode vaultId
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    auto to = DecodeScript(request.params[1].get_str());
    RejectErc55Address(to);
    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[2].get_str(), "amount");

    CWithdrawFromVaultMessage msg{vaultId, to, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::WithdrawFromVault) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    CScript ownerAddress;
    {
        // decode vaultId
        auto vault = view->GetVault(vaultId);
        if (!vault) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> not found", vaultId.GetHex()));
        }

        ownerAddress = vault->ownerAddress;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    const UniValue &txInputs = request.params[3];

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerAddress};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(ownerAddress, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue placeauctionbid(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "placeauctionbid",
        "Bid to vault in auction.\n" + HelpRequiringPassphrase(pwallet) + "\n",
        {{"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault id"},
                                                    {"index", RPCArg::Type::NUM, RPCArg::Optional::NO, "Auction index"},
                                                    {"from",
          RPCArg::Type::STR,
          RPCArg::Optional::NO,
          "Address to get tokens. If \"from\" value is: \"*\" (star), it's means auto-selection accounts from wallet."},
                                                    {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount of amount@symbol format"},
                                                    {
             "inputs",
             RPCArg::Type::ARR,
             RPCArg::Optional::OMITTED_NAMED_ARG,
             "A json array of json objects",
             {
                 {
                     "",
                     RPCArg::Type::OBJ,
                     RPCArg::Optional::OMITTED,
                     "",
                     {
                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                     },
                 },
             },
         }},
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                                                    HelpExampleCli("placeauctionbid",
                                                    "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 0 "
                           "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 100@TSLA") +
            HelpExampleRpc(
                "placeauctionbid", R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2", 0, "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "1@DTSLA")")},
    }
        .Check(request);

    RPCTypeCheck(
        request.params, {UniValue::VSTR, UniValue::VNUM, UniValue::VSTR, UniValue::VSTR, UniValue::VARR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot make auction bid while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    // decode vaultId
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    uint32_t index = request.params[1].get_int();
    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[3].get_str(), "amount");

    auto [view, accountView, vaultView] = GetSnapshots();

    CScript from = {};
    auto fromStr = request.params[2].get_str();
    if (fromStr == "*") {
        auto selectedAccounts = SelectAccountsByTargetBalances(
            GetAllMineAccounts(pwallet, *view), CBalances{TAmounts{{amount.nTokenId, amount.nValue}}}, SelectionPie);

        for (auto &account : selectedAccounts) {
            if (account.second.balances[amount.nTokenId] >= amount.nValue) {
                from = account.first;
                break;
            }
        }

        if (from.empty()) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                               "Not enough tokens on account, call sendtokenstoaddress to increase it.\n");
        }
    } else {
        from = DecodeScript(fromStr);
    }

    RejectErc55Address(from);

    CAuctionBidMessage msg{vaultId, index, from, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AuctionBid) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CTransactionRef optAuthTx;
    std::set<CScript> auths{from};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[4], *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listauctions(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listauctions",
        "List all available auctions.\n",
        {
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::OBJ,
                     RPCArg::Optional::OMITTED,
                     "",
                     {{"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Vault id"},
                      {"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Height to iterate from"}}},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of orders to return, 100 by default"},
                },
            }, },
        RPCResult{"[                         (json array of objects)\n"
                  "{...}                 (object) Json object with auction information\n"
                  "]\n"},
        RPCExamples{
          HelpExampleCli("listauctions", "") +
            HelpExampleCli(
                "listauctions", "'{\"start\": {\"vaultId\":\"eeea650e5de30b77d17e3907204d200dfa4996e5c4d48b000ae8e70078fe7542\", "
                "\"height\": 1000}, \"including_start\": true, "
                "\"limit\":100}'") +
            HelpExampleRpc("listauctions", "") +
            HelpExampleRpc(
                "listauctions", R"({"start": {"vaultId":"eeea650e5de30b77d17e3907204d200dfa4996e5c4d48b000ae8e70078fe7542", "height": 1000}, "including_start": true, "limit":100})")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    // parse pagination
    CVaultId vaultId;
    size_t limit = 100;
    uint32_t height = 0;
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                UniValue startObj = paginationObj["start"].get_obj();
                including_start = false;
                if (!startObj["vaultId"].isNull()) {
                    vaultId = ParseHashV(startObj["vaultId"], "vaultId");
                }
                if (!startObj["height"].isNull()) {
                    height = startObj["height"].get_int64();
                }
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue valueArr{UniValue::VARR};

    auto [view, accountView, vaultView] = GetSnapshots();

    view->ForEachVaultAuction(
        [&, &view = view](const CVaultId &vaultId, const CAuctionData &data) {
            if (!including_start) {
                including_start = true;
                return (true);
            }
            valueArr.push_back(AuctionToJSON(*view, vaultId, data));
            return --limit != 0;
        },
        height,
        vaultId);

    return GetRPCResultCache().Set(request, valueArr);
}

UniValue auctionhistoryToJSON(const CCustomCSView &view,
                              const AuctionHistoryKey &key,
                              const AuctionHistoryValue &value) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("winner", ScriptToString(key.owner));
    obj.pushKV("blockHeight", (uint64_t)key.blockHeight);
    {
        LOCK(cs_main);
        if (auto block = ::ChainActive()[key.blockHeight]) {
            obj.pushKV("blockHash", block->GetBlockHash().GetHex());
            obj.pushKV("blockTime", block->GetBlockTime());
        }
    }
    obj.pushKV("vaultId", key.vaultId.GetHex());
    obj.pushKV("batchIndex", (uint64_t)key.index);
    obj.pushKV("auctionBid", tokenAmountString(view, value.bidAmount));
    obj.pushKV("auctionWon", AmountsToJSON(view, value.collaterals));
    return obj;
}

UniValue listauctionhistory(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "listauctionhistory",
        "\nReturns information about auction history.\n",
        {
          {"owner|vaultId",
             RPCArg::Type::STR,
             RPCArg::Optional::OMITTED,
             "Single account ID (CScript or address) or vaultId or reserved words: \"mine\" - to list history for all "
             "owned accounts or \"all\" to list whole DB (default = \"mine\")."},
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"maxBlockHeight",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Optional height to iterate from (downto genesis block)"},
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Vault id"},
                    {"index", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Batch index"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of orders to return, 100 by default"},
                },
            }, },
        RPCResult{"[{},{}...]     (array) Objects with auction history information\n"},
        RPCExamples{HelpExampleCli("listauctionhistory", "all '{\"height\":160}'") +
                    HelpExampleRpc("listauctionhistory", "")},
    }
        .Check(request);

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is needed for auction history");
    }

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    // parse pagination
    size_t limit = 100;
    AuctionHistoryKey start = {~0u};
    {
        if (request.params.size() > 1) {
            UniValue paginationObj = request.params[1].get_obj();
            if (!paginationObj["index"].isNull()) {
                start.index = paginationObj["index"].get_int();
            }
            if (!paginationObj["vaultId"].isNull()) {
                start.vaultId = ParseHashV(paginationObj["vaultId"], "vaultId");
            }
            if (!paginationObj["maxBlockHeight"].isNull()) {
                start.blockHeight = paginationObj["maxBlockHeight"].get_int64();
            }
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    std::string account = "mine";
    if (request.params.size() > 0) {
        account = request.params[0].getValStr();
    }

    int filter = -1;
    bool isMine = false;

    if (account == "mine") {
        isMine = true;
    } else if (account != "all") {
        filter = DecodeScriptTxId(account, {start.owner, start.vaultId});
    }

    UniValue ret(UniValue::VARR);

    auto [view, accountView, vaultView] = GetSnapshots();

    accountView->ForEachAuctionHistory(
        [&, &view = view](const AuctionHistoryKey &key, CLazySerialize<AuctionHistoryValue> valueLazy) -> bool {
            if (filter == 0 && start.owner != key.owner) {
                return true;
            }

            if (filter == 1 && start.vaultId != key.vaultId) {
                return true;
            }

            if (isMine && !(IsMineCached(*pwallet, key.owner) & ISMINE_SPENDABLE)) {
                return true;
            }

            ret.push_back(auctionhistoryToJSON(*view, key, valueLazy.get()));

            return --limit != 0;
        },
        start);

    return GetRPCResultCache().Set(request, ret);
}

UniValue vaultToJSON(const CCustomCSView &view,
                     const uint256 &vaultID,
                     const std::string &address,
                     const uint64_t blockHeight,
                     const std::string &type,
                     const uint64_t txn,
                     const std::string &txid,
                     const TAmounts &amounts) {
    UniValue obj(UniValue::VOBJ);

    if (!address.empty()) {
        obj.pushKV("address", address);
    }
    obj.pushKV("blockHeight", blockHeight);
    {
        LOCK(cs_main);
        if (auto block = ::ChainActive()[blockHeight]) {
            obj.pushKV("blockHash", block->GetBlockHash().GetHex());
            obj.pushKV("blockTime", block->GetBlockTime());
        }
    }
    if (!type.empty()) {
        obj.pushKV("type", type);
    }
    // No address no txn
    if (!address.empty()) {
        obj.pushKV("txn", txn);
    }
    if (!txid.empty()) {
        obj.pushKV("txid", txid);
    }
    if (!amounts.empty()) {
        obj.pushKV("amounts", AmountsToJSON(view, amounts));
    }

    return obj;
}

UniValue BatchToJSON(const CCustomCSView &view, const std::vector<CAuctionBatch> batches) {
    UniValue batchArray{UniValue::VARR};
    for (uint64_t i{0}; i < batches.size(); ++i) {
        UniValue batchObj{UniValue::VOBJ};
        batchObj.pushKV("index", i);
        batchObj.pushKV("collaterals", AmountsToJSON(view, batches[i].collaterals.balances));
        batchObj.pushKV("loan", tokenAmountString(view, batches[i].loanAmount));
        batchArray.push_back(batchObj);
    }
    return batchArray;
}

UniValue stateToJSON(const CCustomCSView &view, const VaultStateKey &key, const VaultStateValue &value) {
    auto obj = vaultToJSON(view, key.vaultID, "", key.blockHeight, "", 0, "", {});

    UniValue snapshot(UniValue::VOBJ);
    snapshot.pushKV("state", !value.auctionBatches.empty() ? "inLiquidation" : "active");
    snapshot.pushKV("collateralAmounts", AmountsToJSON(view, value.collaterals));
    snapshot.pushKV("collateralValue", ValueFromUint(value.collateralsValues.totalCollaterals));
    snapshot.pushKV(
        "collateralRatio",
        static_cast<int>(value.ratio != static_cast<uint32_t>(-1) ? value.ratio : value.collateralsValues.ratio()));
    if (!value.auctionBatches.empty()) {
        snapshot.pushKV("batches", BatchToJSON(view, value.auctionBatches));
    }

    obj.pushKV("vaultSnapshot", snapshot);

    return obj;
}

UniValue historyToJSON(const CCustomCSView &view, const VaultHistoryKey &key, const VaultHistoryValue &value) {
    return vaultToJSON(view,
                       key.vaultID,
                       ScriptToString(key.address),
                       key.blockHeight,
                       ToString(CustomTxCodeToType(value.category)),
                       key.txn,
                       value.txid.ToString(),
                       value.diff);
}

UniValue collateralToJSON(const CCustomCSView &view, const VaultHistoryKey &key, const VaultHistoryValue &value) {
    return vaultToJSON(view,
                       key.vaultID,
                       "vaultCollateral",
                       key.blockHeight,
                       ToString(CustomTxCodeToType(value.category)),
                       key.txn,
                       value.txid.ToString(),
                       value.diff);
}

UniValue schemeToJSON(const CCustomCSView &view, const VaultSchemeKey &key, const VaultGlobalSchemeValue &value) {
    auto obj = vaultToJSON(view,
                           key.vaultID,
                           "",
                           key.blockHeight,
                           ToString(CustomTxCodeToType(value.category)),
                           0,
                           value.txid.ToString(),
                           {});

    UniValue scheme(UniValue::VOBJ);
    scheme.pushKV("id", value.loanScheme.identifier);
    scheme.pushKV("rate", value.loanScheme.rate);
    scheme.pushKV("ratio", static_cast<uint64_t>(value.loanScheme.ratio));

    obj.pushKV("loanScheme", scheme);

    return obj;
}

UniValue listvaulthistory(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "listvaulthistory",
        "\nReturns the history of the specified vault.\n",
        {
          {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault to get history for"},
          {
                "options",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"maxBlockHeight",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Optional height to iterate from (down to genesis block), (default = chaintip)."},
                    {"depth",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum depth, from the genesis block is the default"},
                    {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by token"},
                    {"txtype",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "Filter by transaction type, supported letter from {CustomTxType}"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of records to return, 100 by default"},
                },
            }, },
        RPCResult{"[{},{}...]     (array) Objects with vault history information\n"},
        RPCExamples{HelpExampleCli("listvaulthistory",
          "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 "
                                   "'{\"maxBlockHeight\":160,\"depth\":10}'") +
                    HelpExampleRpc("listvaulthistory",
          "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2, "
                                   "'{\"maxBlockHeight\":160,\"depth\":10}'")},
    }
        .Check(request);

    if (!pvaultHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-vaultindex required for vault history");
    }

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    uint256 vaultID = ParseHashV(request.params[0], "vaultId");
    uint32_t maxBlockHeight = std::numeric_limits<uint32_t>::max();
    uint32_t depth = maxBlockHeight;
    std::string tokenFilter;
    uint32_t limit = 100;
    auto txType = CustomTxType::None;
    bool txTypeSearch{false};

    if (request.params.size() == 2) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
                        {
                            {"maxBlockHeight", UniValueType(UniValue::VNUM)},
                            {"depth",          UniValueType(UniValue::VNUM)},
                            {"token",          UniValueType(UniValue::VSTR)},
                            {"txtype",         UniValueType(UniValue::VSTR)},
                            {"limit",          UniValueType(UniValue::VNUM)},
        },
                        true,
                        true);

        if (!optionsObj["maxBlockHeight"].isNull()) {
            maxBlockHeight = (uint32_t)optionsObj["maxBlockHeight"].get_int64();
        }

        if (!optionsObj["depth"].isNull()) {
            depth = (uint32_t)optionsObj["depth"].get_int64();
        }

        if (!optionsObj["token"].isNull()) {
            tokenFilter = optionsObj["token"].get_str();
        }

        if (!optionsObj["txtype"].isNull()) {
            const auto str = optionsObj["txtype"].get_str();
            if (str.size() == 1) {
                // Will search for type ::None if txtype not found.
                txType = CustomTxCodeToType(str[0]);
                txTypeSearch = true;
            }
        }

        if (!optionsObj["limit"].isNull()) {
            limit = (uint32_t)optionsObj["limit"].get_int64();
        }

        if (limit == 0) {
            limit = std::numeric_limits<uint32_t>::max();
        }
    }

    std::function<bool(const uint256 &)> isMatchVault = [&vaultID](const uint256 &id) { return id == vaultID; };

    std::set<uint256> txs;

    auto [view, accountView, vaultView] = GetSnapshots();

    auto hasToken = [&, &view = view](const TAmounts &diffs) {
        for (auto const &diff : diffs) {
            auto token = view->GetToken(diff.first);
            auto const tokenIdStr = token->CreateSymbolKey(diff.first);
            if (tokenIdStr == tokenFilter) {
                return true;
            }
        }
        return false;
    };

    std::map<uint32_t, UniValue, std::greater<>> ret;
    const uint32_t height = view->GetLastHeight();

    maxBlockHeight = std::min(maxBlockHeight, height);
    depth = std::min(depth, maxBlockHeight);

    const auto startBlock = maxBlockHeight - depth;
    auto shouldSkipBlock = [startBlock, maxBlockHeight](uint32_t blockHeight) {
        return startBlock > blockHeight || blockHeight > maxBlockHeight;
    };

    // Get vault TXs
    auto count = limit;

    auto shouldContinue = [&, &view = view](const VaultHistoryKey &key,
                                            CLazySerialize<VaultHistoryValue> valueLazy) -> bool {
        if (!isMatchVault(key.vaultID)) {
            return true;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        const auto &value = valueLazy.get();

        if (txTypeSearch && value.category != uint8_t(txType)) {
            return true;
        }

        if (!tokenFilter.empty() && !hasToken(value.diff)) {
            return true;
        }

        auto &array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;

        if (key.address.empty()) {
            array.push_back(collateralToJSON(*view, key, value));
        } else {
            array.push_back(historyToJSON(*view, key, value));
        }

        return --count != 0;
    };

    VaultHistoryKey startKey{maxBlockHeight, vaultID, std::numeric_limits<uint32_t>::max(), {}};
    vaultView->ForEachVaultHistory(shouldContinue, startKey);

    // Get vault state changes
    count = limit;

    auto shouldContinueState = [&, &view = view](const VaultStateKey &key,
                                                 CLazySerialize<VaultStateValue> valueLazy) -> bool {
        if (!isMatchVault(key.vaultID)) {
            return false;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        const auto &value = valueLazy.get();

        auto &array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(stateToJSON(*view, key, value));

        return --count != 0;
    };

    VaultStateKey stateKey{vaultID, maxBlockHeight};
    if (!txTypeSearch) {
        vaultView->ForEachVaultState(shouldContinueState, stateKey);
    }

    // Get vault schemes
    count = limit;

    std::map<uint32_t, uint256> schemes;

    auto shouldContinueScheme = [&, &view = view, &vaultView = vaultView](
                                    const VaultSchemeKey &key, CLazySerialize<VaultSchemeValue> valueLazy) -> bool {
        if (!isMatchVault(key.vaultID)) {
            return false;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        const auto &value = valueLazy.get();

        if (txTypeSearch && value.category != uint8_t(txType)) {
            return true;
        }

        CLoanScheme loanScheme;
        vaultView->ForEachGlobalScheme(
            [&](VaultGlobalSchemeKey const &schemeKey, CLazySerialize<VaultGlobalSchemeValue> lazyValue) {
                if (lazyValue.get().loanScheme.identifier != value.schemeID) {
                    return true;
                }
                loanScheme = lazyValue.get().loanScheme;
                schemes.emplace(key.blockHeight, schemeKey.schemeCreationTxid);
                return false;
            },
            {key.blockHeight, value.txn});

        auto &array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(schemeToJSON(*view, key, {loanScheme, value.category, value.txid}));

        return --count != 0;
    };

    if (tokenFilter.empty()) {
        vaultView->ForEachVaultScheme(shouldContinueScheme, stateKey);
    }

    // Get vault global scheme changes

    if (!schemes.empty()) {
        auto lastScheme = schemes.cbegin()->second;
        for (auto it = ++schemes.cbegin(); it != schemes.cend();) {
            if (lastScheme == it->second) {
                schemes.erase(it++);
            } else {
                ++it;
            }
        }

        auto minHeight = schemes.cbegin()->first;
        for (auto it = schemes.cbegin(); it != schemes.cend(); ++it) {
            auto nit = std::next(it);
            uint32_t endHeight = nit != schemes.cend() ? nit->first - 1 : std::numeric_limits<uint32_t>::max();
            vaultView->ForEachGlobalScheme(
                [&, &view = view](const VaultGlobalSchemeKey &key, CLazySerialize<VaultGlobalSchemeValue> valueLazy) {
                    if (key.blockHeight < minHeight) {
                        return false;
                    }

                    if (it->second != key.schemeCreationTxid) {
                        return true;
                    }

                    if (shouldSkipBlock(key.blockHeight)) {
                        return true;
                    }

                    const auto &value = valueLazy.get();

                    if (txTypeSearch && value.category != uint8_t(txType)) {
                        return true;
                    }

                    auto &array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
                    array.push_back(schemeToJSON(*view, {vaultID, key.blockHeight}, value));

                    return --count != 0;
                },
                {endHeight, std::numeric_limits<uint32_t>::max(), it->second});
        }
    }

    UniValue slice(UniValue::VARR);
    for (auto it = ret.cbegin(); limit != 0 && it != ret.cend(); ++it) {
        const auto &array = it->second.get_array();
        for (size_t i = 0; limit != 0 && i < array.size(); ++i) {
            slice.push_back(array[i]);
            --limit;
        }
    }

    return GetRPCResultCache().Set(request, slice);
}

UniValue estimateloan(const JSONRPCRequest &request) {
    RPCHelpMan{
        "estimateloan",
        "Returns amount of loan tokens a vault can take depending on a target collateral ratio.\n",
        {{
             "vaultId",
             RPCArg::Type::STR_HEX,
             RPCArg::Optional::NO,
             "vault hex id",
         }, {
             "tokens",
             RPCArg::Type::OBJ,
             RPCArg::Optional::NO,
             "Object with loans token as key and their percent split as value",
             {

                 {"split", RPCArg::Type::NUM, RPCArg::Optional::NO, "The percent split"},
             },
         }, {"targetRatio",
          RPCArg::Type::NUM,
          RPCArg::Optional::OMITTED,
          "Target collateral ratio. (defaults to vault's loan scheme ratio)"}},
        RPCResult{"\"json\"                  (Array) Array of <amount@token> strings\n"},
        RPCExamples{
          HelpExampleCli(
                "estimateloan", R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf '{"TSLA":0.5, "FB": 0.4, "GOOGL":0.1}' 150)") +
            HelpExampleRpc(
                "estimateloan", R"("5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf", {"TSLA":0.5, "FB": 0.4, "GOOGL":0.1}, 150)")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VNUM}, false);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");

    auto [view, accountView, vaultView] = GetSnapshots();

    auto vault = view->GetVault(vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Vault <%s> not found.", vaultId.GetHex()));
    }

    auto vaultState = GetVaultState(*view, vaultId, *vault);
    if (vaultState == VaultState::InLiquidation) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Vault <%s> is in liquidation.", vaultId.GetHex()));
    }

    auto scheme = view->GetLoanScheme(vault->schemeId);
    uint32_t ratio = scheme->ratio;
    if (request.params.size() > 2) {
        ratio = (size_t)request.params[2].get_int64();
    }

    auto collaterals = view->GetVaultCollaterals(vaultId);
    if (!collaterals) {
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot estimate loan without collaterals.");
    }

    auto height = view->GetLastHeight();
    int64_t blockTime{};
    {
        LOCK(cs_main);
        blockTime = ::ChainActive().Tip()->GetBlockTime();
    }
    auto rate = view->GetVaultAssets(vaultId, *collaterals, height + 1, blockTime, false, true);
    if (!rate.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, rate.msg);
    }

    CBalances loanBalances;
    CAmount totalSplit{0};
    if (request.params.size() > 1 && request.params[1].isObject()) {
        for (const auto &tokenId : request.params[1].getKeys()) {
            CAmount split = AmountFromValue(request.params[1][tokenId]);

            auto token = view->GetToken(tokenId);
            if (!token) {
                throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Token %d does not exist!", tokenId));
            }

            auto loanToken = view->GetLoanTokenByID(token->first);
            if (!loanToken) {
                throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("(%s) is not a loan token!", tokenId));
            }

            auto priceFeed = view->GetFixedIntervalPrice(loanToken->fixedIntervalPriceId);
            if (!priceFeed.ok) {
                throw JSONRPCError(RPC_DATABASE_ERROR, priceFeed.msg);
            }

            auto price = priceFeed.val->priceRecord[0];
            if (!priceFeed.val->isLive(view->GetPriceDeviation())) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("No live fixed price for %s", tokenId));
            }

            auto availableValue = MultiplyAmounts(rate.val->totalCollaterals, split);
            auto loanAmount = DivideAmounts(availableValue, price);
            auto amountRatio = MultiplyAmounts(DivideAmounts(loanAmount, ratio), 100);

            loanBalances.Add({token->first, amountRatio});
            totalSplit += split;
        }
        if (totalSplit != COIN) {
            throw JSONRPCError(RPC_MISC_ERROR,
                               strprintf("total split between loan tokens = %s vs expected %s",
                                         GetDecimalString(totalSplit),
                                         GetDecimalString(COIN)));
        }
    }
    auto res = AmountsToJSON(*view, loanBalances.balances);
    return GetRPCResultCache().Set(request, res);
}

UniValue estimatecollateral(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "estimatecollateral",
        "Returns amount of collateral tokens needed to take an amount of loan tokens for a target collateral ratio.\n",
        {
          {"loanAmounts",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Amount as json string, or array. Example: '[ \"amount@token\" ]'"},
          {"targetRatio", RPCArg::Type::NUM, RPCArg::Optional::NO, "Target collateral ratio."},
          {
                "tokens",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "Object with collateral token as key and their percent split as value. (defaults to { DFI: 1 }",
                {
                    {"split", RPCArg::Type::NUM, RPCArg::Optional::NO, "The percent split"},
                },
            }, },
        RPCResult{"\"json\"                  (Array) Array of <amount@token> strings\n"},
        RPCExamples{HelpExampleCli("estimatecollateral", R"(23.55311144@MSFT 150 '{"DFI": 0.8, "BTC":0.2}')") +
                    HelpExampleRpc("estimatecollateral", R"("23.55311144@MSFT" 150 {"DFI": 0.8, "BTC":0.2})")},
    }
        .Check(request);

    RPCTypeCheck(request.params, {UniValueType(), UniValue::VNUM, UniValue::VOBJ}, false);
    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    const CBalances loanBalances = DecodeAmounts(pwallet->chain(), request.params[0], "");
    auto ratio = request.params[1].get_int();

    std::map<std::string, UniValue> collateralSplits;
    if (request.params.size() > 2) {
        request.params[2].getObjMap(collateralSplits);
    } else {
        collateralSplits["DFI"] = 1;
    }

    auto [view, accountView, vaultView] = GetSnapshots();

    CAmount totalLoanValue{0};
    for (const auto &loan : loanBalances.balances) {
        auto loanToken = view->GetLoanTokenByID(loan.first);
        if (!loanToken) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Token with id (" + loan.first.ToString() + ") is not a loan token!");
        }

        auto amountInCurrency = view->GetAmountInCurrency(loan.second, loanToken->fixedIntervalPriceId);
        if (!amountInCurrency) {
            throw JSONRPCError(RPC_DATABASE_ERROR, amountInCurrency.msg);
        }
        totalLoanValue += *amountInCurrency.val;
    }

    const uint32_t height = view->GetLastHeight();
    CBalances collateralBalances;
    CAmount totalSplit{0};
    for (const auto &collateralSplit : collateralSplits) {
        CAmount split = AmountFromValue(collateralSplit.second);

        auto token = view->GetToken(collateralSplit.first);
        if (!token) {
            throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Token %s does not exist!", collateralSplit.first));
        }

        auto collateralToken = view->HasLoanCollateralToken({token->first, height});
        if (!collateralToken || !collateralToken->factor) {
            throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("(%s) is not a valid collateral!", collateralSplit.first));
        }

        auto priceFeed = view->GetFixedIntervalPrice(collateralToken->fixedIntervalPriceId);
        if (!priceFeed.ok) {
            throw JSONRPCError(RPC_DATABASE_ERROR, priceFeed.msg);
        }

        auto price = priceFeed.val->priceRecord[0];
        if (!priceFeed.val->isLive(view->GetPriceDeviation())) {
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("No live fixed price for %s", collateralSplit.first));
        }

        auto requiredValue = MultiplyAmounts(totalLoanValue, split);
        auto collateralValue = DivideAmounts(requiredValue, price);
        auto amountRatio = DivideAmounts(MultiplyAmounts(collateralValue, ratio), 100);
        auto totalAmount = DivideAmounts(amountRatio, collateralToken->factor);

        collateralBalances.Add({token->first, totalAmount});
        totalSplit += split;
    }
    if (totalSplit != COIN) {
        throw JSONRPCError(RPC_MISC_ERROR,
                           strprintf("total split between collateral tokens = %s vs expected %s",
                                     GetDecimalString(totalSplit),
                                     GetDecimalString(COIN)));
    }

    auto res = AmountsToJSON(*view, collateralBalances.balances);
    return GetRPCResultCache().Set(request, res);
}

UniValue estimatevault(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "estimatevault",
        "Returns estimated vault for given collateral and loan amounts.\n",
        {
          {"collateralAmounts",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Collateral amounts as json string, or array. Example: '[ \"amount@token\" ]'"},
          {"loanAmounts",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Loan amounts as json string, or array. Example: '[ \"amount@token\" ]'"},
          },
        RPCResult{"{\n"
                  "  \"collateralValue\" : n.nnnnnnnn,        (amount) The total collateral value in USD\n"
                  "  \"loanValue\" : n.nnnnnnnn,              (amount) The total loan value in USD\n"
                  "  \"informativeRatio\" : n.nnnnnnnn,       (amount) Informative ratio with 8 digit precision\n"
                  "  \"collateralRatio\" : n,                 (uint) Ratio as unsigned int\n"
                  "}\n"},
        RPCExamples{HelpExampleCli("estimatevault", R"('["1000.00000000@DFI"]' '["0.65999990@GOOGL"]')") +
                    HelpExampleRpc("estimatevault", R"(["1000.00000000@DFI"], ["0.65999990@GOOGL"])")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    CBalances collateralBalances = DecodeAmounts(pwallet->chain(), request.params[0], "");
    CBalances loanBalances = DecodeAmounts(pwallet->chain(), request.params[1], "");

    auto [view, accountView, vaultView] = GetSnapshots();
    uint32_t height = view->GetLastHeight();

    CVaultAssets result{};

    for (const auto &collateral : collateralBalances.balances) {
        auto collateralToken = view->HasLoanCollateralToken({collateral.first, height});
        if (!collateralToken || !collateralToken->factor) {
            throw JSONRPCError(RPC_DATABASE_ERROR,
                               strprintf("Token with id (%s) is not a valid collateral!", collateral.first.ToString()));
        }

        auto amountInCurrency = view->GetAmountInCurrency(collateral.second, collateralToken->fixedIntervalPriceId);
        if (!amountInCurrency) {
            throw JSONRPCError(RPC_DATABASE_ERROR, amountInCurrency.msg);
        }
        result.totalCollaterals += MultiplyAmounts(collateralToken->factor, *amountInCurrency.val);
        ;
    }

    for (const auto &loan : loanBalances.balances) {
        auto loanToken = view->GetLoanTokenByID(loan.first);
        if (!loanToken) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Token with id (" + loan.first.ToString() + ") is not a loan token!");
        }

        auto amountInCurrency = view->GetAmountInCurrency(loan.second, loanToken->fixedIntervalPriceId);
        if (!amountInCurrency) {
            throw JSONRPCError(RPC_DATABASE_ERROR, amountInCurrency.msg);
        }
        result.totalLoans += *amountInCurrency.val;
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("collateralValue", ValueFromUint(result.totalCollaterals));
    ret.pushKV("loanValue", ValueFromUint(result.totalLoans));
    ret.pushKV("informativeRatio", ValueFromAmount(result.precisionRatio()));
    ret.pushKV("collateralRatio", int(result.ratio()));
    return GetRPCResultCache().Set(request, ret);
}

UniValue getstoredinterest(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getstoredinterest",
        "Returns the stored interest for the specified vault and token.\n",
        {
          {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id"},
          {"token",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "One of the keys may be specified (id/symbol/creationTx)"},
          },
        RPCResult{
          "{\n"
            "  \"interestToHeight\" : n.nnnnnnnn,        (amount) Interest stored to the point of the hight value\n"
            "  \"interestPerBlock\" : n.nnnnnnnn,        (amount) Interest per block\n"
            "  \"height\" : n,                           (amount) Height stored interest last updated\n"
            "}\n"},
        RPCExamples{HelpExampleCli("getstoredinterest",
          R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf DUSD)") +
                    HelpExampleRpc("getstoredinterest",
          R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf, DUSD)")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    auto [view, accountView, vaultView] = GetSnapshots();

    const auto vaultId = ParseHashV(request.params[0], "vaultId");
    if (const auto vault = view->GetVault(vaultId); !vault) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Vault not found");
    }

    DCT_ID tokenId{};
    if (const auto token = view->GetTokenGuessId(request.params[1].getValStr(), tokenId); !token) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
    }

    const auto interestRate = view->GetInterestRate(vaultId, tokenId, view->GetLastHeight());
    if (!interestRate) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "No stored interest for this token found in the vault.");
    }

    UniValue ret(UniValue::VOBJ);

    ret.pushKV("interestToHeight", GetInterestPerBlockHighPrecisionString(interestRate->interestToHeight));
    ret.pushKV("interestPerBlock", GetInterestPerBlockHighPrecisionString(interestRate->interestPerBlock));
    ret.pushKV("height", static_cast<uint64_t>(interestRate->height));

    return GetRPCResultCache().Set(request, ret);
}

UniValue logstoredinterests(const JSONRPCRequest &request) {
    RPCHelpMan{
        "logstoredinterests",
        "Logs all stored interests.\n",
        {

        },
        RPCResult{"[\"vaultId\": {\n"
                  "  \"token\" : n,                            Token ID\n"
                  "  \"amount\" : n,                           (amount) Token Amount\n"
                  "  \"interestHeight\" : n,                   Height stored interest last updated\n"
                  "  \"interestToHeight\" : n.nnnnnnnn,        Interest stored to the point of the hight value\n"
                  "  \"interestPerBlock\" : n.nnnnnnnn,        nterest per block\n"
                  "}] \n"},
        RPCExamples{HelpExampleCli("logstoredinterests", "") + HelpExampleRpc("logstoredinterests", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    auto [view, accountView, vaultView] = GetSnapshots();
    auto height = view->GetLastHeight();

    using VaultInfo = std::tuple<DCT_ID, CAmount, CInterestRateV3>;
    std::map<std::string, std::vector<VaultInfo>> items;

    view->ForEachVault([&, &view = view](const CVaultId &vaultId, const CVaultData &vaultData) {
        auto vaultTokens = view->GetLoanTokens(vaultId);
        if (!vaultTokens) {
            return true;
        }

        std::vector<VaultInfo> infoItems;

        for (const auto &[tokenId, tokenAmount] : vaultTokens->balances) {
            const auto interestRate = view->GetInterestRate(vaultId, tokenId, height);
            if (interestRate) {
                infoItems.push_back({tokenId, tokenAmount, *interestRate});
            }
        }
        items[vaultId.ToString()] = infoItems;
        return true;
    });

    UniValue ret(UniValue::VARR);
    for (const auto &[vaultId, infoItems] : items) {
        UniValue v(UniValue::VOBJ);
        v.pushKV("vaultId", vaultId);
        UniValue vItems(UniValue::VARR);
        for (const auto &[tokenId, amount, rate] : infoItems) {
            UniValue i(UniValue::VOBJ);
            i.pushKV("token", tokenId.ToString());
            i.pushKV("amount", ValueFromAmount(amount));
            i.pushKV("interestHeight", static_cast<uint64_t>(rate.height));
            i.pushKV("interestToHeight", GetInterestPerBlockHighPrecisionString(rate.interestToHeight));
            i.pushKV("interestPerBlock", GetInterestPerBlockHighPrecisionString(rate.interestPerBlock));
            vItems.push_back(i);
        }
        v.pushKV("items", vItems);
        ret.push_back(v);
    }

    return GetRPCResultCache().Set(request, ret);
}

UniValue getloantokens(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getloantokens",
        "Returns loan tokens stored in a vault.\n",
        {
          {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id"},
          },
        RPCResult{"[ n.nnnnnnn@Symbol, ...]\n"},
        RPCExamples{
          HelpExampleCli("getloantokens", R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf)") +
            HelpExampleRpc("getloantokens", R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf)")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    auto [view, accountView, vaultView] = GetSnapshots();

    const auto vaultId = ParseHashV(request.params[0], "vaultId");
    const auto loanTokens = view->GetLoanTokens(vaultId);
    if (!loanTokens) {
        return UniValue::VARR;
    }

    const auto ret = AmountsToJSON(*view, loanTokens->balances);

    return GetRPCResultCache().Set(request, ret);
}

static const CRPCCommand commands[] = {
  //  category        name                         actor (function)        params
  //  --------------- ----------------------       ---------------------   ----------
    {"vault",  "createvault",        &createvault,        {"ownerAddress", "schemeId", "inputs"}     },
    {"vault",  "closevault",         &closevault,         {"id", "returnAddress", "inputs"}          },
    {"vault",  "listvaults",         &listvaults,         {"options", "pagination"}                  },
    {"vault",  "getvault",           &getvault,           {"id", "verbose"}                          },
    {"vault",  "listvaulthistory",   &listvaulthistory,   {"id", "options"}                          },
    {"vault",  "updatevault",        &updatevault,        {"id", "parameters", "inputs"}             },
    {"vault",  "deposittovault",     &deposittovault,     {"id", "from", "amount", "inputs"}         },
    {"vault",  "withdrawfromvault",  &withdrawfromvault,  {"id", "to", "amount", "inputs"}           },
    {"vault",  "placeauctionbid",    &placeauctionbid,    {"id", "index", "from", "amount", "inputs"}},
    {"vault",  "listauctions",       &listauctions,       {"pagination"}                             },
    {"vault",  "listauctionhistory", &listauctionhistory, {"owner", "pagination"}                    },
    {"vault",  "estimateloan",       &estimateloan,       {"vaultId", "tokens", "targetRatio"}       },
    {"vault",  "estimatecollateral", &estimatecollateral, {"loanAmounts", "targetRatio", "tokens"}   },
    {"vault",  "estimatevault",      &estimatevault,      {"collateralAmounts", "loanAmounts"}       },
    {"hidden", "getstoredinterest",  &getstoredinterest,  {"vaultId", "token"}                       },
    {"hidden", "logstoredinterests", &logstoredinterests, {}                                         },
    {"hidden", "getloantokens",      &getloantokens,      {"vaultId"}                                },
};

void RegisterVaultRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}

#include <masternodes/accountshistory.h>
#include <masternodes/auctionhistory.h>
#include <masternodes/mn_rpc.h>
#include <masternodes/vaulthistory.h>

extern UniValue AmountsToJSON(TAmounts const & diffs);
extern std::string tokenAmountString(CTokenAmount const& amount);

namespace {

    enum class VaultState : uint32_t {
        Unknown = 0,
        Active = (1 << 0),
        InLiquidation = (1 << 1),
        Frozen = (1 << 2),
        MayLiquidate = (1 << 3)
    };

    std::string VaultStateToString(const VaultState& state)
    {
        switch (state) {
            case VaultState::Active:
                return "active";
            case VaultState::Frozen:
                return "frozen";
            case VaultState::InLiquidation:
                return "inLiquidation";
            case VaultState::MayLiquidate:
                return "mayLiquidate";
            case VaultState::Unknown:
                return "unknown";
        }
    }

    VaultState StringToVaultState(const std::string& stateStr)
    {
        if (stateStr == "active") return VaultState::Active;
        if (stateStr == "frozen") return VaultState::Frozen;
        if (stateStr == "inLiquidation") return VaultState::InLiquidation;
        if (stateStr == "mayLiquidate") return VaultState::MayLiquidate;
        return VaultState::Unknown;
    }

    bool WillLiquidateNext(CCustomCSView& view, const CVaultId& vaultId, const CVaultData& vault, int blockTime) {
        auto collaterals = view.GetVaultCollaterals(vaultId);
        if (!collaterals)
            return false;

        int height = view.GetLastHeight();
        bool useNextPrice = true, requireLivePrice = false;
        auto vaultRate = view.GetLoanCollaterals(vaultId, *collaterals, height, blockTime, useNextPrice, requireLivePrice);
        if (!vaultRate)
            return false;

        auto loanScheme = view.GetLoanScheme(vault.schemeId);
        return (vaultRate.val->ratio() < loanScheme->ratio);
    }

    VaultState GetVaultState(CCustomCSView& view, const CVaultId& vaultId, const CVaultData& vault, int blockTime) {
        int height = view.GetLastHeight();
        auto inLiquidation = vault.isUnderLiquidation;
        auto priceIsValid = IsVaultPriceValid(view, vaultId, height);
        auto willLiquidateNext = WillLiquidateNext(view, vaultId, vault, blockTime);

        // Can possibly optimize with flags, but provides clarity for now.
        if (!inLiquidation && priceIsValid && !willLiquidateNext)
            return VaultState::Active;
        if (!inLiquidation && priceIsValid && willLiquidateNext)
            return VaultState::MayLiquidate;
        if (!inLiquidation && !priceIsValid)
            return VaultState::Frozen;
        if (inLiquidation && priceIsValid)
            return VaultState::InLiquidation;
        return VaultState::Unknown;
    }

    UniValue BatchToJSON(CCustomCSView& view, const CVaultId& vaultId, uint32_t batchCount) {
        UniValue batchArray{UniValue::VARR};
        for (uint32_t i = 0; i < batchCount; i++) {
            UniValue batchObj{UniValue::VOBJ};
            auto batch = view.GetAuctionBatch(vaultId, i);
            batchObj.pushKV("index", int(i));
            batchObj.pushKV("collaterals", AmountsToJSON(batch->collaterals.balances));
            batchObj.pushKV("loan", tokenAmountString(batch->loanAmount));
            if (auto bid = view.GetAuctionBid(vaultId, i)) {
                UniValue bidObj{UniValue::VOBJ};
                bidObj.pushKV("owner", ScriptToString(bid->first));
                bidObj.pushKV("amount", tokenAmountString(bid->second));
                batchObj.pushKV("highestBid", bidObj);
            }
            batchArray.push_back(batchObj);
        }
        return batchArray;
    }

    UniValue AuctionToJSON(CCustomCSView& view, const CVaultId& vaultId, const CAuctionData& data) {
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

    UniValue VaultToJSON(CCustomCSView& view, const CVaultId& vaultId, const CVaultData& vault, int blockTime) {

        int height = view.GetLastHeight();
        auto vaultState = GetVaultState(view, vaultId, vault, blockTime);

        UniValue result{UniValue::VOBJ};
        if (vaultState == VaultState::InLiquidation) {
            if (auto data = view.GetAuction(vaultId, height)) {
                result.pushKVs(AuctionToJSON(view, vaultId, *data));
            } else {
                LogPrintf("Warning: Vault in liquidation, but no auctions found\n");
            }
            return result;
        }

        UniValue ratioValue{0}, collValue{0}, loanValue{0}, interestValue{0}, collateralRatio{0};

        auto collaterals = view.GetVaultCollaterals(vaultId);
        if (!collaterals)
            collaterals = CBalances{};

        bool useNextPrice = false, requireLivePrice = vaultState != VaultState::Frozen;
        LogPrint(BCLog::LOAN,"%s():\n", __func__);
        auto rate = view.GetLoanCollaterals(vaultId, *collaterals, height + 1, blockTime, useNextPrice, requireLivePrice);

        if (rate) {
            collValue = ValueFromUint(rate.val->totalCollaterals);
            loanValue = ValueFromUint(rate.val->totalLoans);
            ratioValue = ValueFromAmount(rate.val->precisionRatio());
            collateralRatio = int(rate.val->ratio());
        }

        UniValue loanBalances{UniValue::VARR};
        UniValue interestAmounts{UniValue::VARR};

        if (auto loanTokens = view.GetLoanTokens(vaultId)) {
            TAmounts totalBalances{};
            TAmounts interestBalances{};
            CAmount totalInterests{0};

            for (const auto& loan : loanTokens->balances) {
                auto token = view.GetLoanTokenByID(loan.first);
                if (!token) continue;
                auto rate = view.GetInterestRate(vaultId, loan.first, height);
                if (!rate) continue;
                LogPrint(BCLog::LOAN,"%s()->%s->", __func__, token->symbol); /* Continued */
                auto totalInterest = TotalInterest(*rate, height + 1);
                auto value = loan.second + totalInterest;
                if (auto priceFeed = view.GetFixedIntervalPrice(token->fixedIntervalPriceId)) {
                    auto price = priceFeed.val->priceRecord[0];
                    totalInterests += MultiplyAmounts(price, totalInterest);
                }
                totalBalances.insert({loan.first, value});
                interestBalances.insert({loan.first, totalInterest});
            }
            interestValue = ValueFromAmount(totalInterests);
            loanBalances = AmountsToJSON(totalBalances);
            interestAmounts = AmountsToJSON(interestBalances);
        }

        result.pushKV("vaultId", vaultId.GetHex());
        result.pushKV("loanSchemeId", vault.schemeId);
        result.pushKV("ownerAddress", ScriptToString(vault.ownerAddress));
        result.pushKV("state", VaultStateToString(vaultState));
        result.pushKV("collateralAmounts", AmountsToJSON(collaterals->balances));
        result.pushKV("loanAmounts", loanBalances);
        result.pushKV("interestAmounts", interestAmounts);
        result.pushKV("collateralValue", collValue);
        result.pushKV("loanValue", loanValue);
        result.pushKV("interestValue", interestValue);
        result.pushKV("informativeRatio", ratioValue);
        result.pushKV("collateralRatio", collateralRatio);
        return result;
    }
}

UniValue createvault(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"createvault",
                "Creates a vault transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address"},
                    {"loanSchemeId", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                        "Unique identifier of the loan scheme (8 chars max). If empty, the default loan scheme will be selected (Optional)"
                    },
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                            {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                     {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                     {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                            },
                        },
                    },
                },
                RPCResult{
                   "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                   HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv") +
                   HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv \"\"") +
                   HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv LOAN0001") +
                   HelpExampleRpc("createvault", R"("2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv")") +
                   HelpExampleRpc("createvault", R"("2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv", "")") +
                   HelpExampleRpc("createvault", R"("2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv", "LOAN0001")")
                },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot createvault while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, true);

    CVaultMessage vault;
    vault.ownerAddress = DecodeScript(request.params[0].getValStr());

    if (request.params.size() > 1) {
        if (!request.params[1].isNull()) {
            vault.schemeId = request.params[1].get_str();
        }
    }

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::Vault)
             << vault;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2]);

    rawTx.vout.emplace_back(Params().GetConsensus().vaultCreationFee, scriptMeta);

    CCoinControl coinControl;

    // Set change to foundation address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue closevault(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"closevault",
                "Close vault transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault to be closed"},
                    {"to", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address to receive collaterals (if any) and half fee back"},
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                            {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                     {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                     {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                            },
                        },
                    },
                },
                RPCResult{
                   "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                    HelpExampleCli("closevault", "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF") +
                    HelpExampleRpc("closevault", R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2", "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF")")
                },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot closevault while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR}, false);

    int targetHeight;
    CScript ownerAddress;
    CCloseVaultMessage msg;
    msg.vaultId = ParseHashV(request.params[0], "vaultId");
    {
        // decode vaultId
        auto vault = pcustomcsview->GetVault(msg.vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> does not found", msg.vaultId.GetHex()));

        if (vault->isUnderLiquidation)
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");

        ownerAddress = vault->ownerAddress;
        targetHeight = pcustomcsview->GetLastHeight() + 1;
    }

    msg.to = DecodeScript(request.params[1].getValStr());

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CloseVault)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[2]);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Set change to foundation address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listvaults(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"listvaults",
               "List all available vaults.\n",
               {
                    {
                       "options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {
                                "ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                "Vault owner address (or CScript) or reserved word \"mine\""
                            },
                            {
                                "loanSchemeId", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                "Vault's loan scheme id"
                            },
                            {
                                "state", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                "Wether the vault is under a given state. (default = 'unknown')"
                            },
                            {
                                "verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                "Flag for verbose list (default = false), otherwise only ids, ownerAddress, loanSchemeIds and state are listed"
                            }
                        },
                    },
                    {
                       "pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {
                                "start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                "Optional first key to iterate from, in lexicographical order. "
                                "Typically it's set to last ID from previous request."
                            },
                            {
                                "including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                "If true, then iterate including starting position. False by default"
                            },
                            {
                                "limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Maximum number of orders to return, 100 by default"
                            },
                        },
                    },
               },
               RPCResult{
                    "[                         (json array of objects)\n"
                        "{...}                 (object) Json object with vault information\n"
                    "]\n"
               },
               RPCExamples{
                       HelpExampleCli("listvaults", "")
                       + HelpExampleCli("listvaults", "'{\"loanSchemeId\": \"LOAN1502\"}'")
                       + HelpExampleCli("listvaults", "'{\"loanSchemeId\": \"LOAN1502\"}' '{\"start\":\"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013\", ""\"including_start\": true, ""\"limit\":100}'")
                       + HelpExampleCli("listvaults", "{} '{\"start\":\"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013\", ""\"including_start\": true, ""\"limit\":100}'")
                       + HelpExampleRpc("listvaults", "")
                       + HelpExampleRpc("listvaults", R"({"loanSchemeId": "LOAN1502"})")
                       + HelpExampleRpc("listvaults", R"({"loanSchemeId": "LOAN1502"}, {"start":"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013", "including_start": true, "limit":100})")
                       + HelpExampleRpc("listvaults", R"({}, {"start":"3ef9fd5bd1d0ce94751e6286710051361e8ef8fac43cca9cb22397bf0d17e013", "including_start": true, "limit":100})")
               },
    }.Check(request);

    CScript ownerAddress = {};
    std::string loanSchemeId;
    VaultState state{VaultState::Unknown};
    bool verbose{false};
    bool isMine{false};
    if (request.params.size() > 0) {
        UniValue optionsObj = request.params[0].get_obj();
        if (!optionsObj["ownerAddress"].isNull()) {
            auto ownerAddressStr = optionsObj["ownerAddress"].getValStr();
            isMine = (ownerAddressStr == "mine");
            if (!isMine)
                ownerAddress = DecodeScript(ownerAddressStr);
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
                limit = (size_t) paginationObj["limit"].get_int64();
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

    CCustomCSView view(*pcustomcsview);
    auto height = view.GetLastHeight();
    auto blockTime = WITH_LOCK(cs_main, return ::ChainActive()[height]->GetBlockTime());

    view.ForEachVault([&](const CVaultId& vaultId, const CVaultData& data) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
        if (!ownerAddress.empty() && ownerAddress != data.ownerAddress) {
            return false;
        }

        if (isMine && !(IsMineCached(*pwallet, data.ownerAddress) & ISMINE_SPENDABLE)) {
            return true;
        }
        auto vaultState = GetVaultState(view, vaultId, data, blockTime);

        if ((loanSchemeId.empty() || loanSchemeId == data.schemeId)
        && (state == VaultState::Unknown || state == vaultState)) {
            UniValue vaultObj{UniValue::VOBJ};
            if(!verbose){
                vaultObj.pushKV("vaultId", vaultId.GetHex());
                vaultObj.pushKV("ownerAddress", ScriptToString(data.ownerAddress));
                vaultObj.pushKV("loanSchemeId", data.schemeId);
                vaultObj.pushKV("state", VaultStateToString(vaultState));
            } else {
                vaultObj = VaultToJSON(view, vaultId, data, blockTime);
            }
            valueArr.push_back(vaultObj);
            limit--;
        }
        return limit != 0;
    }, start, ownerAddress);

    return valueArr;
}

UniValue getvault(const JSONRPCRequest& request) {

    RPCHelpMan{"getvault",
               "Returns information about vault.\n",
                {
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id",},
                },
                RPCResult{
                    "\"json\"                  (string) vault data in json form\n"
                },
               RPCExamples{
                       HelpExampleCli("getvault", "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf") +
                       HelpExampleRpc("getvault", R"("5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf")")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);

    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");

    CCustomCSView view(*pcustomcsview);
    auto vault = view.GetVault(vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Vault <%s> not found", vaultId.GetHex()));
    }

    auto height = view.GetLastHeight();
    auto blockTime = WITH_LOCK(cs_main, return ::ChainActive()[height]->GetBlockTime());

    return VaultToJSON(view, vaultId, *vault, blockTime);
}

UniValue updatevault(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"updatevault",
               "\nCreates (and submits to local node and network) an `update vault transaction`, \n"
               "and saves vault updates to database.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                        {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault id"},
                        {"parameters", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                            {
                                {"ownerAddress", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Vault's owner address"},
                                {"loanSchemeId", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Vault's loan scheme id"},
                            },
                        },
                        {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                            {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                    {
                                        {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                        {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                    },
                                },
                            },
                        },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli(
                               "updatevault",
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 '{"ownerAddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanSchemeId": "LOANSCHEME001"}')")
                       + HelpExampleRpc(
                               "updatevault",
                               R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2", {"ownerAddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanSchemeId": "LOANSCHEME001"})")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update vault while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 must be non-null");

    int targetHeight;
    CVaultMessage vault;
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    {
        // decode vaultId
        auto storedVault = pcustomcsview->GetVault(vaultId);
        if (!storedVault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> does not found", vaultId.GetHex()));

        if(storedVault->isUnderLiquidation)
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");

        vault = *storedVault;
        targetHeight = pcustomcsview->GetLastHeight() + 1;
    }

    CUpdateVaultMessage msg{
        vaultId,
        vault.ownerAddress,
        vault.schemeId
    };

    if (request.params[1].isNull()){
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 2 must be non-null and expected as object at least with one of"
                           "{\"ownerAddress\",\"loanSchemeId\"}");
    }
    UniValue params = request.params[1].get_obj();
    if (params["ownerAddress"].isNull() && params["loanSchemeId"].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "At least ownerAddress OR loanSchemeId must be set");

    if (!params["ownerAddress"].isNull()){
        auto ownerAddress = params["ownerAddress"].getValStr();
        // check address validity
        CTxDestination ownerDest = DecodeDestination(ownerAddress);
        if (!IsValidDestination(ownerDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid owner address");
        }
        msg.ownerAddress = DecodeScript(ownerAddress);
    }
    if(!params["loanSchemeId"].isNull()){
        auto loanschemeid = params["loanSchemeId"].getValStr();
        msg.schemeId = loanschemeid;
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UpdateVault)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const &txInputs = request.params[2];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{vault.ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to auth address if there's only one auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue deposittovault(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"deposittovault",
               "Deposit collateral token amount to vault.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault id"},
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "Address containing collateral",},
                    {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount of collateral in amount@symbol format",},
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                            },
                        },
                    }
               },
               RPCResult{
                    "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("deposittovault",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 1@DFI") +
                       HelpExampleRpc("deposittovault",
                        R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i", "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "1@DFI")")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot upddeposittovaultate vault while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    if (request.params[0].isNull() || request.params[1].isNull() || request.params[2].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments must be non-null");

    // decode vaultId
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    auto from = DecodeScript(request.params[1].get_str());
    CTokenAmount amount = DecodeAmount(pwallet->chain(),request.params[2].get_str(), "amount");

    CDepositToVaultMessage msg{vaultId, from, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::DepositToVault)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[3];

    CTransactionRef optAuthTx;
    std::set<CScript> auths{from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue withdrawfromvault(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"withdrawfromvault",
               "Withdraw collateral token amount from vault.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"vaultId", RPCArg::Type::STR, RPCArg::Optional::NO, "Vault id"},
                    {"to", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination address for withdraw of collateral",},
                    {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount of collateral in amount@symbol format",},
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                            },
                        },
                    }
               },
               RPCResult{
                    "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("withdrawfromvault",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 1@DFI") +
                       HelpExampleRpc("withdrawfromvault",
                        R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i", "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "1@DFI")")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot withdrawfromvault while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    if (request.params[0].isNull() || request.params[1].isNull() || request.params[2].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments must be non-null");

    // decode vaultId
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    auto to = DecodeScript(request.params[1].get_str());
    CTokenAmount amount = DecodeAmount(pwallet->chain(),request.params[2].get_str(), "amount");

    CWithdrawFromVaultMessage msg{vaultId, to, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::WithdrawFromVault)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight;
    CScript ownerAddress;
    {
        // decode vaultId
        auto vault = pcustomcsview->GetVault(vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> does not found", vaultId.GetHex()));

        ownerAddress = vault->ownerAddress;
        targetHeight = pcustomcsview->GetLastHeight() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[3];

    CTransactionRef optAuthTx;
    std::set<CScript> auths{ownerAddress};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(ownerAddress, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue placeauctionbid(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"placeauctionbid",
               "Bid to vault in auction.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault id"},
                    {"index", RPCArg::Type::NUM, RPCArg::Optional::NO, "Auction index"},
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to get tokens. If \"from\" value is: \"*\" (star), it's means auto-selection accounts from wallet."},
                    {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount of amount@symbol format"},
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                            },
                        },
                    }
               },
               RPCResult{
                    "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("placeauctionbid",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 0 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 100@TSLA") +
                       HelpExampleRpc("placeauctionbid",
                        R"("84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2", 0, "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "1@DTSLA")")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VNUM, UniValue::VSTR, UniValue::VSTR, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot make auction bid while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();

    // decode vaultId
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    uint32_t index = request.params[1].get_int();
    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[3].get_str(), "amount");

    CScript from = {};
    auto fromStr = request.params[2].get_str();
    if (fromStr == "*") {
        auto selectedAccounts = SelectAccountsByTargetBalances(GetAllMineAccounts(pwallet), CBalances{TAmounts{{amount.nTokenId, amount.nValue}}}, SelectionPie);

        for (auto& account : selectedAccounts) {
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

    CAuctionBidMessage msg{vaultId, index, from, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AuctionBid)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CTransactionRef optAuthTx;
    std::set<CScript> auths{from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, request.params[4]);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listauctions(const JSONRPCRequest& request) {

    RPCHelpMan{"listauctions",
               "List all available auctions.\n",
               {
                   {
                       "pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {
                                "start", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {
                                        "vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                        "Vault id"
                                    },
                                    {
                                        "height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                        "Height to iterate from"
                                    }
                                }
                            },
                            {
                                "including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                "If true, then iterate including starting position. False by default"
                            },
                            {
                                "limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Maximum number of orders to return, 100 by default"
                            },
                        },
                    },
               },
               RPCResult{
                    "[                         (json array of objects)\n"
                        "{...}                 (object) Json object with auction information\n"
                    "]\n"
               },
               RPCExamples{
                       HelpExampleCli("listauctions",  "") +
                       HelpExampleCli("listauctions", "'{\"start\": {\"vaultId\":\"eeea650e5de30b77d17e3907204d200dfa4996e5c4d48b000ae8e70078fe7542\", \"height\": 1000}, \"including_start\": true, ""\"limit\":100}'") +
                       HelpExampleRpc("listauctions",  "") +
                       HelpExampleRpc("listauctions", R"({"start": {"vaultId":"eeea650e5de30b77d17e3907204d200dfa4996e5c4d48b000ae8e70078fe7542", "height": 1000}, "including_start": true, "limit":100})")
               },
    }.Check(request);

    // parse pagination
    CVaultId vaultId;
    size_t limit = 100;
    uint32_t height = 0;
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
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
    CCustomCSView view(*pcustomcsview);
    view.ForEachVaultAuction([&](const CVaultId& vaultId, const CAuctionData& data) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
        valueArr.push_back(AuctionToJSON(view, vaultId, data));
        return --limit != 0;
    }, height, vaultId);

    return valueArr;
}

UniValue auctionhistoryToJSON(AuctionHistoryKey const & key, AuctionHistoryValue const & value) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("winner", ScriptToString(key.owner));
    obj.pushKV("blockHeight", (uint64_t) key.blockHeight);
    if (auto block = WITH_LOCK(cs_main, return ::ChainActive()[key.blockHeight])) {
        obj.pushKV("blockHash", block->GetBlockHash().GetHex());
        obj.pushKV("blockTime", block->GetBlockTime());
    }
    obj.pushKV("vaultId", key.vaultId.GetHex());
    obj.pushKV("batchIndex", (uint64_t) key.index);
    obj.pushKV("auctionBid", tokenAmountString(value.bidAmount));
    obj.pushKV("auctionWon", AmountsToJSON(value.collaterals));
    return obj;
}

UniValue listauctionhistory(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"listauctionhistory",
               "\nReturns information about auction history.\n",
               {
                    {"owner|vaultId", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                "Single account ID (CScript or address) or vaultId or reserved words: \"mine\" - to list history for all owned accounts or \"all\" to list whole DB (default = \"mine\")."},
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {
                                "maxBlockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Optional height to iterate from (downto genesis block)"
                            },
                            {
                                "vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                "Vault id"
                            },
                            {
                                "index", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Batch index"
                            },
                            {
                                "limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                "Maximum number of orders to return, 100 by default"
                            },
                        },
                    },
               },
               RPCResult{
                       "[{},{}...]     (array) Objects with auction history information\n"
               },
               RPCExamples{
                       HelpExampleCli("listauctionhistory", "all '{\"height\":160}'")
                       + HelpExampleRpc("listauctionhistory", "")
               },
    }.Check(request);

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is needed for auction history");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

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
                limit = (size_t) paginationObj["limit"].get_int64();
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

    paccountHistoryDB->ForEachAuctionHistory([&](AuctionHistoryKey const & key, CLazySerialize<AuctionHistoryValue> valueLazy) -> bool {
        if (filter == 0 && start.owner != key.owner) {
            return true;
        }

        if (filter == 1 && start.vaultId != key.vaultId) {
            return true;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & ISMINE_SPENDABLE)) {
            return true;
        }

        ret.push_back(auctionhistoryToJSON(key, valueLazy.get()));

        return --limit != 0;
    }, start);

    return ret;
}

UniValue vaultToJSON(const uint256& vaultID, const std::string& address, const uint64_t blockHeight, const std::string& type,
                     const uint64_t txn, const std::string& txid, const TAmounts& amounts) {
    UniValue obj(UniValue::VOBJ);

    if (!address.empty()) {
        obj.pushKV("address", address);
    }
    obj.pushKV("blockHeight", blockHeight);
    if (auto block = WITH_LOCK(cs_main, return ::ChainActive()[blockHeight])) {
        obj.pushKV("blockHash", block->GetBlockHash().GetHex());
        obj.pushKV("blockTime", block->GetBlockTime());
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
        obj.pushKV("amounts", AmountsToJSON(amounts));
    }

    return obj;
}

UniValue BatchToJSON(const std::vector<CAuctionBatch> batches) {
    UniValue batchArray{UniValue::VARR};
    for (uint64_t i{0}; i < batches.size(); ++i) {
        UniValue batchObj{UniValue::VOBJ};
        batchObj.pushKV("index", i);
        batchObj.pushKV("collaterals", AmountsToJSON(batches[i].collaterals.balances));
        batchObj.pushKV("loan", tokenAmountString(batches[i].loanAmount));
        batchArray.push_back(batchObj);
    }
    return batchArray;
}

UniValue stateToJSON(VaultStateKey const & key, VaultStateValue const & value) {
    auto obj = vaultToJSON(key.vaultID, "", key.blockHeight, "", 0, "", {});

    UniValue snapshot(UniValue::VOBJ);
    snapshot.pushKV("state", !value.auctionBatches.empty() ? "inLiquidation" : "active");
    snapshot.pushKV("collateralAmounts", AmountsToJSON(value.collaterals));
    snapshot.pushKV("collateralValue", ValueFromUint(value.collateralsValues.totalCollaterals));
    snapshot.pushKV("collateralRatio", value.ratio != -1 ? static_cast<int>(value.ratio) : static_cast<int>(value.collateralsValues.ratio()));
    if (!value.auctionBatches.empty()) {
        snapshot.pushKV("batches", BatchToJSON(value.auctionBatches));
    }

    obj.pushKV("vaultSnapshot", snapshot);

    return obj;
}

UniValue historyToJSON(VaultHistoryKey const & key, VaultHistoryValue const & value) {
    return vaultToJSON(key.vaultID, ScriptToString(key.address), key.blockHeight, ToString(CustomTxCodeToType(value.category)),
                       key.txn, value.txid.ToString(), value.diff);
}

UniValue schemeToJSON(VaultSchemeKey const & key, const VaultGlobalSchemeValue& value) {
    auto obj = vaultToJSON(key.vaultID, "", key.blockHeight, ToString(CustomTxCodeToType(value.category)), 0, value.txid.ToString(), {});

    UniValue scheme(UniValue::VOBJ);
    scheme.pushKV("id", value.loanScheme.identifier);
    scheme.pushKV("rate", value.loanScheme.rate);
    scheme.pushKV("ratio", static_cast<uint64_t>(value.loanScheme.ratio));

    obj.pushKV("loanScheme", scheme);

    return obj;
}

UniValue listvaulthistory(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"listvaulthistory",
               "\nReturns the history of the specified vault.\n",
               {
                       {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Vault to get history for"},
                       {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                                {"maxBlockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Optional height to iterate from (down to genesis block), (default = chaintip)."},
                                {"depth", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum depth, from the genesis block is the default"},
                                {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Filter by token"},
                                {"txtype", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Filter by transaction type, supported letter from {CustomTxType}"},
                                {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of records to return, 100 by default"},
                        },
                       },
               },
               RPCResult{
                       "[{},{}...]     (array) Objects with vault history information\n"
               },
               RPCExamples{
                       HelpExampleCli("listburnhistory", "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 '{\"maxBlockHeight\":160,\"depth\":10}'")
                       + HelpExampleRpc("listburnhistory", "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2, '{\"maxBlockHeight\":160,\"depth\":10}'")
               },
    }.Check(request);

    if (!pvaultHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-vaultindex required for vault history");
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
                                {"depth", UniValueType(UniValue::VNUM)},
                                {"token", UniValueType(UniValue::VSTR)},
                                {"txtype", UniValueType(UniValue::VSTR)},
                                {"limit", UniValueType(UniValue::VNUM)},
                        }, true, true);

        if (!optionsObj["maxBlockHeight"].isNull()) {
            maxBlockHeight = (uint32_t) optionsObj["maxBlockHeight"].get_int64();
        }

        if (!optionsObj["depth"].isNull()) {
            depth = (uint32_t) optionsObj["depth"].get_int64();
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
            limit = (uint32_t) optionsObj["limit"].get_int64();
        }

        if (limit == 0) {
            limit = std::numeric_limits<uint32_t>::max();
        }
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    std::function<bool(uint256 const &)> isMatchVault = [&vaultID](uint256 const & id) {
        return id == vaultID;
    };

    std::set<uint256> txs;

    auto hasToken = [&tokenFilter](TAmounts const & diffs) {
        for (auto const & diff : diffs) {
            auto token = pcustomcsview->GetToken(diff.first);
            auto const tokenIdStr = token->CreateSymbolKey(diff.first);
            if(tokenIdStr == tokenFilter) {
                return true;
            }
        }
        return false;
    };

    std::map<uint32_t, UniValue, std::greater<uint32_t>> ret;

    maxBlockHeight = std::min(maxBlockHeight, uint32_t(pcustomcsview->GetLastHeight()));
    depth = std::min(depth, maxBlockHeight);

    const auto startBlock = maxBlockHeight - depth;
    auto shouldSkipBlock = [startBlock, maxBlockHeight](uint32_t blockHeight) {
        return startBlock > blockHeight || blockHeight > maxBlockHeight;
    };

    // Get vault TXs
    auto count = limit;

    auto shouldContinue = [&](VaultHistoryKey const & key, CLazySerialize<VaultHistoryValue> valueLazy) -> bool
    {
        if (!isMatchVault(key.vaultID)) {
            return true;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        const auto & value = valueLazy.get();

        if (txTypeSearch && value.category != uint8_t(txType)) {
            return true;
        }

        if(!tokenFilter.empty() && !hasToken(value.diff)) {
            return true;
        }

        auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(historyToJSON(key, value));

        return --count != 0;
    };

    VaultHistoryKey startKey{maxBlockHeight, vaultID, std::numeric_limits<uint32_t>::max(), {}};
    pvaultHistoryDB->ForEachVaultHistory(shouldContinue, startKey);

    // Get vault state changes
    count = limit;

    auto shouldContinueState = [&](VaultStateKey const & key, CLazySerialize<VaultStateValue> valueLazy) -> bool
    {
        if (!isMatchVault(key.vaultID)) {
            return false;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        const auto & value = valueLazy.get();

        auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(stateToJSON(key, value));

        return --count != 0;
    };

    VaultStateKey stateKey{vaultID, maxBlockHeight};
    if (!txTypeSearch) {
        pvaultHistoryDB->ForEachVaultState(shouldContinueState, stateKey);
    }

    // Get vault schemes
    count = limit;

    std::map<uint32_t, uint256> schemes;

    auto shouldContinueScheme = [&](VaultSchemeKey const & key, CLazySerialize<VaultSchemeValue> valueLazy) -> bool
    {
        if (!isMatchVault(key.vaultID)) {
            return false;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        const auto & value = valueLazy.get();

        if (txTypeSearch && value.category != uint8_t(txType)) {
            return true;
        }

        CLoanScheme loanScheme;
        pvaultHistoryDB->ForEachGlobalScheme([&](VaultGlobalSchemeKey const & schemeKey, CLazySerialize<VaultGlobalSchemeValue> lazyValue) {
            if (lazyValue.get().loanScheme.identifier != value.schemeID) {
                return true;
            }
            loanScheme = lazyValue.get().loanScheme;
            schemes.emplace(key.blockHeight, schemeKey.schemeCreationTxid);
            return false;
        }, {key.blockHeight, value.txn});

        auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(schemeToJSON(key, {loanScheme, value.category, value.txid}));

        return --count != 0;
    };

    if (tokenFilter.empty()) {
        pvaultHistoryDB->ForEachVaultScheme(shouldContinueScheme, stateKey);
    }

    // Get vault global scheme changes

    if (!schemes.empty()) {
        auto lastScheme = schemes.cbegin()->second;
        for (auto it = ++schemes.cbegin(); it != schemes.cend(); ) {
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
            pvaultHistoryDB->ForEachGlobalScheme([&](VaultGlobalSchemeKey const & key, CLazySerialize<VaultGlobalSchemeValue> valueLazy){
                if (key.blockHeight < minHeight) {
                    return false;
                }

                if (it->second != key.schemeCreationTxid) {
                    return true;
                }

                if (shouldSkipBlock(key.blockHeight)) {
                    return true;
                }

                const auto & value = valueLazy.get();

                if (txTypeSearch && value.category != uint8_t(txType)) {
                    return true;
                }

                auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
                array.push_back(schemeToJSON({vaultID, key.blockHeight}, value));

                return --count != 0;
            }, {endHeight, std::numeric_limits<uint32_t>::max(), it->second});
        }
    }

    UniValue slice(UniValue::VARR);
    for (auto it = ret.cbegin(); limit != 0 && it != ret.cend(); ++it) {
        const auto& array = it->second.get_array();
        for (size_t i = 0; limit != 0 && i < array.size(); ++i) {
            slice.push_back(array[i]);
            --limit;
        }
    }

    return slice;
}

UniValue estimateloan(const JSONRPCRequest& request) {

    RPCHelpMan{"estimateloan",
               "Returns amount of loan tokens a vault can take depending on a target collateral ratio.\n",
                {
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id",},
                    {"tokens", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Object with loans token as key and their percent split as value",
                        {

                            {"split", RPCArg::Type::NUM, RPCArg::Optional::NO, "The percent split"},
                        },
                    },
                    {"targetRatio", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Target collateral ratio. (defaults to vault's loan scheme ratio)"}
                },
                RPCResult{
                    "\"json\"                  (Array) Array of <amount@token> strings\n"
                },
               RPCExamples{
                       HelpExampleCli("estimateloan", R"(5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf '{"TSLA":0.5, "FB": 0.4, "GOOGL":0.1}' 150)") +
                       HelpExampleRpc("estimateloan", R"("5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf", {"TSLA":0.5, "FB": 0.4, "GOOGL":0.1}, 150)")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VNUM}, false);

    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");

    CCustomCSView view(*pcustomcsview);

    auto vault = view.GetVault(vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Vault <%s> not found.", vaultId.GetHex()));
    }

    auto height = view.GetLastHeight();
    auto blockTime = WITH_LOCK(cs_main, return ::ChainActive()[height]->GetBlockTime());

    auto vaultState = GetVaultState(view, vaultId, *vault, blockTime);
    if (vaultState == VaultState::InLiquidation) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Vault <%s> is in liquidation.", vaultId.GetHex()));
    }

    auto scheme = view.GetLoanScheme(vault->schemeId);
    uint32_t ratio = scheme->ratio;
    if (request.params.size() > 2) {
        ratio = (size_t) request.params[2].get_int64();
    }

    auto collaterals = view.GetVaultCollaterals(vaultId);
    if (!collaterals) {
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot estimate loan without collaterals.");
    }

    auto rate = view.GetLoanCollaterals(vaultId, *collaterals, height + 1, blockTime, false, true);
    if (!rate.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, rate.msg);
    }

    CBalances loanBalances;
    CAmount totalSplit{0};
    if (request.params.size() > 1 && request.params[1].isObject()) {
        for (const auto& tokenId : request.params[1].getKeys()) {
            CAmount split = AmountFromValue(request.params[1][tokenId]);

            auto token = view.GetToken(tokenId);
            if (!token) {
                throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Token %d does not exist!", tokenId));
            }

            auto loanToken = view.GetLoanTokenByID(token->first);
            if (!loanToken) {
                throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("(%s) is not a loan token!", tokenId));
            }

            auto priceFeed = view.GetFixedIntervalPrice(loanToken->fixedIntervalPriceId);
            if (!priceFeed.ok) {
                throw JSONRPCError(RPC_DATABASE_ERROR, priceFeed.msg);
            }

            auto price = priceFeed.val->priceRecord[0];
            if (!priceFeed.val->isLive(view.GetPriceDeviation())) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("No live fixed price for %s", tokenId));
            }

            auto availableValue = MultiplyAmounts(rate.val->totalCollaterals, split);
            auto loanAmount = DivideAmounts(availableValue, price);
            auto amountRatio = MultiplyAmounts(DivideAmounts(loanAmount, ratio), 100);

            loanBalances.Add({token->first, amountRatio});
            totalSplit += split;
        }
        if (totalSplit != COIN)
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("total split between loan tokens = %s vs expected %s", GetDecimaleString(totalSplit), GetDecimaleString(COIN)));
    }
    return AmountsToJSON(loanBalances.balances);
}

UniValue estimatecollateral(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"estimatecollateral",
               "Returns amount of collateral tokens needed to take an amount of loan tokens for a target collateral ratio.\n",
                {
                    {"loanAmounts", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Amount as json string, or array. Example: '[ \"amount@token\" ]'"
                    },
                    {"targetRatio", RPCArg::Type::NUM, RPCArg::Optional::NO, "Target collateral ratio."},
                    {"tokens", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "Object with collateral token as key and their percent split as value. (defaults to { DFI: 1 }",
                        {
                            {"split", RPCArg::Type::NUM, RPCArg::Optional::NO, "The percent split"},
                        },
                    },
                },
                RPCResult{
                    "\"json\"                  (Array) Array of <amount@token> strings\n"
                },
               RPCExamples{
                       HelpExampleCli("estimatecollateral", R"(23.55311144@MSFT 150 '{"DFI": 0.8, "BTC":0.2}')") +
                       HelpExampleRpc("estimatecollateral", R"("23.55311144@MSFT" 150 {"DFI": 0.8, "BTC":0.2})")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValueType(), UniValue::VNUM, UniValue::VOBJ}, false);

    const CBalances loanBalances = DecodeAmounts(pwallet->chain(), request.params[0], "");
    auto ratio = request.params[1].get_int();

    std::map<std::string, UniValue> collateralSplits;
    if (request.params.size() > 2) {
        request.params[2].getObjMap(collateralSplits);
    } else {
        collateralSplits["DFI"] = 1;
    }

    CCustomCSView view(*pcustomcsview);

    CAmount totalLoanValue{0};
    for (const auto& loan : loanBalances.balances) {
        auto loanToken = view.GetLoanTokenByID(loan.first);
        if (!loanToken) throw JSONRPCError(RPC_INVALID_PARAMETER, "Token with id (" + loan.first.ToString() + ") is not a loan token!");

        auto amountInCurrency = view.GetAmountInCurrency(loan.second, loanToken->fixedIntervalPriceId);
        if (!amountInCurrency) {
            throw JSONRPCError(RPC_DATABASE_ERROR, amountInCurrency.msg);
        }
        totalLoanValue += *amountInCurrency.val;
    }

    uint32_t height = ::ChainActive().Height();
    CBalances collateralBalances;
    CAmount totalSplit{0};
    for (const auto& collateralSplit : collateralSplits) {
        CAmount split = AmountFromValue(collateralSplit.second);

        auto token = view.GetToken(collateralSplit.first);
        if (!token) {
            throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Token %s does not exist!", collateralSplit.first));
        }

        auto collateralToken = view.HasLoanCollateralToken({token->first, height});
        if (!collateralToken || !collateralToken->factor) {
            throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("(%s) is not a valid collateral!", collateralSplit.first));
        }

        auto priceFeed = view.GetFixedIntervalPrice(collateralToken->fixedIntervalPriceId);
        if (!priceFeed.ok) {
            throw JSONRPCError(RPC_DATABASE_ERROR, priceFeed.msg);
        }

        auto price = priceFeed.val->priceRecord[0];
        if (!priceFeed.val->isLive(view.GetPriceDeviation())) {
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
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("total split between collateral tokens = %s vs expected %s", GetDecimaleString(totalSplit), GetDecimaleString(COIN)));
    }

    return AmountsToJSON(collateralBalances.balances);
}

UniValue estimatevault(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"estimatevault",
               "Returns estimated vault for given collateral and loan amounts.\n",
                {
                    {"collateralAmounts", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Collateral amounts as json string, or array. Example: '[ \"amount@token\" ]'"
                    },
                    {"loanAmounts", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Loan amounts as json string, or array. Example: '[ \"amount@token\" ]'"
                    },
                },
                RPCResult{
                       "{\n"
                       "  \"collateralValue\" : n.nnnnnnnn,        (amount) The total collateral value in USD\n"
                       "  \"loanValue\" : n.nnnnnnnn,              (amount) The total loan value in USD\n"
                       "  \"informativeRatio\" : n.nnnnnnnn,       (amount) Informative ratio with 8 digit precision\n"
                       "  \"collateralRatio\" : n,                 (uint) Ratio as unsigned int\n"
                       "}\n"
                },
               RPCExamples{
                       HelpExampleCli("estimatevault", R"('["1000.00000000@DFI"]' '["0.65999990@GOOGL"]')") +
                       HelpExampleRpc("estimatevault", R"(["1000.00000000@DFI"], ["0.65999990@GOOGL"])")
               },
    }.Check(request);

    CBalances collateralBalances = DecodeAmounts(pwallet->chain(), request.params[0], "");
    CBalances loanBalances = DecodeAmounts(pwallet->chain(), request.params[1], "");

    CCustomCSView view(*pcustomcsview);
    auto height = (uint32_t) view.GetLastHeight();

    CCollateralLoans result{};

    for (const auto& collateral : collateralBalances.balances) {
        auto collateralToken = view.HasLoanCollateralToken({collateral.first, height});
        if (!collateralToken || !collateralToken->factor) {
            throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Token with id (%s) is not a valid collateral!", collateral.first.ToString()));
        }

        auto amountInCurrency = view.GetAmountInCurrency(collateral.second, collateralToken->fixedIntervalPriceId);
        if (!amountInCurrency) {
            throw JSONRPCError(RPC_DATABASE_ERROR, amountInCurrency.msg);
        }
        result.totalCollaterals += MultiplyAmounts(collateralToken->factor, *amountInCurrency.val);;
    }

    for (const auto& loan : loanBalances.balances) {
        auto loanToken = view.GetLoanTokenByID(loan.first);
        if (!loanToken) throw JSONRPCError(RPC_INVALID_PARAMETER, "Token with id (" + loan.first.ToString() + ") is not a loan token!");

        auto amountInCurrency = view.GetAmountInCurrency(loan.second, loanToken->fixedIntervalPriceId);
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
    return ret;
}

static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"vault",        "createvault",               &createvault,           {"ownerAddress", "schemeId", "inputs"}},
    {"vault",        "closevault",                &closevault,            {"id", "returnAddress", "inputs"}},
    {"vault",        "listvaults",                &listvaults,            {"options", "pagination"}},
    {"vault",        "getvault",                  &getvault,              {"id"}},
    {"vault",        "listvaulthistory",          &listvaulthistory,      {"id", "options"}},
    {"vault",        "updatevault",               &updatevault,           {"id", "parameters", "inputs"}},
    {"vault",        "deposittovault",            &deposittovault,        {"id", "from", "amount", "inputs"}},
    {"vault",        "withdrawfromvault",         &withdrawfromvault,     {"id", "to", "amount", "inputs"}},
    {"vault",        "placeauctionbid",           &placeauctionbid,       {"id", "index", "from", "amount", "inputs"}},
    {"vault",        "listauctions",              &listauctions,          {"pagination"}},
    {"vault",        "listauctionhistory",        &listauctionhistory,    {"owner", "pagination"}},
    {"vault",        "estimateloan",              &estimateloan,          {"vaultId", "tokens", "targetRatio"}},
    {"vault",        "estimatecollateral",        &estimatecollateral,    {"loanAmounts", "targetRatio", "tokens"}},
    {"vault",        "estimatevault",             &estimatevault,         {"collateralAmounts", "loanAmounts"}},
};

void RegisterVaultRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

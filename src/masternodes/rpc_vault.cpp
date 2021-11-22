#include <masternodes/accountshistory.h>
#include <masternodes/auctionhistory.h>
#include <masternodes/mn_rpc.h>

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

    bool WillLiquidateNext(const CVaultId& vaultId, const CVaultData& vault) {
        auto height = ::ChainActive().Height();
        auto blockTime = ::ChainActive()[height]->GetBlockTime();

        auto collaterals = pcustomcsview->GetVaultCollaterals(vaultId);
        if (!collaterals)
            return false;

        bool useNextPrice = true, requireLivePrice = false;
        auto vaultRate = pcustomcsview->GetLoanCollaterals(vaultId, *collaterals, height, blockTime, useNextPrice, requireLivePrice);
        if (!vaultRate)
            return false;

        auto loanScheme = pcustomcsview->GetLoanScheme(vault.schemeId);
        return (vaultRate.val->ratio() < loanScheme->ratio);
    }

    VaultState GetVaultState(const CVaultId& vaultId, const CVaultData& vault) {
        auto height = ::ChainActive().Height();
        auto inLiquidation = vault.isUnderLiquidation;
        auto priceIsValid = IsVaultPriceValid(*pcustomcsview, vaultId, height);
        auto willLiquidateNext = WillLiquidateNext(vaultId, vault);

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

    UniValue BatchToJSON(const CVaultId& vaultId, uint32_t batchCount) {
        UniValue batchArray{UniValue::VARR};
        for (uint32_t i = 0; i < batchCount; i++) {
            UniValue batchObj{UniValue::VOBJ};
            auto batch = pcustomcsview->GetAuctionBatch(vaultId, i);
            batchObj.pushKV("index", int(i));
            batchObj.pushKV("collaterals", AmountsToJSON(batch->collaterals.balances));
            batchObj.pushKV("loan", tokenAmountString(batch->loanAmount));
            if (auto bid = pcustomcsview->GetAuctionBid(vaultId, i)) {
                UniValue bidObj{UniValue::VOBJ};
                bidObj.pushKV("owner", ScriptToString(bid->first));
                bidObj.pushKV("amount", tokenAmountString(bid->second));
                batchObj.pushKV("highestBid", bidObj);
            }
            batchArray.push_back(batchObj);
        }
        return batchArray;
    }

    UniValue AuctionToJSON(const CVaultId& vaultId, const CAuctionData& data) {
        UniValue auctionObj{UniValue::VOBJ};
        auto vault = pcustomcsview->GetVault(vaultId);
        auctionObj.pushKV("vaultId", vaultId.GetHex());
        auctionObj.pushKV("loanSchemeId", vault->schemeId);
        auctionObj.pushKV("ownerAddress", ScriptToString(vault->ownerAddress));
        auctionObj.pushKV("state", VaultStateToString(VaultState::InLiquidation));
        auctionObj.pushKV("liquidationHeight", int64_t(data.liquidationHeight));
        auctionObj.pushKV("batchCount", int64_t(data.batchCount));
        auctionObj.pushKV("liquidationPenalty", ValueFromAmount(data.liquidationPenalty * 100));
        auctionObj.pushKV("batches", BatchToJSON(vaultId, data.batchCount));
        return auctionObj;
    }

    UniValue VaultToJSON(const CVaultId& vaultId, const CVaultData& vault) {
        UniValue result{UniValue::VOBJ};
        auto vaultState = GetVaultState(vaultId, vault);
        auto height = ::ChainActive().Height();

        if (vaultState == VaultState::InLiquidation) {
            if (auto data = pcustomcsview->GetAuction(vaultId, height)) {
                result.pushKVs(AuctionToJSON(vaultId, *data));
            } else {
                LogPrintf("Warning: Vault in liquidation, but no auctions found\n");
            }
            return result;
        }

        UniValue ratioValue{0}, collValue{0}, loanValue{0}, interestValue{0}, collateralRatio{0};

        auto collaterals = pcustomcsview->GetVaultCollaterals(vaultId);
        if (!collaterals)
            collaterals = CBalances{};

        auto blockTime = ::ChainActive().Tip()->GetBlockTime();
        bool useNextPrice = false, requireLivePrice = vaultState != VaultState::Frozen;
        LogPrint(BCLog::LOAN,"%s():\n", __func__);
        auto rate = pcustomcsview->GetLoanCollaterals(vaultId, *collaterals, height + 1, blockTime, useNextPrice, requireLivePrice);

        if (rate) {
            collValue = ValueFromUint(rate.val->totalCollaterals);
            loanValue = ValueFromUint(rate.val->totalLoans);
            ratioValue = ValueFromAmount(rate.val->precisionRatio());
            collateralRatio = int(rate.val->ratio());
        }

        UniValue loanBalances{UniValue::VARR};
        UniValue interestAmounts{UniValue::VARR};

        if (auto loanTokens = pcustomcsview->GetLoanTokens(vaultId)) {
            TAmounts totalBalances{};
            TAmounts interestBalances{};
            CAmount totalInterests{0};

            for (const auto& loan : loanTokens->balances) {
                auto token = pcustomcsview->GetLoanTokenByID(loan.first);
                if (!token) continue;
                auto rate = pcustomcsview->GetInterestRate(vaultId, loan.first);
                if (!rate) continue;
                LogPrint(BCLog::LOAN,"%s()->%s->", __func__, token->symbol); /* Continued */
                auto totalInterest = TotalInterest(*rate, height + 1);
                auto value = loan.second + totalInterest;
                if (auto priceFeed = pcustomcsview->GetFixedIntervalPrice(token->fixedIntervalPriceId)) {
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

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

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
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
        // decode vaultId
        auto vault = pcustomcsview->GetVault(msg.vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> does not found", msg.vaultId.GetHex()));

        if (vault->isUnderLiquidation)
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");

        ownerAddress = vault->ownerAddress;
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

    RPCHelpMan{"listvaults",
               "List all available vaults.\n",
               {
                    {
                       "options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {
                                "ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                "Address of the vault owner."
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

    LOCK(cs_main);

    pcustomcsview->ForEachVault([&](const CVaultId& vaultId, const CVaultData& data) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
        if (!ownerAddress.empty() && ownerAddress != data.ownerAddress) {
            return false;
        }
        auto vaultState = GetVaultState(vaultId, data);

        if ((loanSchemeId.empty() || loanSchemeId == data.schemeId)
        && (state == VaultState::Unknown || state == vaultState)) {
            UniValue vaultObj{UniValue::VOBJ};
            if(!verbose){
                vaultObj.pushKV("vaultId", vaultId.GetHex());
                vaultObj.pushKV("ownerAddress", ScriptToString(data.ownerAddress));
                vaultObj.pushKV("loanSchemeId", data.schemeId);
                vaultObj.pushKV("state", VaultStateToString(vaultState));
            } else {
                vaultObj = VaultToJSON(vaultId, data);
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

    LOCK(cs_main);

    auto vault = pcustomcsview->GetVault(vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Vault <%s> not found", vaultId.GetHex()));
    }

    return VaultToJSON(vaultId, *vault);
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
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
        // decode vaultId
        auto storedVault = pcustomcsview->GetVault(vaultId);
        if (!storedVault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> does not found", vaultId.GetHex()));

        if(storedVault->isUnderLiquidation)
            throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");

        vault = *storedVault;
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

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

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
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
        // decode vaultId
        auto vault = pcustomcsview->GetVault(vaultId);
        if (!vault)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Vault <%s> does not found", vaultId.GetHex()));

        ownerAddress = vault->ownerAddress;
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
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to get tokens"},
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
    auto from = DecodeScript(request.params[2].get_str());
    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[3].get_str(), "amount");

    CAuctionBidMessage msg{vaultId, index, from, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AuctionBid)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

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

    LOCK(cs_main);
    pcustomcsview->ForEachVaultAuction([&](const CVaultId& vaultId, const CAuctionData& data) {
        if (!including_start)
        {
            including_start = true;
            return (true);
        }
        valueArr.push_back(AuctionToJSON(vaultId, data));
        return --limit != 0;
    }, height, vaultId);

    return valueArr;
}

UniValue auctionhistoryToJSON(AuctionHistoryKey const & key, AuctionHistoryValue const & value) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("winner", ScriptToString(key.owner));
    obj.pushKV("blockHeight", (uint64_t) key.blockHeight);
    if (auto block = ::ChainActive()[key.blockHeight]) {
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
                    {"owner", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned accounts or \"all\" to list whole DB (default = \"mine\")."},
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

    bool isMine = false;
    if (account == "mine") {
        isMine = true;
    } else if (account != "all") {
        start.owner = DecodeScript(account);
    }

    LOCK(cs_main);
    UniValue ret(UniValue::VARR);

    paccountHistoryDB->ForEachAuctionHistory([&](AuctionHistoryKey const & key, CLazySerialize<AuctionHistoryValue> valueLazy) -> bool {
        if (!start.owner.empty() && start.owner != key.owner) {
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

    LOCK(cs_main);
    auto height = ::ChainActive().Height();

    auto vault = pcustomcsview->GetVault(vaultId);
    if (!vault) {
        throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Vault <%s> not found.", vaultId.GetHex()));
    }

    auto vaultState = GetVaultState(vaultId, *vault);
    if (vaultState == VaultState::InLiquidation) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Vault <%s> is in liquidation.", vaultId.GetHex()));
    }

    auto scheme = pcustomcsview->GetLoanScheme(vault->schemeId);
    uint32_t ratio = scheme->ratio;
    if (request.params.size() > 2) {
        ratio = (size_t) request.params[2].get_int64();
    }

    auto collaterals = pcustomcsview->GetVaultCollaterals(vaultId);
    if (!collaterals) {
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot estimate loan without collaterals.");
    }

    auto blockTime = ::ChainActive().Tip()->GetBlockTime();
    auto rate = pcustomcsview->GetLoanCollaterals(vaultId, *collaterals, height + 1, blockTime, false, true);
    if (!rate.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, rate.msg);
    }

    auto collValue = ValueFromUint(rate.val->totalCollaterals);
    UniValue ret(UniValue::VARR);
    CBalances loanBalances;
    CAmount totalSplit{0};
    if (request.params.size() > 1 && request.params[1].isObject()) {
        for (const auto& tokenId : request.params[1].getKeys()) {
            CAmount split = AmountFromValue(request.params[1][tokenId]);

            auto token = pcustomcsview->GetToken(tokenId);
            if (!token) {
                throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("Token %d does not exist!", tokenId));
            }

            auto loanToken = pcustomcsview->GetLoanTokenByID(token->first);
            if (!loanToken) {
                throw JSONRPCError(RPC_DATABASE_ERROR, strprintf("(%s) is not a loan token!", tokenId));
            }

            auto priceFeed = pcustomcsview->GetFixedIntervalPrice(loanToken->fixedIntervalPriceId);
            if (!priceFeed.ok) {
                throw JSONRPCError(RPC_DATABASE_ERROR, priceFeed.msg);
            }

            auto price = priceFeed.val->priceRecord[0];
            if (!priceFeed.val->isLive(pcustomcsview->GetPriceDeviation())) {
                throw JSONRPCError(RPC_MISC_ERROR, strprintf("No live fixed price for %s", tokenId));
            }

            auto availableValue = MultiplyAmounts(rate.val->totalCollaterals, split);
            auto loanAmount = DivideAmounts(availableValue, price);
            auto amountRatio = MultiplyAmounts(DivideAmounts(loanAmount, ratio), 100);

            loanBalances.Add({token->first, amountRatio});
            totalSplit += split;
        }
        if (totalSplit != COIN)
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("total split between loan tokens = %d vs expected %d", totalSplit, COIN));
    }
    return AmountsToJSON(loanBalances.balances);
}

static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"vault",        "createvault",               &createvault,           {"ownerAddress", "schemeId", "inputs"}},
    {"vault",        "closevault",                &closevault,            {"id", "returnAddress", "inputs"}},
    {"vault",        "listvaults",                &listvaults,            {"options", "pagination"}},
    {"vault",        "getvault",                  &getvault,              {"id"}},
    {"vault",        "updatevault",               &updatevault,           {"id", "parameters", "inputs"}},
    {"vault",        "deposittovault",            &deposittovault,        {"id", "from", "amount", "inputs"}},
    {"vault",        "withdrawfromvault",         &withdrawfromvault,     {"id", "to", "amount", "inputs"}},
    {"vault",        "placeauctionbid",           &placeauctionbid,       {"id", "index", "from", "amount", "inputs"}},
    {"vault",        "listauctions",              &listauctions,          {"pagination"}},
    {"vault",        "listauctionhistory",        &listauctionhistory,    {"owner", "pagination"}},
    {"vault",        "estimateloan",              &estimateloan,          {"vaultId", "tokens", "targetRatio"}},
};

void RegisterVaultRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

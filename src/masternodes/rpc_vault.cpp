#include <masternodes/mn_rpc.h>

extern UniValue AmountsToJSON(TAmounts const & diffs);
extern std::string tokenAmountString(CTokenAmount const& amount);

namespace {
    UniValue VaultToJSON(const CVaultMessage& vault, const CVaultId& id) {
        auto height = ::ChainActive().Height();
        auto collaterals = pcustomcsview->GetVaultCollaterals(id);
        UniValue collValue{UniValue::VSTR};
        UniValue loanValue{UniValue::VSTR};

        if(collaterals){
            auto rate = pcustomcsview->CalculateCollateralizationRatio(id, *collaterals, height);
            CAmount totalCollateral = 0, totalLoan = 0;
            if (rate)
            {
                totalCollateral += rate->totalCollaterals();
                totalLoan += rate->totalLoans();
            }
            collValue=ValueFromAmount(totalCollateral);
            loanValue=ValueFromAmount(totalLoan);
        }
        UniValue collateralBalances{UniValue::VARR};
        UniValue loanBalances{UniValue::VARR};

        if(auto collateral = pcustomcsview->GetVaultCollaterals(id))
            collateralBalances = AmountsToJSON(collateral->balances);

        if(auto loan = pcustomcsview->GetLoanTokens(id))
            loanBalances = AmountsToJSON(loan->balances);

        UniValue result{UniValue::VOBJ};
        result.pushKV("loanSchemeId", vault.schemeId);
        result.pushKV("ownerAddress", ScriptToString(vault.ownerAddress));
        result.pushKV("isUnderLiquidation", vault.isUnderLiquidation);
        result.pushKV("collateralAmounts", collateralBalances);
        result.pushKV("loanAmount", loanBalances);
        result.pushKV("collateralValue",collValue);
        result.pushKV("loanValue",loanValue);
        return result;
    }
}

UniValue createvault(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    RPCHelpMan{"createvault",
                "Creates a vault transaction.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address or \"\" to generate a new address"},
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
                   HelpExampleCli("createvault", "") +
                   HelpExampleCli("createvault", "" "LOAN0001") +
                   HelpExampleCli("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv LOAN0001") +
                   HelpExampleCli("createvault", "") +
                   HelpExampleRpc("createvault", "\"\", LOAN0001")+
                   HelpExampleRpc("createvault", "2MzfSNCkjgCbNLen14CYrVtwGomfDA5AGYv, LOAN0001")
                },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot createvault while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    CVaultMessage vault;
    std::string ownerAddress{};
    if(request.params.size() > 0){
        ownerAddress = request.params[0].getValStr();
        if(ownerAddress.empty()){
            // generate new address
            LOCK(pwallet->cs_wallet);

            if (!pwallet->CanGetAddresses()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
            }
            CTxDestination dest;
            std::string error;
            if (!pwallet->GetNewDestination(OutputType::LEGACY, "*", dest, error)) {
                throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, error);
            }
            ownerAddress = EncodeDestination(dest);
        } else {
            // check address validity
            CTxDestination ownerDest = DecodeDestination(ownerAddress);
            if (!IsValidDestination(ownerDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid owner address");
            }
        }
    }
    vault.ownerAddress = DecodeScript(ownerAddress);
    vault.schemeId = pcustomcsview->GetDefaultLoanScheme().get();

    if(request.params.size() > 1){
        if(!request.params[1].isNull()){
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
    std::set<CScript> auths{vault.ownerAddress};
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
    {
        LOCK(cs_main);
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, vault});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CVaultMessage{}, coinview);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listvaults(const JSONRPCRequest& request) {

    RPCHelpMan{"listvaults",
               "List all available vaults\n",
               {},
               RPCResult{
                    "[                         (json array of objects)\n"
                        "{...}                 (object) Json object with vault information\n"
                    "]\n"
               },
               RPCExamples{
                       HelpExampleCli("listvaults",  "") +
                       HelpExampleRpc("listvaults", "")
               },
    }.Check(request);

    UniValue valueArr{UniValue::VOBJ};

    LOCK(cs_main);
    pcustomcsview->ForEachVault([&](const CVaultId& id, const CVaultMessage& data) {
        UniValue vaultObj{UniValue::VOBJ};
        vaultObj.pushKV("ownerAddress", ScriptToString(data.ownerAddress));
        vaultObj.pushKV("loanSchemeId", data.schemeId);
        vaultObj.pushKV("isUnderLiquidation", data.isUnderLiquidation);
        valueArr.pushKV(id.GetHex(), vaultObj);
        return true;
    });

    return valueArr;
}

UniValue getvault(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"getvault",
               "Returns information about vault\n",
                {
                    {"vaultId", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id",},
                },
                RPCResult{
                    "\"json\"                  (string) vault data in json form\n"
                },
               RPCExamples{
                       HelpExampleCli("getvault",  "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf") +
                       HelpExampleRpc("getvault", "5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR}, false);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview); // don't write into actual DB

    auto vaultRes = mnview.GetVault(vaultId);
    if (!vaultRes.ok) {
        throw JSONRPCError(RPC_DATABASE_ERROR, vaultRes.msg);
    }

    return VaultToJSON(*vaultRes.val, vaultId);
}

UniValue updatevault(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"updatevault",
               "\nCreates (and submits to local node and network) a `update vault transaction`, \n"
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
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 '{"ownerAddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanSchemeId": "LOANSCHEME001"}')")
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
    LockedCoinsScopedGuard lcGuard(pwallet);

    if (request.params[0].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 must be non-null");

    // decode vaultid
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    auto vaultRes = pcustomcsview->GetVault(vaultId);
    if (!vaultRes.ok)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, vaultRes.msg);

    CVaultMessage dbVault{vaultRes.val.get()};
    if(dbVault.isUnderLiquidation)
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");

    if (request.params[1].isNull()){
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 2 must be non-null and expected as object at least with one of"
                           "{\"ownerAddress\",\"loanSchemeId\"}");
    }
    UniValue params = request.params[1].get_obj();
    if(params["ownerAddress"].isNull() && params["loanSchemeId"].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "At least ownerAddress OR loanSchemeId must be set");

    if (!params["ownerAddress"].isNull()){
        auto ownerAddress = params["ownerAddress"].getValStr();
        // check address validity
        CTxDestination ownerDest = DecodeDestination(ownerAddress);
            if (!IsValidDestination(ownerDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid owner address");
            }
        dbVault.ownerAddress = DecodeScript(ownerAddress);
    }
    if(!params["loanSchemeId"].isNull()){
        auto loanschemeid = params["loanSchemeId"].getValStr();
        dbVault.schemeId = loanschemeid;
    }

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CUpdateVaultMessage msg{
        vaultId,
        dbVault.ownerAddress,
        dbVault.schemeId
    };

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
    std::set<CScript> auths{vaultRes.val->ownerAddress};
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
    {
        LOCK(cs_main);
        CCustomCSView mnview(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CUpdateVaultMessage{}, coins);
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue deposittovault(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"deposittovault",
               "Deposit collateral token amount to vault\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"vaultId", RPCArg::Type::STR, RPCArg::Optional::NO, "Vault id"},
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
                    "\"txid\"                  (string) The transaction id.\n"
               },
               RPCExamples{
                       HelpExampleCli("deposittovault",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 1@DFI") +
                       HelpExampleRpc("deposittovault",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i, mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF, 1@DFI")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VSTR, UniValue::VSTR, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot update vault while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    if (request.params[0].isNull() || request.params[1].isNull() || request.params[2].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments must be non-null");

    // decode vaultid
    CVaultId vaultId = ParseHashV(request.params[0], "vaultId");
    auto vaultRes = pcustomcsview->GetVault(vaultId);
    if (!vaultRes.ok)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, vaultRes.msg);
    std::string from = request.params[1].get_str();
    CTokenAmount amount = DecodeAmount(pwallet->chain(),request.params[2].get_str(), from);

    CDepositToVaultMessage msg{vaultId, DecodeScript(from), amount};
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
    std::set<CScript> auths{DecodeScript(from)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(DecodeScript(from), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CDepositToVaultMessage{}, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue auctionbid(const JSONRPCRequest& request) {
    CWallet *const pwallet = GetWallet(request);

    RPCHelpMan{"auctionbid",
               "Bid to vault in auction\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"vaultid", RPCArg::Type::STR, RPCArg::Optional::NO, "Vault id"},
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
                    "\"txid\"                  (string) The transaction id.\n"
               },
               RPCExamples{
                       HelpExampleCli("auctionbid",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i 0 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 100@TSLA") +
                       HelpExampleRpc("deposittovault",
                        "84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2i 0 mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF 1@DTSLA")
               },
    }.Check(request);

    RPCTypeCheck(request.params,
                 {UniValue::VSTR, UniValue::VNUM, UniValue::VSTR, UniValue::VSTR, UniValue::VARR},
                 false);

    if (pwallet->chain().isInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot make auction bid while still in Initial Block Download");

    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode vaultid
    CVaultId vaultId = ParseHashV(request.params[0], "vaultid");
    uint32_t index = request.params[1].get_int();
    auto from = DecodeScript(request.params[2].get_str());
    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[3].get_str(), "amount");

    CAuctionBidMessage msg{vaultId, index, from, amount};
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::DepositToVault)
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
    {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, targetHeight);
        auto metadata = ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg});
        execTestTx(CTransaction(rawTx), targetHeight, metadata, CAuctionBidMessage{}, coins);
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listauctions(const JSONRPCRequest& request) {

    RPCHelpMan{"listauction",
               "List all available auctions\n",
               {},
               RPCResult{
                    "[                         (json array of objects)\n"
                        "{...}                 (object) Json object with auction information\n"
                    "]\n"
               },
               RPCExamples{
                       HelpExampleCli("listauctions",  "") +
                       HelpExampleRpc("listauctions", "")
               },
    }.Check(request);

    UniValue valueArr{UniValue::VARR};

    LOCK(cs_main);
    pcustomcsview->ForEachVaultAuction([&](const CVaultId& vaultId, uint32_t height, const CAuctionData& data) {
        UniValue vaultObj{UniValue::VOBJ};
        vaultObj.pushKV("vaultId", vaultId.GetHex());
        vaultObj.pushKV("batchCount", int(data.batchCount));
        vaultObj.pushKV("liquidationPenalty", data.liquidationPenalty);
        UniValue batchArray{UniValue::VARR};
        for (uint32_t i = 0; i < data.batchCount; i++) {
            UniValue batchObj{UniValue::VOBJ};
            auto batch = pcustomcsview->GetAuctionBatch(vaultId, i);
            batchObj.pushKV("collaterals", AmountsToJSON(batch->collaterals.balances));
            batchObj.pushKV("loan", tokenAmountString(batch->loanAmount));
            batchArray.push_back(batchObj);
        }
        vaultObj.pushKV("batches", batchArray);
        valueArr.push_back(vaultObj);
        return true;
    });

    return valueArr;
}

static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"vault",        "createvault",               &createvault,           {"ownerAddress", "schemeId", "inputs"}},
    {"vault",        "listvaults",                &listvaults,            {}},
    {"vault",        "getvault",                  &getvault,              {"id"}},
    {"vault",        "updatevault",               &updatevault,           {"id", "parameters", "inputs"}},
    {"vault",        "deposittovault",            &deposittovault,        {"id", "from", "amount", "inputs"}},
    {"vault",        "auctionbid",                &auctionbid,            {"id", "index", "from", "amount", "inputs"}},
    {"vault",        "listauctions",              &listauctions,          {}},
};

void RegisterVaultRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

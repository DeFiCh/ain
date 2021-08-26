#include <masternodes/mn_rpc.h>

namespace {
    UniValue AmountsToArr(const TAmounts& balances){
        UniValue BalancesArr{UniValue::VARR};
            for (const auto balance : balances){
                CTokenAmount tokenAmount{balance.first, balance.second};
                const auto token = pcustomcsview->GetToken(tokenAmount.nTokenId);
                const auto valueString = ValueFromAmount(tokenAmount.nValue).getValStr();
                BalancesArr.push_back(valueString + "@" + token->CreateSymbolKey(tokenAmount.nTokenId));
            }
        return BalancesArr;
    }
    UniValue VaultToJSON(const CVaultMessage& vault, const CVaultId& id) {
        UniValue collateralBalances{UniValue::VARR};
        UniValue loanBalances{UniValue::VARR};
        auto collateral = pcustomcsview->GetVaultCollaterals(id);
        if(collateral)
            collateralBalances = AmountsToArr(collateral.get().balances);

        auto loan = pcustomcsview->GetLoanTokens(id);
        if(loan)
            loanBalances = AmountsToArr(loan.get().balances);

        UniValue result{UniValue::VOBJ};
        result.pushKV("loanschemeid", vault.schemeId);
        result.pushKV("owneraddress", ScriptToString(vault.ownerAddress));
        result.pushKV("isunderliquidation", vault.isUnderLiquidation);
        result.pushKV("collateralamounts", collateralBalances);
        result.pushKV("loanamount", loanBalances);
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
                    {"owneraddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address or \"\" to generate a new address"},
                    {"loanschemeid", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
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
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid owneraddress address");
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

    pcustomcsview->ForEachVault([&](const CVaultId& id, const CVaultMessage& data) {
        UniValue vaultObj{UniValue::VOBJ};
        vaultObj.pushKV("owneraddress", ScriptToString(data.ownerAddress));
        vaultObj.pushKV("loanschemeid", data.schemeId);
        vaultObj.pushKV("isunderliquidation", data.isUnderLiquidation);
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
                    {"vaultid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault hex id",},
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

    CVaultId vaultId = ParseHashV(request.params[0], "vaultid");

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
                        {"vaultid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "vault id"},
                        {"parameters", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                            {
                                {"owneraddress", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Vault's owner address"},
                                {"loanschemeid", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Vault's loan scheme id"},
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
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 '{"owneraddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanschemeid": "LOANSCHEME001"}')")
                       + HelpExampleRpc(
                               "updatevault",
                               R"(84b22eee1964768304e624c416f29a91d78a01dc5e8e12db26bdac0670c67bb2 '{"owneraddress": "mwSDMvn1Hoc8DsoB7AkLv7nxdrf5Ja4jsF", "loanschemeid": "LOANSCHEME001"}')")
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
    CVaultId vaultId = ParseHashV(request.params[0], "vaultid");
    auto vaultRes = pcustomcsview->GetVault(vaultId);
    if (!vaultRes.ok)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, vaultRes.msg);

    CVaultMessage dbVault{vaultRes.val.get()};
    if(dbVault.isUnderLiquidation)
        throw JSONRPCError(RPC_TRANSACTION_REJECTED, "Vault is under liquidation.");

    if (request.params[1].isNull()){
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 2 must be non-null and expected as object at least with one of"
                           "{\"owneraddress\",\"loanschemeid\"}");
    }
    UniValue params = request.params[1].get_obj();
    if(params["owneraddress"].isNull() && params["loanschemeid"].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "At least owneraddress OR loanschemeid must be set");

    if (!params["owneraddress"].isNull()){
        auto owneraddress = params["owneraddress"].getValStr();
        // check address validity
        CTxDestination ownerDest = DecodeDestination(owneraddress);
            if (!IsValidDestination(ownerDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid owneraddress address");
            }
        dbVault.ownerAddress = DecodeScript(owneraddress);
    }
    if(!params["loanschemeid"].isNull()){
        auto loanschemeid = params["loanschemeid"].getValStr();
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
                    {"vaultid", RPCArg::Type::STR, RPCArg::Optional::NO, "Vault id"},
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
    CVaultId vaultId = ParseHashV(request.params[0], "vaultid");
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


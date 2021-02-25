#include <masternodes/mn_rpc.h>

std::string tokenAmountString(CTokenAmount const& amount) {
    const auto token = pcustomcsview->GetToken(amount.nTokenId);
    const auto valueString = strprintf("%d.%08d", amount.nValue / COIN, amount.nValue % COIN);
    return valueString + "@" + token->symbol + (token->IsDAT() ? "" : "#" + amount.nTokenId.ToString());
}

UniValue AmountsToJSON(TAmounts const & diffs) {
    UniValue obj(UniValue::VARR);

    for (auto const & diff : diffs) {
        auto token = pcustomcsview->GetToken(diff.first);
        auto const tokenIdStr = token->CreateSymbolKey(diff.first);
        obj.push_back(ValueFromAmount(diff.second).getValStr() + "@" + tokenIdStr);
    }
    return obj;
}

UniValue accountToJSON(CScript const& owner, CTokenAmount const& amount, bool verbose, bool indexed_amounts) {
    // encode CScript into JSON
    UniValue ownerObj(UniValue::VOBJ);
    ScriptPubKeyToUniv(owner, ownerObj, true);
    if (!verbose) { // cut info
        if (ownerObj["addresses"].isArray() && !ownerObj["addresses"].get_array().empty()) {
            ownerObj = ownerObj["addresses"].get_array().getValues()[0];
        } else {
            ownerObj = {UniValue::VSTR};
            ownerObj.setStr(owner.GetHex());
        }
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("key", owner.GetHex() + "@" + amount.nTokenId.ToString());
    obj.pushKV("owner", ownerObj);

    if (indexed_amounts) {
        UniValue amountObj(UniValue::VOBJ);
        amountObj.pushKV(amount.nTokenId.ToString(), ValueFromAmount(amount.nValue));
        obj.pushKV("amount", amountObj);
    }
    else {
        obj.pushKV("amount", tokenAmountString(amount));
    }

    return obj;
}

UniValue accounthistoryToJSON(AccountHistoryKey const & key, AccountHistoryValue const & value) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("owner", ScriptToString(key.owner));
    obj.pushKV("blockHeight", (uint64_t) key.blockHeight);
    if (auto block = ::ChainActive()[key.blockHeight]) {
        obj.pushKV("blockHash", block->GetBlockHash().GetHex());
        obj.pushKV("blockTime", block->GetBlockTime());
    }
    obj.pushKV("type", ToString(CustomTxCodeToType(value.category)));
    obj.pushKV("txn", (uint64_t) key.txn);
    obj.pushKV("txid", value.txid.ToString());
    obj.pushKV("amounts", AmountsToJSON(value.diff));
    return obj;
}

UniValue rewardhistoryToJSON(RewardHistoryKey const & key, std::pair<DCT_ID, TAmounts> const & value) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("owner", ScriptToString(key.owner));
    obj.pushKV("blockHeight", (uint64_t) key.blockHeight);
    if (auto block = ::ChainActive()[key.blockHeight]) {
        obj.pushKV("blockHash", block->GetBlockHash().GetHex());
        obj.pushKV("blockTime", block->GetBlockTime());
    }
    obj.pushKV("type", RewardToString(RewardType(key.category)));
    obj.pushKV("poolID", value.first.ToString());
    obj.pushKV("amounts", AmountsToJSON(value.second));
    return obj;
}

UniValue outputEntryToJSON(COutputEntry const & entry, CBlockIndex const * index, uint256 const & txid, std::string const & type) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("owner", EncodeDestination(entry.destination));
    obj.pushKV("blockHeight", index->height);
    obj.pushKV("blockHash", index->GetBlockHash().GetHex());
    obj.pushKV("blockTime", index->GetBlockTime());
    obj.pushKV("type", type);
    obj.pushKV("txn", (uint64_t) entry.vout);
    obj.pushKV("txid", txid.ToString());
    obj.pushKV("amounts", AmountsToJSON({{DCT_ID{0}, entry.amount}}));
    return obj;
}

static void searchInWallet(CWallet const * pwallet,
                           CScript const & account,
                           isminetype filter,
                           std::function<bool(CWalletTx const *)> shouldSkipTx,
                           std::function<bool(COutputEntry const &)> onSent,
                           std::function<bool(COutputEntry const &)> onReceive) {

    CTxDestination destination;
    ExtractDestination(account, destination);

    CAmount nFee;
    std::list<COutputEntry> listSent;
    std::list<COutputEntry> listReceived;

    LOCK(pwallet->cs_wallet);

    const auto& txOrdered = pwallet->mapWallet.get<ByOrder>();

    for (const auto& tx : txOrdered) {
        auto* pwtx = &tx;

        if (pwtx->IsCoinBase()) {
            continue;
        }

        if (shouldSkipTx(pwtx)) {
            continue;
        }

        pwtx->GetAmounts(listReceived, listSent, nFee, filter);

        for (auto& sent : listSent) {
            if (!IsValidDestination(sent.destination)) {
                continue;
            }
            if (IsValidDestination(destination) && destination != sent.destination) {
                continue;
            }
            sent.amount = -sent.amount;
            if (!onSent(sent)) {
                return;
            }
        }

        for (const auto& recv : listReceived) {
            if (!IsValidDestination(recv.destination)) {
                continue;
            }
            if (IsValidDestination(destination) && destination != recv.destination) {
                continue;
            }
            if (!onReceive(recv)) {
                return;
            }
        }
    }
}

static CScript hexToScript(std::string const& str) {
    if (!IsHex(str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct hex:\n");
    }
    const auto raw = ParseHex(str);
    return CScript{raw.begin(), raw.end()};
}

static BalanceKey decodeBalanceKey(std::string const& str) {
    const auto pair = SplitAmount(str);
    DCT_ID tokenID{};
    if (!pair.second.empty()) {
        auto id = DCT_ID::FromString(pair.second);
        if (!id.ok) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct balance key:\n" + id.msg);
        }
        tokenID = *id.val;
    }
    return {hexToScript(pair.first), tokenID};
}

static CAccounts DecodeRecipientsDefaultInternal(CWallet* const pwallet, UniValue const& values) {
    UniValue recipients(UniValue::VOBJ);
    for (const auto& key : values.getKeys()) {
        recipients.pushKV(key, values[key]);
    }
    auto accounts = DecodeRecipients(pwallet->chain(), recipients);
    for (const auto& account : accounts) {
        if (IsMineCached(*pwallet, account.first) != ISMINE_SPENDABLE && account.second.balances.find(DCT_ID{0}) != account.second.balances.end()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("The address (%s) is not your own address", ScriptToString(account.first)));
        }
    }
    return accounts;
}

static AccountSelectionMode ParseAccountSelectionParam(const std::string selectionParam) {
    if (selectionParam == "forward") {
        return SelectionForward;
    }
    else if (selectionParam == "crumbs") {
        return SelectionCrumbs;
    }
    else if (selectionParam == "pie") {
        return SelectionPie;
    }
    else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalide accounts selection mode.");
    }
}

UniValue listaccounts(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"listaccounts",
               "\nReturns information about all accounts on chain.\n",
               {
                       {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                                {"start", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last ID from previous request."},
                                {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                                {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of orders to return, 100 by default"},
                        },
                       },
                       {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                   "Flag for verbose list (default = true), otherwise limited objects are listed"},
                       {"indexed_amounts", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Format of amounts output (default = false): (true: {tokenid:amount}, false: \"amount@tokenid\")"},
                       {"is_mine_only", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Get balances about all accounts belonging to the wallet"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with accounts information\n"
               },
               RPCExamples{
                       HelpExampleCli("listaccounts", "")
                       + HelpExampleRpc("listaccounts", "'{}' false")
                       + HelpExampleRpc("listaccounts", "'{\"start\":\"a914b12ecde1759f792e0228e4fa6d262902687ca7eb87@0\","
                                                      "\"limit\":1000"
                                                      "}'")
               },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    BalanceKey start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start = decodeBalanceKey(paginationObj["start"].get_str());
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start.tokenID.v++;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }
    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    bool indexed_amounts = false;
    if (request.params.size() > 2) {
        indexed_amounts = request.params[2].get_bool();
    }
    bool isMineOnly = false;
    if (request.params.size() > 3) {
        isMineOnly = request.params[3].get_bool();
    }


    UniValue ret(UniValue::VARR);

    LOCK(cs_main);
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (isMineOnly) {
            if (IsMineCached(*pwallet, owner) == ISMINE_SPENDABLE) {
                ret.push_back(accountToJSON(owner, balance, verbose, indexed_amounts));
                limit--;
            }
        } else {
            ret.push_back(accountToJSON(owner, balance, verbose, indexed_amounts));
            limit--;
        }

        return limit != 0;
    }, start);

    return ret;
}

UniValue getaccount(const JSONRPCRequest& request) {
    RPCHelpMan{"getaccount",
               "\nReturns information about account.\n",
               {
                    {"owner", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Owner address in base58/bech32/hex encoding"},
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last tokenID from previous request."},
                            {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of orders to return, 100 by default"},
                        },
                    },
                    {"indexed_amounts", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Format of amounts output (default = false): (true: obj = {tokenid:amount,...}, false: array = [\"amount@tokenid\"...])"},
                },
                RPCResult{
                       "{...}     (array) Json object with order information\n"
                },
                RPCExamples{
                       HelpExampleCli("getaccount", "owner_address")
                },
    }.Check(request);

    // decode owner
    const auto reqOwner = DecodeScript(request.params[0].get_str());

    // parse pagination
    size_t limit = 100;
    DCT_ID start = {};
    bool including_start = true;
    {
        if (request.params.size() > 1) {
            UniValue paginationObj = request.params[1].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t) paginationObj["start"].get_int64();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start.v++;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }
    bool indexed_amounts = false;
    if (request.params.size() > 2) {
        indexed_amounts = request.params[2].get_bool();
    }

    UniValue ret(UniValue::VARR);
    if (indexed_amounts) {
        ret.setObject();
    }

    LOCK(cs_main);
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (owner != reqOwner) {
            return false;
        }

        if (indexed_amounts)
            ret.pushKV(balance.nTokenId.ToString(), ValueFromAmount(balance.nValue));
        else
            ret.push_back(tokenAmountString(balance));

        limit--;
        return limit != 0;
    }, BalanceKey{reqOwner, start});
    return ret;
}

UniValue gettokenbalances(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"gettokenbalances",
               "\nReturns the balances of all accounts that belong to the wallet.\n",
               {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last tokenID from previous request."},
                            {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of tokens to return, 100 by default"},
                        },
                    },
                    {"indexed_amounts", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Format of amounts output (default = false): (true: obj = {tokenid:amount,...}, false: array = [\"amount@tokenid\"...])"},
                    {"symbol_lookup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Use token symbols in output (default = false)"},
                },
                RPCResult{
                       "{...}     (array) Json object with balances information\n"
                },
                RPCExamples{
                       HelpExampleCli("gettokenbalances", "")
                },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    DCT_ID start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t) paginationObj["start"].get_int64();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start.v++;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }
    bool indexed_amounts = false;
    if (request.params.size() > 1) {
        indexed_amounts = request.params[1].getBool();
    }
    bool symbol_lookup = false;
    if (request.params.size() > 2) {
        symbol_lookup = request.params[2].getBool();
    }

    UniValue ret(UniValue::VARR);
    if (indexed_amounts) {
        ret.setObject();
    }

    LOCK(cs_main);
    CBalances totalBalances;
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (IsMineCached(*pwallet, owner) == ISMINE_SPENDABLE) {
            totalBalances.Add(balance);
        }
        return true;
    });
    auto it = totalBalances.balances.lower_bound(start);
    for (int i = 0; it != totalBalances.balances.end() && i < limit; it++, i++) {
        CTokenAmount bal = CTokenAmount{(*it).first, (*it).second};
        std::string tokenIdStr = bal.nTokenId.ToString();
        if (symbol_lookup) {
            auto token = pcustomcsview->GetToken(bal.nTokenId);
            tokenIdStr = token->CreateSymbolKey(bal.nTokenId);
        }
        if (indexed_amounts)
            ret.pushKV(tokenIdStr, ValueFromAmount(bal.nValue));
        else
            ret.push_back(ValueFromAmount(bal.nValue).getValStr() + "@" + tokenIdStr);
    }
    return ret;
}

UniValue utxostoaccount(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"utxostoaccount",
               "\nCreates (and submits to local node and network) a transfer transaction from the wallet UTXOs to specfied account.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"amounts", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                 "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"}
                        },
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
                       HelpExampleCli("utxostoaccount", "'{\"address1\":\"1.0@DFI\","
                                                     "\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]"
                                                     "}' '[]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet); // no need here, but for symmetry

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, false);

    // decode recipients
    CUtxosToAccountMessage msg{};
    msg.to = DecodeRecipientsDefaultInternal(pwallet, request.params[0].get_obj());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UtxosToAccount)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    CScript scriptBurn;
    scriptBurn << OP_RETURN;

    // burn
    const auto toBurn = SumAllTransfers(msg.to);
    if (toBurn.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    for (const auto& kv : toBurn.balances) {
        if (rawTx.vout.empty()) { // first output is metadata
            rawTx.vout.push_back(CTxOut(kv.second, scriptMeta, kv.first));
        } else {
            rawTx.vout.push_back(CTxOut(kv.second, scriptBurn, kv.first));
        }
    }

    // fund
    fund(rawTx, pwallet, {});

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyUtxosToAccountTx(mnview_dummy, CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue accounttoaccount(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"accounttoaccount",
               "\nCreates (and submits to local node and network) a transfer transaction from the specified account to the specfied accounts.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                    {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
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
                       HelpExampleCli("accounttoaccount", "sender_address "
                                                     "'{\"address1\":\"1.0@DFI\",\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]}' "
                                                     "'[]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, false);

    // decode sender and recipients
    CAccountToAccountMessage msg{};
    msg.to = DecodeRecipientsDefaultInternal(pwallet, request.params[1].get_obj());

    if (SumAllTransfers(msg.to).balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    msg.from = DecodeScript(request.params[0].get_str());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToAccount)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[2];

    CTransactionRef optAuthTx;
    std::set<CScript> auths{msg.from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(msg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyAccountToAccountTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
        if (!res.ok) {
            /// @todo unlock
            if (res.code == CustomTxErrCodes::NotEnoughBalance) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Execution test failed: not enough balance on owner's account, call utxostoaccount to increase it.\n" +
                                   res.msg);
            }
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue accounttoutxos(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"accounttoutxos",
               "\nCreates (and submits to local node and network) a transfer transaction from the specified account to UTXOs.\n"
               "The third optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                    {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "The defi address is the key, the value is amount in amount@token format. "
                                 "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"
                            },
                        },
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
                       HelpExampleCli("accounttoutxos", "sender_address '{\"address1\":\"100@DFI\"}' '[]'")
                       + HelpExampleCli("accounttoutxos", "sender_address '{\"address1\":\"1.0@DFI\",\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]}' '[]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, true);

    // decode sender and recipients
    CAccountToUtxosMessage msg{};
    msg.from = DecodeScript(request.params[0].get_str());
    const auto to = DecodeRecipients(pwallet->chain(), request.params[1]);
    msg.balances = SumAllTransfers(to);
    if (msg.balances.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    // dummy encode, mintingOutputsStart isn't filled
    CScript scriptMeta;
    {
        std::vector<unsigned char> dummyMetadata(std::min((msg.balances.balances.size() * to.size()) * 40, (size_t)1024)); // heuristic to increse tx size before funding
        scriptMeta << OP_RETURN << dummyMetadata;
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // auth
    UniValue const & txInputs = request.params[2];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{msg.from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

     // Set change to from address
    CTxDestination dest;
    ExtractDestination(msg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // re-encode with filled mintingOutputsStart
    {
        scriptMeta = {};
        msg.mintingOutputsStart = rawTx.vout.size();
        CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToUtxos)
                       << msg;
        scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    }
    rawTx.vout[0].scriptPubKey = scriptMeta;

    // add outputs starting from mintingOutputsStart (must be unfunded, because it's minting)
    for (const auto& recip : to) {
        for (const auto& amount : recip.second.balances) {
            if (amount.second != 0) {
                rawTx.vout.push_back(CTxOut(amount.second, recip.first, amount.first));
            }
        }
    }

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyAccountToUtxosTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                                 ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
        if (!res.ok) {
            if (res.code == CustomTxErrCodes::NotEnoughBalance) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Execution test failed: not enough balance on owner's account, call utxostoaccount to increase it.\n" +
                                   res.msg);
            }
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listaccounthistory(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);
    RPCHelpMan{"listaccounthistory",
               "\nReturns information about account history.\n",
               {
                        {"owner", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                    "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned accounts or \"all\" to list whole DB (default = \"mine\")."},
                        {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"maxBlockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional height to iterate from (downto genesis block), (default = chaintip)."},
                                 {"depth", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum depth, from the genesis block is the default"},
                                 {"no_rewards", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "Filter out rewards"},
                                 {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                  "Filter by token"},
                                 {"txtype", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                  "Filter by transaction type, supported letter from 'CRTMNnpuslrUbBG'"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of records to return, 100 by default"},
                            },
                        },
               },
               RPCResult{
                       "[{},{}...]     (array) Objects with account history information\n"
               },
               RPCExamples{
                       HelpExampleCli("listaccounthistory", "all '{\"maxBlockHeight\":160,\"depth\":10}'")
                       + HelpExampleRpc("listaccounthistory", "address false")
               },
    }.Check(request);

    std::string accounts = "mine";
    if (request.params.size() > 0) {
        accounts = request.params[0].getValStr();
    }

    const auto acFull = gArgs.GetBoolArg("-acindex", DEFAULT_ACINDEX);
    const auto acMineOnly = gArgs.GetBoolArg("-acindex-mineonly", DEFAULT_ACINDEX_MINEONLY);

    if (!acMineOnly && !acFull) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex or -acindex-mineonly is need for account history");
    }

    uint32_t maxBlockHeight = std::numeric_limits<uint32_t>::max();
    uint32_t depth = maxBlockHeight;
    bool noRewards = false;
    std::string tokenFilter;
    uint32_t limit = 100;
    auto txType = CustomTxType::None;

    if (request.params.size() > 1) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
            {
                {"maxBlockHeight", UniValueType(UniValue::VNUM)},
                {"depth", UniValueType(UniValue::VNUM)},
                {"no_rewards", UniValueType(UniValue::VBOOL)},
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

        if (!optionsObj["no_rewards"].isNull()) {
            noRewards = optionsObj["no_rewards"].get_bool();
        }

        if (!optionsObj["token"].isNull()) {
            tokenFilter = optionsObj["token"].get_str();
        }

        if (!optionsObj["txtype"].isNull()) {
            const auto str = optionsObj["txtype"].get_str();
            if (str.size() == 1) {
                txType = CustomTxCodeToType(str[0]);
            }
        }
        if (!optionsObj["limit"].isNull()) {
            limit = (uint32_t) optionsObj["limit"].get_int64();
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    pwallet->BlockUntilSyncedToCurrentChain();
    maxBlockHeight = std::min(maxBlockHeight, uint32_t(chainHeight(*pwallet->chain().lock())));
    depth = std::min(depth, maxBlockHeight);
    // start block for asc order
    const auto startBlock = maxBlockHeight - depth;

    CScript account;
    auto shouldSkipBlock = [startBlock, maxBlockHeight](uint32_t blockHeight) {
        return startBlock > blockHeight || blockHeight > maxBlockHeight;
    };

    std::function<bool(CScript const &)> isMatchOwner = [](CScript const &) {
        return true;
    };

    isminetype filter = ISMINE_ALL;

    bool isMine = false;
    if (accounts == "mine") {
        isMine = true;
        filter = ISMINE_SPENDABLE;
    } else if (accounts != "all") {
        account = DecodeScript(accounts);
        isMine = IsMineCached(*pwallet, account) & ISMINE_ALL;
        if (acMineOnly && !isMine) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "account " + accounts + " is not mine, it's needed -acindex to find it");
        }
        isMatchOwner = [&account](CScript const & owner) {
            return owner == account;
        };
    }

    std::set<uint256> txs;
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && CustomTxType::None == txType;

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

    LOCK(cs_main);
    std::map<uint32_t, UniValue, std::greater<uint32_t>> ret;

    auto count = limit;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> valueLazy) -> bool {
        if (!isMatchOwner(key.owner)) {
            return false;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        const auto & value = valueLazy.get();

        if (CustomTxType::None != txType && value.category != uint8_t(txType)) {
            return true;
        }

        if(!tokenFilter.empty() && !hasToken(value.diff)) {
            return true;
        }

        auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(accounthistoryToJSON(key, value));
        if (shouldSearchInWallet) {
            txs.insert(value.txid);
        }
        return --count != 0;
    };

    AccountHistoryKey startKey{account, maxBlockHeight, std::numeric_limits<uint32_t>::max()};

    pcustomcsview->ForEachMineAccountHistory(shouldContinueToNextAccountHistory, startKey);

    if (!isMine) {
        count = limit;
        pcustomcsview->ForEachAllAccountHistory(shouldContinueToNextAccountHistory, startKey);
    }

    if (shouldSearchInWallet) {
        uint256 txid;
        CBlockIndex const * index;
        auto insertEntry = [&](COutputEntry const & entry, std::string const & info) -> bool {
            auto& array = ret.emplace(index->height, UniValue::VARR).first->second;
            array.push_back(outputEntryToJSON(entry, index, txid, info));
            return --count != 0;
        };
        searchInWallet(pwallet, account, filter, [&](CWalletTx const * pwtx) -> bool {
            txid = pwtx->GetHash();
            if (txs.count(txid)) {
                return true;
            }

            // Check we have index before progressing, wallet might be reindexing.
            if (!(index = LookupBlockIndex(pwtx->hashBlock))) {
                return true;
            }

            if (startBlock > index->height || index->height > maxBlockHeight) {
                return true;
            }

            return false;
        }, std::bind(insertEntry, std::placeholders::_1, "sent"),
           std::bind(insertEntry, std::placeholders::_1, "receive"));
    }

    if (!noRewards) {
        auto shouldContinueToNextReward = [&](RewardHistoryKey const & key, CLazySerialize<RewardHistoryValue> valueLazy) -> bool {
            if (!isMatchOwner(key.owner)) {
                return false;
            }

            if (shouldSkipBlock(key.blockHeight)) {
                return true;
            }

            if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
                return true;
            }

            if(!tokenFilter.empty()) {
                bool tokenFound = false;
                for (auto& value : valueLazy.get()) {
                    if ((tokenFound = hasToken(value.second))) {
                        break;
                    }
                }
                if (!tokenFound) {
                    return true;
                }
            }

            auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
            for (const auto & value : valueLazy.get()) {
                array.push_back(rewardhistoryToJSON(key, value));
                if (--count == 0) {
                    break;
                }
            }
            return count != 0;
        };

        RewardHistoryKey startKey{account, maxBlockHeight, 0};

        count = limit;
        pcustomcsview->ForEachMineRewardHistory(shouldContinueToNextReward, startKey);

        if (!isMine) {
            count = limit;
            pcustomcsview->ForEachAllRewardHistory(shouldContinueToNextReward, startKey);
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

UniValue accounthistorycount(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);
    RPCHelpMan{"accounthistorycount",
               "\nReturns count of account history.\n",
               {
                   {"owner", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                       "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned accounts or \"all\" to list whole DB (default = \"mine\")."},

                   {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                            {"no_rewards", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Filter out rewards"},
                            {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by token"},
                            {"txtype", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by transaction type, supported letter from 'CRTMNnpuslrUbBG'"},
                       },
                   },
               },
               RPCResult{
                       "count     (int) Count of account history\n"
               },
               RPCExamples{
                       HelpExampleCli("accounthistorycount", "all '{no_rewards: true}'")
                       + HelpExampleRpc("accounthistorycount", "")
               },
    }.Check(request);

    std::string accounts = "mine";
    if (request.params.size() > 0) {
        accounts = request.params[0].getValStr();
    }

    const auto acFull = gArgs.GetBoolArg("-acindex", DEFAULT_ACINDEX);
    const auto acMineOnly = gArgs.GetBoolArg("-acindex-mineonly", DEFAULT_ACINDEX_MINEONLY);

    if (!acMineOnly && !acFull) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex or -acindex-mineonly is need for account history");
    }

    bool noRewards = false;
    std::string tokenFilter;
    auto txType = CustomTxType::None;

    if (request.params.size() > 1) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
            {
                {"no_rewards", UniValueType(UniValue::VBOOL)},
                {"token", UniValueType(UniValue::VSTR)},
                {"txtype", UniValueType(UniValue::VSTR)},
            }, true, true);

        noRewards = optionsObj["no_rewards"].getBool();

        if (!optionsObj["token"].isNull()) {
            tokenFilter = optionsObj["token"].get_str();
        }

        if (!optionsObj["txtype"].isNull()) {
            const auto str = optionsObj["txtype"].get_str();
            if (str.size() == 1) {
                txType = CustomTxCodeToType(str[0]);
            }
        }
    }

    CScript owner;
    bool isMine = false;
    isminetype filter = ISMINE_ALL;

    if (accounts == "mine") {
        isMine = true;
        filter = ISMINE_SPENDABLE;
    } else if (accounts != "all") {
        owner = DecodeScript(accounts);
        isMine = IsMineCached(*pwallet, owner) & ISMINE_ALL;
        if (acMineOnly && !isMine) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "account " + accounts + " is not mine, it's needed -acindex to find it");
        }
    }

    std::set<uint256> txs;
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && CustomTxType::None == txType;

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

    LOCK(cs_main);
    UniValue ret(UniValue::VARR);

    uint64_t count = 0;
    const auto currentHeight = uint32_t(::ChainActive().Height());

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> valueLazy) -> bool {
        if (!owner.empty() && owner != key.owner) {
            return false;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        const auto& value = valueLazy.get();

        if (CustomTxType::None != txType && value.category != uint8_t(txType)) {
            return true;
        }

        if(!tokenFilter.empty() && !hasToken(value.diff)) {
            return true;
        }

        if (shouldSearchInWallet) {
            txs.insert(value.txid);
        }
        ++count;
        return true;
    };

    AccountHistoryKey startAccountKey{owner, currentHeight, std::numeric_limits<uint32_t>::max()};

    pcustomcsview->ForEachMineAccountHistory(shouldContinueToNextAccountHistory, startAccountKey);

    if (!isMine) {
        pcustomcsview->ForEachAllAccountHistory(shouldContinueToNextAccountHistory, startAccountKey);
    }

    if (shouldSearchInWallet) {
        auto incCount = [&count](COutputEntry const &) { ++count; return true; };
        searchInWallet(pwallet, owner, filter, [&](CWalletTx const * pwtx) -> bool {
            if (txs.count(pwtx->GetHash())) {
                return true;
            }

            auto index = LookupBlockIndex(pwtx->hashBlock);

            // Check we have index before progressing, wallet might be reindexing.
            if (!index) {
                return true;
            }

            if (index->height > currentHeight) {
                return true;
            }

            return false;
        }, incCount, incCount);
    }

    if (noRewards) {
        return count;
    }

    auto shouldContinueToNextReward = [&](RewardHistoryKey const & key, CLazySerialize<RewardHistoryValue> valueLazy) -> bool {
        if (!owner.empty() && owner != key.owner) {
            return false;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        if(!tokenFilter.empty()) {
            bool tokenFound = false;
            for (const auto & value : valueLazy.get()) {
                if ((tokenFound = hasToken(value.second))) {
                    break;
                }
            }
            if (!tokenFound) {
                return true;
            }
        }
        ++count;
        return true;
    };

    RewardHistoryKey startHistoryKey{owner, currentHeight, 0};

    pcustomcsview->ForEachMineRewardHistory(shouldContinueToNextReward, startHistoryKey);

    if (!isMine) {
        pcustomcsview->ForEachAllRewardHistory(shouldContinueToNextReward, startHistoryKey);
    }

    return count;
}

UniValue listcommunitybalances(const JSONRPCRequest& request) {
    RPCHelpMan{"listcommunitybalances",
               "\nReturns information about all community balances.\n",
               {
               },
               RPCResult{
                       "{balance_type:value,...}     (array) Json object with accounts information\n"
               },
               RPCExamples{
                       HelpExampleCli("listcommunitybalances", "")
                       + HelpExampleRpc("listcommunitybalances", "")
               },
    }.Check(request);

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    for (auto kv : Params().GetConsensus().nonUtxoBlockSubsidies) {
        ret.pushKV(GetCommunityAccountName(kv.first), ValueFromAmount(pcustomcsview->GetCommunityBalance(kv.first)));
    }

    return ret;
}

UniValue sendtokenstoaddress(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"sendtokenstoaddress",
               "\nCreates (and submits to local node and network) a transfer transaction from your accounts balances (may be picked manualy or autoselected) to the specfied accounts.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"from", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The source defi address is the key, the value is amount in amount@token format. "
                                                                                     "If obj is empty (no address keys exists) then will try to auto-select accounts from wallet "
                                                                                     "with necessary balances to transfer."},
                        },
                    },
                    {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                    },
                    {"selectionMode", RPCArg::Type::STR, /* default */ "pie", "If param \"from\" is empty this param indicates accounts autoselection mode."
                                                                              "May be once of:\n"
                                                                              "  \"forward\" - Selecting accounts without sorting, just as address list sorted.\n"
                                                                              "  \"crumbs\" - Selecting accounts by ascending of sum token amounts.\n"
                                                                              "    It means that we will select first accounts with minimal sum of neccessary token amounts.\n"
                                                                              "  \"pie\" - Selecting accounts by descending of sum token amounts.\n"
                                                                              "    It means that we will select first accounts with maximal sum of neccessary token amounts."
                    },
                },
                RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                        HelpExampleCli("sendtokenstoaddress", "'{}' "
                                                    "'{\"dstAddress1\":\"1.0@DFI\",\"dstAddress2\":[\"2.0@BTC\", \"3.0@ETH\"]}' \"crumbs\"") +
                        HelpExampleCli("sendtokenstoaddress", "'{\"srcAddress1\":\"2.0@DFI\", \"srcAddress2\":[\"3.0@DFI\", \"2.0@ETH\"]}' "
                                                    "'{\"dstAddress1\":[\"5.0@DFI\", \"2.0@ETH\"]}'")
                        },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VOBJ, UniValue::VSTR}, false);

    CAnyAccountsToAccountsMessage msg;
    msg.to = DecodeRecipientsDefaultInternal(pwallet, request.params[1].get_obj());

    const CBalances sumTransfersTo = SumAllTransfers(msg.to);
    if (sumTransfersTo.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts in \"to\" param");
    }

    if (request.params[0].get_obj().empty()) { // autoselection
        CAccounts foundMineAccounts = GetAllMineAccounts(pwallet);

        AccountSelectionMode selectionMode = SelectionPie;
        if (request.params[2].isStr()) {
            selectionMode = ParseAccountSelectionParam(request.params[2].get_str());
        }

        msg.from = SelectAccountsByTargetBalances(foundMineAccounts, sumTransfersTo, selectionMode);

        if (msg.from.empty()) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Not enough balance on wallet accounts, call utxostoaccount to increase it.\n");
        }
    }
    else {
        msg.from = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AnyAccountsToAccounts)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    if (scriptMeta.size() > nMaxDatacarrierBytes) {
        throw JSONRPCError(RPC_VERIFY_REJECTED, "The output custom script size has exceeded the maximum OP_RETURN script size."
                                                "It may happened because too many \"from\" or \"to\" accounts balances."
                                                "If you use autoselection, you can try to use \"pie\" selection mode for decreasing accounts count.");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue txInputs(UniValue::VARR);

    // auth
    std::set<CScript> auths;
    for (const auto& acc : msg.from) {
        auths.emplace(acc.first);
    }
    CTransactionRef optAuthTx;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to from address if there's only one auth address
    if (auths.size() == 1) {
        CTxDestination dest;
        ExtractDestination(*auths.cbegin(), dest);
        if (IsValidDestination(dest)) {
            coinControl.destChange = dest;
        }
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyAnyAccountsToAccountsTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
        if (!res.ok) {
            if (res.code == CustomTxErrCodes::NotEnoughBalance) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Execution test failed: not enough balance on owner's account, call utxostoaccount to increase it.\n" +
                                   res.msg);
            }
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();

}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  -------------   ------------------------ ----------------------  ----------
    {"accounts",    "listaccounts",          &listaccounts,          {"pagination", "verbose", "indexed_amounts", "is_mine_only"}},
    {"accounts",    "getaccount",            &getaccount,            {"owner", "pagination", "indexed_amounts"}},
    {"accounts",    "gettokenbalances",      &gettokenbalances,      {"pagination", "indexed_amounts", "symbol_lookup"}},
    {"accounts",    "utxostoaccount",        &utxostoaccount,        {"amounts", "inputs"}},
    {"accounts",    "accounttoaccount",      &accounttoaccount,      {"from", "to", "inputs"}},
    {"accounts",    "accounttoutxos",        &accounttoutxos,        {"from", "to", "inputs"}},
    {"accounts",    "listaccounthistory",    &listaccounthistory,    {"owner", "options"}},
    {"accounts",    "accounthistorycount",   &accounthistorycount,   {"owner", "options"}},
    {"accounts",    "listcommunitybalances", &listcommunitybalances, {}},
    {"accounts",    "sendtokenstoaddress",   &sendtokenstoaddress,   {"from", "to", "selectionMode"}},
};

void RegisterAccountsRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

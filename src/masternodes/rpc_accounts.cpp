#include <masternodes/accountshistory.h>
#include <masternodes/mn_rpc.h>

std::string tokenAmountString(CTokenAmount const& amount) {
    const auto token = pcustomcsview->GetToken(amount.nTokenId);
    const auto valueString = ValueFromAmount(amount.nValue).getValStr();
    return valueString + "@" + token->CreateSymbolKey(amount.nTokenId);
}

UniValue AmountsToJSON(TAmounts const & diffs) {
    UniValue obj(UniValue::VARR);

    for (auto const & diff : diffs) {
        obj.push_back(tokenAmountString({diff.first, diff.second}));
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

UniValue rewardhistoryToJSON(CScript const & owner, uint32_t height, DCT_ID const & poolId, RewardType type, CTokenAmount amount) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("owner", ScriptToString(owner));
    obj.pushKV("blockHeight", (uint64_t) height);
    if (auto block = ::ChainActive()[height]) {
        obj.pushKV("blockHash", block->GetBlockHash().GetHex());
        obj.pushKV("blockTime", block->GetBlockTime());
    }
    obj.pushKV("type", RewardToString(type));
    if (type & RewardType::Rewards) {
        obj.pushKV("rewardType", RewardTypeToString(type));
    }
    obj.pushKV("poolID", poolId.ToString());
    TAmounts amounts({{amount.nTokenId,amount.nValue}});
    obj.pushKV("amounts", AmountsToJSON(amounts));
    return obj;
}

UniValue outputEntryToJSON(COutputEntry const & entry, CBlockIndex const * index, CWalletTx const * pwtx) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("owner", EncodeDestination(entry.destination));
    obj.pushKV("blockHeight", index->nHeight);
    obj.pushKV("blockHash", index->GetBlockHash().GetHex());
    obj.pushKV("blockTime", index->GetBlockTime());
    if (pwtx->IsCoinBase()) {
        obj.pushKV("type", "blockReward");
    } else if (entry.amount < 0) {
        obj.pushKV("type", "sent");
    } else {
        obj.pushKV("type", "receive");
    }
    obj.pushKV("txn", (uint64_t) entry.vout);
    obj.pushKV("txid", pwtx->GetHash().ToString());
    TAmounts amounts({{DCT_ID{0},entry.amount}});
    obj.pushKV("amounts", AmountsToJSON(amounts));
    return obj;
}

static void onPoolRewards(CCustomCSView & view, CScript const & owner, uint32_t begin, uint32_t end, std::function<void(uint32_t, DCT_ID, RewardType, CTokenAmount)> onReward) {
    CCustomCSView mnview(view);
    static const uint32_t eunosHeight = Params().GetConsensus().EunosHeight;
    view.ForEachPoolId([&] (DCT_ID const & poolId) {
        auto height = view.GetShare(poolId, owner);
        if (!height || *height >= end) {
            return true; // no share or target height is before a pool share' one
        }
        auto onLiquidity = [&]() -> CAmount {
            return mnview.GetBalance(owner, poolId).nValue;
        };
        uint32_t firstHeight = 0;
        auto beginHeight = std::max(*height, begin);
        view.CalculatePoolRewards(poolId, onLiquidity, beginHeight, end,
            [&](RewardType type, CTokenAmount amount, uint32_t height) {
                if (amount.nValue == 0) {
                    return;
                }
                onReward(height, poolId, type, amount);
                // prior Eunos account balance includes rewards
                // thus we don't need to increment it by first one
                if (!firstHeight) {
                    firstHeight = height;
                }
                if (height >= eunosHeight || firstHeight != height) {
                    mnview.AddBalance(owner, amount); // update owner liquidity
                }
            }
        );
        return true;
    });
}

static void searchInWallet(CWallet const * pwallet,
                           CScript const & account,
                           isminetype filter,
                           std::function<bool(CBlockIndex const *, CWalletTx const *)> shouldSkipTx,
                           std::function<bool(COutputEntry const &, CBlockIndex const *, CWalletTx const *)> txEntry) {

    CTxDestination destination;
    ExtractDestination(account, destination);

    CAmount nFee;
    std::list<COutputEntry> listSent;
    std::list<COutputEntry> listReceived;

    auto locked_chain = pwallet->chain().lock();
    LOCK2(pwallet->cs_wallet, locked_chain->mutex());

    const auto& txOrdered = pwallet->mapWallet.get<ByOrder>();

    for (auto it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
        auto* pwtx = &(*it);

        auto index = LookupBlockIndex(pwtx->hashBlock);
        if (!index || index->nHeight == 0) { // skip genesis block
            continue;
        }

        if (shouldSkipTx(index, pwtx)) {
            continue;
        }

        if (!pwtx->IsTrusted(*locked_chain)) {
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
            if (!txEntry(sent, index, pwtx)) {
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
            if (!txEntry(recv, index, pwtx)) {
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
    auto pwallet = GetWallet(request);

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
                                                      "\"limit\":100"
                                                      "}'")
               },
    }.Check(request);

    pwallet->BlockUntilSyncedToCurrentChain();

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
    CCustomCSView mnview(*pcustomcsview);
    auto targetHeight = ::ChainActive().Height() + 1;

    mnview.ForEachAccount([&](CScript const & account) {

        if (isMineOnly && IsMineCached(*pwallet, account) != ISMINE_SPENDABLE) {
            return true;
        }

        mnview.CalculateOwnerRewards(account, targetHeight);

        // output the relavant balances only for account
        mnview.ForEachBalance([&](CScript const & owner, CTokenAmount balance) {
            if (account != owner) {
                return false;
            }
            ret.push_back(accountToJSON(owner, balance, verbose, indexed_amounts));
            return --limit != 0;
        }, {account, start.tokenID});

        start.tokenID = DCT_ID{}; // reset to start id
        return limit != 0;
    }, start.owner);

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
    CCustomCSView mnview(*pcustomcsview);
    auto targetHeight = ::ChainActive().Height() + 1;

    mnview.CalculateOwnerRewards(reqOwner, targetHeight);

    mnview.ForEachBalance([&](CScript const & owner, CTokenAmount balance) {
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
    auto pwallet = GetWallet(request);

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

    pwallet->BlockUntilSyncedToCurrentChain();

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
    CCustomCSView mnview(*pcustomcsview);
    auto targetHeight = ::ChainActive().Height() + 1;

    mnview.ForEachAccount([&](CScript const & account) {
        if (IsMineCached(*pwallet, account) == ISMINE_SPENDABLE) {
            mnview.CalculateOwnerRewards(account, targetHeight);
            mnview.ForEachBalance([&](CScript const & owner, CTokenAmount balance) {
                return account == owner && totalBalances.Add(balance);
            }, {account, DCT_ID{}});
        }
        return true;
    });
    auto it = totalBalances.balances.lower_bound(start);
    for (int i = 0; it != totalBalances.balances.end() && i < limit; it++, i++) {
        CTokenAmount bal = CTokenAmount{(*it).first, (*it).second};
        std::string tokenIdStr = bal.nTokenId.ToString();
        if (symbol_lookup) {
            auto token = mnview.GetToken(bal.nTokenId);
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
    auto pwallet = GetWallet(request);

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
    execTestTx(CTransaction(rawTx), targetHeight);

    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}


UniValue sendutxosfrom(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"sendutxosfrom",
               "\nSend a transaction using UTXOs from the specfied address.\n" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of sender"},
                       {"to", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of receiver"},
                       {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "The amount to send"},
                       {"change", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The address to send change to (Default: from address)"},
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("sendutxosfrom", R"("from" "to" 100)")
                       + HelpExampleRpc("sendutxosfrom", R"("from", "to", 100")")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    auto locked_chain = pwallet->chain().lock();
    LOCK2(pwallet->cs_wallet, locked_chain->mutex());

    CTxDestination fromDest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(fromDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address");
    }

    CTxDestination toDest = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(toDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid to address");
    }

    // Amount
    CAmount nAmount = AmountFromValue(request.params[2]);

    CCoinControl coin_control;
    if (request.params[3].isNull()) {
        coin_control.destChange = fromDest;
    } else {
        CTxDestination changeDest = DecodeDestination(request.params[3].get_str());
        if (!IsValidDestination(changeDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid change address");
        }
        coin_control.destChange = changeDest;
    }

    // Only match from address destination
    coin_control.matchDestination = fromDest;

    EnsureWalletIsUnlocked(pwallet);

    CTransactionRef tx = SendMoney(*locked_chain, pwallet, toDest, nAmount, {0}, false /* fSubtractFeeFromAmount */, coin_control, {});
    return tx->GetHash().GetHex();
}

UniValue accounttoaccount(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue accounttoutxos(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

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
        CDataStream dummyMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        dummyMetadata << static_cast<unsigned char>(CustomTxType::AccountToUtxos) << msg;

        std::vector<unsigned char> padding(10);
        for (const auto& recip : to) {
            for (const auto& amount : recip.second.balances) {
                if (amount.second != 0) {
                    CTxOut out{amount.second, recip.first, amount.first};
                    dummyMetadata << out << padding;
                    LogPrint(BCLog::ESTIMATEFEE, "%s: out size %d padding %d\n", __func__, sizeof(out), sizeof(unsigned char) * padding.size());
                }
            }
        }

        scriptMeta << OP_RETURN << ToByteVector(dummyMetadata);
        LogPrint(BCLog::ESTIMATEFEE, "%s: dummyMetadata size %d\n", __func__, dummyMetadata.size());
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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

class CScopeAccountReverter {
    CCustomCSView & view;
    CScript const & owner;
    TAmounts const & balances;

public:
    CScopeAccountReverter(CCustomCSView & view, CScript const & owner, TAmounts const & balances)
        : view(view), owner(owner), balances(balances) {}

    ~CScopeAccountReverter() {
        for (const auto& balance : balances) {
            auto amount = -balance.second;
            auto token = view.GetToken(balance.first);
            auto IsPoolShare = token && token->IsPoolShare();
            if (amount > 0) {
                view.AddBalance(owner, {balance.first, amount});
                if (IsPoolShare) {
                    if (view.GetBalance(owner, balance.first).nValue == amount) {
                        view.SetShare(balance.first, owner, 0);
                    }
                }
            } else {
                view.SubBalance(owner, {balance.first, -amount});
                if (IsPoolShare) {
                    if (view.GetBalance(owner, balance.first).nValue == 0) {
                        view.DelShare(balance.first, owner);
                    } else {
                        view.SetShare(balance.first, owner, 0);
                    }
                }
            }
        }
    }
};

UniValue listaccounthistory(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

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
                                  "Filter by transaction type, supported letter from {CustomTxType}"},
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

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is needed for account history");
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

    std::function<bool(CScript const &)> isMatchOwner = [](CScript const &) {
        return true;
    };

    CScript account;
    bool isMine = false;
    isminetype filter = ISMINE_ALL;

    if (accounts == "mine") {
        isMine = true;
        filter = ISMINE_SPENDABLE;
    } else if (accounts != "all") {
        account = DecodeScript(accounts);
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
    CCustomCSView view(*pcustomcsview);
    CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
    std::map<uint32_t, UniValue, std::greater<uint32_t>> ret;

    maxBlockHeight = std::min(maxBlockHeight, uint32_t(::ChainActive().Height()));
    depth = std::min(depth, maxBlockHeight);

    const auto startBlock = maxBlockHeight - depth;
    auto shouldSkipBlock = [startBlock, maxBlockHeight](uint32_t blockHeight) {
        return startBlock > blockHeight || blockHeight > maxBlockHeight;
    };

    CScript lastOwner;
    auto count = limit;
    auto lastHeight = maxBlockHeight;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> valueLazy) -> bool {
        if (!isMatchOwner(key.owner)) {
            return false;
        }

        std::unique_ptr<CScopeAccountReverter> reverter;
        if (!noRewards) {
            reverter = MakeUnique<CScopeAccountReverter>(view, key.owner, valueLazy.get().diff);
        }

        bool accountRecord = true;
        auto workingHeight = key.blockHeight;

        if (shouldSkipBlock(key.blockHeight)) {
            // show rewards in interval [startBlock, lastHeight)
            if (!noRewards && startBlock > workingHeight) {
                accountRecord = false;
                workingHeight = startBlock;
            } else {
                return true;
            }
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        const auto & value = valueLazy.get();

        if (CustomTxType::None != txType && value.category != uint8_t(txType)) {
            return true;
        }

        if (isMine) {
            // starts new account owned by the wallet
            if (lastOwner != key.owner) {
                count = limit;
            } else if (count == 0) {
                return true;
            }
        }

        // starting new account
        if (account.empty() && lastOwner != key.owner) {
            view.Discard();
            lastOwner = key.owner;
            lastHeight = maxBlockHeight;
        }

        if (accountRecord && (tokenFilter.empty() || hasToken(value.diff))) {
            auto& array = ret.emplace(workingHeight, UniValue::VARR).first->second;
            array.push_back(accounthistoryToJSON(key, value));
            if (shouldSearchInWallet) {
                txs.insert(value.txid);
            }
            --count;
        }

        if (!noRewards && count && lastHeight > workingHeight) {
            onPoolRewards(view, key.owner, workingHeight, lastHeight,
                [&](int32_t height, DCT_ID poolId, RewardType type, CTokenAmount amount) {
                    if (tokenFilter.empty() || hasToken({{amount.nTokenId, amount.nValue}})) {
                        auto& array = ret.emplace(height, UniValue::VARR).first->second;
                        array.push_back(rewardhistoryToJSON(key.owner, height, poolId, type, amount));
                        count ? --count : 0;
                    }
                }
            );
        }

        lastHeight = workingHeight;

        return count != 0 || isMine;
    };

    AccountHistoryKey startKey{account, maxBlockHeight, std::numeric_limits<uint32_t>::max()};

    if (!noRewards && !account.empty()) {
        // revert previous tx to restore account balances to maxBlockHeight
        paccountHistoryDB->ForEachAccountHistory([&](AccountHistoryKey const & key, AccountHistoryValue const & value) {
            if (startKey.blockHeight > key.blockHeight) {
                return false;
            }
            if (!isMatchOwner(key.owner)) {
                return false;
            }
            CScopeAccountReverter(view, key.owner, value.diff);
            return true;
        }, {account, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()});
    }

    paccountHistoryDB->ForEachAccountHistory(shouldContinueToNextAccountHistory, startKey);

    if (shouldSearchInWallet) {
        count = limit;
        searchInWallet(pwallet, account, filter,
            [&](CBlockIndex const * index, CWalletTx const * pwtx) {
                return txs.count(pwtx->GetHash()) || startBlock > index->nHeight || index->nHeight > maxBlockHeight;
            },
            [&](COutputEntry const & entry, CBlockIndex const * index, CWalletTx const * pwtx) {
                auto& array = ret.emplace(index->nHeight, UniValue::VARR).first->second;
                array.push_back(outputEntryToJSON(entry, index, pwtx));
                return --count != 0;
            }
        );
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

UniValue listburnhistory(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"listburnhistory",
               "\nReturns information about burn history.\n",
               {
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
                       "[{},{}...]     (array) Objects with burn history information\n"
               },
               RPCExamples{
                       HelpExampleCli("listburnhistory", "'{\"maxBlockHeight\":160,\"depth\":10}'")
                       + HelpExampleRpc("listburnhistory", "")
               },
    }.Check(request);

    uint32_t maxBlockHeight = std::numeric_limits<uint32_t>::max();
    uint32_t depth = maxBlockHeight;
    std::string tokenFilter;
    uint32_t limit = 100;
    auto txType = CustomTxType::None;
    bool txTypeSearch{false};

    if (request.params.size() == 1) {
        UniValue optionsObj = request.params[0].get_obj();
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
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    std::function<bool(CScript const &)> isMatchOwner = [](CScript const &) {
        return true;
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

    LOCK(cs_main);
    CCustomCSView view(*pcustomcsview);
    CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
    std::map<uint32_t, UniValue, std::greater<uint32_t>> ret;

    maxBlockHeight = std::min(maxBlockHeight, uint32_t(::ChainActive().Height()));
    depth = std::min(depth, maxBlockHeight);

    const auto startBlock = maxBlockHeight - depth;
    auto shouldSkipBlock = [startBlock, maxBlockHeight](uint32_t blockHeight) {
        return startBlock > blockHeight || blockHeight > maxBlockHeight;
    };

    auto count = limit;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> valueLazy) -> bool
    {
        if (!isMatchOwner(key.owner)) {
            return false;
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
        array.push_back(accounthistoryToJSON(key, value));

        --count;

        return count != 0;
    };

    AccountHistoryKey startKey{{}, maxBlockHeight, std::numeric_limits<uint32_t>::max()};
    pburnHistoryDB->ForEachAccountHistory(shouldContinueToNextAccountHistory, startKey);

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
    auto pwallet = GetWallet(request);

    RPCHelpMan{"accounthistorycount",
               "\nReturns count of account history.\n",
               {
                   {"owner", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                       "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned accounts or \"all\" to list whole DB (default = \"mine\")."},

                   {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                            {"no_rewards", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Filter out rewards"},
                            {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by token"},
                            {"txtype", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by transaction type, supported letter from {CustomTxType}"},
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

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is need for account history");
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

    pwallet->BlockUntilSyncedToCurrentChain();

    CScript owner;
    bool isMine = false;
    isminetype filter = ISMINE_ALL;

    if (accounts == "mine") {
        isMine = true;
        filter = ISMINE_SPENDABLE;
    } else if (accounts != "all") {
        owner = DecodeScript(accounts);
        isMine = IsMineCached(*pwallet, owner) & ISMINE_ALL;
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
    CCustomCSView view(*pcustomcsview);
    CCoinsViewCache coins(&::ChainstateActive().CoinsTip());

    CScript lastOwner;
    uint64_t count = 0;
    auto lastHeight = uint32_t(::ChainActive().Height());
    const auto currentHeight = lastHeight;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> valueLazy) -> bool {
        if (!owner.empty() && owner != key.owner) {
            return false;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        const auto& value = valueLazy.get();

        std::unique_ptr<CScopeAccountReverter> reverter;
        if (!noRewards) {
            reverter = MakeUnique<CScopeAccountReverter>(view, key.owner, value.diff);
        }

        if (CustomTxType::None != txType && value.category != uint8_t(txType)) {
            return true;
        }

        if (tokenFilter.empty() || hasToken(value.diff)) {
            if (shouldSearchInWallet) {
                txs.insert(value.txid);
            }
            ++count;
        }

        if (!noRewards) {
            // starting new account
            if (lastOwner != key.owner) {
                view.Discard();
                lastOwner = key.owner;
                lastHeight = currentHeight;
            }
            onPoolRewards(view, key.owner, key.blockHeight, lastHeight,
                [&](int32_t, DCT_ID, RewardType, CTokenAmount amount) {
                    if (tokenFilter.empty() || hasToken({{amount.nTokenId, amount.nValue}})) {
                        ++count;
                    }
                }
            );
            lastHeight = key.blockHeight;
        }

        return true;
    };

    AccountHistoryKey startAccountKey{owner, currentHeight, std::numeric_limits<uint32_t>::max()};
    paccountHistoryDB->ForEachAccountHistory(shouldContinueToNextAccountHistory, startAccountKey);

    if (shouldSearchInWallet) {
        searchInWallet(pwallet, owner, filter,
            [&](CBlockIndex const * index, CWalletTx const * pwtx) {
                return txs.count(pwtx->GetHash()) || index->nHeight > currentHeight;
            },
            [&count](COutputEntry const &, CBlockIndex const *, CWalletTx const *) {
                ++count;
                return true;
            }
        );
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
    CAmount burnt{0};
    for (const auto& kv : Params().GetConsensus().newNonUTXOSubsidies)
    {
        // Skip these as any unused balance will be burnt.
        if (kv.first == CommunityAccountType::Options) {
            continue;
        }
        if (kv.first == CommunityAccountType::Unallocated ||
            kv.first == CommunityAccountType::IncentiveFunding) {
            burnt += pcustomcsview->GetCommunityBalance(kv.first);
            continue;
        }

        if (kv.first == CommunityAccountType::Loan) {
            if (::ChainActive().Height() >= Params().GetConsensus().FortCanningHeight) {
                burnt += pcustomcsview->GetCommunityBalance(kv.first);
            }
            continue;
        }

        ret.pushKV(GetCommunityAccountName(kv.first), ValueFromAmount(pcustomcsview->GetCommunityBalance(kv.first)));
    }
    ret.pushKV("Burnt", ValueFromAmount(burnt));

    return ret;
}

UniValue sendtokenstoaddress(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

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
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();

}


UniValue getburninfo(const JSONRPCRequest& request) {
    RPCHelpMan{"getburninfo",
               "\nReturns burn address and burnt coin and token information.\n"
               "Requires full acindex for correct amount, tokens and feeburn values.\n",
               {
               },
               RPCResult{
                       "{\n"
                       "  \"address\" : \"address\",        (string) The defi burn address\n"
                       "  \"amount\" : n.nnnnnnnn,        (string) The amount of DFI burnt\n"
                       "  \"tokens\" :  [\n"
                       "      { (array of burnt tokens)"
                       "      \"name\" : \"name\"\n"
                       "      \"amount\" : n.nnnnnnnn\n"
                       "    ]\n"
                       "  \"feeburn\" : n.nnnnnnnn,        (string) The amount of fees burnt\n"
                       "  \"emissionburn\" : n.nnnnnnnn,   (string) The amount of non-utxo coinbase rewards burnt\n"
                       "}\n"
               },
               RPCExamples{
                       HelpExampleCli("getburninfo", "")
                       + HelpExampleRpc("getburninfo", "")
               },
    }.Check(request);

    CAmount burntDFI{0};
    CAmount burntFee{0};
    CAmount auctionFee{0};
    CAmount paybackFee{0};
    CBalances burntTokens;
    auto calcBurn = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> valueLazy) -> bool
    {
        const auto & value = valueLazy.get();

        // UTXO burn
        if (value.category == uint8_t(CustomTxType::None)) {
            for (auto const & diff : value.diff) {
                burntDFI += diff.second;
            }
            return true;
        }

        // Fee burn
        if (value.category == uint8_t(CustomTxType::CreateMasternode)
        || value.category == uint8_t(CustomTxType::CreateToken)
        || value.category == uint8_t(CustomTxType::Vault)) {
            for (auto const & diff : value.diff) {
                burntFee += diff.second;
            }
            return true;
        }

        // withdraw burn
        if (value.category == uint8_t(CustomTxType::PaybackLoan)) {
            for (auto const & diff : value.diff) {
                paybackFee += diff.second;
            }
            return true;
        }

        // auction burn
        if (value.category == uint8_t(CustomTxType::AuctionBid)) {
            for (auto const & diff : value.diff) {
                auctionFee += diff.second;
            }
            return true;
        }

        // Token burn
        for (auto const & diff : value.diff) {
            burntTokens.Add({diff.first, diff.second});
        }

        return true;
    };

    AccountHistoryKey startKey{{}, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
    pburnHistoryDB->ForEachAccountHistory(calcBurn, startKey);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", ScriptToString(Params().GetConsensus().burnAddress));
    result.pushKV("amount", ValueFromAmount(burntDFI));
    UniValue tokens(UniValue::VARR);
    for (const auto& item : burntTokens.balances)
    {
        tokens.push_back(tokenAmountString({{item.first}, item.second}));
    }
    result.pushKV("tokens", tokens);
    result.pushKV("feeburn", ValueFromAmount(burntFee));

    if (auctionFee) {
        result.pushKV("auctionburn", ValueFromAmount(auctionFee));
    }

    if (paybackFee) {
        result.pushKV("paybackburn", ValueFromAmount(paybackFee));
    }

    LOCK(cs_main);

    CAmount burnt{0};
    for (const auto& kv : Params().GetConsensus().newNonUTXOSubsidies) {
        if (kv.first == CommunityAccountType::Unallocated || kv.first == CommunityAccountType::IncentiveFunding ||
        (::ChainActive().Height() >= Params().GetConsensus().FortCanningHeight && kv.first == CommunityAccountType::Loan)) {
            burnt += pcustomcsview->GetCommunityBalance(kv.first);
        }
    }
    result.pushKV("emissionburn", ValueFromAmount(burnt));

    return result;
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  -------------   ------------------------ ----------------------  ----------
    {"accounts",    "listaccounts",          &listaccounts,          {"pagination", "verbose", "indexed_amounts", "is_mine_only"}},
    {"accounts",    "getaccount",            &getaccount,            {"owner", "pagination", "indexed_amounts"}},
    {"accounts",    "gettokenbalances",      &gettokenbalances,      {"pagination", "indexed_amounts", "symbol_lookup"}},
    {"accounts",    "utxostoaccount",        &utxostoaccount,        {"amounts", "inputs"}},
    {"accounts",    "sendutxosfrom",         &sendutxosfrom,         {"from", "to", "amount", "change"}},
    {"accounts",    "accounttoaccount",      &accounttoaccount,      {"from", "to", "inputs"}},
    {"accounts",    "accounttoutxos",        &accounttoutxos,        {"from", "to", "inputs"}},
    {"accounts",    "listaccounthistory",    &listaccounthistory,    {"owner", "options"}},
    {"accounts",    "listburnhistory",       &listburnhistory,       {"options"}},
    {"accounts",    "accounthistorycount",   &accounthistorycount,   {"owner", "options"}},
    {"accounts",    "listcommunitybalances", &listcommunitybalances, {}},
    {"accounts",    "sendtokenstoaddress",   &sendtokenstoaddress,   {"from", "to", "selectionMode"}},
    {"accounts",    "getburninfo",           &getburninfo,           {}},
};

void RegisterAccountsRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

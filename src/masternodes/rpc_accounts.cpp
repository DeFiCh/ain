#include <masternodes/accountshistory.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/mn_rpc.h>

#include <functional>
#include <queue>
#include <set>

std::string tokenAmountString(CTokenAmount const& amount) {
    const auto token = pcustomcsview->GetToken(amount.nTokenId);
    const auto valueString = GetDecimaleString(amount.nValue);
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
    if (pwtx->IsCoinBase()) {
        obj.pushKV("type", "blockReward");
    } else if (entry.amount < 0) {
        obj.pushKV("type", "sent");
    } else {
        obj.pushKV("type", "receive");
    }
    obj.pushKV("txn", (uint64_t) pwtx->nIndex);
    obj.pushKV("txid", pwtx->GetHash().ToString());
    TAmounts amounts({{DCT_ID{0},entry.amount}});
    obj.pushKV("amounts", AmountsToJSON(amounts));
    return obj;
}

static void onPoolRewards(CImmutableCSView & view, CScript const & owner, uint32_t begin, uint32_t end, std::function<void(uint32_t, DCT_ID, RewardType, CTokenAmount)> onReward) {
    CImmutableCSView mnview(view);
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

        LockAssertion lock(cs_main); // for clang
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
            if (IsValidDestination(destination) && account != GetScriptForDestination(sent.destination)) {
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
            if (IsValidDestination(destination) && account != GetScriptForDestination(recv.destination)) {
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

    CImmutableCSView mnview(*pcustomcsview);
    auto targetHeight = mnview.GetLastHeight() + 1;

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

    CImmutableCSView mnview(*pcustomcsview);
    auto targetHeight = mnview.GetLastHeight() + 1;

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

    CBalances totalBalances;
    CImmutableCSView mnview(*pcustomcsview);
    for (const auto& account : GetAllMineAccounts(mnview, pwallet)) {
        totalBalances.AddBalances(account.second.balances);
    }

    auto it = totalBalances.balances.lower_bound(start);
    for (size_t i = 0; it != totalBalances.balances.end() && i < limit; it++, i++) {
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

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

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

void RevertOwnerBalances(CImmutableCSView & view, CScript const & owner, TAmounts const & balances) {
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

struct CRewardHistory {
    uint32_t height;
    CScript const & owner;
    TAmounts balances;
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
                                 {"txn", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Order in block, unlimited by default"},
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
    uint32_t txn = std::numeric_limits<uint32_t>::max();

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
                {"txn", UniValueType(UniValue::VNUM)},
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
            if (txType == CustomTxType::None) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid tx type (" + str + ")");
            }
        }
        if (!optionsObj["limit"].isNull()) {
            limit = (uint32_t) optionsObj["limit"].get_int64();
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }

        if (!optionsObj["txn"].isNull()) {
            txn = (uint32_t) optionsObj["txn"].get_int64();
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

    CImmutableCSView view(*pcustomcsview);

    auto hasToken = [&](TAmounts const & diffs) {
        for (auto const & diff : diffs) {
            auto token = view.GetToken(diff.first);
            auto const tokenIdStr = token->CreateSymbolKey(diff.first);
            if(tokenIdStr == tokenFilter) {
                return true;
            }
        }
        return false;
    };

    uint32_t height = view.GetLastHeight();
    std::map<uint32_t, UniValue, std::greater<uint32_t>> ret;

    maxBlockHeight = std::min(maxBlockHeight, height);
    depth = std::min(depth, maxBlockHeight);

    auto count = limit;
    const auto startBlock = maxBlockHeight - depth;

    std::set<CScript> rewardAccounts;
    std::queue<CRewardHistory> rewardsHistory;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> lazy) -> bool {

        if (startBlock > key.blockHeight) {
            return false;
        }

        if (!isMatchOwner(key.owner)) {
            return false;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        bool accountRecord = maxBlockHeight >= key.blockHeight;
        if (!accountRecord && noRewards) {
            return true;
        }

        auto& value = lazy.get();

        if (!noRewards) {
            auto& owner = *(rewardAccounts.insert(key.owner).first);
            rewardsHistory.push({key.blockHeight, owner, value.diff});
        }

        if (CustomTxType::None != txType && value.category != uint8_t(txType)) {
            return true;
        }

        if (accountRecord && (tokenFilter.empty() || hasToken(value.diff))) {
            auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
            array.push_back(accounthistoryToJSON(key, value));
            if (shouldSearchInWallet) {
                txs.insert(value.txid);
            }
            --count;
        }

        return count != 0;
    };

    CAccountHistoryStorage historyView(*paccountHistoryDB);
    if (!noRewards && maxBlockHeight < height) {
        historyView.ForEachAccountHistory([&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> lazy) {
            return key.blockHeight > maxBlockHeight && shouldContinueToNextAccountHistory(key, std::move(lazy));
        }, account);
    }
    historyView.ForEachAccountHistory(shouldContinueToNextAccountHistory, account, maxBlockHeight, txn);

    auto lastHeight = maxBlockHeight;
    for (count = limit; !rewardsHistory.empty(); rewardsHistory.pop()) {

        const auto& key = rewardsHistory.front();
        if (key.height > lastHeight) {
            RevertOwnerBalances(view, key.owner, key.balances);
            continue;
        }

        for (const auto& account : rewardAccounts) {
            onPoolRewards(view, account, key.height, lastHeight,
                [&](int32_t height, DCT_ID poolId, RewardType type, CTokenAmount amount) {
                    if (tokenFilter.empty() || hasToken({{amount.nTokenId, amount.nValue}})) {
                        auto& array = ret.emplace(height, UniValue::VARR).first->second;
                        array.push_back(rewardhistoryToJSON(account, height, poolId, type, amount));
                        count ? --count : 0;
                    }
                }
            );
        }

        if (!count) break;
        lastHeight = key.height;
        RevertOwnerBalances(view, key.owner, key.balances);
    }

    if (shouldSearchInWallet) {
        count = limit;
        searchInWallet(pwallet, account, filter,
            [&](CBlockIndex const * index, CWalletTx const * pwtx) {
                uint32_t height = index->nHeight;
                return txs.count(pwtx->GetHash()) || startBlock > height || height > maxBlockHeight;
            },
            [&](COutputEntry const & entry, CBlockIndex const * index, CWalletTx const * pwtx) {
                uint32_t height = index->nHeight;
                uint32_t nIndex = pwtx->nIndex;
                if (txn != std::numeric_limits<uint32_t>::max() && height == maxBlockHeight && nIndex > txn) {
                    return true;
                }
                auto& array = ret.emplace(index->nHeight, UniValue::VARR).first->second;
                array.push_back(outputEntryToJSON(entry, index, pwtx));
                return --count != 0;
            }
        );
    }

    UniValue slice(UniValue::VARR);

    if (!ret.empty()) {
        LOCK(cs_main);
        for (auto it = ret.cbegin(); limit != 0 && it != ret.cend(); ++it) {
            const auto& array = it->second.get_array();
            for (size_t i = 0; limit != 0 && i < array.size(); ++i) {
                auto value = array[i];
                if (auto block = ::ChainActive()[it->first]) {
                    value.pushKV("blockHash", block->GetBlockHash().GetHex());
                    value.pushKV("blockTime", block->GetBlockTime());
                }
                slice.push_back(value);
                --limit;
            }
        }
    }

    return slice;
}

UniValue getaccounthistory(const JSONRPCRequest& request) {

    RPCHelpMan{"getaccounthistory",
               "\nReturns information about account history.\n",
               {
                    {"owner", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Single account ID (CScript or address)."},
                    {"blockHeight", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "Block Height to search in."},
                    {"txn", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "for order in block."},
               },
               RPCResult{
                       "{}  An object with account history information\n"
               },
               RPCExamples{
                       HelpExampleCli("getaccounthistory", "mxxA2sQMETJFbXcNbNbUzEsBCTn1JSHXST 103 2")
                       + HelpExampleCli("getaccounthistory", "mxxA2sQMETJFbXcNbNbUzEsBCTn1JSHXST, 103, 2")
               },
    }.Check(request);

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is needed for account history");
    }

    auto owner = DecodeScript(request.params[0].getValStr());
    uint32_t blockHeight = request.params[1].get_int();
    uint32_t txn = request.params[2].get_int();

    UniValue result(UniValue::VOBJ);
    AccountHistoryKey AccountKey{owner, blockHeight, txn};
    if (auto value = paccountHistoryDB->ReadAccountHistory(AccountKey)) {
        result = accounthistoryToJSON(AccountKey, *value);
    }

    return result;
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
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid tx type (" + str + ")");
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

    std::set<uint256> txs;
    CImmutableCSView view(*pcustomcsview);

    auto hasToken = [&](TAmounts const & diffs) {
        for (auto const & diff : diffs) {
            auto token = view.GetToken(diff.first);
            auto const tokenIdStr = token->CreateSymbolKey(diff.first);
            if(tokenIdStr == tokenFilter) {
                return true;
            }
        }
        return false;
    };

    uint32_t height = view.GetLastHeight();
    std::map<uint32_t, UniValue, std::greater<uint32_t>> ret;

    maxBlockHeight = std::min(maxBlockHeight, height);
    depth = std::min(depth, maxBlockHeight);

    auto count = limit;
    const auto startBlock = maxBlockHeight - depth;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, AccountHistoryValue const & value) -> bool
    {
        if (startBlock > key.blockHeight) {
            return false;
        }

        if (txTypeSearch && value.category != uint8_t(txType)) {
            return true;
        }

        if (!tokenFilter.empty() && !hasToken(value.diff)) {
            return true;
        }

        auto& array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(accounthistoryToJSON(key, value));

        return --count != 0;
    };

    CBurnHistoryStorage burnView(*pburnHistoryDB);

    burnView.ForEachAccountHistory(shouldContinueToNextAccountHistory, {}, maxBlockHeight);

    UniValue slice(UniValue::VARR);

    if (!ret.empty()) {
        LOCK(cs_main);
        for (auto it = ret.cbegin(); limit != 0 && it != ret.cend(); ++it) {
            const auto& array = it->second.get_array();
            for (size_t i = 0; limit != 0 && i < array.size(); ++i) {
                auto value = array[i];
                if (auto block = ::ChainActive()[it->first]) {
                    value.pushKV("blockHash", block->GetBlockHash().GetHex());
                    value.pushKV("blockTime", block->GetBlockTime());
                }
                slice.push_back(value);
                --limit;
            }
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
            if (txType == CustomTxType::None) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid tx type (" + str + ")");
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
    CImmutableCSView view(*pcustomcsview);
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && CustomTxType::None == txType;

    auto hasToken = [&](TAmounts const & diffs) {
        for (auto const & diff : diffs) {
            auto token = view.GetToken(diff.first);
            auto const tokenIdStr = token->CreateSymbolKey(diff.first);
            if(tokenIdStr == tokenFilter) {
                return true;
            }
        }
        return false;
    };

    uint64_t count = 0;
    const auto currentHeight = view.GetLastHeight();
    uint32_t lastHeight = currentHeight;

    std::set<CScript> rewardAccounts;
    std::queue<CRewardHistory> rewardsHistory;

    auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, CLazySerialize<AccountHistoryValue> lazy) -> bool {
        if (!owner.empty() && owner != key.owner) {
            return false;
        }

        if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
            return true;
        }

        auto& value = lazy.get();

        if (!noRewards) {
            auto& owner = *(rewardAccounts.insert(key.owner).first);
            rewardsHistory.push({key.blockHeight, owner, value.diff});
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

        return true;
    };

    CAccountHistoryStorage historyView(*paccountHistoryDB);

    historyView.ForEachAccountHistory(shouldContinueToNextAccountHistory, owner);

    for (; !rewardsHistory.empty(); rewardsHistory.pop()) {
        const auto& key = rewardsHistory.front();
        for (const auto& account : rewardAccounts) {
            onPoolRewards(view, account, key.height, lastHeight,
                [&](int32_t, DCT_ID, RewardType, CTokenAmount amount) {
                    if (tokenFilter.empty() || hasToken({{amount.nTokenId, amount.nValue}})) {
                        ++count;
                    }
                }
            );
        }
        lastHeight = key.height;
        RevertOwnerBalances(view, key.owner, key.balances);
    }

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

    CAmount burnt{0};
    CImmutableCSView view(*pcustomcsview);

    auto height = view.GetLastHeight();
    auto postFortCanningHeight = height >= Params().GetConsensus().FortCanningHeight;

    for (const auto& kv : Params().GetConsensus().newNonUTXOSubsidies)
    {
        // Skip these as any unused balance will be burnt.
        if (kv.first == CommunityAccountType::Options) {
            continue;
        }
        if (kv.first == CommunityAccountType::Unallocated ||
            kv.first == CommunityAccountType::IncentiveFunding) {
            burnt += view.GetCommunityBalance(kv.first);
            continue;
        }

        if (kv.first == CommunityAccountType::Loan) {
            if (postFortCanningHeight) {
                burnt += view.GetCommunityBalance(kv.first);
            }
            continue;
        }

        ret.pushKV(GetCommunityAccountName(kv.first), ValueFromAmount(view.GetCommunityBalance(kv.first)));
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

    CImmutableCSView view(*pcustomcsview);

    if (request.params[0].get_obj().empty()) { // autoselection
        CAccounts foundMineAccounts = GetAllMineAccounts(view, pwallet);

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

    int targetHeight = view.GetLastHeight() + 1;

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
    CAmount dfiPaybackFee{0};
    CBalances burntTokens;
    CBalances dexfeeburn;
    CBalances paybackfees;
    CBalances paybacktokens;
    CBalances dfi2203Tokens;

    UniValue dfipaybacktokens{UniValue::VARR};

    auto calcBurn = [&](AccountHistoryKey const & key, AccountHistoryValue const & value) -> bool
    {
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
        if (value.category == uint8_t(CustomTxType::PaybackLoan)
        || value.category == uint8_t(CustomTxType::PaybackLoanV2)) {
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

        // dex fee burn
        if (value.category == uint8_t(CustomTxType::PoolSwap)
        ||  value.category == uint8_t(CustomTxType::PoolSwapV2)) {
            for (auto const & diff : value.diff) {
                dexfeeburn.Add({diff.first, diff.second});
            }
            return true;
        }

        // Token burn
        for (auto const & diff : value.diff) {
            burntTokens.Add({diff.first, diff.second});
        }

        return true;
    };

    CBurnHistoryStorage burnView(*pburnHistoryDB);

    burnView.ForEachAccountHistory(calcBurn);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", ScriptToString(Params().GetConsensus().burnAddress));
    result.pushKV("amount", ValueFromAmount(burntDFI));

    result.pushKV("tokens", AmountsToJSON(burntTokens.balances));
    result.pushKV("feeburn", ValueFromAmount(burntFee));
    result.pushKV("auctionburn", ValueFromAmount(auctionFee));
    result.pushKV("paybackburn", ValueFromAmount(paybackFee));
    result.pushKV("dexfeetokens", AmountsToJSON(dexfeeburn.balances));

    CAmount burnt{0};
    CImmutableCSView view(*pcustomcsview);

    if (auto attributes = view.GetAttributes()) {
        CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
        auto tokenBalances = attributes->GetValue(liveKey, CBalances{});
        for (const auto& balance : tokenBalances.balances) {
            if (balance.first == DCT_ID{0}) {
                dfiPaybackFee = balance.second;
            } else {
                dfipaybacktokens.push_back(tokenAmountString({balance.first, balance.second}));
            }
        }
        liveKey = {AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackTokens};
        auto paybacks = attributes->GetValue(liveKey, CTokenPayback{});
        paybackfees = std::move(paybacks.tokensFee);
        paybacktokens = std::move(paybacks.tokensPayback);

        liveKey = {AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Burned};
        dfi2203Tokens = attributes->GetValue(liveKey, CBalances{});
    }

    result.pushKV("dfipaybackfee", ValueFromAmount(dfiPaybackFee));
    result.pushKV("dfipaybacktokens", dfipaybacktokens);

    result.pushKV("paybackfees", AmountsToJSON(paybackfees.balances));
    result.pushKV("paybacktokens", AmountsToJSON(paybacktokens.balances));

    auto height = view.GetLastHeight();
    auto postFortCanningHeight = height >= Params().GetConsensus().FortCanningHeight;

    for (const auto& kv : Params().GetConsensus().newNonUTXOSubsidies) {
        if (kv.first == CommunityAccountType::Unallocated || kv.first == CommunityAccountType::IncentiveFunding ||
        (postFortCanningHeight && kv.first == CommunityAccountType::Loan)) {
            burnt += view.GetCommunityBalance(kv.first);
        }
    }
    result.pushKV("emissionburn", ValueFromAmount(burnt));
    result.pushKV("dfip2203", AmountsToJSON(dfi2203Tokens.balances));

    return result;
}

UniValue getcustomtxcodes(const JSONRPCRequest& request) {
    RPCHelpMan{"getcustomtxcodes",
               "\nList all available custom transaction types.\n",
               {
               },
               RPCResult{
                       "{\"1\": \"ICXCreateOrder\", \"2\": \"ICXMakeOffer\", ...}     (object) List of custom transaction types { [single letter representation]: custom transaction type name}\n"
               },
               RPCExamples{
                       HelpExampleCli("getcustomtxcodes", "")
                       + HelpExampleRpc("getcustomtxcodes", "")
               },
    }.Check(request);

    UniValue typeObj(UniValue::VOBJ);
    for (auto i = 0; i < std::numeric_limits<uint8_t>::max(); i++) {
        auto type = CustomTxCodeToType(i);
        if (type != CustomTxType::None && type != CustomTxType::Reject) {
            typeObj.pushKV(std::string(1, i), ToString(type));
        }
    }
    return typeObj;
}

UniValue HandleSendDFIP2201DFIInput(const JSONRPCRequest& request, CWalletCoinsUnlocker pwallet,
        const std::pair<std::string, CScript>& contractPair, CTokenAmount amount) {
    CUtxosToAccountMessage msg{};
    msg.to = {{contractPair.second, {{{{0}, amount.nValue}}}}};

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UtxosToAccount)
                << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(amount.nValue, scriptMeta));

    // change
    CCoinControl coinControl;
    CTxDestination dest;
    ExtractDestination(Params().GetConsensus().foundationShareScript, dest);
    coinControl.destChange = dest;

    // Only use inputs from dest
    coinControl.matchDestination = dest;

    // fund
    fund(rawTx, pwallet, {}, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight);

    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue HandleSendDFIP2201BTCInput(const JSONRPCRequest& request, CWalletCoinsUnlocker pwallet,
         const std::pair<std::string, CScript>& contractPair, CTokenAmount amount) {
    if (request.params[2].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "BTC source address must be provided for " + contractPair.first);
    }
    CTxDestination dest = DecodeDestination(request.params[2].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    const auto script = GetScriptForDestination(dest);

    CSmartContractMessage msg{};
    msg.name = contractPair.first;
    msg.accounts = {{script, {{{amount.nTokenId, amount.nValue}}}}};
    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SmartContract)
                << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auth{script};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auth, false, optAuthTx, request.params[3]);

    // Set change address
    CCoinControl coinControl;
    coinControl.destChange = dest;

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue HandleSendDFIP2201(const JSONRPCRequest& request, CWalletCoinsUnlocker pwallet) {
    auto contracts = Params().GetConsensus().smartContracts;
    const auto& contractPair = contracts.find(SMART_CONTRACT_DFIP_2201);
    assert(contractPair != contracts.end());

    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[1].get_str(), "amount");

    if (amount.nTokenId.v == 0) {
        return HandleSendDFIP2201DFIInput(request, std::move(pwallet), *contractPair, amount);
    } else {
        return HandleSendDFIP2201BTCInput(request, std::move(pwallet), *contractPair, amount);
    }
}

UniValue executesmartcontract(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"executesmartcontract",
               "\nCreates and sends a transaction to either fund or execute a smart contract. Available contracts: dbtcdfiswap" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Name of the smart contract to send funds to"},
                       {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount to send in amount@token format"},
                       {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address to be used in contract execution if required"},
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
                       HelpExampleCli("executesmartcontract", "dbtcdfiswap 1000@DFI")
                       + HelpExampleRpc("executesmartcontract", "dbtcdfiswap, 1000@DFI")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const auto& contractName = request.params[0].get_str();
    if (contractName == "dbtcdfiswap") {
        return HandleSendDFIP2201(request, std::move(pwallet));
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Specified smart contract not found");
    }
    return NullUniValue;
}

UniValue futureswap(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"futureswap",
               "\nCreates and submits to the network a futures contract" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to fund contract and receive resulting token"},
                       {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount to send in amount@token format"},
                       {"destination", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Expected dToken if DUSD supplied"},
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
                       HelpExampleCli("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ 1000@TSLA")
                       + HelpExampleCli("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ 1000@DUSD TSLA")
                       + HelpExampleRpc("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ, 1000@TSLA")
                       + HelpExampleRpc("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ, 1000@DUSD, TSLA")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const auto dest = DecodeDestination(request.params[0].getValStr());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    CFutureSwapMessage msg{};
    msg.owner = GetScriptForDestination(dest);
    msg.source = DecodeAmount(pwallet->chain(), request.params[1], "");

    if (!request.params[2].isNull()) {
        DCT_ID destTokenID{};

        const auto destToken = pcustomcsview->GetTokenGuessId(request.params[2].getValStr(), destTokenID);
        if (!destToken) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Destination token not found");
        }

        msg.destination = destTokenID.v;
    }

    // Encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::DFIP2203)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auth{msg.owner};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auth, false, optAuthTx, request.params[3]);

    // Set change address
    CCoinControl coinControl;
    coinControl.destChange = dest;

    // Fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // Check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue withdrawfutureswap(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"withdrawfutureswap",
               "\nCreates and submits to the network a withdrawl from futures contract transaction.\n"
               " Withdrawal will be back to the address specified in the futures contract." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address used to fund contract with"},
                       {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount to withdraw in amount@token format"},
                       {"destination", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "The dToken if DUSD supplied"},
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
                       HelpExampleCli("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ 1000@TSLA")
                       + HelpExampleRpc("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ, 1000@TSLA")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const auto dest = DecodeDestination(request.params[0].getValStr());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    CFutureSwapMessage msg{};
    msg.owner = GetScriptForDestination(dest);
    msg.source = DecodeAmount(pwallet->chain(), request.params[1], "");
    msg.withdraw = true;

    if (!request.params[2].isNull()) {
        DCT_ID destTokenID{};

        const auto destToken = pcustomcsview->GetTokenGuessId(request.params[2].getValStr(), destTokenID);
        if (!destToken) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Destination token not found");
        }

        msg.destination = destTokenID.v;
    }

    // Encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::DFIP2203)
             << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = pcustomcsview->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auth{msg.owner};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auth, false, optAuthTx, request.params[3]);

    // Set change address
    CCoinControl coinControl;
    coinControl.destChange = dest;

    // Fund
    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // Check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listpendingfutureswaps(const JSONRPCRequest& request) {
    RPCHelpMan{"listpendingfutureswaps",
               "Get all pending futures.\n",
               {},
               RPCResult{
                       "\"json\"          (string) array containing json-objects having following fields:\n"
                       "    owner :       \"address\"\n"
                       "    values : [{\n"
                       "        tokenSymbol : \"SYMBOL\"\n"
                       "        amount :      n.nnnnnnnn\n"
                       "        destination : \"SYMBOL\"\n"
                       "    }...]\n"
               },
               RPCExamples{
                       HelpExampleCli("listpendingfutureswaps", "")
               },
    }.Check(request);

    UniValue listFutures{UniValue::VARR};
    CImmutableCSView futureSwapView(*pfutureSwapView);
    CImmutableCSView view(*pcustomcsview);

    futureSwapView.ForEachFuturesUserValues([&](const CFuturesUserKey& key, const CFuturesUserValue& futuresValues){
        CTxDestination dest;
        ExtractDestination(key.owner, dest);
        if (!IsValidDestination(dest))
            return true;

        const auto source = view.GetToken(futuresValues.source.nTokenId);
        if (!source)
            return true;

        UniValue value{UniValue::VOBJ};
        value.pushKV("owner", EncodeDestination(dest));
        value.pushKV("source", tokenAmountString(futuresValues.source));

        if (source->symbol == "DUSD") {
            const auto destination = view.GetLoanTokenByID({futuresValues.destination});
            if (!destination)
                return true;

            value.pushKV("destination", destination->symbol);
        } else
            value.pushKV("destination", "DUSD");

        listFutures.push_back(value);

        return true;
    });

    return listFutures;
}

UniValue getpendingfutureswaps(const JSONRPCRequest& request) {
    RPCHelpMan{"getpendingfutureswaps",
               "Get specific pending futures.\n",
                {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to get all pending future swaps"},
                },
                RPCResult{
                    "{\n"
                    "    owner :       \"address\"\n"
                    "    values : [{\n"
                    "    tokenSymbol : \"SYMBOL\"\n"
                    "    amount :      n.nnnnnnnn\n"
                    "    destination : \"SYMBOL\"\n"
                    "    }...]\n"
                    "}\n"
               },
               RPCExamples{
                       HelpExampleCli("getpendingfutureswaps", "address")
               },
    }.Check(request);

    const auto owner = DecodeScript(request.params[0].get_str());

    UniValue listValues{UniValue::VARR};
    CImmutableCSView futureSwapView(*pfutureSwapView);
    CImmutableCSView view(*pcustomcsview);

    std::vector<CFuturesUserKey> ownerEntries;
    futureSwapView.ForEachFuturesCScript([&](const CFuturesCScriptKey& key, const std::string&){
        if (key.owner != owner) {
            return false;
        }

        ownerEntries.push_back({key.height, key.owner, key.txn});

        return true;
    }, CFuturesCScriptKey{owner, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()});

    for (const auto& entry : ownerEntries) {
        const auto resVal = futureSwapView.GetFuturesUserValues(entry);
        if (!resVal)
            continue;

        const auto& futureValue = *resVal;
        UniValue value{UniValue::VOBJ};

        const auto source = view.GetToken(futureValue.source.nTokenId);
        if (!source)
            continue;

        value.pushKV("source", tokenAmountString(futureValue.source));

        if (source->symbol == "DUSD") {
            const auto destination = view.GetLoanTokenByID({futureValue.destination});
            if (!destination)
                continue;

            value.pushKV("destination", destination->symbol);
        } else {
            value.pushKV("destination", "DUSD");
        }

        listValues.push_back(value);
    }

    UniValue obj{UniValue::VOBJ};
    obj.pushKV("owner", ScriptToString(owner));
    obj.pushKV("values", listValues);
    return obj;
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
    {"accounts",    "getaccounthistory",     &getaccounthistory,     {"owner", "blockHeight", "txn"}},
    {"accounts",    "listburnhistory",       &listburnhistory,       {"options"}},
    {"accounts",    "accounthistorycount",   &accounthistorycount,   {"owner", "options"}},
    {"accounts",    "listcommunitybalances", &listcommunitybalances, {}},
    {"accounts",    "sendtokenstoaddress",   &sendtokenstoaddress,   {"from", "to", "selectionMode"}},
    {"accounts",    "getburninfo",           &getburninfo,           {}},
    {"accounts",    "executesmartcontract",  &executesmartcontract,  {"name", "amount", "inputs"}},
    {"accounts",    "getcustomtxcodes",      &getcustomtxcodes,      {}},
    {"accounts",    "futureswap",            &futureswap,            {"address", "amount", "destination", "inputs"}},
    {"accounts",    "withdrawfutureswap",    &withdrawfutureswap,    {"address", "amount", "destination", "inputs"}},
    {"accounts",    "listpendingfutureswaps",&listpendingfutureswaps,{}},
    {"accounts",    "getpendingfutureswaps", &getpendingfutureswaps, {"address"}},
};

void RegisterAccountsRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

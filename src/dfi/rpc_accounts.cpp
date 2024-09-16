#include <ain_rs_exports.h>
#include <dfi/accountshistory.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/mn_rpc.h>
#include <dfi/threadpool.h>
#include <dfi/validation.h>
#include <dfi/vaulthistory.h>
#include <ffi/ffihelpers.h>
#include <boost/asio.hpp>

static bool DEFAULT_DVM_OWNERSHIP_CHECK = true;

std::string tokenAmountString(const CCustomCSView &view,
                              const CTokenAmount &amount,
                              AmountFormat format = AmountFormat::Symbol) {
    const auto token = view.GetToken(amount.nTokenId);
    if (!token) {
        return {};
    }
    const auto amountString = ValueFromAmount(amount.nValue).getValStr();

    std::string tokenStr = {};
    switch (format) {
        case AmountFormat::Id:
            tokenStr = amount.nTokenId.ToString();
            break;
        case AmountFormat::Symbol:
            tokenStr = token->CreateSymbolKey(amount.nTokenId);
            break;
        case AmountFormat::Combined:
            tokenStr = amount.nTokenId.ToString() + "#" + token->CreateSymbolKey(amount.nTokenId);
            break;
        case AmountFormat::Unknown:
            tokenStr = "unknown";
            break;
    }
    return amountString + "@" + tokenStr;
}

UniValue AmountsToJSON(const CCustomCSView &view, const TAmounts &diffs, AmountFormat format = AmountFormat::Symbol) {
    UniValue obj(UniValue::VARR);

    for (const auto &diff : diffs) {
        obj.push_back(tokenAmountString(view, {diff.first, diff.second}, format));
    }
    return obj;
}

UniValue accountToJSON(const CCustomCSView &view,
                       const CScript &owner,
                       const CTokenAmount &amount,
                       bool verbose,
                       bool indexed_amounts,
                       AmountFormat format = AmountFormat::Symbol) {
    // encode CScript into JSON
    UniValue ownerObj(UniValue::VOBJ);
    ScriptPubKeyToUniv(owner, ownerObj, true);
    if (!verbose) {  // cut info
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
    } else {
        obj.pushKV("amount", tokenAmountString(view, amount, format));
    }

    return obj;
}

UniValue accounthistoryToJSON(const CCustomCSView &view,
                              const AccountHistoryKey &key,
                              const AccountHistoryValue &value,
                              AmountFormat format = AmountFormat::Symbol) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("owner", ScriptToString(key.owner));
    obj.pushKV("blockHeight", (uint64_t)key.blockHeight);
    {
        LOCK(cs_main);
        if (auto block = ::ChainActive()[key.blockHeight]) {
            obj.pushKV("blockHash", block->GetBlockHash().GetHex());
            obj.pushKV("blockTime", block->GetBlockTime());
        }
    }
    obj.pushKV("type", ToString(CustomTxCodeToType(value.category)));
    obj.pushKV("txn", (uint64_t)key.txn);
    obj.pushKV("txid", value.txid.ToString());
    obj.pushKV("amounts", AmountsToJSON(view, value.diff, format));
    return obj;
}

UniValue rewardhistoryToJSON(const CCustomCSView &view,
                             const CScript &owner,
                             uint32_t height,
                             DCT_ID const &poolId,
                             RewardType type,
                             CTokenAmount amount,
                             AmountFormat format = AmountFormat::Id) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("owner", ScriptToString(owner));
    obj.pushKV("blockHeight", (uint64_t)height);
    {
        LOCK(cs_main);
        if (auto block = ::ChainActive()[height]) {
            obj.pushKV("blockHash", block->GetBlockHash().GetHex());
            obj.pushKV("blockTime", block->GetBlockTime());
        }
    }
    obj.pushKV("type", RewardToString(type));
    if (type & RewardType::Rewards) {
        obj.pushKV("rewardType", RewardTypeToString(type));
    }
    obj.pushKV("poolID", poolId.ToString());
    TAmounts amounts({
        {amount.nTokenId, amount.nValue}
    });
    obj.pushKV("amounts", AmountsToJSON(view, amounts, format));
    return obj;
}

UniValue outputEntryToJSON(const CCustomCSView &view,
                           const COutputEntry &entry,
                           const CBlockIndex *index,
                           const CWalletTx *pwtx,
                           AmountFormat format = AmountFormat::Symbol) {
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
    obj.pushKV("txn", (uint64_t)pwtx->nIndex);
    obj.pushKV("txid", pwtx->GetHash().ToString());
    TAmounts amounts({
        {DCT_ID{0}, entry.amount}
    });
    obj.pushKV("amounts", AmountsToJSON(view, amounts, format));
    return obj;
}

static void onPoolRewards(CCustomCSView &view,
                          const CScript &owner,
                          const uint32_t begin,
                          const uint32_t end,
                          std::function<void(uint32_t, DCT_ID, RewardType, CTokenAmount)> addToResult) {
    CCustomCSView mnview(view);
    static const uint32_t eunosHeight = Params().GetConsensus().DF8EunosHeight;

    view.ForEachPoolId([&](DCT_ID const &poolId) {
        const auto height = view.GetShare(poolId, owner);
        if (!height || *height >= end) {
            return true;  // no share or target height is before a pool share' one
        }
        auto onLiquidity = [&]() -> CAmount { return mnview.GetBalance(owner, poolId).nValue; };
        uint32_t firstHeight{};
        const auto beginHeight = std::max(*height, begin);

        auto onReward = [&](RewardType type, CTokenAmount amount, uint32_t height) {
            if (amount.nValue == 0) {
                return;
            }
            addToResult(height, poolId, type, amount);
            // prior Eunos account balance includes rewards
            // thus we don't need to increment it by first one
            if (!firstHeight) {
                firstHeight = height;
            }
            if (height >= eunosHeight || firstHeight != height) {
                mnview.AddBalance(owner, amount);  // update owner liquidity
            }
        };

        if (beginHeight < Params().GetConsensus().DF24Height) {
            // Calculate just up to the fork height
            const auto endNewHeight =
                end >= Params().GetConsensus().DF24Height ? Params().GetConsensus().DF24Height : end;
            view.CalculatePoolRewards(poolId, onLiquidity, beginHeight, endNewHeight, onReward);
        }

        if (end >= Params().GetConsensus().DF24Height) {
            // Calculate from the fork height
            auto beginNewHeight = beginHeight < Params().GetConsensus().DF24Height
                                      ? Params().GetConsensus().DF24Height - 1
                                      : beginHeight - 1;
            // End must be above start and then one more beyond the range.
            auto newEndHeight = beginNewHeight + 2;
            // Loop over one block a time to build account history with correct height records
            for (; newEndHeight <= end; ++beginNewHeight, ++newEndHeight) {
                view.CalculateStaticPoolRewards(onLiquidity, onReward, poolId.v, beginNewHeight, newEndHeight);
            }
        }

        return true;
    });
}

static void searchInWallet(const CWallet *pwallet,
                           const CScript &account,
                           isminetype filter,
                           std::function<bool(const CBlockIndex *, const CWalletTx *)> shouldSkipTx,
                           std::function<bool(const COutputEntry &, const CBlockIndex *, const CWalletTx *)> txEntry) {
    CTxDestination destination;
    ExtractDestination(account, destination);

    CAmount nFee;
    std::list<COutputEntry> listSent;
    std::list<COutputEntry> listReceived;

    auto locked_chain = pwallet->chain().lock();
    LOCK2(pwallet->cs_wallet, locked_chain->mutex());

    const auto &txOrdered = pwallet->mapWallet.get<ByOrder>();

    for (auto it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
        auto *pwtx = &(*it);

        auto index = LookupBlockIndex(pwtx->hashBlock);
        if (!index || index->nHeight == 0) {  // skip genesis block
            continue;
        }

        if (shouldSkipTx(index, pwtx)) {
            continue;
        }

        if (!pwtx->IsTrusted(*locked_chain)) {
            continue;
        }

        pwtx->GetAmounts(listReceived, listSent, nFee, filter);

        for (auto &sent : listSent) {
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

        for (const auto &recv : listReceived) {
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

static CScript hexToScript(const std::string &str) {
    if (!IsHex(str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct hex:\n");
    }
    const auto raw = ParseHex(str);
    return CScript{raw.begin(), raw.end()};
}

static BalanceKey decodeBalanceKey(const std::string &str) {
    const auto pair = SplitAmount(str);
    DCT_ID tokenID{};
    if (!pair.second.empty()) {
        auto id = DCT_ID::FromString(pair.second);
        if (!id.ok) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "(" + str + ") doesn't represent a correct balance key:\n" + id.msg);
        }
        tokenID = *id.val;
    }
    return {hexToScript(pair.first), tokenID};
}

static UniValue DecodeRecipientsGetRecipients(const UniValue &values) {
    UniValue recipients(UniValue::VOBJ);
    for (const auto &key : values.getKeys()) {
        recipients.pushKV(key, values[key]);
    }
    return recipients;
}

static CAccounts DecodeRecipientsDefaultInternal(CWallet *const pwallet,
                                                 const UniValue &values,
                                                 bool checkOwnership = true) {
    const auto recipients = DecodeRecipientsGetRecipients(values);
    auto accounts = DecodeRecipients(pwallet->chain(), recipients);
    if (!checkOwnership) {
        return accounts;
    }
    for (const auto &account : accounts) {
        if (IsMineCached(*pwallet, account.first) != ISMINE_SPENDABLE &&
            account.second.balances.find(DCT_ID{0}) != account.second.balances.end()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               strprintf("The address (%s) is not your own address", ScriptToString(account.first)));
        }
    }
    return accounts;
}

static AccountSelectionMode ParseAccountSelectionParam(const std::string selectionParam) {
    if (selectionParam == "forward") {
        return SelectionForward;
    } else if (selectionParam == "crumbs") {
        return SelectionCrumbs;
    } else if (selectionParam == "pie") {
        return SelectionPie;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalide accounts selection mode.");
    }
}

UniValue listaccounts(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "listaccounts",
        "\nReturns information about all accounts on chain.\n",
        {
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "Optional first key to iterate from, in lexicographical order."
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
            }, {"verbose",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Flag for verbose list (default = true), otherwise limited objects are listed"},
          {"indexed_amounts",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Format of amounts output (default = false): (true: {tokenid:amount}, false: \"amount@tokenid\")"},
          {"is_mine_only",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Get balances about all accounts belonging to the wallet"},
          },
        RPCResult{"{id:{...},...}     (array) Json object with accounts information\n"},
        RPCExamples{HelpExampleCli("listaccounts", "") + HelpExampleRpc("listaccounts", "'{}' false") +
                    HelpExampleRpc("listaccounts",
          "'{\"start\":\"a914b12ecde1759f792e0228e4fa6d262902687ca7eb87@0\","
                                   "\"limit\":100"
                                   "}'")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    // parse pagination
    size_t limit = 100;
    BalanceKey start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
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

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    CalcMissingRewardTempFix(*view, targetHeight, *pwallet);

    view->ForEachAccount(
        [&, &view = view](const CScript &account) {
            if (isMineOnly && IsMineCached(*pwallet, account) != ISMINE_SPENDABLE) {
                return true;
            }

            view->CalculateOwnerRewards(account, targetHeight);

            // output the relavant balances only for account
            view->ForEachBalance(
                [&](CScript const &owner, CTokenAmount balance) {
                    if (account != owner) {
                        return false;
                    }
                    ret.push_back(accountToJSON(*view, owner, balance, verbose, indexed_amounts));
                    return --limit != 0;
                },
                {account, start.tokenID});

            start.tokenID = DCT_ID{};  // reset to start id
            return limit != 0;
        },
        start.owner);

    return GetRPCResultCache().Set(request, ret);
}

UniValue getaccount(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getaccount",
        "\nReturns information about account.\n",
        {
          {"owner", RPCArg::Type::STR, RPCArg::Optional::NO, "Owner address in base58/bech32/hex encoding"},
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "Optional first key to iterate from, in lexicographical order."
                     "Typically it's set to last tokenID from previous request."},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of orders to return, 100 by default"},
                },
            }, {"indexed_amounts",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Format of amounts output (default = false): (true: obj = {tokenid:amount,...}, false: array = "
             "[\"amount@tokenid\"...])"},
          },
        RPCResult{"{...}     (array) Json object with order information\n"},
        RPCExamples{HelpExampleCli("getaccount", "owner_address")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    const auto userAddress = request.params[0].get_str();

    // decode owner
    CScript reqOwner;
    if (IsHex(userAddress)) {  // ScriptPubKey
        const auto hexVec = ParseHex(userAddress);
        reqOwner = CScript(hexVec.begin(), hexVec.end());
        CTxDestination owner;
        if (!ExtractDestination(reqOwner, owner) || !IsValidDestination(owner)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner address");
        }
    } else {  // Address
        const auto owner = DecodeDestination(userAddress);
        if (!IsValidDestination(owner)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner address");
        }
        reqOwner = GetScriptForDestination(owner);
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start = {};
    bool including_start = true;
    {
        if (request.params.size() > 1) {
            UniValue paginationObj = request.params[1].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t)paginationObj["start"].get_int64();
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

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    view->CalculateOwnerRewards(reqOwner, targetHeight);

    std::map<DCT_ID, CAmount> balances{};
    CTxDestination dest;
    if (ExtractDestination(reqOwner, dest) && dest.index() == WitV16KeyEthHashType) {
        const auto keyID = std::get<WitnessV16EthHash>(dest);
        auto r = XResultValue(evm_try_get_balance(result, keyID.GetByteArray()));
        if (!r) {
            throw JSONRPCError(RPC_MISC_ERROR, r.msg);
        }
        if (const auto balance = *r) {
            balances[DCT_ID{}] = balance;
        }
    }

    view->ForEachBalance(
        [&](const CScript &owner, CTokenAmount balance) {
            if (owner != reqOwner) {
                return false;
            }

            balances[balance.nTokenId] += balance.nValue;

            limit--;
            return limit != 0;
        },
        BalanceKey{reqOwner, start});

    for (const auto &[id, amount] : balances) {
        if (indexed_amounts) {
            ret.pushKV(id.ToString(), ValueFromAmount(amount));
        } else {
            ret.push_back(tokenAmountString(*view, {id, amount}));
        }
    }

    return GetRPCResultCache().Set(request, ret);
}

UniValue gettokenbalances(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "gettokenbalances",
        "\nReturns the balances of all accounts that belong to the wallet.\n",
        {
          {
                "pagination",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"start",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "Optional first key to iterate from, in lexicographical order."
                     "Typically it's set to last tokenID from previous request."},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of tokens to return, 100 by default"},
                },
            }, {"indexed_amounts",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Format of amounts output (default = false): (true: obj = {tokenid:amount,...}, false: array = "
             "[\"amount@tokenid\"...])"},
          {"symbol_lookup",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Use token symbols in output (default = false)"},
          {"evm",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Include DFI balances in the EVM layer (default = false): Note: This does not include DST20 tokens"},
          },
        RPCResult{"{...}     (array) Json object with balances information\n"},
        RPCExamples{HelpExampleCli("gettokenbalances", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t)paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t)paginationObj["start"].get_int64();
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
    auto evm_dfi_lookup = false;
    if (request.params.size() > 3) {
        evm_dfi_lookup = request.params[3].getBool();
    }

    UniValue ret(UniValue::VARR);
    if (indexed_amounts) {
        ret.setObject();
    }

    CBalances totalBalances;

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    CalcMissingRewardTempFix(*view, targetHeight, *pwallet);

    view->ForEachAccount([&, &view = view](const CScript &account) {
        if (IsMineCached(*pwallet, account) == ISMINE_SPENDABLE) {
            view->CalculateOwnerRewards(account, targetHeight);
            view->ForEachBalance([&](CScript const &owner,
                                     CTokenAmount balance) { return account == owner && totalBalances.Add(balance); },
                                 {account, DCT_ID{}});
        }
        return true;
    });

    if (evm_dfi_lookup) {
        for (const auto keyID : pwallet->GetKeys()) {
            // TODO: Use GetHex when eth key is fixed to be stored in LE
            auto res = XResultValue(evm_try_get_balance(result, keyID.GetByteArray()));
            if (res) {
                auto evmAmount = *res;
                totalBalances.Add({{}, static_cast<CAmount>(evmAmount)});
            } else {
                throw JSONRPCError(RPC_MISC_ERROR, res.msg);
            }
        }
    }

    auto it = totalBalances.balances.lower_bound(start);
    for (size_t i = 0; it != totalBalances.balances.end() && i < limit; it++, i++) {
        auto bal = CTokenAmount{(*it).first, (*it).second};
        std::string tokenIdStr = bal.nTokenId.ToString();
        if (symbol_lookup) {
            auto token = view->GetToken(bal.nTokenId);
            tokenIdStr = token->CreateSymbolKey(bal.nTokenId);
        }
        if (indexed_amounts) {
            ret.pushKV(tokenIdStr, ValueFromAmount(bal.nValue));
        } else {
            ret.push_back(ValueFromAmount(bal.nValue).getValStr() + "@" + tokenIdStr);
        }
    }
    return GetRPCResultCache().Set(request, ret);
}

UniValue utxostoaccount(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "utxostoaccount",
        "\nCreates (and submits to local node and network) a transfer transaction from the wallet UTXOs to specfied "
        "account.\n"
        "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {
                "amounts",
                RPCArg::Type::OBJ,
                RPCArg::Optional::NO,
                "",
                {{"address",
                  RPCArg::Type::STR,
                  RPCArg::Optional::NO,
                  "The defi address is the key, the value is amount in amount@token format. "
                  "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"}},
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
        RPCExamples{HelpExampleCli("utxostoaccount",
                          "'{\"address1\":\"1.0@DFI\","
                                   "\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]"
                                   "}' '[]'")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, false);

    // decode recipients
    CUtxosToAccountMessage msg{};
    auto ownershipCheck = gArgs.GetBoolArg("-dvmownerskipcheck", DEFAULT_DVM_OWNERSHIP_CHECK);
    msg.to = DecodeRecipientsDefaultInternal(pwallet, request.params[0].get_obj(), ownershipCheck);

    for (const auto &[to, amount] : msg.to) {
        RejectErc55Address(to);
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UtxosToAccount) << msg;
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

    for (const auto &kv : toBurn.balances) {
        if (rawTx.vout.empty()) {  // first output is metadata
            rawTx.vout.push_back(CTxOut(kv.second, scriptMeta, kv.first));
        } else {
            rawTx.vout.push_back(CTxOut(kv.second, scriptBurn, kv.first));
        }
    }

    bool funded = false;
    if (msg.to.size() == 1) {
        CCoinControl coinControl;
        // try to fill inputs from target address and use it as change
        const auto &target = msg.to.begin()->first;
        CTxDestination dest;
        ExtractDestination(target, dest);
        if (IsMineCached(*pwallet, target) == ISMINE_SPENDABLE) {
            if (IsValidDestination(dest)) {
                coinControl.destChange = dest;
                coinControl.matchDestination = dest;
                try {
                    fund(rawTx, pwallet, {}, &coinControl, request.metadata.coinSelectOpts);
                    funded = true;
                } catch (const UniValue &e) {
                    // likely not enough utxos, silent catch and try again outside
                }
            }
        }
    }
    // fund
    if (!funded) {
        fund(rawTx, pwallet, {}, nullptr, request.metadata.coinSelectOpts);
    }
    // check execution
    execTestTx(CTransaction(rawTx), targetHeight);

    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue sendutxosfrom(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "sendutxosfrom",
        "\nSend a transaction using UTXOs from the specfied address.\n" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of sender"},
                                                    {"to", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of receiver"},
                                                    {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "The amount to send"},
                                                    {"change",
             RPCArg::Type::STR,
             RPCArg::Optional::OMITTED,
             "The address to send change to (Default: from address)"},
                                                    },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{HelpExampleCli("sendutxosfrom", R"("from" "to" 100)") +
                    HelpExampleRpc("sendutxosfrom", R"("from", "to", 100")")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
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

    CTransactionRef tx =
        SendMoney(*locked_chain, pwallet, toDest, nAmount, {0}, false /* fSubtractFeeFromAmount */, coin_control, {});
    return tx->GetHash().GetHex();
}

UniValue accounttoaccount(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "accounttoaccount",
        "\nCreates (and submits to local node and network) a transfer transaction from the specified account to the "
        "specfied accounts.\n"
        "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                          {
                "to",
                RPCArg::Type::OBJ,
                RPCArg::Optional::NO,
                "",
                {
                    {"address",
                     RPCArg::Type::STR,
                     RPCArg::Optional::NO,
                     "The defi address is the key, the value is amount in amount@token format. "
                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
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
        RPCExamples{HelpExampleCli("accounttoaccount",
                          "sender_address "
                                   "'{\"address1\":\"1.0@DFI\",\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]}' "
                                   "'[]'")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, false);

    // decode sender and recipients
    CAccountToAccountMessage msg{};
    auto ownershipCheck = gArgs.GetBoolArg("-dvmownerskipcheck", DEFAULT_DVM_OWNERSHIP_CHECK);
    msg.to = DecodeRecipientsDefaultInternal(pwallet, request.params[1].get_obj(), ownershipCheck);

    if (SumAllTransfers(msg.to).balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    msg.from = DecodeScript(request.params[0].get_str());

    for (const auto &[to, amount] : msg.to) {
        RejectErc55Address(to);
    }
    RejectErc55Address(msg.from);

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToAccount) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    const UniValue &txInputs = request.params[2];

    auto [view, accountView, vaultView] = GetSnapshots();

    CTransactionRef optAuthTx;
    std::set<CScript> auths{msg.from};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(msg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue accounttoutxos(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "accounttoutxos",
        "\nCreates (and submits to local node and network) a transfer transaction from the specified account to "
        "UTXOs.\n"
        "The third optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                          {
                "to",
                RPCArg::Type::OBJ,
                RPCArg::Optional::NO,
                "",
                {
                    {"address",
                     RPCArg::Type::STR,
                     RPCArg::Optional::NO,
                     "The defi address is the key, the value is amount in amount@token format. "
                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
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
                          HelpExampleCli("accounttoutxos", "sender_address '{\"address1\":\"100@DFI\"}' '[]'") +
            HelpExampleCli("accounttoutxos",
                          "sender_address '{\"address1\":\"1.0@DFI\",\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]}' '[]'")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, true);

    // decode sender and recipients
    CAccountToUtxosMessage msg{};
    msg.from = DecodeScript(request.params[0].get_str());
    RejectErc55Address(msg.from);
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
        for (const auto &recip : to) {
            for (const auto &amount : recip.second.balances) {
                if (amount.second != 0) {
                    CTxOut out{amount.second, recip.first, amount.first};
                    dummyMetadata << out << padding;
                    LogPrint(BCLog::ESTIMATEFEE,
                             "%s: out size %d padding %d\n",
                             __func__,
                             sizeof(out),
                             sizeof(unsigned char) * padding.size());
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
    auto [view, accountView, vaultView] = GetSnapshots();
    const UniValue &txInputs = request.params[2];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{msg.from};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(msg.from, dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // re-encode with filled mintingOutputsStart
    {
        scriptMeta = {};
        msg.mintingOutputsStart = rawTx.vout.size();
        CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToUtxos) << msg;
        scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    }
    rawTx.vout[0].scriptPubKey = scriptMeta;

    // add outputs starting from mintingOutputsStart (must be unfunded, because it's minting)
    for (const auto &recip : to) {
        for (const auto &amount : recip.second.balances) {
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
    CCustomCSView &view;
    const CScript &owner;
    const TAmounts &balances;

public:
    CScopeAccountReverter(CCustomCSView &view, const CScript &owner, const TAmounts &balances)
        : view(view),
          owner(owner),
          balances(balances) {}

    ~CScopeAccountReverter() {
        for (const auto &balance : balances) {
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

UniValue listaccounthistory(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "listaccounthistory",
        "\nReturns information about account history.\n",
        {
          {"owner",
             RPCArg::Type::STR,
             RPCArg::Optional::OMITTED,
             "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned "
             "accounts or \"all\" to list whole DB (default = \"mine\")."},
          {
                "options",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"maxBlockHeight",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Optional height to iterate from (downto genesis block), (default = chaintip)."},
                    {"depth",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum depth, from the genesis block is the default"},
                    {"no_rewards", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Filter out rewards"},
                    {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by token"},
                    {
                        "txtype",
                        RPCArg::Type::STR,
                        RPCArg::Optional::OMITTED,
                        "Filter by transaction type, supported letter from {CustomTxType}. Ignored if txtypes is "
                        "provided",
                    },
                    {"txtypes",
                     RPCArg::Type::ARR,
                     RPCArg::Optional::OMITTED,
                     "Filter multiple transaction types, supported letter from {CustomTxType}",
                     {
                         {
                             "Transaction Type",
                             RPCArg::Type::STR,
                             RPCArg::Optional::OMITTED,
                             "letter from {CustomTxType}",
                         },
                     }},
                    {"limit",
                     RPCArg::Type::NUM,
                     RPCArg::Optional::OMITTED,
                     "Maximum number of records to return, 100 by default"},
                    {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Number of entries to skip"},
                    {"including_start",
                     RPCArg::Type::BOOL,
                     RPCArg::Optional::OMITTED,
                     "If true, then iterate including starting position. False by default"},
                    {"txn", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Order in block, unlimited by default"},
                    {"format",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "Return amounts with the following: 'id' -> <amount>@id; (default)'symbol' -> <amount>@symbol"},

                },
            }, },
        RPCResult{"[{},{}...]     (array) Objects with account history information\n"},
        RPCExamples{HelpExampleCli("listaccounthistory", "all '{\"maxBlockHeight\":160,\"depth\":10}'") +
                    HelpExampleRpc("listaccounthistory", "address false")},
    }
        .Check(request);

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is needed for account history");
    }

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    uint32_t maxBlockHeight = std::numeric_limits<uint32_t>::max();
    uint32_t depth = maxBlockHeight;
    bool noRewards = false;
    std::string tokenFilter;
    uint32_t limit = 100;
    std::set<CustomTxType> txTypes{};
    bool hasTxFilter = false;
    uint32_t start{0};
    bool includingStart = true;
    uint32_t txn = std::numeric_limits<uint32_t>::max();
    AmountFormat format = AmountFormat::Symbol;

    if (request.params.size() > 1) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
                        {
                            {"maxBlockHeight",  UniValueType(UniValue::VNUM) },
                            {"depth",           UniValueType(UniValue::VNUM) },
                            {"no_rewards",      UniValueType(UniValue::VBOOL)},
                            {"token",           UniValueType(UniValue::VSTR) },
                            {"txtype",          UniValueType(UniValue::VSTR) },
                            {"txtypes",         UniValueType(UniValue::VARR) },
                            {"limit",           UniValueType(UniValue::VNUM) },
                            {"start",           UniValueType(UniValue::VNUM) },
                            {"including_start", UniValueType(UniValue::VBOOL)},
                            {"txn",             UniValueType(UniValue::VNUM) },
                            {"format",          UniValueType(UniValue::VSTR) }
        },
                        true,
                        true);

        if (!optionsObj["maxBlockHeight"].isNull()) {
            maxBlockHeight = (uint32_t)optionsObj["maxBlockHeight"].get_int64();
        }
        if (!optionsObj["depth"].isNull()) {
            depth = (uint32_t)optionsObj["depth"].get_int64();
        }

        if (!optionsObj["no_rewards"].isNull()) {
            noRewards = optionsObj["no_rewards"].get_bool();
        }

        if (!optionsObj["token"].isNull()) {
            tokenFilter = optionsObj["token"].get_str();
        }
        if (!optionsObj["txtypes"].isNull()) {
            hasTxFilter = true;
            const auto types = optionsObj["txtypes"].get_array().getValues();
            if (!types.empty()) {
                for (const auto &type : types) {
                    auto str = type.get_str();
                    if (str.size() == 1) {
                        txTypes.insert(CustomTxCodeToType(str[0]));
                    } else {
                        txTypes.insert(FromString(str));
                    }
                }
            }
        } else if (!optionsObj["txtype"].isNull()) {
            hasTxFilter = true;
            const auto str = optionsObj["txtype"].get_str();
            if (str.size() == 1) {
                txTypes.insert(CustomTxCodeToType(str[0]));
            } else {
                txTypes.insert(FromString(str));
            }
        }
        if (!optionsObj["limit"].isNull()) {
            limit = (uint32_t)optionsObj["limit"].get_int64();
        }
        if (!optionsObj["start"].isNull()) {
            start = (uint32_t)optionsObj["start"].get_int64();
            includingStart = false;
        }
        if (!optionsObj["including_start"].isNull()) {
            includingStart = (uint32_t)optionsObj["including_start"].get_bool();
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }

        if (!optionsObj["txn"].isNull()) {
            txn = (uint32_t)optionsObj["txn"].get_int64();
        }

        if (!optionsObj["format"].isNull()) {
            const auto formatStr = optionsObj["format"].getValStr();
            if (formatStr == "symbol") {
                format = AmountFormat::Symbol;
            } else if (formatStr == "id") {
                format = AmountFormat::Id;
            } else {
                throw JSONRPCError(RPC_INVALID_REQUEST, "format must be one of the following: \"id\", \"symbol\"");
            }
        }

        if (!includingStart) {
            start++;
        }
    }

    std::function<bool(const CScript &)> isMatchOwner = [](const CScript &) { return true; };

    std::string accounts = "mine";
    if (request.params.size() > 0) {
        accounts = request.params[0].getValStr();
    }

    bool isMine = false;
    isminetype filter = ISMINE_ALL;

    std::set<CScript> accountSet{CScript{}};

    if (accounts == "mine") {
        isMine = true;
        filter = ISMINE_SPENDABLE;
    } else if (accounts != "all") {
        if (request.params[0].isArray()) {
            accountSet.clear();
            for (const auto &acc : request.params[0].get_array().getValues()) {
                accountSet.emplace(DecodeScript(acc.get_str()));
            }
        } else {
            accountSet.clear();
            accountSet.emplace(DecodeScript(accounts));
        }
    }

    std::set<uint256> txs;
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && !hasTxFilter;

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

    for (const auto &account : accountSet) {
        const auto startBlock = maxBlockHeight - depth;
        auto shouldSkipBlock = [startBlock, maxBlockHeight](uint32_t blockHeight) {
            return startBlock > blockHeight || blockHeight > maxBlockHeight;
        };

        CScript lastOwner;
        auto count = limit + start;
        auto lastHeight = maxBlockHeight;

        if (!account.empty()) {
            isMatchOwner = [&account](const CScript &owner) { return owner == account; };
        } else {
            isMatchOwner = [](const CScript &owner) { return true; };
        }

        auto shouldContinueToNextAccountHistory = [&, &view = view](const AccountHistoryKey &key,
                                                                    AccountHistoryValue value) -> bool {
            if (!isMatchOwner(key.owner)) {
                return false;
            }

            std::unique_ptr<CScopeAccountReverter> reverter;
            if (!noRewards) {
                reverter = std::make_unique<CScopeAccountReverter>(*view, key.owner, value.diff);
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

            if (hasTxFilter && txTypes.find(CustomTxCodeToType(value.category)) == txTypes.end()) {
                return true;
            }

            if (isMine) {
                // starts new account owned by the wallet
                if (lastOwner != key.owner) {
                    count = limit + start;
                } else if (count == 0) {
                    return true;
                }
            }

            // starting new account
            if (account.empty() && lastOwner != key.owner) {
                lastOwner = key.owner;
                lastHeight = maxBlockHeight;
            }

            if (accountRecord && (tokenFilter.empty() || hasToken(value.diff))) {
                auto &array = ret.emplace(workingHeight, UniValue::VARR).first->second;
                array.push_back(accounthistoryToJSON(*view, key, value, format));
                if (shouldSearchInWallet) {
                    txs.insert(value.txid);
                }
                --count;
            }

            if (!noRewards && count && lastHeight > workingHeight) {
                onPoolRewards(*view,
                              key.owner,
                              workingHeight,
                              lastHeight,
                              [&](int32_t height, DCT_ID poolId, RewardType type, CTokenAmount amount) {
                                  if (tokenFilter.empty() || hasToken({
                                                                 {amount.nTokenId, amount.nValue}
                                  })) {
                                      auto &array = ret.emplace(height, UniValue::VARR).first->second;
                                      array.push_back(
                                          rewardhistoryToJSON(*view, key.owner, height, poolId, type, amount, format));
                                      count ? --count : 0;
                                  }
                              });
            }

            lastHeight = workingHeight;

            return count != 0 || isMine;
        };

        if (!noRewards && !account.empty()) {
            // revert previous tx to restore account balances to maxBlockHeight
            accountView->ForEachAccountHistory(
                [&, &view = view](const AccountHistoryKey &key, const AccountHistoryValue &value) {
                    if (maxBlockHeight > key.blockHeight) {
                        return false;
                    }
                    if (!isMatchOwner(key.owner)) {
                        return false;
                    }
                    CScopeAccountReverter(*view, key.owner, value.diff);
                    return true;
                },
                account);
        }

        accountView->ForEachAccountHistory(shouldContinueToNextAccountHistory, account, maxBlockHeight, txn);

        if (shouldSearchInWallet) {
            count = limit + start;
            searchInWallet(
                pwallet,
                account,
                filter,
                [&](const CBlockIndex *index, const CWalletTx *pwtx) {
                    uint32_t height = index->nHeight;
                    return txs.count(pwtx->GetHash()) || startBlock > height || height > maxBlockHeight;
                },
                [&, &view = view](const COutputEntry &entry, const CBlockIndex *index, const CWalletTx *pwtx) {
                    uint32_t height = index->nHeight;
                    uint32_t nIndex = pwtx->nIndex;
                    if (txn != std::numeric_limits<uint32_t>::max() && height == maxBlockHeight && nIndex > txn) {
                        return true;
                    }
                    auto &array = ret.emplace(index->nHeight, UniValue::VARR).first->second;
                    array.push_back(outputEntryToJSON(*view, entry, index, pwtx, format));
                    return --count != 0;
                });
        }
    }

    UniValue slice(UniValue::VARR);
    for (auto it = ret.cbegin(); limit != 0 && it != ret.cend(); ++it) {
        const auto &array = it->second.get_array();
        for (size_t i = 0; limit != 0 && i < array.size(); ++i) {
            if (start != 0) {
                --start;
                continue;
            }
            slice.push_back(array[i]);
            --limit;
        }
    }

    return GetRPCResultCache().Set(request, slice);
}

UniValue getaccounthistory(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getaccounthistory",
        "\nReturns information about account history.\n",
        {
          {"owner", RPCArg::Type::STR, RPCArg::Optional::NO, "Single account ID (CScript or address)."},
          {"blockHeight", RPCArg::Type::NUM, RPCArg::Optional::NO, "Block Height to search in."},
          {"txn", RPCArg::Type::NUM, RPCArg::Optional::NO, "for order in block."},
          },
        RPCResult{"{}  An object with account history information\n"},
        RPCExamples{HelpExampleCli("getaccounthistory", "mxxA2sQMETJFbXcNbNbUzEsBCTn1JSHXST 103 2") +
                    HelpExampleCli("getaccounthistory", "mxxA2sQMETJFbXcNbNbUzEsBCTn1JSHXST, 103, 2")},
    }
        .Check(request);

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is needed for account history");
    }

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    auto owner = DecodeScript(request.params[0].getValStr());
    uint32_t blockHeight = request.params[1].get_int();
    uint32_t txn = request.params[2].get_int();

    auto [view, accountView, vaultView] = GetSnapshots();

    UniValue result(UniValue::VOBJ);
    AccountHistoryKey AccountKey{owner, blockHeight, txn};
    LOCK(cs_main);
    if (auto value = accountView->ReadAccountHistory(AccountKey)) {
        result = accounthistoryToJSON(*view, AccountKey, *value);
    }

    return GetRPCResultCache().Set(request, result);
}

UniValue listburnhistory(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "listburnhistory",
        "\nReturns information about burn history.\n",
        {
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
        RPCResult{"[{},{}...]     (array) Objects with burn history information\n"},
        RPCExamples{HelpExampleCli("listburnhistory", "'{\"maxBlockHeight\":160,\"depth\":10}'") +
                    HelpExampleRpc("listburnhistory", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

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
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    std::function<bool(const CScript &)> isMatchOwner = [](const CScript &) { return true; };

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

    auto count = limit;

    auto shouldContinueToNextAccountHistory = [&, &view = view](const AccountHistoryKey &key,
                                                                AccountHistoryValue value) -> bool {
        if (!isMatchOwner(key.owner)) {
            return false;
        }

        if (shouldSkipBlock(key.blockHeight)) {
            return true;
        }

        if (txTypeSearch && value.category != uint8_t(txType)) {
            return true;
        }

        if (!tokenFilter.empty() && !hasToken(value.diff)) {
            return true;
        }

        auto &array = ret.emplace(key.blockHeight, UniValue::VARR).first->second;
        array.push_back(accounthistoryToJSON(*view, key, value));

        --count;

        return count != 0;
    };

    {
        LOCK(cs_main);
        pburnHistoryDB->ForEachAccountHistory(shouldContinueToNextAccountHistory, {}, maxBlockHeight);
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

UniValue accounthistorycount(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "accounthistorycount",
        "\nReturns count of account history.\n",
        {
          {"owner",
             RPCArg::Type::STR,
             RPCArg::Optional::OMITTED,
             "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned "
             "accounts or \"all\" to list whole DB (default = \"mine\")."},

          {
                "options",
                RPCArg::Type::OBJ,
                RPCArg::Optional::OMITTED,
                "",
                {
                    {"no_rewards", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Filter out rewards"},
                    {"token", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by token"},
                    {"txtype",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "Filter by transaction type, supported letter from {CustomTxType}"},
                    {"txtypes",
                     RPCArg::Type::ARR,
                     RPCArg::Optional::OMITTED,
                     "Filter multiple transaction types, supported letter from {CustomTxType}",
                     {
                         {
                             "Transaction Type",
                             RPCArg::Type::STR,
                             RPCArg::Optional::OMITTED,
                             "letter from {CustomTxType}",
                         },
                     }},
                },
            }, },
        RPCResult{"count     (int) Count of account history\n"},
        RPCExamples{HelpExampleCli("accounthistorycount", "all '{no_rewards: true}'") +
                    HelpExampleRpc("accounthistorycount", "")},
    }
        .Check(request);

    if (!paccountHistoryDB) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "-acindex is need for account history");
    }

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    std::string accounts = "mine";
    if (request.params.size() > 0) {
        accounts = request.params[0].getValStr();
    }

    bool noRewards = false;
    std::string tokenFilter;
    std::set<CustomTxType> txTypes{};
    bool hasTxFilter = false;

    if (request.params.size() > 1) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
                        {
                            {"no_rewards", UniValueType(UniValue::VBOOL)},
                            {"token",      UniValueType(UniValue::VSTR) },
                            {"txtype",     UniValueType(UniValue::VSTR) },
                            {"txtypes",    UniValueType(UniValue::VARR) },
        },
                        true,
                        true);

        noRewards = optionsObj["no_rewards"].getBool();

        if (!optionsObj["token"].isNull()) {
            tokenFilter = optionsObj["token"].get_str();
        }

        if (!optionsObj["txtypes"].isNull()) {
            hasTxFilter = true;
            const auto types = optionsObj["txtypes"].get_array().getValues();
            if (!types.empty()) {
                for (const auto &type : types) {
                    auto str = type.get_str();
                    if (str.size() == 1) {
                        txTypes.insert(CustomTxCodeToType(str[0]));
                    } else {
                        txTypes.insert(FromString(str));
                    }
                }
            }
        } else if (!optionsObj["txtype"].isNull()) {
            hasTxFilter = true;
            const auto str = optionsObj["txtype"].get_str();
            if (str.size() == 1) {
                txTypes.insert(CustomTxCodeToType(str[0]));
            } else {
                txTypes.insert(FromString(str));
            }
        }
    }

    std::set<CScript> accountSet{CScript{}};
    bool isMine = false;
    isminetype filter = ISMINE_ALL;

    if (accounts == "mine") {
        isMine = true;
        filter = ISMINE_SPENDABLE;
    } else if (accounts != "all") {
        accountSet.clear();
        if (request.params[0].isArray()) {
            for (const auto &acc : request.params[0].get_array().getValues()) {
                accountSet.emplace(DecodeScript(acc.get_str()));
            }
        } else {
            const auto owner = DecodeScript(accounts);
            accountSet.emplace(owner);
            isMine = IsMineCached(*pwallet, owner) & ISMINE_ALL;
        }
    }

    std::set<uint256> txs;
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && !hasTxFilter;

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

    uint64_t count{};

    for (const auto &owner : accountSet) {
        CScript lastOwner;
        uint32_t lastHeight = view->GetLastHeight();
        const auto currentHeight = lastHeight;

        auto shouldContinueToNextAccountHistory = [&, &view = view](const AccountHistoryKey &key,
                                                                    AccountHistoryValue value) -> bool {
            if (!owner.empty() && owner != key.owner) {
                return false;
            }

            if (isMine && !(IsMineCached(*pwallet, key.owner) & filter)) {
                return true;
            }

            std::unique_ptr<CScopeAccountReverter> reverter;
            if (!noRewards) {
                reverter = std::make_unique<CScopeAccountReverter>(*view, key.owner, value.diff);
            }

            if (hasTxFilter && txTypes.find(CustomTxCodeToType(value.category)) == txTypes.end()) {
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
                    lastOwner = key.owner;
                    lastHeight = currentHeight;
                }
                onPoolRewards(*view,
                              key.owner,
                              key.blockHeight,
                              lastHeight,
                              [&](int32_t, DCT_ID, RewardType, CTokenAmount amount) {
                                  if (tokenFilter.empty() || hasToken({
                                                                 {amount.nTokenId, amount.nValue}
                                  })) {
                                      ++count;
                                  }
                              });
                lastHeight = key.blockHeight;
            }

            return true;
        };

        accountView->ForEachAccountHistory(shouldContinueToNextAccountHistory, owner, currentHeight);

        if (shouldSearchInWallet) {
            searchInWallet(
                pwallet,
                owner,
                filter,
                [&](const CBlockIndex *index, const CWalletTx *pwtx) {
                    return txs.count(pwtx->GetHash()) || static_cast<uint32_t>(index->nHeight) > currentHeight;
                },
                [&count](const COutputEntry &, const CBlockIndex *, const CWalletTx *) {
                    ++count;
                    return true;
                });
        }
    }

    return GetRPCResultCache().Set(request, count);
}

UniValue listcommunitybalances(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listcommunitybalances",
        "\nReturns information about all community balances.\n",
        {},
        RPCResult{"{balance_type:value,...}     (array) Json object with accounts information\n"},
        RPCExamples{HelpExampleCli("listcommunitybalances", "") + HelpExampleRpc("listcommunitybalances", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    UniValue ret(UniValue::VOBJ);

    auto [view, accountView, vaultView] = GetSnapshots();
    CAmount burnt{0};
    for (const auto &kv : Params().GetConsensus().blockTokenRewards) {
        // Skip these as any unused balance will be burnt.
        if (kv.first == CommunityAccountType::Options) {
            continue;
        }
        if (kv.first == CommunityAccountType::Unallocated || kv.first == CommunityAccountType::IncentiveFunding) {
            burnt += view->GetCommunityBalance(kv.first);
            continue;
        }

        if (kv.first == CommunityAccountType::Loan) {
            if (view->GetLastHeight() >= Params().GetConsensus().DF11FortCanningHeight) {
                burnt += view->GetCommunityBalance(kv.first);
            }
            continue;
        }

        ret.pushKV(GetCommunityAccountName(kv.first), ValueFromAmount(view->GetCommunityBalance(kv.first)));
    }
    ret.pushKV("Burnt", ValueFromAmount(burnt));

    return GetRPCResultCache().Set(request, ret);
}

UniValue sendtokenstoaddress(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "sendtokenstoaddress",
        "\nCreates (and submits to local node and network) a transfer transaction from your accounts balances (may be "
        "picked manualy or autoselected) to the specfied accounts.\n" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {
                "from",
                RPCArg::Type::OBJ,
                RPCArg::Optional::NO,
                "",
                {
                    {"address",
                     RPCArg::Type::STR,
                     RPCArg::Optional::OMITTED,
                     "The source defi address is the key, the value is amount in amount@token format. "
                     "If obj is empty (no address keys exists) then will try to auto-select accounts from wallet "
                     "with necessary balances to transfer."},
                },
            }, {
                "to",
                RPCArg::Type::OBJ,
                RPCArg::Optional::NO,
                "",
                {
                    {"address",
                     RPCArg::Type::STR,
                     RPCArg::Optional::NO,
                     "The defi address is the key, the value is amount in amount@token format. "
                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                },
            }, {"selectionMode",
             RPCArg::Type::STR,
             /* default */ "pie",
             "If param \"from\" is empty this param indicates accounts autoselection mode."
             "May be once of:\n"
             "  \"forward\" - Selecting accounts without sorting, just as address list sorted.\n"
             "  \"crumbs\" - Selecting accounts by ascending of sum token amounts.\n"
             "    It means that we will select first accounts with minimal sum of neccessary token amounts.\n"
             "  \"pie\" - Selecting accounts by descending of sum token amounts.\n"
             "    It means that we will select first accounts with maximal sum of neccessary token amounts."},
                          },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                          HelpExampleCli("sendtokenstoaddress",
                          "'{}' "
                           "'{\"dstAddress1\":\"1.0@DFI\",\"dstAddress2\":[\"2.0@BTC\", \"3.0@ETH\"]}' \"crumbs\"") +
            HelpExampleCli("sendtokenstoaddress",
                          "'{\"srcAddress1\":\"2.0@DFI\", \"srcAddress2\":[\"3.0@DFI\", \"2.0@ETH\"]}' "
                           "'{\"dstAddress1\":[\"5.0@DFI\", \"2.0@ETH\"]}'")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VOBJ, UniValue::VSTR}, false);

    CAnyAccountsToAccountsMessage msg;
    auto ownershipCheck = gArgs.GetBoolArg("-dvmownerskipcheck", DEFAULT_DVM_OWNERSHIP_CHECK);
    msg.to = DecodeRecipientsDefaultInternal(pwallet, request.params[1].get_obj(), ownershipCheck);

    const CBalances sumTransfersTo = SumAllTransfers(msg.to);
    if (sumTransfersTo.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts in \"to\" param");
    }

    if (request.params[0].get_obj().empty()) {  // autoselection
        auto [view, accountView, vaultView] = GetSnapshots();
        CAccounts foundMineAccounts = GetAllMineAccounts(pwallet, *view);

        AccountSelectionMode selectionMode = SelectionPie;
        if (request.params[2].isStr()) {
            selectionMode = ParseAccountSelectionParam(request.params[2].get_str());
        }

        msg.from = SelectAccountsByTargetBalances(foundMineAccounts, sumTransfersTo, selectionMode);

        if (msg.from.empty()) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                               "Not enough balance on wallet accounts, call utxostoaccount to increase it.\n");
        }
    } else {
        msg.from = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());
    }

    for (const auto &[to, amount] : msg.to) {
        RejectErc55Address(to);
    }
    for (const auto &[from, amount] : msg.from) {
        RejectErc55Address(from);
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AnyAccountsToAccounts) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    if (scriptMeta.size() > nMaxDatacarrierBytes) {
        throw JSONRPCError(
            RPC_VERIFY_REJECTED,
            "The output custom script size has exceeded the maximum OP_RETURN script size."
            "It may happened because too many \"from\" or \"to\" accounts balances."
            "If you use autoselection, you can try to use \"pie\" selection mode for decreasing accounts count.");
    }

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue txInputs(UniValue::VARR);

    // auth
    auto [view, accountView, vaultView] = GetSnapshots();
    std::set<CScript> auths;
    for (const auto &acc : msg.from) {
        auths.emplace(acc.first);
    }
    CTransactionRef optAuthTx;
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

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
    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue transferdomain(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);
    // TODO: Add support for non-JSON parameteric input that's human friendly and intuitive
    RPCHelpMan{
        "transferdomain",
        "Creates (and submits to local node and network) a tx to transfer assets across domains. DVM to EVM/EVM to "
        "DVM, etc.\n" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {
                "array",
                RPCArg::Type::ARR,
                RPCArg::Optional::NO,
                "A json array of src and dst json objects",
                {
                    {
                        "",
                        RPCArg::Type::OBJ,
                        RPCArg::Optional::OMITTED,
                        "",
                        {
                            {
                                "src",
                                RPCArg::Type::OBJ,
                                RPCArg::Optional::OMITTED,
                                "Source arguments",
                                {
                                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Source address"},
                                    {"amount",
                                     RPCArg::Type::STR,
                                     RPCArg::Optional::NO,
                                     "Amount transfered, the value is amount in amount@token format"},
                                    {"domain",
                                     RPCArg::Type::NUM,
                                     RPCArg::Optional::NO,
                                     "Domain of source: 2 - DVM, 3 - EVM"},
                                    // {"data", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Optional data"},
                                },
                            },
                            {
                                "dst",
                                RPCArg::Type::OBJ,
                                RPCArg::Optional::OMITTED,
                                "Destination arguments",
                                {
                                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination address"},
                                    {"amount",
                                     RPCArg::Type::STR,
                                     RPCArg::Optional::NO,
                                     "Amount transfered, the value is amount in amount@token format"},
                                    {"domain",
                                     RPCArg::Type::NUM,
                                     RPCArg::Optional::NO,
                                     "Domain of source: 2 - DVM, 3 - EVM"},
                                    // {"data", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Optional data"},
                                },
                            },
                            {
                                "nonce",
                                RPCArg::Type::NUM,
                                RPCArg::Optional::OMITTED,
                                "Optional parameter to specify the transaction nonce",
                            },
                            {
                                "singlekeycheck",
                                RPCArg::Type::BOOL,
                                RPCArg::Optional::OMITTED,
                                "Optional flag to ensure single key check between the corresponding address types "
                                "(default = true)",
                            },
                        },
                    },
                },
            }, },
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{
                          HelpExampleCli(
                "transferdomain", R"('[{"src":{"address":"<DFI_address>", "amount":"1.0@DFI", "domain": 2}, "dst":{"address":"<ETH_address>", "amount":"1.0@DFI", "domain": 3}}]')") +
            HelpExampleCli(
                "transferdomain", R"('[{"src":{"address":"<ETH_address>", "amount":"1.0@DFI", "domain": 3}, "dst":{"address":"<DFI_address>", "amount":"1.0@DFI", "domain": 2}}]')")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot transferdomain while still in Initial Block Download");
    }

    pwallet->BlockUntilSyncedToCurrentChain();

    EnsureWalletIsUnlocked(pwallet);

    RPCTypeCheck(request.params, {UniValue::VARR}, false);

    UniValue srcDstArray(UniValue::VARR);

    srcDstArray = request.params[0].get_array();

    CrossBoundaryResult result;
    CTransferDomainMessage msg;
    std::set<CScript> auths;
    std::vector<std::pair<EvmAddressData, uint64_t>> nonce_cache;

    for (unsigned int i = 0; i < srcDstArray.size(); i++) {
        const UniValue &elem = srcDstArray[i].get_obj();
        RPCTypeCheckObj(elem,
                        {
                            {"src",            UniValueType(UniValue::VOBJ) },
                            {"dst",            UniValueType(UniValue::VOBJ) },
                            {"nonce",          UniValueType(UniValue::VNUM) },
                            {"singlekeycheck", UniValueType(UniValue::VBOOL)},
        },
                        true,
                        true);

        const UniValue &srcObj = elem["src"].get_obj();
        const UniValue &dstObj = elem["dst"].get_obj();
        const UniValue &nonceObj = elem["nonce"];
        const UniValue &singlekeycheckObj = elem["singlekeycheck"];

        CTransferDomainItem src, dst;
        CTxDestination srcDest, dstDest;

        if (!srcObj["address"].isNull()) {
            srcDest = DecodeDestination(srcObj["address"].getValStr());
            if (!IsValidDestination(srcDest)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid src address provided");
            }
            src.address = GetScriptForDestination(srcDest);
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, src argument \"address\" must not be null");
        }

        if (!srcObj["amount"].isNull()) {
            src.amount = DecodeAmount(pwallet->chain(), srcObj["amount"], "");
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, src argument \"amount\" must not be null");
        }

        if (!srcObj["domain"].isNull()) {
            src.domain = srcObj["domain"].get_int();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, src argument \"domain\" must not be null");
        }
        auto isEVMIn = src.domain == static_cast<uint8_t>(VMDomain::DVM);

        auto srcKey = AddrToPubKey(pwallet, ScriptToString(src.address));
        if (isEVMIn) {
            auths.insert(src.address);
        } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM)) {
            if (srcKey.Compress()) {
                const auto auth = GetScriptForDestination(WitnessV0KeyHash(srcKey.GetID()));
                auths.insert(auth);
            } else {
                throw JSONRPCError(
                    RPC_INVALID_PARAMETER,
                    strprintf("Failed to get compressed address for Bech32 equivilent of ERC55 address"));
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Invalid parameters, src argument \"domain\" must be either %d (DFI token to "
                                         "EVM) or %d (EVM to DFI token)",
                                         static_cast<uint8_t>(VMDomain::DVM),
                                         static_cast<uint8_t>(VMDomain::EVM)));
        }

        // if (!srcObj["data"].isNull())
        //     src.data.assign(srcObj["data"].getValStr().begin(), srcObj["data"].getValStr().end());

        if (!dstObj["address"].isNull()) {
            dstDest = DecodeDestination(dstObj["address"].getValStr());
            if (!IsValidDestination(dstDest)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid dst address provided");
            }
            dst.address = GetScriptForDestination(dstDest);
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, dst argument \"address\" must not be null");
        }

        if (!dstObj["amount"].isNull()) {
            dst.amount = DecodeAmount(pwallet->chain(), dstObj["amount"], "");
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, dst argument \"amount\" must not be null");
        }

        if (!dstObj["domain"].isNull()) {
            dst.domain = dstObj["domain"].get_int();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, dst argument \"domain\" must not be null");
        }

        // if (!dstObj["data"].isNull())
        //     dst.data.assign(dstObj["data"].getValStr().begin(), dstObj["data"].getValStr().end());

        // Single key check
        auto singlekeycheck = gArgs.GetBoolArg("-tdsinglekeycheck", true);
        if (!singlekeycheckObj.isNull()) {
            singlekeycheck = singlekeycheckObj.getBool();
        }
        if (singlekeycheck) {
            auto dstKey = AddrToPubKey(pwallet, ScriptToString(dst.address));
            auto [uncompSrcKey, compSrcKey] = GetBothPubkeyCompressions(srcKey);
            auto [uncompDstKey, compDstKey] = GetBothPubkeyCompressions(dstKey);
            if (uncompSrcKey != uncompDstKey || compSrcKey != compDstKey) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Dst address does not match source key");
            }
        }

        // Create signed EVM TX
        CKey key;
        if (!pwallet->GetKey(srcKey.GetID(), key)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for from address not found in wallet");
        }
        std::array<uint8_t, 32> privKey{};
        std::copy(key.begin(), key.end(), privKey.begin());

        EvmAddressData to{};
        std::string nativeAddress = "";
        if (isEVMIn) {
            to = CKeyID::FromOrDefaultDestination(dstDest).GetByteArray();
            nativeAddress = ScriptToString(src.address);
        } else {
            nativeAddress = ScriptToString(dst.address);
        }
        auto dest = GetDestinationForKey(srcKey, OutputType::ERC55);
        auto from = CKeyID::FromOrDefaultDestination(dest).GetByteArray();

        uint64_t nonce = 0;
        bool useNonce = !nonceObj.isNull();
        if (useNonce) {
            nonce = nonceObj.get_int64();
        }
        const auto createResult = evm_try_create_and_sign_transfer_domain_tx(
            result,
            CreateTransferDomainContext{from,
                                        to,
                                        nativeAddress,
                                        isEVMIn,
                                        static_cast<uint64_t>(dst.amount.nValue),
                                        dst.amount.nTokenId.v,
                                        Params().GetConsensus().evmChainId,
                                        privKey,
                                        useNonce,
                                        nonce});
        if (!result.ok) {
            throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed to create and sign TX: %s", result.reason.c_str()));
        }

        std::vector<uint8_t> evmTx(createResult.tx.size());
        std::copy(createResult.tx.begin(), createResult.tx.end(), evmTx.begin());
        if (isEVMIn) {
            dst.data = evmTx;
        } else {
            src.data = evmTx;
        }

        nonce_cache.push_back({from, createResult.nonce});
        msg.transfers.push_back({src, dst});
    }

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);

    metadata << static_cast<unsigned char>(CustomTxType::TransferDomain) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    UniValue txInputs(UniValue::VARR);
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, txInputs, *view);

    rawTx.vout.emplace_back(0, scriptMeta);

    CCoinControl coinControl;

    // Return change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    auto txRef = sign(rawTx, pwallet, optAuthTx);
    // check execution
    execTestTx(*txRef, targetHeight, optAuthTx);
    for (auto &nonce : nonce_cache) {
        evm_try_store_account_nonce(result, nonce.first, nonce.second);
        if (!result.ok) {
            throw JSONRPCError(RPC_DATABASE_ERROR, "Could not cache nonce");
        }
    }

    return send(txRef, optAuthTx)->GetHash().GetHex();
}

UniValue getburninfo(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getburninfo",
        "\nReturns burn address and burnt coin and token information.\n"
        "Requires full acindex for correct amount, tokens and feeburn values.\n",
        {},
        RPCResult{"{\n"
                  "  \"address\" : \"address\",        (string) The defi burn address\n"
                  "  \"amount\" : n.nnnnnnnn,        (string) The amount of DFI burnt\n"
                  "  \"tokens\" :  [\n"
                  "      { (array of burnt tokens)"
                  "      \"name\" : \"name\"\n"
                  "      \"amount\" : n.nnnnnnnn\n"
                  "    ]\n"
                  "  \"feeburn\" : n.nnnnnnnn,        (string) The amount of fees burnt\n"
                  "  \"emissionburn\" : n.nnnnnnnn,   (string) The amount of non-utxo coinbase rewards burnt\n"
                  "}\n"},
        RPCExamples{HelpExampleCli("getburninfo", "") + HelpExampleRpc("getburninfo", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    auto initialResult = GetMemoizedResultCache().GetOrDefault(request);
    auto totalResult = std::get_if<CGetBurnInfoResult>(&initialResult.data);

    CAmount dfiPaybackFee{0};
    CAmount burnt{0};

    CBalances paybackfees;
    CBalances paybacktokens;
    CBalances dfi2203Tokens;
    CBalances dfipaybacktokens;
    CBalances dfiToDUSDTokens;

    auto [view, accountView, vaultView] = GetSnapshots();
    const auto height = view->GetLastHeight();
    uint256 hash;
    {
        LOCK(cs_main);
        hash = ::ChainActive().Tip()->GetBlockHash();
    }
    auto fortCanningHeight = Params().GetConsensus().DF11FortCanningHeight;
    auto burnAddress = Params().GetConsensus().burnAddress;
    const auto attributes = view->GetAttributes();

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackDFITokens};
    auto tokenBalances = attributes->GetValue(liveKey, CBalances{});
    for (const auto &balance : tokenBalances.balances) {
        if (balance.first == DCT_ID{0}) {
            dfiPaybackFee = balance.second;
        } else {
            dfipaybacktokens.Add({balance.first, balance.second});
        }
    }
    liveKey = {AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::PaybackTokens};
    auto paybacks = attributes->GetValue(liveKey, CTokenPayback{});
    paybackfees = std::move(paybacks.tokensFee);
    paybacktokens = std::move(paybacks.tokensPayback);

    liveKey = {AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Burned};
    dfi2203Tokens = attributes->GetValue(liveKey, CBalances{});

    liveKey = {AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2206FBurned};
    dfiToDUSDTokens = attributes->GetValue(liveKey, CBalances{});

    for (const auto &kv : Params().GetConsensus().blockTokenRewards) {
        if (kv.first == CommunityAccountType::Unallocated || kv.first == CommunityAccountType::IncentiveFunding ||
            (height >= fortCanningHeight && kv.first == CommunityAccountType::Loan)) {
            burnt += view->GetCommunityBalance(kv.first);
        }
    }

    auto nWorkers = DfTxTaskPool->GetAvailableThreads();
    if (static_cast<size_t>(height) < nWorkers) {
        nWorkers = height;
    }

    const auto chunkSize = height / nWorkers;

    TaskGroup g;
    BufferPool<CGetBurnInfoResult> resultsPool{nWorkers};

    LOCK(cs_main);  // Lock for pburnHistoryDB

    auto &pool = DfTxTaskPool->pool;
    auto processedHeight = initialResult.height;
    auto i = 0;
    while (processedHeight < height) {
        auto startHeight = initialResult.height + (chunkSize * (i + 1));
        auto stopHeight = initialResult.height + (chunkSize * (i));

        g.AddTask();
        boost::asio::post(pool, [startHeight, stopHeight, &g, &resultsPool] {
            auto currentResult = resultsPool.Acquire();

            pburnHistoryDB->ForEachAccountHistory(
                [currentResult, stopHeight](const AccountHistoryKey &key, const AccountHistoryValue &value) {
                    // Stop on chunk range for worker
                    if (key.blockHeight <= stopHeight) {
                        return false;
                    }

                    // UTXO burn
                    if (value.category == uint8_t(CustomTxType::None)) {
                        for (auto const &diff : value.diff) {
                            currentResult->burntDFI += diff.second;
                        }
                        return true;
                    }

                    // Fee burn
                    if (value.category == uint8_t(CustomTxType::CreateMasternode) ||
                        value.category == uint8_t(CustomTxType::CreateToken) ||
                        value.category == uint8_t(CustomTxType::Vault) ||
                        value.category == uint8_t(CustomTxType::CreateCfp) ||
                        value.category == uint8_t(CustomTxType::CreateVoc)) {
                        for (auto const &diff : value.diff) {
                            currentResult->burntFee += diff.second;
                        }
                        return true;
                    }

                    // withdraw burn
                    if (value.category == uint8_t(CustomTxType::PaybackLoan) ||
                        value.category == uint8_t(CustomTxType::PaybackLoanV2) ||
                        value.category == uint8_t(CustomTxType::PaybackWithCollateral)) {
                        for (const auto &[id, amount] : value.diff) {
                            currentResult->paybackFee.Add({id, amount});
                        }
                        return true;
                    }

                    // auction burn
                    if (value.category == uint8_t(CustomTxType::AuctionBid)) {
                        for (auto const &diff : value.diff) {
                            currentResult->auctionFee += diff.second;
                        }
                        return true;
                    }

                    // dex fee burn
                    if (value.category == uint8_t(CustomTxType::PoolSwap) ||
                        value.category == uint8_t(CustomTxType::PoolSwapV2)) {
                        for (auto const &diff : value.diff) {
                            currentResult->dexfeeburn.Add({diff.first, diff.second});
                        }
                        return true;
                    }

                    // token burn with burnToken tx
                    if (value.category == uint8_t(CustomTxType::BurnToken)) {
                        for (auto const &diff : value.diff) {
                            currentResult->burntTokens.Add({diff.first, diff.second});
                        }
                        return true;
                    }

                    // Token burn
                    for (auto const &diff : value.diff) {
                        currentResult->burntTokens.Add({diff.first, diff.second});
                    }

                    return true;
                },
                {},
                startHeight,
                std::numeric_limits<uint32_t>::max());

            resultsPool.Release(currentResult);
            g.RemoveTask();
        });

        // perfect accuracy: processedHeight += (startHeight > height) ? chunksRemainder : chunkSize;
        processedHeight += chunkSize;
        i++;
    }

    g.WaitForCompletion();

    for (const auto &r : resultsPool.GetBuffer()) {
        totalResult->burntDFI += r->burntDFI;
        totalResult->burntFee += r->burntFee;
        totalResult->auctionFee += r->auctionFee;
        totalResult->burntTokens.AddBalances(r->burntTokens.balances);
        totalResult->dexfeeburn.AddBalances(r->dexfeeburn.balances);
        totalResult->paybackFee.AddBalances(r->paybackFee.balances);
    }

    GetMemoizedResultCache().Set(request, {height, hash, *totalResult});

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", ScriptToString(burnAddress));
    result.pushKV("amount", ValueFromAmount(totalResult->burntDFI));

    result.pushKV("tokens", AmountsToJSON(*view, totalResult->burntTokens.balances));
    result.pushKV("feeburn", ValueFromAmount(totalResult->burntFee));
    result.pushKV("auctionburn", ValueFromAmount(totalResult->auctionFee));
    result.pushKV("paybackburn", AmountsToJSON(*view, totalResult->paybackFee.balances));
    result.pushKV("dexfeetokens", AmountsToJSON(*view, totalResult->dexfeeburn.balances));

    result.pushKV("dfipaybackfee", ValueFromAmount(dfiPaybackFee));
    result.pushKV("dfipaybacktokens", AmountsToJSON(*view, dfipaybacktokens.balances));

    result.pushKV("paybackfees", AmountsToJSON(*view, paybackfees.balances));
    result.pushKV("paybacktokens", AmountsToJSON(*view, paybacktokens.balances));

    result.pushKV("emissionburn", ValueFromAmount(burnt));
    result.pushKV("dfip2203", AmountsToJSON(*view, dfi2203Tokens.balances));
    result.pushKV("dfip2206f", AmountsToJSON(*view, dfiToDUSDTokens.balances));

    return GetRPCResultCache().Set(request, result);
}

UniValue HandleSendDFIP2201DFIInput(const JSONRPCRequest &request,
                                    CWalletCoinsUnlocker pwallet,
                                    const std::pair<std::string, CScript> &contractPair,
                                    CTokenAmount amount) {
    CUtxosToAccountMessage msg{};
    msg.to = {
        {contractPair.second, {{{{0}, amount.nValue}}}}
    };

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UtxosToAccount) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

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
    fund(rawTx, pwallet, {}, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight);

    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue HandleSendDFIP2201BTCInput(const JSONRPCRequest &request,
                                    CWalletCoinsUnlocker pwallet,
                                    const std::pair<std::string, CScript> &contractPair,
                                    CTokenAmount amount,
                                    CCustomCSView &mnview) {
    if (request.params[2].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "BTC source address must be provided for " + contractPair.first);
    }
    CTxDestination dest = DecodeDestination(request.params[2].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    const auto script = GetScriptForDestination(dest);
    RejectErc55Address(script);

    CSmartContractMessage msg{};
    msg.name = contractPair.first;
    msg.accounts = {
        {script, {{{amount.nTokenId, amount.nValue}}}}
    };
    // encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SmartContract) << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auth{script};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auth, false, optAuthTx, request.params[3], mnview, request.metadata.coinSelectOpts);

    // Set change address
    CCoinControl coinControl;
    coinControl.destChange = dest;

    // fund
    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue HandleSendDFIP2201(const JSONRPCRequest &request, CWalletCoinsUnlocker pwallet, CCustomCSView &mnview) {
    auto contracts = Params().GetConsensus().smartContracts;
    const auto &contractPair = contracts.find(SMART_CONTRACT_DFIP_2201);
    assert(contractPair != contracts.end());

    CTokenAmount amount = DecodeAmount(pwallet->chain(), request.params[1].get_str(), "amount");

    if (amount.nTokenId.v == 0) {
        return HandleSendDFIP2201DFIInput(request, std::move(pwallet), *contractPair, amount);
    } else {
        return HandleSendDFIP2201BTCInput(request, std::move(pwallet), *contractPair, amount, mnview);
    }
}

UniValue executesmartcontract(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "executesmartcontract",
        "\nCreates and sends a transaction to either fund or execute a smart contract. Available contracts: "
        "dbtcdfiswap" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Name of the smart contract to send funds to"},
                          {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount to send in amount@token format"},
                          {"address",
             RPCArg::Type::STR,
             RPCArg::Optional::OMITTED,
             "Address to be used in contract execution if required"},
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
        RPCExamples{HelpExampleCli("executesmartcontract", "dbtcdfiswap 1000@DFI") +
                    HelpExampleRpc("executesmartcontract", "dbtcdfiswap, 1000@DFI")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const auto &contractName = request.params[0].get_str();
    if (contractName == "dbtcdfiswap") {
        auto [view, accountView, vaultView] = GetSnapshots();
        return HandleSendDFIP2201(request, std::move(pwallet), *view);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Specified smart contract not found");
    }
    return NullUniValue;
}

UniValue futureswap(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "futureswap",
        "\nCreates and submits to the network a futures contract" + HelpRequiringPassphrase(pwallet) + "\n",
        {
                                                    {"address",
             RPCArg::Type::STR,
             RPCArg::Optional::NO,
             "Address to fund contract and receive resulting token"},
                                                    {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount to send in amount@token format"},
                                                    {"destination", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Expected dToken if DUSD supplied"},
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
        RPCExamples{HelpExampleCli("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ 1000@TSLA") +
                    HelpExampleCli("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ 1000@DUSD TSLA") +
                    HelpExampleRpc("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ, 1000@TSLA") +
                    HelpExampleRpc("futureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ, 1000@DUSD, TSLA")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const auto dest = DecodeDestination(request.params[0].getValStr());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    CFutureSwapMessage msg{};
    msg.owner = GetScriptForDestination(dest);
    msg.source = DecodeAmount(pwallet->chain(), request.params[1], "");

    RejectErc55Address(msg.owner);

    auto [view, accountView, vaultView] = GetSnapshots();

    if (!request.params[2].isNull()) {
        DCT_ID destTokenID{};

        const auto destToken = view->GetTokenGuessId(request.params[2].getValStr(), destTokenID);
        if (!destToken) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Destination token not found");
        }

        msg.destination = destTokenID.v;
    }

    // Encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::FutureSwap) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auth{msg.owner};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auth, false, optAuthTx, request.params[3], *view, request.metadata.coinSelectOpts);

    // Set change address
    CCoinControl coinControl;
    coinControl.destChange = dest;

    // Fund
    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // Check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue withdrawfutureswap(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "withdrawfutureswap",
        "\nCreates and submits to the network a withdrawal from futures contract transaction.\n"
        " Withdrawal will be back to the address specified in the futures contract." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address used to fund contract with"},
                          {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Amount to withdraw in amount@token format"},
                          {"destination", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "The dToken if DUSD supplied"},
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
        RPCExamples{HelpExampleCli("withdrawfutureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ 1000@TSLA") +
                    HelpExampleRpc("withdrawfutureswap", "dLb2jq51qkaUbVkLyCiVQCoEHzRSzRPEsJ, 1000@TSLA")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
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

    auto [view, accountView, vaultView] = GetSnapshots();

    if (!request.params[2].isNull()) {
        DCT_ID destTokenID{};

        const auto destToken = view->GetTokenGuessId(request.params[2].getValStr(), destTokenID);
        if (!destToken) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Destination token not found");
        }

        msg.destination = destTokenID.v;
    }

    RejectErc55Address(msg.owner);

    // Encode
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::FutureSwap) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransactionRef optAuthTx;
    std::set<CScript> auth{msg.owner};
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auth, false, optAuthTx, request.params[3], *view, request.metadata.coinSelectOpts);

    // Set change address
    CCoinControl coinControl;
    coinControl.destChange = dest;

    // Fund
    fund(rawTx, pwallet, optAuthTx, &coinControl, request.metadata.coinSelectOpts);

    // Check execution
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue listpendingfutureswaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listpendingfutureswaps",
        "Get all pending futures.\n",
        {},
        RPCResult{"\"json\"          (string) array containing json-objects having following fields:\n"
                  "    owner :       \"address\"\n"
                  "    values : [{\n"
                  "        tokenSymbol : \"SYMBOL\"\n"
                  "        amount :      n.nnnnnnnn\n"
                  "        destination : \"SYMBOL\"\n"
                  "    }...]\n"},
        RPCExamples{HelpExampleCli("listpendingfutureswaps", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    UniValue listFutures{UniValue::VARR};

    auto [view, accountView, vaultView] = GetSnapshots();

    view->ForEachFuturesUserValues(
        [&, &view = view](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
            CTxDestination dest;
            ExtractDestination(key.owner, dest);
            if (!IsValidDestination(dest)) {
                return true;
            }

            const auto source = view->GetToken(futuresValues.source.nTokenId);
            if (!source) {
                return true;
            }

            UniValue value{UniValue::VOBJ};
            value.pushKV("owner", EncodeDestination(dest));
            value.pushKV("source", ValueFromAmount(futuresValues.source.nValue).getValStr() + '@' + source->symbol);

            if (source->symbol == "DUSD") {
                const auto destination = view->GetLoanTokenByID({futuresValues.destination});
                if (!destination) {
                    return true;
                }
                value.pushKV("destination", destination->symbol);
            } else {
                value.pushKV("destination", "DUSD");
            }

            listFutures.push_back(value);

            return true;
        });

    return GetRPCResultCache().Set(request, listFutures);
}

UniValue getpendingfutureswaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getpendingfutureswaps",
        "Get specific pending futures.\n",
        {
          {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to get all pending future swaps"},
          },
        RPCResult{"{\n"
                  "    owner :       \"address\"\n"
                  "    values : [{\n"
                  "    tokenSymbol : \"SYMBOL\"\n"
                  "    amount :      n.nnnnnnnn\n"
                  "    destination : \"SYMBOL\"\n"
                  "    }...]\n"
                  "}\n"},
        RPCExamples{HelpExampleCli("getpendingfutureswaps", "address")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    UniValue listValues{UniValue::VARR};

    const auto owner = DecodeScript(request.params[0].get_str());

    auto [view, accountView, vaultView] = GetSnapshots();
    const uint32_t height = view->GetLastHeight();

    std::vector<CFuturesUserValue> storedFutures;
    view->ForEachFuturesUserValues(
        [&](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
            if (key.owner == owner) {
                storedFutures.push_back(futuresValues);
            }

            return true;
        },
        {height, owner, std::numeric_limits<uint32_t>::max()});

    for (const auto &item : storedFutures) {
        UniValue value{UniValue::VOBJ};

        const auto source = view->GetToken(item.source.nTokenId);
        if (!source) {
            continue;
        }

        value.pushKV("source", ValueFromAmount(item.source.nValue).getValStr() + '@' + source->symbol);

        if (source->symbol == "DUSD") {
            const auto destination = view->GetLoanTokenByID({item.destination});
            if (!destination) {
                continue;
            }
            value.pushKV("destination", destination->symbol);
        } else {
            value.pushKV("destination", "DUSD");
        }

        listValues.push_back(value);
    }

    UniValue obj{UniValue::VOBJ};
    obj.pushKV("owner", request.params[0].get_str());
    obj.pushKV("values", listValues);
    return GetRPCResultCache().Set(request, obj);
}

UniValue releaselockedtokens(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "releaselockedtokens",
        "\nreleases a tranche of locked loan tokens\n",
        {
          {
                "releasePart",
                RPCArg::Type::NUM,
                RPCArg::Optional::NO,
                "Percentagepoints to be released",
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
        RPCExamples{HelpExampleCli("releaselockedtokens", "3") + HelpExampleRpc("releaselockedtokens", "1.23")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VARR}, true);

    CDataStream varStream(SER_NETWORK, PROTOCOL_VERSION);
    if (request.params.size() != 1 && !request.params[0].isNum()) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid releaseRatio");
    }

    auto releaseRatio = AmountFromValue(request.params[0]) / 100;
    CReleaseLockMessage msg{std::move(releaseRatio)};

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::TokenLockRelease) << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    auto [view, accountView, vaultView] = GetSnapshots();

    const UniValue &txInputs = request.params[1];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(
        pwallet, rawTx.nVersion, auths, true, optAuthTx, txInputs, *view, request.metadata.coinSelectOpts);

    CCoinControl coinControl;

    // Set change to selected foundation address
    if (!auths.empty()) {
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

UniValue listlockedtokens(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listlockedtokens",
        "Get all locked loan tokens.\n",
        {},
        RPCResult{"\"json\"      (string) array containing json-objects having following fields:\n"
                  "    owner  :  \"address\"\n"
                  "    values : [\"amount1@token1\",\"amount1@token1\"...]\n"},
        RPCExamples{HelpExampleCli("listlockedtokens", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    UniValue listLockedTokens{UniValue::VARR};

    auto [view, accountView, vaultView] = GetSnapshots();

    view->ForEachTokenLockUserValues(
        [&, &view = view](const CTokenLockUserKey &key, const CTokenLockUserValue &lockValues) {
            CTxDestination dest;
            ExtractDestination(key.owner, dest);
            if (!IsValidDestination(dest)) {
                return true;
            }

            UniValue value{UniValue::VOBJ};
            value.pushKV("owner", EncodeDestination(dest));
            UniValue balances{UniValue::VARR};
            for (const auto tokenAmount : lockValues.balances) {
                const auto source = view->GetToken(tokenAmount.first);
                if (!source) {
                    continue;
                }
                balances.push_back(ValueFromAmount(tokenAmount.second).getValStr() + '@' + source->symbol);
            }
            value.pushKV("values", balances);

            listLockedTokens.push_back(value);

            return true;
        });

    return GetRPCResultCache().Set(request, listLockedTokens);
}

UniValue getlockedtokens(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getlockedtokens",
        "\nGet specific locked tokens.\n",
        {
          {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to get all locked tokens"},
          },
        RPCResult{"[\"amount1@token1\",\"amount1@token1\"...]\n"},
        RPCExamples{HelpExampleCli("getlockedtokens", "address")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }

    const auto owner = DecodeScript(request.params[0].get_str());

    auto [view, accountView, vaultView] = GetSnapshots();

    CTokenLockUserKey key{owner};
    const auto &value = view->GetTokenLockUserValue(key);

    UniValue obj{UniValue::VARR};
    for (const auto tokenAmount : value.balances) {
        const auto source = view->GetToken(tokenAmount.first);
        if (!source) {
            continue;
        }
        obj.push_back(ValueFromAmount(tokenAmount.second).getValStr() + '@' + source->symbol);
    }
    return GetRPCResultCache().Set(request, obj);
}

UniValue logaccountbalances(const JSONRPCRequest &request) {
    RPCHelpMan{
        "logaccountbalances",
        "\nLogs all account balances in accounts for debugging.\n",
        {
          {"logfile",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "Log file (default = false), if set to true, prints to the log file, otherwise no log output"},
          {"rpcresult",
             RPCArg::Type::BOOL,
             RPCArg::Optional::OMITTED,
             "RPC Result (default = true), if set to true, returns an RPC result, otherwise no RPC output"},
          },
        RPCResult{"{...} (array) Json object with account balances if rpcresult is enabled."
                  "This is for debugging purposes only.\n"},
        RPCExamples{HelpExampleCli("logaccountbalances", "true true")},
    }
        .Check(request);

    auto &p = request.params;
    auto outToLog = false;
    auto outToRpc = true;

    if (p.size() > 0) {
        outToLog = p[0].get_bool();
    }
    if (p.size() > 1) {
        outToRpc = p[1].get_bool();
    }

    auto [view, accountView, vaultView] = GetSnapshots();

    std::map<std::string, std::vector<CTokenAmount>> accounts;
    size_t count{};
    view->ForEachBalance([&](const CScript &owner, CTokenAmount balance) {
        ++count;
        auto ownerStr = ScriptToString(owner);
        if (outToLog) {
            LogPrintf("AccountBalance: (%s: %d@%d)\n", ownerStr, balance.nValue, balance.nTokenId.v);
        }
        if (outToRpc) {
            accounts[ownerStr].push_back(CTokenAmount{{balance.nTokenId.v}, balance.nValue});
        }
        return true;
    });

    if (outToLog) {
        LogPrintf("IndexStats: (balances: %d)\n", count);
    }

    if (!outToRpc) {
        return {};
    }

    UniValue result{UniValue::VOBJ};
    UniValue accountsJson{UniValue::VOBJ};
    for (auto &[key, v] : accounts) {
        UniValue b{UniValue::VARR};
        for (auto &item : v) {
            b.push_back(item.ToString());
        }
        accountsJson.pushKV(key, b);
    }

    result.pushKV("accounts", accountsJson);
    result.pushKV("count", static_cast<uint64_t>(count));
    return result;
}

UniValue listpendingdusdswaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "listpendingdusdswaps",
        "Get all pending DFI-to_DUSD swaps.\n",
        {},
        RPCResult{"\"json\"          (string) array containing json-objects having following fields:\n"
                  "[{\n"
                  "    owner :       \"address\"\n"
                  "    amount :      n.nnnnnnnn\n"
                  "}...]\n"},
        RPCExamples{HelpExampleCli("listpendingdusdswaps", "")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    UniValue listFutures{UniValue::VARR};

    auto [view, accountView, vaultView] = GetSnapshots();

    view->ForEachFuturesDUSD([&](const CFuturesUserKey &key, const CAmount &amount) {
        CTxDestination dest;
        ExtractDestination(key.owner, dest);
        if (!IsValidDestination(dest)) {
            return true;
        }

        UniValue value{UniValue::VOBJ};
        value.pushKV("owner", EncodeDestination(dest));
        value.pushKV("amount", ValueFromAmount(amount));

        listFutures.push_back(value);

        return true;
    });

    return GetRPCResultCache().Set(request, listFutures);
}

UniValue getpendingdusdswaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "getpendingdusdswaps",
        "Get specific pending DFI-to-DUSD swap.\n",
        {
          {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to get all pending future swaps"},
          },
        RPCResult{"{\n"
                  "    owner :       \"address\"\n"
                  "    amount :      n.nnnnnnnn\n"
                  "}\n"},
        RPCExamples{HelpExampleCli("getpendingfutureswaps", "address")},
    }
        .Check(request);

    if (auto res = GetRPCResultCache().TryGet(request)) {
        return *res;
    }
    UniValue listValues{UniValue::VARR};

    const auto owner = DecodeScript(request.params[0].get_str());

    auto [view, accountView, vaultView] = GetSnapshots();
    const uint32_t height = view->GetLastHeight();

    CAmount total{};
    view->ForEachFuturesDUSD(
        [&](const CFuturesUserKey &key, const CAmount &amount) {
            if (key.owner == owner) {
                total += amount;
            }

            return true;
        },
        {height, owner, std::numeric_limits<uint32_t>::max()});

    UniValue obj{UniValue::VOBJ};
    if (total) {
        obj.pushKV("owner", request.params[0].get_str());
        obj.pushKV("amount", ValueFromAmount(total));
    }

    return GetRPCResultCache().Set(request, obj);
}

static const CRPCCommand commands[] = {
  //  category       name                     actor (function)        params
  //  -------------  ------------------------ ----------------------  ----------
    {"accounts", "listaccounts",           &listaccounts,           {"pagination", "verbose", "indexed_amounts", "is_mine_only"}},
    {"accounts", "getaccount",             &getaccount,             {"owner", "pagination", "indexed_amounts"}                  },
    {"accounts",
     "gettokenbalances",                   &gettokenbalances,
     {"pagination", "indexed_amounts", "symbol_lookup", "include_eth"}                                                          },
    {"accounts", "utxostoaccount",         &utxostoaccount,         {"amounts", "inputs"}                                       },
    {"accounts", "sendutxosfrom",          &sendutxosfrom,          {"from", "to", "amount", "change"}                          },
    {"accounts", "accounttoaccount",       &accounttoaccount,       {"from", "to", "inputs"}                                    },
    {"accounts", "accounttoutxos",         &accounttoutxos,         {"from", "to", "inputs"}                                    },
    {"accounts", "listaccounthistory",     &listaccounthistory,     {"owner", "options"}                                        },
    {"accounts", "getaccounthistory",      &getaccounthistory,      {"owner", "blockHeight", "txn"}                             },
    {"accounts", "listburnhistory",        &listburnhistory,        {"options"}                                                 },
    {"accounts", "accounthistorycount",    &accounthistorycount,    {"owner", "options"}                                        },
    {"accounts", "listcommunitybalances",  &listcommunitybalances,  {}                                                          },
    {"accounts", "sendtokenstoaddress",    &sendtokenstoaddress,    {"from", "to", "selectionMode"}                             },
    {"accounts", "transferdomain",         &transferdomain,         {"array"}                                                   },
    {"accounts", "getburninfo",            &getburninfo,            {}                                                          },
    {"accounts", "executesmartcontract",   &executesmartcontract,   {"name", "amount", "inputs"}                                },
    {"accounts", "futureswap",             &futureswap,             {"address", "amount", "destination", "inputs"}              },
    {"accounts", "withdrawfutureswap",     &withdrawfutureswap,     {"address", "amount", "destination", "inputs"}              },
    {"accounts", "listpendingfutureswaps", &listpendingfutureswaps, {}                                                          },
    {"accounts", "getpendingfutureswaps",  &getpendingfutureswaps,  {"address"}                                                 },
    {"accounts", "listpendingdusdswaps",   &listpendingdusdswaps,   {}                                                          },
    {"accounts", "getpendingdusdswaps",    &getpendingdusdswaps,    {"address"}                                                 },
    {"hidden",   "logaccountbalances",     &logaccountbalances,     {"logfile", "rpcresult"}                                    },
    {"accounts", "listlockedtokens",       &listlockedtokens,       {}                                                          },
    {"accounts", "getlockedtokens",        &getlockedtokens,        {"address"}                                                 },
    {"accounts", "releaselockedtokens",    &releaselockedtokens,    {"releasePart"}                                             },
};

void RegisterAccountsRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}

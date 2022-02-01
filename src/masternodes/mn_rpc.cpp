// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_rpc.h>

#include <base58.h>
#include <policy/settings.h>

extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
extern bool DecodeHexTx(CTransaction& tx, std::string const& strHexTx); // in core_io.h

CAccounts GetAllMineAccounts(CWallet * const pwallet) {

    CAccounts walletAccounts;

    LOCK(cs_main);
    CCustomCSView mnview(*pcustomcsview);
    auto targetHeight = ::ChainActive().Height() + 1;

    mnview.ForEachAccount([&](CScript const & account) {
        if (IsMineCached(*pwallet, account) == ISMINE_SPENDABLE) {
            mnview.CalculateOwnerRewards(account, targetHeight);
            mnview.ForEachBalance([&](CScript const & owner, CTokenAmount balance) {
                return account == owner && walletAccounts[owner].Add(balance);
            }, {account, DCT_ID{}});
        }
        return true;
    });

    return walletAccounts;
}

CAccounts SelectAccountsByTargetBalances(const CAccounts& accounts, const CBalances& targetBalances, AccountSelectionMode selectionMode) {

    std::set<DCT_ID> tokenIds;
    std::vector<std::pair<CScript, CBalances>> foundAccountsBalances;
    // iterate at all accounts to finding all accounts with neccessaru token balances
    for (const auto& account : accounts) {
        // selectedBalances accumulates overlap between account balances and residual balances
        CBalances selectedBalances;
        // iterate at residual balances to find neccessary tokens in account
        for (const auto& balance : targetBalances.balances) {
            // find neccessary token amount from current account
            const auto& accountBalances = account.second.balances;
            auto foundTokenAmount = accountBalances.find(balance.first);
            // account balance has neccessary token
            if (foundTokenAmount != accountBalances.end()) {
                tokenIds.insert(foundTokenAmount->first);
                // add token amount to selected balances from current account
                selectedBalances.Add(CTokenAmount{foundTokenAmount->first, foundTokenAmount->second});
            }
        }
        if (!selectedBalances.balances.empty()) {
            // added account and selected balances from account to selected accounts
            foundAccountsBalances.emplace_back(account.first, selectedBalances);
        }
    }

    auto sortByTokenAmount = [selectionMode](const std::pair<CScript, CBalances>& p1,
                                             const std::pair<CScript, CBalances>& p2,
                                             const DCT_ID id) {
        auto it1 = p1.second.balances.find(id);
        auto it2 = p2.second.balances.find(id);
        if (it1 != p1.second.balances.end() && it2 != p2.second.balances.end()) {
            return selectionMode == SelectionCrumbs ? it1->second < it2->second : it1->second > it2->second;
        } else {
            return false; // should never happen
        }
    };

    CAccounts selectedAccountsBalances;
    CBalances residualBalances(targetBalances);
    // selecting accounts balances
    for (const auto tokenId : tokenIds) {
        if (selectionMode != SelectionForward) {
            std::sort(foundAccountsBalances.begin(), foundAccountsBalances.end(),
                      std::bind(sortByTokenAmount, std::placeholders::_1, std::placeholders::_2, tokenId));
        }
        for (const auto& accountBalances : foundAccountsBalances) {
            // Substract residualBalances and tokenBalance with remainder.
            // Substraction with remainder will remove tokenAmount from balances if remainder
            // of token's amount is not zero (we got negative result of substraction)
            auto itTokenAmount = accountBalances.second.balances.find(tokenId);
            if (itTokenAmount != accountBalances.second.balances.end()) {
                CTokenAmount tokenBalance{itTokenAmount->first, itTokenAmount->second};
                auto remainder = residualBalances.SubWithRemainder(tokenBalance);
                // calculate final balances by substraction account balances with remainder
                // it is necessary to get rid of excess
                if (remainder != tokenBalance) {
                    tokenBalance.Sub(remainder.nValue);
                    selectedAccountsBalances[accountBalances.first].Add(tokenBalance);
                }
            }
        }
        // if residual balances is empty we found all neccessary token amounts and can stop selecting
        if (residualBalances.balances.empty()) {
            break;
        }
    }

    const auto selectedBalancesSum = SumAllTransfers(selectedAccountsBalances);
    if (selectedBalancesSum != targetBalances) {
        // we have not enough tokens balance to transfer
        return {};
    }
    return selectedAccountsBalances;
}

CMutableTransaction fund(CMutableTransaction & mtx, CWalletCoinsUnlocker& pwallet, CTransactionRef optAuthTx, CCoinControl* coin_control) {
    CAmount fee_out;
    int change_position = mtx.vout.size();

    std::string strFailReason;
    CCoinControl coinControl;
    if (coin_control) {
        coinControl = *coin_control;
    }
    // add outputs from possible linked auth tx into 'linkedCoins' pool
    if (optAuthTx) {
        for (size_t i = 0; i < optAuthTx->vout.size(); ++i) {
            CTxOut const & out = optAuthTx->vout[i];
            if (!out.scriptPubKey.IsUnspendable()) { // skip for possible metadata
                coinControl.m_linkedCoins.emplace(COutPoint(optAuthTx->GetHash(), i), out);
            }
        }
    }

    // we does not honor non locking spends anymore
    // it ensures auto auth not overlap regular tx inputs
    const bool lockUnspents = true;
    if (!pwallet->FundTransaction(mtx, fee_out, change_position, strFailReason, lockUnspents, {} /*setSubtractFeeFromOutputs*/, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }
    for (auto& txin : mtx.vin) {
        if (!coinControl.IsSelected(txin.prevout)) {
            pwallet.AddLockedCoin(txin.prevout);
        }
    }
    for (const auto& coin : coinControl.m_linkedCoins) {
        pwallet.AddLockedCoin(coin.first);
    }
    return mtx;
}

static CTransactionRef sign(CMutableTransaction& mtx, CWallet* const pwallet, CTransactionRef optAuthTx) {

    // assemble prevouts from optional linked tx
    UniValue prevtxs(UniValue::VARR);
    if (optAuthTx) {
        for (size_t i = 0; i < optAuthTx->vout.size(); ++i) {
            if (!optAuthTx->vout[i].scriptPubKey.IsUnspendable()) {
                UniValue prevout(UniValue::VOBJ);
                prevout.pushKV("txid", optAuthTx->GetHash().GetHex());
                prevout.pushKV("vout", (uint64_t)i);
                prevout.pushKV("scriptPubKey", optAuthTx->vout[i].scriptPubKey.GetHex());
                //prevout.pushKV("redeemScript", );
                //prevout.pushKV("witnessScript", );
                prevout.pushKV("amount", ValueFromAmount(optAuthTx->vout[i].nValue));
                prevtxs.push_back(prevout);
            }
        }
    }

    /// from "signrawtransactionwithwallet":

    // Fetch previous transactions (inputs):
    std::map<COutPoint, Coin> coins;
    for (const CTxIn& txin : mtx.vin) {
        coins[txin.prevout]; // Create empty map entry keyed by prevout.
    }
    pwallet->chain().findCoins(coins);  // LOCK2(cs_main, ::mempool.cs);

    UniValue signedVal = SignTransaction(mtx, prevtxs, pwallet, coins, false, {} /*hashType*/);
    if (signedVal["complete"].getBool()) {
        if (!DecodeHexTx(mtx, signedVal["hex"].get_str(), true)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Can't decode signed auth TX");
        }
    }
    else {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Can't sign TX: " + signedVal["errors"].write());
    }
    return MakeTransactionRef(std::move(mtx));
}

static CTransactionRef send(CTransactionRef tx, CTransactionRef optAuthTx) {

    if (optAuthTx) {
        send(optAuthTx, {});
    }

    // from "sendrawtransaction":
    CAmount max_raw_tx_fee = {COIN / 10}; /// @todo check it with 0

    std::string err_string;
    AssertLockNotHeld(cs_main);
    const TransactionError err = BroadcastTransaction(tx, err_string, max_raw_tx_fee, /*relay*/
                                                      true, /*wait_callback*/ true);
    if (TransactionError::OK != err) {
        throw JSONRPCTransactionError(err, err_string);
    }
    return tx;
}

CWalletCoinsUnlocker::CWalletCoinsUnlocker(std::shared_ptr<CWallet> pwallet) : 
    pwallet(std::move(pwallet)) {
}

CWalletCoinsUnlocker::~CWalletCoinsUnlocker() {
    if (!coins.empty()) {
        LOCK(pwallet->cs_wallet);
        for (auto& coin : coins) {
            pwallet->UnlockCoin(coin);
        }
    }
}

CWallet* CWalletCoinsUnlocker::operator->() {
    return pwallet.get();
}

CWallet& CWalletCoinsUnlocker::operator*() {
    return *pwallet;
}

CWalletCoinsUnlocker::operator CWallet*() {
    return pwallet.get();
}

void CWalletCoinsUnlocker::AddLockedCoin(const COutPoint& coin) {
    coins.push_back(coin);
}

CTransactionRef signsend(CMutableTransaction& mtx, CWalletCoinsUnlocker& pwallet, CTransactionRef optAuthTx) {
    return send(sign(mtx, pwallet, optAuthTx), optAuthTx);
}

// returns either base58/bech32 address, or hex if format is unknown
std::string ScriptToString(CScript const& script) {
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) {
        return script.GetHex();
    }
    return EncodeDestination(dest);
}

int chainHeight(interfaces::Chain::Lock& locked_chain)
{
    LOCK(locked_chain.mutex());
    if (auto height = locked_chain.getHeight())
        return *height;
    return 0;
}

static std::vector<CTxIn> GetInputs(UniValue const& inputs) {
    std::vector<CTxIn> vin{};
    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        vin.push_back(CTxIn(txid, nOutput));
    }
    return vin;
}

std::optional<CScript> AmIFounder(CWallet* const pwallet) {
    for(auto const & script : Params().GetConsensus().foundationMembers) {
        if(IsMineCached(*pwallet, script) == ISMINE_SPENDABLE)
            return { script };
    }
    return {};
}

static std::optional<CTxIn> GetAuthInputOnly(CWalletCoinsUnlocker& pwallet, CTxDestination const& auth) {

    std::vector<COutput> vecOutputs;
    CCoinControl cctl;
    cctl.m_min_depth = 1;
    cctl.matchDestination = auth;
    cctl.m_tokenFilter = {DCT_ID{0}};

    auto locked_chain = pwallet->chain().lock();
    LOCK2(pwallet->cs_wallet, locked_chain->mutex());

    pwallet->AvailableCoins(*locked_chain, vecOutputs, true, &cctl, 1, MAX_MONEY, MAX_MONEY, 1);

    if (vecOutputs.empty()) {
        return {};
    }
    // any selected inputs should be mark as locked
    CTxIn txin(vecOutputs[0].tx->GetHash(), vecOutputs[0].i);
    pwallet->LockCoin(txin.prevout);
    pwallet.AddLockedCoin(txin.prevout);
    return txin;
}

static CTransactionRef CreateAuthTx(CWalletCoinsUnlocker& pwallet, std::set<CScript> const & auths, int32_t txVersion) {
    CMutableTransaction mtx(txVersion);
    CCoinControl coinControl;

    // Encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AutoAuthPrep);

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    mtx.vout.push_back(CTxOut(0, scriptMeta));

    // Only set change to auth on single auth TXs
    if (auths.size() == 1) {

        // Get auth ScriptPubkey
        auto auth = *auths.cbegin();

        // Create output to cover 1KB transaction
        CTxOut authOut(GetMinimumFee(*pwallet, 1000, coinControl, nullptr), auth);
        mtx.vout.push_back(authOut);
        fund(mtx, pwallet, {}, &coinControl);

        // AutoAuthPrep, auth output and change
        if (mtx.vout.size() == 3) {
            mtx.vout[1].nValue += mtx.vout[2].nValue; // Combine values
            mtx.vout[1].scriptPubKey = auth;
            mtx.vout.erase(mtx.vout.begin() + 2); // Delete "change" output
        }

        return sign(mtx, pwallet, {});
    }

    // create tx with one dust output per script
    for (auto const & auth : auths) {
        CTxOut authOut(1, auth);
        authOut.nValue = GetDustThreshold(authOut, mtx.nVersion, ::dustRelayFee);
        mtx.vout.push_back(authOut);
    }

    return fund(mtx, pwallet, {}, &coinControl), sign(mtx, pwallet, {});
}

static std::optional<CTxIn> GetAnyFoundationAuthInput(CWalletCoinsUnlocker& pwallet) {
    for (auto const & founderScript : Params().GetConsensus().foundationMembers) {
        if (IsMineCached(*pwallet, founderScript) == ISMINE_SPENDABLE) {
            CTxDestination destination;
            if (ExtractDestination(founderScript, destination)) {
                if (auto auth = GetAuthInputOnly(pwallet, destination)) {
                    return auth;
                }
            }
        }
    }
    return {};
}

std::vector<CTxIn> GetAuthInputsSmart(CWalletCoinsUnlocker& pwallet, int32_t txVersion, std::set<CScript>& auths, bool needFounderAuth, CTransactionRef & optAuthTx, UniValue const& explicitInputs) {

    if (!explicitInputs.isNull() && !explicitInputs.empty()) {
        return GetInputs(explicitInputs);
    }

    std::vector<CTxIn> result;
    std::set<CScript> notFoundYet;
    // first, look for "common" auth inputs, tracking missed
    for (auto const & auth : auths) {
        CTxDestination destination;
        if (!ExtractDestination(auth, destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Can't extract destination for " + auth.GetHex());
        }
        if (IsMineCached(*pwallet, auth) != ISMINE_SPENDABLE) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Incorrect authorization for " + EncodeDestination(destination));
        }
        auto authInput = GetAuthInputOnly(pwallet, destination);
        if (authInput) {
            result.push_back(authInput.value());
        }
        else {
            notFoundYet.insert(auth);
        }
    }
    // Look for founder's auth. minttoken may already have an auth in result.
    if (needFounderAuth && result.empty()) {
        auto anyFounder = AmIFounder(pwallet);
        if (!anyFounder) {
            // Called from minttokens if auth not empty here which can use collateralAddress
            if (auths.empty()) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Need foundation member authorization");
            }
        } else {
            auths.insert(anyFounder.value());
            auto authInput = GetAnyFoundationAuthInput(pwallet);
            if (authInput) {
                result.push_back(authInput.value());
            }
            else {
                notFoundYet.insert(anyFounder.value());
            }
        }
    }

    // at last, create additional tx for missed
    if (!notFoundYet.empty()) {
        try {
            optAuthTx = CreateAuthTx(pwallet, notFoundYet, txVersion); // success or throw
        } catch (const UniValue& objError) {
            throw JSONRPCError(objError["code"].get_int(), "Add-on auth TX failed: " + objError["message"].getValStr());
        }
        // if we are here - we've got signed optional auth tx - add all of the outputs into inputs (to do not miss any coins for sure)
        for (size_t i = 0; i < optAuthTx->vout.size(); ++i) {
            if (!optAuthTx->vout[i].scriptPubKey.IsUnspendable())
                result.push_back(CTxIn(optAuthTx->GetHash(), i));
        }
    }

    return result;
}

void execTestTx(const CTransaction& tx, uint32_t height, CTransactionRef optAuthTx) {
    std::vector<unsigned char> metadata;
    auto txType = GuessCustomTxType(tx, metadata);
    auto txMessage = customTypeToMessage(txType);
    auto res = CustomMetadataParse(height, Params().GetConsensus(), metadata, txMessage);
    if (res) {
        LOCK(cs_main);
        CCoinsViewCache coins(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coins, *optAuthTx, height);
        CCustomCSView view(*pcustomcsview);
        res = CustomTxVisit(view, coins, tx, height, Params().GetConsensus(), txMessage, ::ChainActive().Tip()->nTime);
    }
    if (!res) {
        if (res.code == CustomTxErrCodes::NotEnoughBalance) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                               strprintf("Test %sTx execution failed: not enough balance on owner's account, call utxostoaccount to increase it.\n%s", ToString(txType), res.msg));
        }
        throw JSONRPCError(RPC_INVALID_REQUEST, strprintf("Test %sTx execution failed:\n%s", ToString(txType), res.msg));
    }
}

CWalletCoinsUnlocker GetWallet(const JSONRPCRequest& request) {
    auto wallet = GetWalletForJSONRPCRequest(request);

    EnsureWalletIsAvailable(wallet.get(), request.fHelp);
    return CWalletCoinsUnlocker{std::move(wallet)};
}

UniValue setgov(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"setgov",
               "\nSet special 'governance' variables:: ATTRIBUTES, ICX_TAKERFEE_PER_BTC, LP_LOAN_TOKEN_SPLITS, LP_SPLITS, ORACLE_BLOCK_INTERVAL, ORACLE_DEVIATION\n",
               {
                    {"variables", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Object with variables",
                        {
                            {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Variable's name is the key, value is the data. Exact data type depends on variable's name."},
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
                       HelpExampleCli("setgov", "'{\"LP_SPLITS\": {\"2\":0.2,\"3\":0.8}'")
                       + HelpExampleRpc("setgov", "'{\"ICX_TAKERFEE_PER_BTC\":109440}'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CDataStream varStream(SER_NETWORK, PROTOCOL_VERSION);
    if (request.params.size() > 0 && request.params[0].isObject()) {
        for (const std::string& name : request.params[0].getKeys()) {
            auto gv = GovVariable::Create(name);
            if (!gv) {
                throw JSONRPCError(RPC_INVALID_REQUEST, "Variable " + name + " not registered");
            }
            const auto res = gv->Import(request.params[0][name]);
            if (!res) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, res.msg);
            }
            varStream << name << *gv;
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SetGovVariable)
             << varStream;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[1];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to selected foundation address
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

UniValue setgovheight(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"setgovheight",
               "\nChange governance variable at height: ATTRIBUTES, ICX_TAKERFEE_PER_BTC, LP_LOAN_TOKEN_SPLITS, LP_SPLITS, ORACLE_DEVIATION\n",
               {
                       {"variables", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Object with variable",
                        {
                                {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Variable name is the key, value is the data. Exact data type depends on variable name."},
                        },
                       },
                       {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "Start height for the changes to take effect."},
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
                       HelpExampleCli("setgovheight", "'{\"LP_SPLITS\": {\"2\":0.2,\"3\":0.8}' 100000")
                       + HelpExampleRpc("setgovheight", "'{\"ICX_TAKERFEE_PER_BTC\":109440}', 100000")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VNUM, UniValue::VARR}, true);

    CDataStream varStream(SER_NETWORK, PROTOCOL_VERSION);
    const auto keys = request.params[0].getKeys();
    if (!keys.empty()) {
        const std::string& name = request.params[0].getKeys()[0];
        auto gv = GovVariable::Create(name);
        if (!gv) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Variable " + name + " not registered");
        }
        const auto res = gv->Import(request.params[0][name]);
        if (!res) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, res.msg);
        }
        varStream << name << *gv;
    } else {
        throw JSONRPCError(RPC_INVALID_REQUEST, "No Governance variable provided.");
    }

    const uint32_t startHeight = request.params[1].get_int();

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SetGovVariableHeight)
             << varStream
             << startHeight;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const & txInputs = request.params[2];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to selected foundation address
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

UniValue getgov(const JSONRPCRequest& request) {
    RPCHelpMan{"getgov",
               "\nReturns information about governance variable:\n"
               "ATTRIBUTES, ICX_TAKERFEE_PER_BTC, LP_DAILY_LOAN_TOKEN_REWARD, LP_LOAN_TOKEN_SPLITS, LP_DAILY_DFI_REWARD,\n"
               "LOAN_LIQUIDATION_PENALTY, LP_SPLITS, ORACLE_BLOCK_INTERVAL, ORACLE_DEVIATION\n",
               {
                       {"name", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Variable name"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with variable information\n"
               },
               RPCExamples{
                       HelpExampleCli("getgov", "LP_SPLITS")
                       + HelpExampleRpc("getgov", "LP_DAILY_DFI_REWARD")
               },
    }.Check(request);

    LOCK(cs_main);

    auto name = request.params[0].getValStr();
    auto var = pcustomcsview->GetVariable(name);
    if (var) {
        UniValue ret(UniValue::VOBJ);
        ret.pushKV(var->GetName(),var->Export());
        return ret;
    }
    throw JSONRPCError(RPC_INVALID_REQUEST, "Variable '" + name + "' not registered");
}

UniValue listgovs(const JSONRPCRequest& request) {
    RPCHelpMan{"listgovs",
               "\nReturns information about all governance variables including pending changes\n",
               {},
               RPCResult{
                       "[[{id:{...}},{height:{...}},...], ...]     (array) Json array with JSON objects with variable information\n"
               },
               RPCExamples{
                       HelpExampleCli("listgovs", "")
                       + HelpExampleRpc("listgovs", "")
               },
    }.Check(request);

    std::vector<std::string> vars{"ICX_TAKERFEE_PER_BTC", "LP_DAILY_LOAN_TOKEN_REWARD", "LP_LOAN_TOKEN_SPLITS", "LP_DAILY_DFI_REWARD",
                                  "LOAN_LIQUIDATION_PENALTY", "LP_SPLITS", "ORACLE_BLOCK_INTERVAL", "ORACLE_DEVIATION", "ATTRIBUTES"};

    LOCK(cs_main);

    // Get all stored Gov var changes
    auto pending = pcustomcsview->GetAllStoredVariables();

    UniValue result(UniValue::VARR);
    for (const auto& name : vars) {
        UniValue innerResult(UniValue::VARR);
        auto var = pcustomcsview->GetVariable(name);
        if (var) {
            UniValue ret(UniValue::VOBJ);
            ret.pushKV(var->GetName(),var->Export());
            innerResult.push_back(ret);
        }

        // Get and add any pending changes
        for (const auto& items : pending[name]) {
            UniValue ret(UniValue::VOBJ);
            ret.pushKV(std::to_string(items.first),items.second->Export());
            innerResult.push_back(ret);
        }
        result.push_back(innerResult);
    }

    return result;
}


UniValue isappliedcustomtx(const JSONRPCRequest& request) {
    RPCHelpMan{"isappliedcustomtx",
               "\nChecks that custom transaction was affected on chain\n",
               {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "A transaction hash"},
                    {"blockHeight", RPCArg::Type::NUM, RPCArg::Optional::NO, "The height of block which contain tx"}
               },
               RPCResult{
                       "(bool) The boolean indicate that custom transaction was affected on chain\n"
               },
               RPCExamples{
                       HelpExampleCli("isappliedcustomtx", "b2bb09ffe9f9b292f13d23bafa1225ef26d0b9906da7af194c5738b63839b235 1005")
                       + HelpExampleRpc("isappliedcustomtx", "b2bb09ffe9f9b292f13d23bafa1225ef26d0b9906da7af194c5738b63839b235 1005")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VNUM}, false);

    LOCK(cs_main);

    UniValue result(UniValue::VBOOL);
    result.setBool(false);

    uint256 txHash = ParseHashV(request.params[0], "txid");
    int blockHeight = request.params[1].get_int();

    auto blockindex = ::ChainActive()[blockHeight];
    if (!blockindex) {
        return result;
    }

    uint256 hashBlock;
    CTransactionRef tx;
    if (!GetTransaction(txHash, tx, Params().GetConsensus(), hashBlock, blockindex)) {
        return result;
    }

    if (tx->IsCoinBase() && blockHeight > 0) {
        return result;
    }

    std::vector<unsigned char> metadata;
    auto txType = GuessCustomTxType(*tx, metadata);
    if (txType == CustomTxType::None) {
        return result;
    }

    // post Dakota it's not allowed tx to be skipped
    // so tx that can be found in a block is applyed
    if (blockHeight >= Params().GetConsensus().DakotaHeight) {
        result.setBool(true);
    } else {
        result.setBool(!IsSkippedTx(tx->GetHash()));
    }

    return result;
}


static std::string GetContractCall(const std::string& str) {
    if (str == "DFIP2201") {
        return "dbtcdfiswap";
    }

    return str;
}

UniValue listsmartcontracts(const JSONRPCRequest& request) {
    RPCHelpMan{"listsmartcontracts",
               "\nReturns information on smart contracts\n",
               {
               },
               RPCResult{
                       "(array) JSON array with smart contract information\n"
                       "\"name\":\"name\"         smart contract name\n"
                       "\"address\":\"address\"   smart contract address\n"
                       "\"token id\":x.xxxxxxxx   smart contract balance per token\n"
               },
               RPCExamples{
                       HelpExampleCli("listsmartcontracts", "")
                       + HelpExampleRpc("listsmartcontracts", "")
               },
    }.Check(request);

    UniValue arr(UniValue::VARR);
    for (const auto& item : Params().GetConsensus().smartContracts) {
        UniValue obj(UniValue::VOBJ);
        CTxDestination dest;
        ExtractDestination(item.second, dest);
        obj.pushKV("name", item.first);
        obj.pushKV("call", GetContractCall(item.first));
        obj.pushKV("address", EncodeDestination(dest));

        pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount balance) {
            if (owner != item.second) {
                return false;
            }
            obj.pushKV(balance.nTokenId.ToString(), ValueFromAmount(balance.nValue));
            return true;
        }, BalanceKey{item.second, {0}});

        arr.push_back(obj);
    }
    return arr;
}


static UniValue clearmempool(const JSONRPCRequest& request)
{
    auto pwallet = GetWallet(request);

    RPCHelpMan("clearmempool",
               "\nClears the memory pool and returns a list of the removed transactions.\n",
               {},
               RPCResult{
                       "[                     (json array of string)\n"
                       "  \"hash\"              (string) The transaction hash\n"
                       "  ,...\n"
                       "]\n"
               },
               RPCExamples{
                       HelpExampleCli("clearmempool", "")
                       + HelpExampleRpc("clearmempool", "")
               }
    ).Check(request);

    std::vector<uint256> vtxid;
    mempool.queryHashes(vtxid);

    UniValue removed(UniValue::VARR);
    for (const uint256& hash : vtxid)
        removed.push_back(hash.ToString());

    LOCK(cs_main);
    mempool.clear();

    std::vector<uint256> vHashOut;
    pwallet->ZapSelectTx(vtxid, vHashOut);

    return removed;
}

static const CRPCCommand commands[] =
{
//  category        name                     actor (function)        params
//  --------------  ----------------------   --------------------    ----------,
    {"blockchain",  "setgov",                &setgov,                {"variables", "inputs"}},
    {"blockchain",  "setgovheight",          &setgovheight,          {"variables", "height", "inputs"}},
    {"blockchain",  "getgov",                &getgov,                {"name"}},
    {"blockchain",  "listgovs",              &listgovs,              {""}},
    {"blockchain",  "isappliedcustomtx",     &isappliedcustomtx,     {"txid", "blockHeight"}},
    {"blockchain",  "listsmartcontracts",    &listsmartcontracts,    {}},
    {"blockchain",  "clearmempool",          &clearmempool,          {} },
};

void RegisterMNBlockchainRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

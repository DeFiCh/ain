// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/criminals.h>
#include <masternodes/mn_checks.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <core_io.h>
#include <consensus/validation.h>
#include <index/txindex.h>
#include <net.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <rpc/client.h>
#include <rpc/server.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/standard.h>
#include <univalue/include/univalue.h>
#include <util/validation.h>
#include <validation.h>
#include <version.h>

//#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/ismine.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <rpc/rawtransaction_util.h>

extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
extern bool DecodeHexTx(CTransaction& tx, std::string const& strHexTx); // in core_io.h

extern UniValue ListReceived(interfaces::Chain::Lock& locked_chain, CWallet * const pwallet, const UniValue& params, bool by_label) EXCLUSIVE_LOCKS_REQUIRED(pwallet->cs_wallet);

static CAccounts FindAccountsFromWallet(CWallet* const pwallet, bool includeWatchOnly = false) {

    // make request for getting all addresses from wallet
    UniValue params(UniValue::VARR);
    // min confirmations
    params.push_back(UniValue(0));
    // include empty addresses
    params.push_back(UniValue(true));
    // include watch only addresses
    params.push_back(UniValue(includeWatchOnly));

    auto locked_chain = pwallet->chain().lock();
    LOCK(pwallet->cs_wallet);

    UniValue addresses = ListReceived(*locked_chain, pwallet, params, false);

    CAccounts walletAccounts;
    {
        LOCK(cs_main);
        for (uint32_t i = 0; i < addresses.size(); i++) {
            CScript ownerScript = DecodeScript(addresses[i]["address"].get_str());
            CBalances accountBalances;
            pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
                if (owner != ownerScript)
                    return false;
                accountBalances.Add(balance);
                return true;
            }, BalanceKey{ownerScript, 0});
            if (!accountBalances.balances.empty())
                walletAccounts.emplace(ownerScript, accountBalances);
        }
    }

    return walletAccounts;
}

typedef enum {
    // selecting accounts without sorting
    SelectionForward,
    // selecting accounts by ascending of sum token amounts
    // it means that we select first accounts with min sum of
    // neccessary token amounts
    SelectionCrumbs,
    // selecting accounts by descending of sum token amounts
    // it means that we select first accounts with max sum of
    // neccessary token amounts
    SelectionPie,
} AccountSelectionMode;

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

static CAccounts SelectAccountsByTargetBalances(const CAccounts& accounts, const CBalances& targetBalances, AccountSelectionMode selectionMode) {

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
                // add token amount to selected balances from current account
                selectedBalances.Add(CTokenAmount{foundTokenAmount->first, foundTokenAmount->second});
            }
        }
        if (!selectedBalances.balances.empty()) {
            // added account and selected balances from account to selected accounts
            foundAccountsBalances.emplace_back(account.first, selectedBalances);
        }
    }

    if (selectionMode != SelectionForward) {
        // we need sort vector by ascending or descending sum of token amounts
        std::sort(foundAccountsBalances.begin(), foundAccountsBalances.end(), [&](const std::pair<CScript, CBalances>& p1, const std::pair<CScript, CBalances>& p2) {
            return (selectionMode == SelectionCrumbs) ?
                    p1.second.GetAllTokensAmount() > p2.second.GetAllTokensAmount() :
                    p1.second.GetAllTokensAmount() < p2.second.GetAllTokensAmount();
        });
    }

    CAccounts selectedAccountsBalances;
    CBalances residualBalances(targetBalances);
    // selecting accounts balances
    for (const auto& accountBalances : foundAccountsBalances) {
        // Substract residualBalances and selectedBalances with remainder.
        // Substraction with remainder will remove tokenAmount from balances if remainder
        // of token's amount is not zero (we got negative result of substraction)
        CBalances finalBalances(accountBalances.second);
        CBalances remainder = residualBalances.SubBalancesWithRemainder(finalBalances.balances);
        // calculate final balances by substraction account balances with remainder
        // it is necessary to get rid of excess
        finalBalances.SubBalances(remainder.balances);
        if (!finalBalances.balances.empty()) {
            selectedAccountsBalances.emplace(accountBalances.first, finalBalances);
        }
        // if residual balances is empty we found all neccessary token amounts and can stop selecting
        if (residualBalances.balances.empty()) {
            break;
        }
    }

    const auto selectedBalancesSum = SumAllTransfers(selectedAccountsBalances);
    if (selectedBalancesSum < targetBalances) {
        // we have not enough tokens balance to transfer
        return {};
    }
    return selectedAccountsBalances;
}

// Special guarding object. Should be created before the first use of funding (straight or by GetAuthInputsSmart())
struct LockedCoinsScopedGuard
    {
        CWallet * const pwallet;
        std::set<COutPoint> lockedCoinsBackup;

    LockedCoinsScopedGuard(CWallet* const pwl) : pwallet(pwl)
    {
        LOCK(pwallet->cs_wallet);
        lockedCoinsBackup = pwallet->setLockedCoins;
    }

    ~LockedCoinsScopedGuard()
    {
        LOCK(pwallet->cs_wallet);
        if (lockedCoinsBackup.empty()) {
            pwallet->UnlockAllCoins();
        }
        else {
            std::vector<COutPoint> diff;
            std::set_difference(pwallet->setLockedCoins.begin(), pwallet->setLockedCoins.end(), lockedCoinsBackup.begin(), lockedCoinsBackup.end(), std::back_inserter(diff));
            for (auto const & coin : diff) {
                pwallet->UnlockCoin(coin);
            }
        }
    }
};

static CMutableTransaction fund(CMutableTransaction & mtx, CWallet* const pwallet, CTransactionRef optAuthTx, CCoinControl* coin_control = nullptr, bool lockUnspents = false) {
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

    if (!pwallet->FundTransaction(mtx, fee_out, change_position, strFailReason, lockUnspents, {} /*setSubtractFeeFromOutputs*/, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }
    return mtx;
}

static CTransactionRef
sign(CMutableTransaction& mtx, CWallet* const pwallet, CTransactionRef optAuthTx) {

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

static CTransactionRef
send(CTransactionRef tx, CTransactionRef optAuthTx) {

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

static CTransactionRef
signsend(CMutableTransaction& mtx, CWallet* const pwallet, CTransactionRef optAuthTx/* = {}*/) {
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

static int chainHeight(interfaces::Chain::Lock& locked_chain)
{
    if (auto height = locked_chain.getHeight())
        return *height;
    return 0;
}

CAmount EstimateMnCreationFee(int targetHeight) {
    // Current height + (1 day blocks) to avoid rejection;
    targetHeight += (60 * 60 / Params().GetConsensus().pos.nTargetSpacing);
    return GetMnCreationFee(targetHeight);
}

std::vector<CTxIn> GetInputs(UniValue const& inputs) {
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

boost::optional<CScript> AmIFounder(CWallet* const pwallet) {
    for(auto const & script : Params().GetConsensus().foundationMembers) {
        if(IsMineCached(*pwallet, script) == ISMINE_SPENDABLE)
            return { script };
    }
    return {};
}

static boost::optional<CTxIn> GetAuthInputOnly(CWallet* const pwallet, CTxDestination const& auth) {

    std::vector<COutput> vecOutputs;
    CCoinControl cctl;
    cctl.m_avoid_address_reuse = false;
    cctl.m_min_depth = 1;
    cctl.m_max_depth = 999999999;
    cctl.matchDestination = auth;
    cctl.m_tokenFilter = {DCT_ID{0}};

    auto locked_chain = pwallet->chain().lock();
    LOCK(pwallet->cs_wallet);

    pwallet->AvailableCoins(*locked_chain, vecOutputs, true, &cctl, 1, MAX_MONEY, MAX_MONEY, 1);

    if (vecOutputs.empty()) {
        return {};
    }
    return { CTxIn(vecOutputs[0].tx->GetHash(), vecOutputs[0].i) };
}

CTransactionRef CreateAuthTx(CWallet* const pwallet, std::set<CScript> const & auths, int32_t txVersion) {
    CMutableTransaction mtx(txVersion);
    CCoinControl coinControl;

    // Only set change to auth on single auth TXs
    if (auths.size() == 1) {

        // Get auth ScriptPubkey
        auto auth = *auths.cbegin();

        // Create output to cover 1KB transaction
        CTxOut authOut(GetMinimumFee(*pwallet, 1000, coinControl, nullptr), auth);
        mtx.vout.push_back(authOut);
        fund(mtx, pwallet, {}, &coinControl, true /*lockUnspents*/);

        // Auth output and change
        if (mtx.vout.size() == 2) {
            mtx.vout[0].nValue += mtx.vout[1].nValue; // Combine values
            mtx.vout[0].scriptPubKey = auth;
            mtx.vout.erase(mtx.vout.begin() + 1); // Delete "change" output
        }

        return sign(mtx, pwallet, {});
    }

    // create tx with one dust output per script
    for (auto const & auth : auths) {
        CTxOut authOut(1, auth);
        authOut.nValue = GetDustThreshold(authOut, mtx.nVersion, ::dustRelayFee);
        mtx.vout.push_back(authOut);
    }

    return fund(mtx, pwallet, {}, &coinControl, true /*lockUnspents*/), sign(mtx, pwallet, {});
}

static boost::optional<CTxIn> GetAnyFoundationAuthInput(CWallet* const pwallet) {
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

static std::vector<CTxIn> GetAuthInputsSmart(CWallet* const pwallet, int32_t txVersion, std::set<CScript>& auths, bool needFounderAuth, CTransactionRef & optAuthTx, UniValue const& explicitInputs) {

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
            result.push_back(authInput.get());
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
            auths.insert(anyFounder.get());
            auto authInput = GetAnyFoundationAuthInput(pwallet);
            if (authInput) {
                result.push_back(authInput.get());
            }
            else {
                notFoundYet.insert(anyFounder.get());
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


static CWallet* GetWallet(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, request.fHelp);
    return pwallet;
}

/*
 *
 *  Issued by: any
*/
UniValue createmasternode(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createmasternode",
               "\nCreates (and submits to local node and network) a masternode creation transaction with given owner and operator addresses, spending the given inputs..\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for keeping collateral amount (any P2PKH or P2WKH address) - used as owner key"},
                   {"operatorAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Optional (== ownerAddress) masternode operator auth address (P2PKH only, unique)"},
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
                   HelpExampleCli("createmasternode", "ownerAddress operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("createmasternode", "ownerAddress operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet); // no need here, but for symmetry

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, at least argument 1 must be non-null");
    }

    std::string ownerAddress = request.params[0].getValStr();
    std::string operatorAddress = request.params.size() > 1 ? request.params[1].getValStr() : ownerAddress;
    CTxDestination ownerDest = DecodeDestination(ownerAddress); // type will be checked on apply/create
    CTxDestination operatorDest = DecodeDestination(operatorAddress);

    // check type here cause need operatorAuthKey. all other validation (for owner for ex.) in further apply/create
    if (operatorDest.which() != 1 && operatorDest.which() != 4) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAddress (" + operatorAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    CKeyID const operatorAuthKey = operatorDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest));

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateMasternode)
             << static_cast<char>(operatorDest.which()) << operatorAuthKey;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    if (request.params.size() > 2) {
        rawTx.vin = GetInputs(request.params[2].get_array());
    }

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(), GetScriptForDestination(ownerDest)));

    fund(rawTx, pwallet, {});

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreateMasternodeTx(mnview_dummy, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, static_cast<char>(operatorDest.which()), operatorAuthKey}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, {})->GetHash().GetHex();
}

UniValue resignmasternode(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"resignmasternode",
               "\nCreates (and submits to local node and network) a transaction resigning your masternode. Collateral will be unlocked after " +
               std::to_string(GetMnResignDelay()) + " blocks.\n"
                                                    "The last optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the MN's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
                   {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
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
                   HelpExampleCli("resignmasternode", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("resignmasternode", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot resign Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VARR }, true);

    std::string const nodeIdStr = request.params[0].getValStr();
    uint256 const nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == 1 ? CTxDestination(PKHash(nodePtr->ownerAuthAddress)) : CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));

        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths{GetScriptForDestination(ownerDest)};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false, optAuthTx, request.params[1]);

    // Return change to owner address
    CCoinControl coinControl;
    if (IsValidDestination(ownerDest)) {
        coinControl.destChange = ownerDest;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ResignMasternode)
             << nodeId;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyResignMasternodeTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, nodeId}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(uint256 const & nodeId, CMasternode const& node, bool verbose) {
    UniValue ret(UniValue::VOBJ);
    if (!verbose) {
        ret.pushKV(nodeId.GetHex(), CMasternode::GetHumanReadableState(node.GetState()));
    }
    else {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("ownerAuthAddress", EncodeDestination(
                node.ownerType == 1 ? CTxDestination(PKHash(node.ownerAuthAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.ownerAuthAddress))));
        obj.pushKV("operatorAuthAddress", EncodeDestination(
                node.operatorType == 1 ? CTxDestination(PKHash(node.operatorAuthAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.operatorAuthAddress))));

        obj.pushKV("creationHeight", node.creationHeight);
        obj.pushKV("resignHeight", node.resignHeight);
        obj.pushKV("resignTx", node.resignTx.GetHex());
        obj.pushKV("banHeight", node.banHeight);
        obj.pushKV("banTx", node.banTx.GetHex());
        obj.pushKV("state", CMasternode::GetHumanReadableState(node.GetState()));
        obj.pushKV("mintedBlocks", (uint64_t) node.mintedBlocks);

        /// @todo add unlock height and|or real resign height
        ret.pushKV(nodeId.GetHex(), obj);
    }
    return ret;
}

UniValue listmasternodes(const JSONRPCRequest& request) {
    RPCHelpMan{"listmasternodes",
               "\nReturns information about specified masternodes (or all, if list of ids is empty).\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                                 {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of orders to return, 100 by default"},
                         },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with masternodes information\n"
               },
               RPCExamples{
                       HelpExampleCli("listmasternodes", "'[mn_id]' false")
                       + HelpExampleRpc("listmasternodes", "'[mn_id]' false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    // parse pagination
    size_t limit = 100;
    uint256 start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
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
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    pcustomcsview->ForEachMasternode([&](uint256 const& nodeId, CMasternode node) {
        ret.pushKVs(mnToJSON(nodeId, node, verbose));
        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getmasternode(const JSONRPCRequest& request) {
    RPCHelpMan{"getmasternode",
               "\nReturns information about specified masternode.\n",
               {
                       {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Masternode's id"},
               },
               RPCResult{
                       "{id:{...}}     (object) Json object with masternode information\n"
               },
               RPCExamples{
                       HelpExampleCli("getmasternode", "mn_id")
                       + HelpExampleRpc("getmasternode", "mn_id")
               },
    }.Check(request);

    uint256 id = ParseHashV(request.params[0], "masternode id");

    LOCK(cs_main);
    auto node = pcustomcsview->GetMasternode(id);
    if (node) {
        return mnToJSON(id, *node, true); // or maybe just node, w/o id?
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode not found");
}

UniValue listcriminalproofs(const JSONRPCRequest& request) {
    RPCHelpMan{"listcriminalproofs",
               "\nReturns information about criminal proofs (pairs of signed blocks by one MN from different forks).\n",
               {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                             {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                              "Optional first key to iterate from, in lexicographical order."
                              "Typically it's set to last ID from previous request."},
                             {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                              "If true, then iterate including starting position. False by default"},
                             {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                              "Maximum number of orders to return, 100 by default"},
                         },
                    },
               },
               RPCResult{
                       "{id:{block1, block2},...}     (array) Json objects with block pairs\n"
               },
               RPCExamples{
                       HelpExampleCli("listcriminalproofs", "")
                       + HelpExampleRpc("listcriminalproofs", "")
               },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    uint256 start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
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
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    auto const proofs = pcriminals->GetUnpunishedCriminals();
    for (auto it = proofs.lower_bound(start); it != proofs.end() && limit != 0; ++it, --limit) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("hash1", it->second.blockHeader.GetHash().ToString());
        obj.pushKV("height1", it->second.blockHeader.height);
        obj.pushKV("hash2", it->second.conflictBlockHeader.GetHash().ToString());
        obj.pushKV("height2", it->second.conflictBlockHeader.height);
        obj.pushKV("mintedBlocks", it->second.blockHeader.mintedBlocks);
        ret.pushKV(it->first.ToString(), obj);
    }
    return ret;
}

UniValue createtoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createtoken",
               "\nCreates (and submits to local node and network) a token creation transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO,
                             "Token's symbol (unique), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                             "Token's name (optional), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'isDAT' property (bool, optional), default is 'False'"},
                            {"decimal", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                             "Token's decimal places (optional, fixed to 8 for now, unchecked)"},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                             "Token's total supply limit (optional, zero for now, unchecked)"},
                            {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'Mintable' property (bool, optional), default is 'True'"},
                            {"tradeable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'Tradeable' property (bool, optional), default is 'True'"},
                            {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                             "Any valid destination for keeping collateral amount - used as token's owner auth"},
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
                       HelpExampleCli("createtoken", "'{\"symbol\":\"MyToken\","
                                                     "\"collateralAddress\":\"address\"}'")
                       + HelpExampleCli("createtoken", "'{\"symbol\":\"MyToken\","
                                                     "\"collateralAddress\":\"address\"}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createtoken", "'{\"symbol\":\"MyToken\","
                                                       "\"collateralAddress\":\"address\"}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"symbol\",\"collateralAddress\"}");
    }

    const UniValue metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    std::string collateralAddress = metaObj["collateralAddress"].getValStr();
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to any valid address");
    }

    CToken token;
    token.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.flags = metaObj["isDAT"].getBool() ? token.flags | (uint8_t)CToken::TokenFlags::DAT : token.flags; // setting isDAT

    if (!metaObj["tradeable"].isNull()) {
        token.flags = metaObj["tradeable"].getBool() ?
            token.flags | uint8_t(CToken::TokenFlags::Tradeable) :
            token.flags & ~uint8_t(CToken::TokenFlags::Tradeable);
    }
    if (!metaObj["mintable"].isNull()) {
        token.flags = metaObj["mintable"].getBool() ?
            token.flags | uint8_t(CToken::TokenFlags::Mintable) :
            token.flags & ~uint8_t(CToken::TokenFlags::Mintable);
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateToken)
             << token;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, metaObj["isDAT"].getBool() /*needFoundersAuth*/, optAuthTx, txInputs);

    rawTx.vout.push_back(CTxOut(GetTokenCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetTokenCollateralAmount(), GetScriptForDestination(collateralDest)));

    CCoinControl coinControl;

    // Return change to auth address
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
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyCreateTokenTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, token}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updatetoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updatetoken",
               "\nCreates (and submits to local node and network) a transaction of token promotion to isDAT or demotion from isDAT. Collateral will be unlocked.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                           {"symbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "New token's symbol, no longer than " +
                            std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                           {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "New token's name (optional), no longer than " +
                            std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                           {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'isDAT' property (bool, optional), default is 'False'"},
                           {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'Mintable' property (bool, optional)"},
                           {"tradeable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'Tradeable' property (bool, optional)"},
                           {"finalize", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Lock token properties forever (bool, optional)"},
                           // it is possible to transfer token's owner. but later
//                           {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
//                            "Any valid destination for keeping collateral amount - used as token's owner auth"},
                           // omitted for now, need to research/discuss
//                           {"decimal", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
//                            "Token's decimal places (optional, fixed to 8 for now, unchecked)"},
//                           {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
//                            "Token's total supply limit (optional, zero for now, unchecked)"},
                        },
                    },
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
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
                       HelpExampleCli("updatetoken", "token '{\"isDAT\":true}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("updatetoken", "token '{\"isDAT\":true}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValueType(), UniValue::VOBJ, UniValue::VARR}, true); // first means "any"

    /// @todo RPCTypeCheckObj or smth to help with option's names and old/new tx type

    std::string const tokenStr = trim_ws(request.params[0].getValStr());
    UniValue metaObj = request.params[1].get_obj();
    UniValue const & txInputs = request.params[2];

    CTokenImplementation tokenImpl;
    CTxDestination ownerDest;
    CScript owner;
    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID id;
        auto token = pcustomcsview->GetTokenGuessId(tokenStr, id);
        if (id == DCT_ID{0}) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't alter DFI token!"));
        }
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));
        }
        tokenImpl = static_cast<CTokenImplementation const& >(*token);
        if (tokenImpl.IsPoolShare()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is the LPS token! Can't alter pool share's tokens!", tokenStr));
        }

        const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
        if (!ExtractDestination(authCoin.out.scriptPubKey, ownerDest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Can't extract destination for token's %s collateral", tokenImpl.symbol));
        }
        owner = authCoin.out.scriptPubKey;
        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!metaObj["symbol"].isNull()) {
        tokenImpl.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    }
    if (!metaObj["name"].isNull()) {
        tokenImpl.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    }
    if (!metaObj["isDAT"].isNull()) {
        tokenImpl.flags = metaObj["isDAT"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::DAT :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::DAT;
    }
    if (!metaObj["tradeable"].isNull()) {
        tokenImpl.flags = metaObj["tradeable"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Tradeable :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::Tradeable;
    }
    if (!metaObj["mintable"].isNull()) {
        tokenImpl.flags = metaObj["mintable"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Mintable :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::Mintable;
    }
    if (!metaObj["finalize"].isNull()) {
        tokenImpl.flags = metaObj["finalize"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Finalized :
                              tokenImpl.flags;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    CTransactionRef optAuthTx;
    std::set<CScript> auths;

    if (targetHeight < Params().GetConsensus().BayfrontHeight) {
        if (metaObj.size() > 1 || !metaObj.exists("isDAT")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Only 'isDAT' flag modification allowed before Bayfront fork (<" + std::to_string(Params().GetConsensus().BayfrontHeight) + ")");
        }

        // before BayfrontHeight it needs only founders auth
        rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths /*auths*/, true /*needFoundersAuth*/, optAuthTx, txInputs);
    }
    else
    { // post-bayfront auth
        bool isFoundersToken = Params().GetConsensus().foundationMembers.find(owner) != Params().GetConsensus().foundationMembers.end();
        if (isFoundersToken) { // need any founder's auth
            rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths /*auths*/, true /*needFoundersAuth*/, optAuthTx, txInputs);
        }
        else {// "common" auth
            auths.insert(owner);
            rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);

    // tx type and serialized data differ:
    if (targetHeight < Params().GetConsensus().BayfrontHeight) {
        metadata << static_cast<unsigned char>(CustomTxType::UpdateToken)
                 << tokenImpl.creationTx <<  metaObj["isDAT"].getBool();
    }
    else {
        metadata << static_cast<unsigned char>(CustomTxType::UpdateTokenAny)
                 << tokenImpl.creationTx << static_cast<CToken>(tokenImpl); // casting to base token's data
    }

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Set change to auth address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        Res res{};
        if (targetHeight < Params().GetConsensus().BayfrontHeight) {
            res = ApplyUpdateTokenTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                          ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, tokenImpl.creationTx, metaObj["isDAT"].getBool()}), Params().GetConsensus());
        }
        else {
            res = ApplyUpdateTokenAnyTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                          ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, tokenImpl.creationTx, static_cast<CToken>(tokenImpl)}), Params().GetConsensus());
        }
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue tokenToJSON(DCT_ID const& id, CTokenImplementation const& token, bool verbose) {
    UniValue tokenObj(UniValue::VOBJ);
    tokenObj.pushKV("symbol", token.symbol);
    tokenObj.pushKV("symbolKey", token.CreateSymbolKey(id));

    tokenObj.pushKV("name", token.name);
    if (verbose) {
        tokenObj.pushKV("decimal", token.decimal);
        tokenObj.pushKV("limit", token.limit);
        tokenObj.pushKV("mintable", token.IsMintable());
        tokenObj.pushKV("tradeable", token.IsTradeable());
        tokenObj.pushKV("isDAT", token.IsDAT());
        tokenObj.pushKV("isLPS", token.IsPoolShare());
        tokenObj.pushKV("finalized", token.IsFinalized());

        tokenObj.pushKV("minted", ValueFromAmount(token.minted));
        tokenObj.pushKV("creationTx", token.creationTx.ToString());
        tokenObj.pushKV("creationHeight", token.creationHeight);
        tokenObj.pushKV("destructionTx", token.destructionTx.ToString());
        tokenObj.pushKV("destructionHeight", token.destructionHeight);
        if (!token.IsPoolShare()) {
            const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
            tokenObj.pushKV("collateralAddress", ScriptToString(authCoin.out.scriptPubKey));
        } else {
            tokenObj.pushKV("collateralAddress", "undefined");
        }
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV(id.ToString(), tokenObj);
    return ret;
}

UniValue listtokens(const JSONRPCRequest& request) {
    RPCHelpMan{"listtokens",
               "\nReturns information about tokens.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of tokens to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids, symbols and names are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with tokens information\n"
               },
               RPCExamples{
                       HelpExampleCli("listtokens", "'{\"start\":128}' false")
                       + HelpExampleRpc("listtokens", "'{\"start\":128}' false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t) paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start.v;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachToken([&](DCT_ID const& id, CTokenImplementation token) {
        ret.pushKVs(tokenToJSON(id, token, verbose));

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue gettoken(const JSONRPCRequest& request) {
    RPCHelpMan{"gettoken",
               "\nReturns information about token.\n",
               {
                       {"key", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "One of the keys may be specified (id/symbol/creationTx)"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with token information\n"
               },
               RPCExamples{
                       HelpExampleCli("gettoken", "GOLD")
                       + HelpExampleRpc("gettoken", "GOLD")
               },
    }.Check(request);

    LOCK(cs_main);

    DCT_ID id;
    auto token = pcustomcsview->GetTokenGuessId(request.params[0].getValStr(), id);
    if (token) {
        return tokenToJSON(id, *static_cast<CTokenImplementation*>(token.get()), true);
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
}

UniValue minttokens(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"minttokens",
               "\nCreates (and submits to local node and network) a transaction minting your token (for accounts and/or UTXOs). \n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"amounts", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Amount in amount@token format."
                    },
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
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
                       HelpExampleCli("minttokens", "10@symbol")
                       + HelpExampleCli("minttokens",
                                      "10@symbol '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("minttokens", "10@symbol '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot mint tokens while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    const CBalances minted = DecodeAmounts(pwallet->chain(), request.params[0], "");
    UniValue const & txInputs = request.params[1];

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    CTransactionRef optAuthTx;
    std::set<CScript> auths;

    // auth
    {
        if (!txInputs.isNull() && !txInputs.empty()) {
            rawTx.vin = GetInputs(txInputs.get_array());            // separate call here to do not process the rest in "else"
        }
        else {
            bool needFoundersAuth = false;
            for (auto const & kv : minted.balances) {

                CTokenImplementation tokenImpl;
                {
                    LOCK(cs_main);
                    auto token = pcustomcsview->GetToken(kv.first);
                    if (!token) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", kv.first.ToString()));
                    }

                    tokenImpl = static_cast<CTokenImplementation const& >(*token);
                    const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
                    if (tokenImpl.IsDAT()) {
                        needFoundersAuth = true;
                    }
                    auths.insert(authCoin.out.scriptPubKey);
                }
            }
            rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, needFoundersAuth, optAuthTx, txInputs);
        } // else
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::MintToken)
             << minted; /// @note here, that whole CBalances serialized!, not a 'minted.balances'!

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Set change to auth address if there's only one auth address
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
        CCoinsViewCache view(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(view, *optAuthTx, targetHeight);
        const auto res = ApplyMintTokenTx(mnview_dummy, view, CTransaction(rawTx), targetHeight,
                                                 ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, minted }), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

CScript hexToScript(std::string const& str) {
    if (!IsHex(str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct hex:\n");
    }
    const auto raw = ParseHex(str);
    return CScript{raw.begin(), raw.end()};
}

BalanceKey decodeBalanceKey(std::string const& str) {
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

std::string tokenAmountString(CTokenAmount const& amount) {
    const auto token = pcustomcsview->GetToken(amount.nTokenId);
    const auto valueString = strprintf("%d.%08d", amount.nValue / COIN, amount.nValue % COIN);
    return valueString + "@" + token->symbol + (token->IsDAT() ? "" : "#" + amount.nTokenId.ToString());
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
    CScript oldOwner;
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (oldOwner == owner) {
            totalBalances.Add(balance);
        } else if (IsMineCached(*pwallet, owner) == ISMINE_SPENDABLE) {
            oldOwner = owner;
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

UniValue poolToJSON(DCT_ID const& id, CPoolPair const& pool, CToken const& token, bool verbose) {
    UniValue poolObj(UniValue::VOBJ);
    poolObj.pushKV("symbol", token.symbol);
    poolObj.pushKV("name", token.name);
    poolObj.pushKV("status", pool.status);
    poolObj.pushKV("idTokenA", pool.idTokenA.ToString());
    poolObj.pushKV("idTokenB", pool.idTokenB.ToString());

    if (verbose) {
        poolObj.pushKV("reserveA", ValueFromAmount(pool.reserveA));
        poolObj.pushKV("reserveB", ValueFromAmount(pool.reserveB));
        poolObj.pushKV("commission", ValueFromAmount(pool.commission));
        poolObj.pushKV("totalLiquidity", ValueFromAmount(pool.totalLiquidity));

        if (pool.reserveB == 0) {
            poolObj.pushKV("reserveA/reserveB", "0");
        } else {
            poolObj.pushKV("reserveA/reserveB", ValueFromAmount((arith_uint256(pool.reserveA) * arith_uint256(COIN) / pool.reserveB).GetLow64()));
        }

        if (pool.reserveA == 0) {
            poolObj.pushKV("reserveB/reserveA", "0");
        } else {
            poolObj.pushKV("reserveB/reserveA", ValueFromAmount((arith_uint256(pool.reserveB) * arith_uint256(COIN) / pool.reserveA).GetLow64()));
        }
        poolObj.pushKV("tradeEnabled", pool.reserveA >= CPoolPair::SLOPE_SWAP_RATE && pool.reserveB >= CPoolPair::SLOPE_SWAP_RATE);

        poolObj.pushKV("ownerAddress", pool.ownerAddress.GetHex()); /// @todo replace with ScriptPubKeyToUniv()

        poolObj.pushKV("blockCommissionA", ValueFromAmount(pool.blockCommissionA));
        poolObj.pushKV("blockCommissionB", ValueFromAmount(pool.blockCommissionB));

        poolObj.pushKV("rewardPct", ValueFromAmount(pool.rewardPct));

        auto rewards = pcustomcsview->GetPoolCustomReward(id);
        if (rewards && !rewards->balances.empty()) {
            for (auto it = rewards->balances.cbegin(), next_it = it; it != rewards->balances.cend(); it = next_it) {
                ++next_it;

                // Get token balance
                const auto balance = pcustomcsview->GetBalance(pool.ownerAddress, it->first).nValue;

                // Make there's enough to pay reward otherwise remove it
                if (balance < it->second) {
                    rewards->balances.erase(it);
                }
            }

            if (!rewards->balances.empty()) {
                UniValue rewardArr(UniValue::VARR);

                for (const auto& reward : rewards->balances) {
                    rewardArr.push_back(CTokenAmount{reward.first, reward.second}.ToString());
                }

                poolObj.pushKV("customRewards", rewardArr);
            }
        }

        poolObj.pushKV("creationTx", pool.creationTx.GetHex());
        poolObj.pushKV("creationHeight", (uint64_t) pool.creationHeight);
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(id.ToString(), poolObj);
    return ret;
}

UniValue listpoolpairs(const JSONRPCRequest& request) {
    RPCHelpMan{"listpoolpairs",
               "\nReturns information about pools.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of pools to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids, symbols and names are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with pools information\n"
               },
               RPCExamples{
                       HelpExampleCli("listpoolpairs", "'{\"start\":128}' false")
                       + HelpExampleRpc("listpoolpairs", "'{\"start\":128}' false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t) paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start.v;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachPoolPair([&](DCT_ID const & id, CLazySerialize<CPoolPair> pool) {
        const auto token = pcustomcsview->GetToken(id);
        if (token) {
            ret.pushKVs(poolToJSON(id, pool.get(), *token, verbose));
        }

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getpoolpair(const JSONRPCRequest& request) {
    RPCHelpMan{"getpoolpair",
               "\nReturns information about pool.\n",
               {
                       {"key", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "One of the keys may be specified (id/symbol/creationTx)"},
                       {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Flag for verbose list (default = true), otherwise limited objects are listed"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with pool information\n"
               },
               RPCExamples{
                       HelpExampleCli("getpoolpair", "GOLD")
                       + HelpExampleRpc("getpoolpair", "GOLD")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].getBool();
    }

    LOCK(cs_main);

    DCT_ID id;
    auto token = pcustomcsview->GetTokenGuessId(request.params[0].getValStr(), id);
    if (token) {
        auto pool = pcustomcsview->GetPoolPair(id);
        if (pool) {
            return poolToJSON(id, *pool, *token, verbose);
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pool not found");
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pool not found");
}

UniValue addpoolliquidity(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"addpoolliquidity",
               "\nCreates (and submits to local node and network) a add pool liquidity transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address(es) is the key(s), the value(s) is amount in amount@token format. "
                                                                                     "You should provide exectly two types of tokens for pool's 'token A' and 'token B' in any combinations."
                                                                                     "If multiple tokens from one address are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"
                                                                                     "If \"from\" obj contain only one amount entry with address-key: \"*\" (star), it's means auto-selection accounts from wallet."},
                        },
                       },
                       {"shareAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address for crediting tokens."},
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
                       HelpExampleCli("addpoolliquidity",
                                      "'{\"address1\":\"1.0@DFI\",\"address2\":\"1.0@DFI\"}' "
                                      "share_address '[]'")
                       + HelpExampleCli("addpoolliquidity",
                                      "'{\"*\": [\"2.0@BTC\", \"3.0@ETH\"]}' "
                                      "share_address '[]'")
                       + HelpExampleRpc("addpoolliquidity",
                                      "'{\"address1\":\"1.0@DFI\",\"address2\":\"1.0@DFI\"}' "
                                      "share_address '[]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, { UniValue::VOBJ, UniValue::VSTR, UniValue::VARR }, true);

    // decode
    CLiquidityMessage msg{};
    if (request.params[0].get_obj().getKeys().size() == 1 &&
            request.params[0].get_obj().getKeys()[0] == "*") { // auto-selection accounts from wallet
        CAccounts foundWalletAccounts = FindAccountsFromWallet(pwallet);

        CBalances sumTransfers = DecodeAmounts(pwallet->chain(), request.params[0].get_obj()["*"], "*");

        msg.from = SelectAccountsByTargetBalances(foundWalletAccounts, sumTransfers, SelectionPie);

        if (msg.from.empty()) {
            throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Not enough balance on wallet accounts, call utxostoaccount to increase it.\n");
        }
    }
    else {
        msg.from = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());
    }
    msg.shareAddress = DecodeScript(request.params[1].get_str());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AddPoolLiquidity)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // auth
    std::set<CScript> auths;
    for (const auto& kv : msg.from) {
        auths.emplace(kv.first);
    }
    UniValue const & txInputs = request.params[2];
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
        const auto res = ApplyAddPoolLiquidityTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight, ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue removepoolliquidity(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"removepoolliquidity",
               "\nCreates (and submits to local node and network) a remove pool liquidity transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address which has tokens"},
                       {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Liquidity amount@Liquidity pool symbol"},
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
                       HelpExampleCli("removepoolliquidity", "from_address 1.0@LpSymbol")
                       + HelpExampleRpc("removepoolliquidity", "from_address 1.0@LpSymbol")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);

    std::string from = request.params[0].get_str();
    std::string amount = request.params[1].get_str();
    UniValue const & txInputs = request.params[2];

    // decode
    CRemoveLiquidityMessage msg{};
    msg.from = DecodeScript(from);
    msg.amount = DecodeAmount(pwallet->chain(), amount, from);

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::RemovePoolLiquidity)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

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
        const auto res = ApplyRemovePoolLiquidityTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight, ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
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
    msg.to = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());

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
    msg.from = DecodeScript(request.params[0].get_str());
    msg.to = DecodeRecipients(pwallet->chain(), request.params[1].get_obj());
    if (SumAllTransfers(msg.to).balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

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

UniValue createpoolpair(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createpoolpair",
               "\nCreates (and submits to local node and network) a poolpair transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                            {"tokenA", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "One of the keys may be specified (id/symbol)"},
                            {"tokenB", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "One of the keys may be specified (id/symbol)"},
                            {"commission", RPCArg::Type::NUM, RPCArg::Optional::NO,
                            "Pool commission, up to 10^-8"},
                            {"status", RPCArg::Type::BOOL, RPCArg::Optional::NO,
                            "Pool Status: True is Active, False is Restricted"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "Address of the pool owner."},
                            {"customRewards", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "Token reward to be paid on each block, multiple can be specified."},
                            {"pairSymbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                             "Pair symbol (unique), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
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
                       HelpExampleCli("createpoolpair",   "'{\"tokenA\":\"MyToken1\","
                                                          "\"tokenB\":\"MyToken2\","
                                                          "\"commission\":\"0.001\","
                                                          "\"status\":\"True\","
                                                          "\"ownerAddress\":\"Address\","
                                                          "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\""
                                                          "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createpoolpair", "'{\"tokenA\":\"MyToken1\","
                                                          "\"tokenB\":\"MyToken2\","
                                                          "\"commission\":\"0.001\","
                                                          "\"status\":\"True\","
                                                          "\"ownerAddress\":\"Address\","
                                                          "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\""
                                                          "}' '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    std::string tokenA, tokenB, pairSymbol;
    CAmount commission = 0; // !!!
    CScript ownerAddress;
    CBalances rewards;
    bool status = true; // default Active
    UniValue metadataObj = request.params[0].get_obj();
    if (!metadataObj["tokenA"].isNull()) {
        tokenA = metadataObj["tokenA"].getValStr();
    }
    if (!metadataObj["tokenB"].isNull()) {
        tokenB = metadataObj["tokenB"].getValStr();
    }
    if (!metadataObj["commission"].isNull()) {
        commission = AmountFromValue(metadataObj["commission"]);
    }
    if (!metadataObj["status"].isNull()) {
        status = metadataObj["status"].getBool();
    }
    if (!metadataObj["ownerAddress"].isNull()) {
        ownerAddress = DecodeScript(metadataObj["ownerAddress"].getValStr());
    }
    if (!metadataObj["pairSymbol"].isNull()) {
        pairSymbol = metadataObj["pairSymbol"].getValStr();
    }
    if (!metadataObj["customRewards"].isNull()) {
        rewards = DecodeAmounts(pwallet->chain(), metadataObj["customRewards"], "");
    }

    int targetHeight;
    DCT_ID idtokenA, idtokenB;
    {
        LOCK(cs_main);

        auto token = pcustomcsview->GetTokenGuessId(tokenA, idtokenA);
        if (!token)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "TokenA was not found");

        auto token2 = pcustomcsview->GetTokenGuessId(tokenB, idtokenB);
        if (!token2)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "TokenB was not found");

        targetHeight = ::ChainActive().Height() + 1;
    }

    CPoolPairMessage poolPairMsg;
    poolPairMsg.idTokenA = idtokenA;
    poolPairMsg.idTokenB = idtokenB;
    poolPairMsg.commission = commission;
    poolPairMsg.status = status;
    poolPairMsg.ownerAddress = ownerAddress;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreatePoolPair)
             << poolPairMsg << pairSymbol;

    if (targetHeight >= Params().GetConsensus().ClarkeQuayHeight) {
        metadata << rewards;
    }

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

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
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyCreatePoolPairTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, poolPairMsg, pairSymbol}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue updatepoolpair(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updatepoolpair",
               "\nCreates (and submits to local node and network) a pool status update transaction.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the pool's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"pool", RPCArg::Type::STR, RPCArg::Optional::NO, "The pool's symbol, id or creation tx"},
                           {"status", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Pool Status new property (bool)"},
                           {"commission", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Pool commission, up to 10^-8"},
                           {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of the pool owner."},
                           {"customRewards", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Token reward to be paid on each block, multiple can be specified."},
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
                       HelpExampleCli("updatepoolpair", "'{\"pool\":\"POOL\",\"status\":true,"
                                                     "\"commission\":0.01,\"ownerAddress\":\"Address\","
                                                     "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\"}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("updatepoolpair", "'{\"pool\":\"POOL\",\"status\":true,"
                                                       "\"commission\":0.01,\"ownerAddress\":\"Address\","
                                                       "\"customRewards\":\"[\\\"1@tokena\\\",\\\"10@tokenb\\\"]\"}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    bool status = true;
    CAmount commission = -1;
    CScript ownerAddress;
    CBalances rewards;
    UniValue const & metaObj = request.params[0].get_obj();
    UniValue const & txInputs = request.params[1];

    std::string const poolStr = trim_ws(metaObj["pool"].getValStr());
    DCT_ID poolId;
    int targetHeight;
    {
        LOCK(cs_main);
        auto token = pcustomcsview->GetTokenGuessId(poolStr, poolId);
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pool %s does not exist!", poolStr));
        }

        auto pool = pcustomcsview->GetPoolPair(poolId);
        if (!pool) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pool %s does not exist!", poolStr));
        }
        status = pool->status;
        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!metaObj["status"].isNull()) {
        status = metaObj["status"].getBool();
    }
    if (!metaObj["commission"].isNull()) {
        commission = AmountFromValue(metaObj["commission"]);
    }
    if (!metaObj["ownerAddress"].isNull()) {
        ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
    }
    if (!metaObj["customRewards"].isNull()) {
        rewards = DecodeAmounts(pwallet->chain(), metaObj["customRewards"], "");

        if (rewards.balances.empty()) {
            // Add special case to wipe rewards
            rewards.balances.insert(std::pair<DCT_ID, CAmount>(DCT_ID{std::numeric_limits<uint32_t>::max()}, std::numeric_limits<CAmount>::max()));
        }
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, true /*needFoundersAuth*/, optAuthTx, txInputs);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdatePoolPair)
             << poolId << status << commission << ownerAddress;

    if (targetHeight >= Params().GetConsensus().ClarkeQuayHeight) {
        metadata << rewards;
    }

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CCoinControl coinControl;

    // Set change to selected foundation address
    CTxDestination dest;
    ExtractDestination(*auths.cbegin(), dest);
    if (IsValidDestination(dest)) {
        coinControl.destChange = dest;
    }

    fund(rawTx, pwallet, optAuthTx, &coinControl);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplyUpdatePoolPairTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, poolId, status, commission, ownerAddress}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

void CheckAndFillPoolSwapMessage(const JSONRPCRequest& request, CPoolSwapMessage &poolSwapMsg) {
    std::string tokenFrom, tokenTo;
    UniValue metadataObj = request.params[0].get_obj();
    if (!metadataObj["from"].isNull()) {
        poolSwapMsg.from = DecodeScript(metadataObj["from"].getValStr());
    }
    if (!metadataObj["tokenFrom"].isNull()) {
        tokenFrom = metadataObj["tokenFrom"].getValStr();
    }
    if (!metadataObj["amountFrom"].isNull()) {
        poolSwapMsg.amountFrom = AmountFromValue(metadataObj["amountFrom"]);
    }
    if (!metadataObj["to"].isNull()) {
        poolSwapMsg.to = DecodeScript(metadataObj["to"].getValStr());
    }
    if (!metadataObj["tokenTo"].isNull()) {
        tokenTo = metadataObj["tokenTo"].getValStr();
    }
    {
        LOCK(cs_main);
        auto token = pcustomcsview->GetTokenGuessId(tokenFrom, poolSwapMsg.idTokenFrom);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TokenFrom was not found");

        auto token2 = pcustomcsview->GetTokenGuessId(tokenTo, poolSwapMsg.idTokenTo);
        if (!token2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TokenTo was not found");

        if (!metadataObj["maxPrice"].isNull()) {
            CAmount maxPrice = AmountFromValue(metadataObj["maxPrice"]);
            poolSwapMsg.maxPrice.integer = maxPrice / COIN;
            poolSwapMsg.maxPrice.fraction = maxPrice % COIN;
        }
        else {
            // This is only for maxPrice calculation

            auto poolPair = pcustomcsview->GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo);
            if (!poolPair) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't find the poolpair " + tokenFrom + "-" + tokenTo);
            }
            CPoolPair const & pool = poolPair->second;
            if (pool.totalLiquidity <= CPoolPair::MINIMUM_LIQUIDITY) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Pool is empty!");
            }

            auto resA = pool.reserveA;
            auto resB = pool.reserveB;
            if (poolSwapMsg.idTokenFrom != pool.idTokenA) {
                std::swap(resA, resB);
            }
            arith_uint256 price256 = arith_uint256(resB) * CPoolPair::PRECISION / arith_uint256(resA);
            price256 += price256 * 3 / 100; // +3%
            // this should not happen IRL, but for sure:
            if (price256 / CPoolPair::PRECISION > std::numeric_limits<int64_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Current price +3% overflow!");
            }
            auto priceInt = price256 / CPoolPair::PRECISION;
            poolSwapMsg.maxPrice.integer = priceInt.GetLow64();
            poolSwapMsg.maxPrice.fraction = (price256 - priceInt * CPoolPair::PRECISION).GetLow64(); // cause there is no operator "%"
        }
    }
}

UniValue poolswap(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"poolswap",
               "\nCreates (and submits to local node and network) a poolswap transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
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
                                 HelpExampleCli("poolswap",   "'{\"from\":\"MyAddress\","
                                                                    "\"tokenFrom\":\"MyToken1\","
                                                                    "\"amountFrom\":\"0.001\","
                                                                    "\"to\":\"Address\","
                                                                    "\"tokenTo\":\"Token2\","
                                                                    "\"maxPrice\":\"0.01\""
                                                                    "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                                         + HelpExampleRpc("poolswap", "'{\"from\":\"MyAddress\","
                                                                            "\"tokenFrom\":\"MyToken1\","
                                                                            "\"amountFrom\":\"0.001\","
                                                                            "\"to\":\"Address\","
                                                                            "\"tokenTo\":\"Token2\","
                                                                            "\"maxPrice\":\"0.01\""
                                                                            "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                             },
              }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CPoolSwapMessage poolSwapMsg{};
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);
    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::PoolSwap)
             << poolSwapMsg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    UniValue const & txInputs = request.params[1];
    CTransactionRef optAuthTx;
    std::set<CScript> auths{poolSwapMsg.from};
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to from address
    CTxDestination dest;
    ExtractDestination(poolSwapMsg.from, dest);
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
        const auto res = ApplyPoolSwapTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, poolSwapMsg}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue testpoolswap(const JSONRPCRequest& request) {

    RPCHelpMan{"testpoolswap",
               "\nTests a poolswap transaction with given metadata and returns poolswap result.\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
                       },
                   },
               },
               RPCResult{
                          "\"amount@tokenId\"    (string) The string with amount result of poolswap in format AMOUNT@TOKENID.\n"
               },
               RPCExamples{
                    HelpExampleCli("testpoolswap",   "'{\"from\":\"MyAddress\","
                                                    "\"tokenFrom\":\"MyToken1\","
                                                    "\"amountFrom\":\"0.001\","
                                                    "\"to\":\"Address\","
                                                    "\"tokenTo\":\"Token2\","
                                                    "\"maxPrice\":\"0.01\""
                                                    "}'")
                    + HelpExampleRpc("testpoolswap", "'{\"from\":\"MyAddress\","
                                                    "\"tokenFrom\":\"MyToken1\","
                                                    "\"amountFrom\":\"0.001\","
                                                    "\"to\":\"Address\","
                                                    "\"tokenTo\":\"Token2\","
                                                    "\"maxPrice\":\"0.01\""
                                                    "}'")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, true);

    CPoolSwapMessage poolSwapMsg{};
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);

    int targetHeight = ::ChainActive().Height() + 1;

    // test execution and get amount
    Res res = Res::Ok();
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // create dummy cache for test state writing

        auto poolPair = mnview_dummy.GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo);

        const std::string base{"PoolSwap creation: " + poolSwapMsg.ToString()};

        CPoolPair pp = poolPair->second;
        res = pp.Swap({poolSwapMsg.idTokenFrom, poolSwapMsg.amountFrom}, poolSwapMsg.maxPrice, [&] (const CTokenAmount &tokenAmount) {
            auto resPP = mnview_dummy.SetPoolPair(poolPair->first, pp);
            if (!resPP.ok) {
                return Res::Err("%s: %s", base, resPP.msg);
            }

            return Res::Ok(tokenAmount.ToString());
        }, targetHeight >= Params().GetConsensus().BayfrontGardensHeight);

        if (!res.ok)
            throw JSONRPCError(RPC_VERIFY_ERROR, res.msg);
    }
    return UniValue(res.msg);
}

UniValue poolShareToJSON(DCT_ID const & poolId, CScript const & provider, CAmount const& amount, CPoolPair const& poolPair, bool verbose) {
    UniValue poolObj(UniValue::VOBJ);
    poolObj.pushKV("poolID", poolId.ToString());
    poolObj.pushKV("owner", ScriptToString(provider));
    poolObj.pushKV("%", uint64_t(amount*100/poolPair.totalLiquidity));

    if (verbose) {
        poolObj.pushKV("amount", ValueFromAmount(amount));
        poolObj.pushKV("totalLiquidity", ValueFromAmount(poolPair.totalLiquidity));
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(poolId.ToString() + "@" + ScriptToString(provider), poolObj);
    return ret;
}

UniValue listpoolshares(const JSONRPCRequest& request) {
    RPCHelpMan{"listpoolshares",
               "\nReturns information about pool shares.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of pools to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only % are shown."},
                        {"is_mine_only", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Get shares for all accounts belonging to the wallet (default = false)"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with pools information\n"
               },
               RPCExamples{
                       HelpExampleCli("listpoolshares", "'{\"start\":128}' false false")
                       + HelpExampleRpc("listpoolshares", "'{\"start\":128}' false false")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].getBool();
    }

    bool isMineOnly = false;
    if (request.params.size() > 2) {
        isMineOnly = request.params[2].get_bool();
    }

    CWallet* const pwallet = GetWallet(request);

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
                start.v = (uint32_t) paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start.v;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    PoolShareKey startKey{ start, CScript{} };
//    startKey.poolID = start;
//    startKey.owner = CScript(0);

    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachPoolShare([&](DCT_ID const & poolId, CScript const & provider) {
        const CTokenAmount tokenAmount = pcustomcsview->GetBalance(provider, poolId);
        if(tokenAmount.nValue) {
            const auto poolPair = pcustomcsview->GetPoolPair(poolId);
            if(poolPair) {
                if (isMineOnly) {
                    if (IsMineCached(*pwallet, provider) == ISMINE_SPENDABLE) {
                        ret.pushKVs(poolShareToJSON(poolId, provider, tokenAmount.nValue, *poolPair, verbose));
                        limit--;
                    }
                } else {
                    ret.pushKVs(poolShareToJSON(poolId, provider, tokenAmount.nValue, *poolPair, verbose));
                    limit--;
                }
            }
        }

        return limit != 0;
    }, startKey);

    return ret;
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

static void searchInWallet(CWallet const * pwallet, CScript const & account,
                           std::function<bool(CWalletTx const *)> shouldSkipTx,
                           std::function<bool(COutputEntry const &)> onSent,
                           std::function<bool(COutputEntry const &)> onReceive) {

    CTxDestination destination;
    ExtractDestination(account, destination);

    if (!IsValidDestination(destination)) {
        return;
    }

    CAmount nFee;
    std::list<COutputEntry> listSent;
    std::list<COutputEntry> listReceived;

    LOCK(pwallet->cs_wallet);

    const auto& txOrdered = pwallet->wtxOrdered;

    for (const auto& tx : txOrdered) {
        auto pwtx = tx.second;

        if (pwtx->IsCoinBase()) {
            continue;
        }

        if (shouldSkipTx(pwtx)) {
            continue;
        }

        pwtx->GetAmounts(listReceived, listSent, nFee, ISMINE_ALL_USED);

        for (auto& sent : listSent) {
            if (!IsValidDestination(sent.destination) || destination != sent.destination) {
                continue;
            }
            sent.amount = -sent.amount;
            if (!onSent(sent)) {
                return;
            }
        }

        for (const auto& recv : listReceived) {
            if (!IsValidDestination(recv.destination) || destination != recv.destination) {
                continue;
            }
            if (!onReceive(recv)) {
                return;
            }
        }
    }
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

    bool isMine = false;
    if (accounts == "mine") {
        isMine = true;
    } else if (accounts != "all") {
        account = DecodeScript(accounts);
        isMine = IsMineCached(*pwallet, account) == ISMINE_SPENDABLE;
        if (acMineOnly && !isMine) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "account " + accounts + " is not mine, it's needed -acindex to find it");
        }
        isMatchOwner = [&account](CScript const & owner) {
            return owner == account;
        };
    }

    std::set<uint256> txs;
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && !account.empty();

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

    if (isMine) {
        pcustomcsview->ForEachMineAccountHistory(shouldContinueToNextAccountHistory, startKey);
    } else {
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
        searchInWallet(pwallet, account, [&](CWalletTx const * pwtx) -> bool {
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
        count = limit;
        auto shouldContinueToNextReward = [&](RewardHistoryKey const & key, CLazySerialize<RewardHistoryValue> valueLazy) -> bool {
            if (!isMatchOwner(key.owner)) {
                return false;
            }

            if (shouldSkipBlock(key.blockHeight)) {
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

        if (isMine) {
            pcustomcsview->ForEachMineRewardHistory(shouldContinueToNextReward, startKey);
        } else {
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

    if (request.params.size() > 1) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
            {
                {"no_rewards", UniValueType(UniValue::VBOOL)},
                {"token", UniValueType(UniValue::VSTR)},
            }, true, true);

        noRewards = optionsObj["no_rewards"].getBool();

        if (!optionsObj["token"].isNull()) {
            tokenFilter = optionsObj["token"].get_str();
        }
    }

    CScript owner;
    bool isMine = false;

    if (accounts == "mine") {
        isMine = true;
    } else if (accounts != "all") {
        owner = DecodeScript(accounts);
        isMine = IsMineCached(*pwallet, owner) == ISMINE_SPENDABLE;
        if (acMineOnly && !isMine) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "account " + accounts + " is not mine, it's needed -acindex to find it");
        }
    }

    std::set<uint256> txs;
    const bool shouldSearchInWallet = (tokenFilter.empty() || tokenFilter == "DFI") && !owner.empty();

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

        const auto& value = valueLazy.get();

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

    if (isMine) {
        pcustomcsview->ForEachMineAccountHistory(shouldContinueToNextAccountHistory, startAccountKey);
    } else {
        pcustomcsview->ForEachAllAccountHistory(shouldContinueToNextAccountHistory, startAccountKey);
    }

    if (shouldSearchInWallet) {
        auto incCount = [&count](COutputEntry const &) { ++count; return true; };
        searchInWallet(pwallet, owner, [&](CWalletTx const * pwtx) -> bool {
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

    if (isMine) {
        pcustomcsview->ForEachMineRewardHistory(shouldContinueToNextReward, startHistoryKey);
    } else {
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

UniValue setgov(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"setgov",
               "\nSet special 'governance' variables. Two types of them implemented for now: LP_SPLITS and LP_DAILY_DFI_REWARD\n",
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
                       + HelpExampleRpc("setgov", "'{\"LP_DAILY_DFI_REWARD\":109440}'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CDataStream varStream(SER_NETWORK, PROTOCOL_VERSION);
    if (request.params.size() > 0 && request.params[0].isObject()) {
        for (const std::string& name : request.params[0].getKeys()) {
            auto gv = GovVariable::Create(name);
            if(!gv)
                throw JSONRPCError(RPC_INVALID_REQUEST, "Variable " + name + " not registered");
            gv->Import(request.params[0][name]);
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
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        CCoinsViewCache coinview(&::ChainstateActive().CoinsTip());
        if (optAuthTx)
            AddCoins(coinview, *optAuthTx, targetHeight);
        const auto res = ApplySetGovernanceTx(mnview_dummy, coinview, CTransaction(rawTx), targetHeight,
                                      ToByteVector(varStream), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

UniValue getgov(const JSONRPCRequest& request) {
    RPCHelpMan{"getgov",
               "\nReturns information about governance variable. Two types of them implemented for now: LP_SPLITS and LP_DAILY_DFI_REWARD\n",
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

UniValue isappliedcustomtx(const JSONRPCRequest& request) {
    RPCHelpMan{"isappliedcustomtx",
               "\nChecks that custom transaction was affected on chain\n",
               {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "A transaction hash"},
                    {"blockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The height of block which contain tx"}
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

    uint256 txHash = ParseHashV(request.params[0], "txid");
    int blockHeight = request.params[1].get_int();

    const auto undo = pcustomcsview->GetUndo(UndoKey{static_cast<uint32_t>(blockHeight), txHash});

    if (!undo) { // no changes done
        result.setBool(false);
    } else {
        result.setBool(true);
    }

    return result;
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

    msg.to = DecodeRecipients(pwallet->chain(), request.params[1].get_obj());
    const CBalances sumTransfersTo = SumAllTransfers(msg.to);
    if (sumTransfersTo.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts in \"to\" param");
    }

    if (request.params[0].get_obj().empty()) { // autoselection
        CAccounts foundWalletAccounts = FindAccountsFromWallet(pwallet);

        std::string selectionParam = request.params[2].isStr() ? request.params[2].get_str() : "pie";
        AccountSelectionMode selectionMode = ParseAccountSelectionParam(selectionParam);

        msg.from = SelectAccountsByTargetBalances(foundWalletAccounts, sumTransfersTo, selectionMode);

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

UniValue setoracledata(const JSONRPCRequest &request) {
    CWallet *const pwallet = GetWallet(request);

    // TODO (IntegralTeam Y): correct help
    RPCHelpMan{"setoracledata",
               "\nCreates (and submits to local node and network) a set oracle data transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO, "balances timestamp",},
                       {"prices", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "tokens raw prices:the array of price and token strings in price@token#number format. ",
                        {
                                {"", RPCArg::Type::STR, RPCArg::Optional::NO, ""}
                            }
                        },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                         HelpExampleCli("setoracledata", "1612237937 '{[“38293.12@BTC#1”, “1328.32@ETH#2”]}' ")
                       + HelpExampleRpc("setoracledata", "1612237637 '{[“38293.12@BTC#1”, “1328.32@ETH#2”]}' ")
               },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();
    LockedCoinsScopedGuard lcGuard(pwallet);

    // decode
    UniValue const & timestampUni = request.params[0];
    auto balances = DecodeAmounts(pwallet->chain(), request.params[1], "");

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ}, false);

    // TODO (IntegralTeam Y): need to get oracleId
    COracleId oracleId{};
    BTCTimeStamp timestamp{};
    try {
        timestamp = static_cast<BTCTimeStamp>(timestampUni.get_int64());
    } catch (...) {
        throw JSONRPCError(RPC_TRANSACTION_ERROR, "failed to decode timestamp");
    }

    CSetOracleDataMessage msg{oracleId, timestamp, balances};

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::SetOracleData)
                   << msg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.emplace_back(0, scriptMeta);

    UniValue const &txInputs = request.params[2];
    CTransactionRef optAuthTx;
    std::set<CScript> auths;
    rawTx.vin = GetAuthInputsSmart(pwallet, rawTx.nVersion, auths, false /*needFoundersAuth*/, optAuthTx, txInputs);

    CCoinControl coinControl;

    // Set change to auth address if there's only one auth address
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
        const auto res = ApplySetOracleDataTx(
                mnview_dummy,
                coinview,
                CTransaction(rawTx),
                targetHeight,
                ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}),
                Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, pwallet, optAuthTx)->GetHash().GetHex();
}

static bool GetCustomTXInfo(const int nHeight, const CTransactionRef tx, CustomTxType& guess, Res& res, UniValue& txResults)
{
    std::vector<unsigned char> metadata;
    guess = GuessCustomTxType(*tx, metadata);
    CCustomCSView mnview_dummy(*pcustomcsview);

    switch (guess)
    {
        case CustomTxType::CreateMasternode:
            res = ApplyCreateMasternodeTx(mnview_dummy, *tx, nHeight, metadata, &txResults);
            break;
        case CustomTxType::ResignMasternode:
            res = ApplyResignMasternodeTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, true, &txResults);
            break;
        case CustomTxType::CreateToken:
            res = ApplyCreateTokenTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::UpdateToken:
            res = ApplyUpdateTokenTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::UpdateTokenAny:
            res = ApplyUpdateTokenAnyTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::MintToken:
            res = ApplyMintTokenTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::CreatePoolPair:
            res = ApplyCreatePoolPairTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::UpdatePoolPair:
            res = ApplyUpdatePoolPairTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::PoolSwap:
            res = ApplyPoolSwapTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::AddPoolLiquidity:
            res = ApplyAddPoolLiquidityTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::RemovePoolLiquidity:
            res = ApplyRemovePoolLiquidityTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::UtxosToAccount:
            res = ApplyUtxosToAccountTx(mnview_dummy, *tx, nHeight, metadata, Params().GetConsensus(), &txResults);
            break;
        case CustomTxType::AccountToUtxos:
            res = ApplyAccountToUtxosTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::AccountToAccount:
            res = ApplyAccountToAccountTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::SetGovVariable:
            res = ApplySetGovernanceTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::AnyAccountsToAccounts:
            res = ApplyAnyAccountsToAccountsTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        case CustomTxType::SetOracleData:
            res = ApplySetOracleDataTx(mnview_dummy, ::ChainstateActive().CoinsTip(), *tx, nHeight, metadata, Params().GetConsensus(), true, &txResults);
            break;
        default:
            return false;
    }

    return true;
}

static UniValue getcustomtx(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    RPCHelpMan{"getcustomtx",
        "\nGet detailed information about a DeFiChain custom transaction. Will search wallet transactions and mempool transaction,\n"
        "if a blockhash is provided and that block is available then details for that transaction can be returned. -txindex\n"
        "can be enabled to return details for any transaction.",
        {
            {"txid", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction id"},
            {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED_NAMED_ARG, "The block in which to look for the transaction"},
        },
        RPCResult{
            "{\n"
            "  \"type\":               (string) The transaction type.\n"
            "  \"valid\"               (bool) Whether the transaction was valid.\n"
            "  \"results\"             (json object) Set of results related to the transaction type\n"
            "  \"block height\"        (string) The block height containing the transaction.\n"
            "  \"blockhash\"           (string) The block hash containing the transaction.\n"
            "  \"confirmations\": n,   (numeric) The number of confirmations for the transaction."
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("getcustomtx", "\"66ea2ac081e2917f075e2cca7c1c0baa12fb85c469f34561185fa64d7d2f9305\"")
                    + HelpExampleRpc("getcustomtx", "\"66ea2ac081e2917f075e2cca7c1c0baa12fb85c469f34561185fa64d7d2f9305\"")
        },
    }.Check(request);

    const uint256 hash(ParseHashV(request.params[0], "txid"));

    CTransactionRef tx;
    uint256 hashBlock;

    // Search wallet if available
    if (pwallet) {
        LOCK(pwallet->cs_wallet);
        auto it = pwallet->mapWallet.find(hash);
        if (it != pwallet->mapWallet.end())
        {
            tx = it->second.tx;
            hashBlock = it->second.hashBlock;
        }
    }

    CBlockIndex* blockindex{nullptr};

    // No wallet or not a wallet TX, try mempool, txindex and a block if hash provided
    if (!pwallet || !tx)
    {
        if (!request.params[1].isNull()) {
            LOCK(cs_main);

            uint256 blockhash = ParseHashV(request.params[1], "blockhash");
            blockindex = LookupBlockIndex(blockhash);
            if (!blockindex) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
            }
        }

        bool f_txindex_ready{false};
        if (g_txindex && !blockindex) {
            f_txindex_ready = g_txindex->BlockUntilSyncedToCurrentChain();
        }

        if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, blockindex)) {
            std::string errmsg;
            if (blockindex) {
                if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                    throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
                }
                errmsg = "No such transaction found in the provided block.";
            } else if (!g_txindex) {
                errmsg = "No such mempool or wallet transaction. Use -txindex or provide a block hash.";
            } else if (!f_txindex_ready) {
                errmsg = "No such mempool or wallet transaction. Transactions are still in the process of being indexed.";
            } else {
                errmsg = "No such mempool, wallet or blockchain transaction.";
            }
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg);
        }
    }

    int nHeight{0};
    bool actualHeight{false};
    CustomTxType guess;
    UniValue txResults(UniValue::VOBJ);
    Res res{};

    if (tx)
    {
        LOCK(cs_main);

        // Found a block hash but no block index yet
        if (!hashBlock.IsNull() && !blockindex) {
            blockindex = LookupBlockIndex(hashBlock);
        }

        // Default to next block height
        nHeight = ::ChainActive().Height() + 1;

        // Get actual height if blockindex avaiable
        if (blockindex) {
            nHeight = blockindex->nHeight;
            actualHeight = true;
        }

        // Skip coinbase TXs except for genesis block
        if ((tx->IsCoinBase() && nHeight > 0)) {
            return "Coinbase transaction. Not a custom transaction.";
        }

        if (!GetCustomTXInfo(nHeight, tx, guess, res, txResults)) {
            return "Not a custom transaction";
        }

    } else {
        // Should not really get here without prior failure.
        return "Could not find matching transaction.";
    }

    UniValue result(UniValue::VOBJ);

    result.pushKV("type", ToString(guess));
    if (!actualHeight) {
        result.pushKV("valid", res.ok ? true : false);
    } else {
        auto undo = pcustomcsview->GetUndo(UndoKey{static_cast<uint32_t>(nHeight), hash});
        result.pushKV("valid", undo ? true : false);
    }

    if (!res.ok) {
        result.pushKV("error", res.msg);
    } else {
        result.pushKV("results", txResults);
    }

    if (!hashBlock.IsNull()) {
        LOCK(cs_main);

        result.pushKV("blockhash", hashBlock.GetHex());
        if (blockindex) {
            result.pushKV("blockHeight", blockindex->nHeight);
            result.pushKV("blockTime", blockindex->GetBlockTime());
            result.pushKV("confirmations", 1 + ::ChainActive().Height() - blockindex->nHeight);
        } else {
            result.pushKV("confirmations", 0);
        }
    }

    return result;
}


static const CRPCCommand commands[] =
{ //  category      name                  actor (function)     params
  //  ----------------- ------------------------    -----------------------     ----------

    {"masternodes", "createmasternode",      &createmasternode,      {"ownerAddress", "operatorAddress", "inputs"}},
    {"masternodes", "resignmasternode",      &resignmasternode,      {"mn_id", "inputs"}},
    {"masternodes", "listmasternodes",       &listmasternodes,       {"pagination", "verbose"}},
    {"masternodes", "getmasternode",         &getmasternode,         {"mn_id"}},
    {"masternodes", "listcriminalproofs",    &listcriminalproofs,    {}},
    {"tokens",      "createtoken",           &createtoken,           {"metadata", "inputs"}},
    {"tokens",      "updatetoken",           &updatetoken,           {"token", "metadata", "inputs"}},
    {"tokens",      "listtokens",            &listtokens,            {"pagination", "verbose"}},
    {"tokens",      "gettoken",              &gettoken,              {"key" }},
    {"tokens",      "getcustomtx",           &getcustomtx,           {"txid", "blockhash"}},
    {"tokens",      "minttokens",            &minttokens,            {"amounts", "inputs"}},
    {"accounts",    "listaccounts",          &listaccounts,          {"pagination", "verbose", "indexed_amounts", "is_mine_only"}},
    {"accounts",    "getaccount",            &getaccount,            {"owner", "pagination", "indexed_amounts"}},
    {"poolpair",    "listpoolpairs",         &listpoolpairs,         {"pagination", "verbose"}},
    {"poolpair",    "getpoolpair",           &getpoolpair,           {"key", "verbose" }},
    {"poolpair",    "addpoolliquidity",      &addpoolliquidity,      {"from", "shareAddress", "inputs"}},
    {"poolpair",    "removepoolliquidity",   &removepoolliquidity,   {"from", "amount", "inputs"}},
    {"accounts",    "gettokenbalances",      &gettokenbalances,      {"pagination", "indexed_amounts", "symbol_lookup"}},
    {"accounts",    "utxostoaccount",        &utxostoaccount,        {"amounts", "inputs"}},
    {"accounts",    "accounttoaccount",      &accounttoaccount,      {"from", "to", "inputs"}},
    {"accounts",    "accounttoutxos",        &accounttoutxos,        {"from", "to", "inputs"}},
    {"poolpair",    "createpoolpair",        &createpoolpair,        {"metadata", "inputs"}},
    {"poolpair",    "updatepoolpair",        &updatepoolpair,        {"metadata", "inputs"}},
    {"poolpair",    "poolswap",              &poolswap,              {"metadata", "inputs"}},
    {"poolpair",    "listpoolshares",        &listpoolshares,     {"pagination", "verbose", "is_mine_only"}},
    {"poolpair",    "testpoolswap",          &testpoolswap,          {"metadata"}},
    {"accounts",    "listaccounthistory",    &listaccounthistory,    {"owner", "options"}},
    {"accounts",    "accounthistorycount",   &accounthistorycount,   {"owner", "options"}},
    {"accounts",    "listcommunitybalances", &listcommunitybalances, {}},
    {"blockchain",  "setgov",                &setgov,                {"variables", "inputs"}},
    {"blockchain",  "getgov",                &getgov,                {"name"}},
    {"blockchain",  "isappliedcustomtx",     &isappliedcustomtx,     {"txid", "blockHeight"}},
    {"accounts",    "sendtokenstoaddress",   &sendtokenstoaddress,   {"from", "to", "selectionMode"}},
    {"oracles",     "setoracledata",         &setoracledata,          {"timestamp", "prices"}},
};

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

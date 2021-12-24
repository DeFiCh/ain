// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/client.h>
#include <rpc/protocol.h>
#include <util/system.h>

#include <set>
#include <stdint.h>

class CRPCConvertParam
{
public:
    std::string methodName; //!< method whose params want conversion
    int paramIdx;           //!< 0-based idx of param to convert
    std::string paramName;  //!< parameter name
};

// clang-format off
/**
 * Specify a (method, idx, name) here if the argument is a non-string RPC
 * argument and needs to be converted from JSON.
 *
 * @note Parameter indexes start from 0.
 */
static const CRPCConvertParam vRPCConvertParams[] =
{
    { "setmocktime", 0, "timestamp" },
    { "utxoupdatepsbt", 1, "descriptors" },
    { "generatetoaddress", 0, "nblocks" },
    { "generatetoaddress", 2, "maxtries" },
    { "getnetworkhashps", 0, "nblocks" },
    { "getnetworkhashps", 1, "height" },
    { "sendtoaddress", 1, "amount" },
    { "sendtoaddress", 4, "subtractfeefromamount" },
    { "sendtoaddress", 5 , "replaceable" },
    { "sendtoaddress", 6 , "conf_target" },
    { "sendtoaddress", 8, "avoid_reuse" },
    { "settxfee", 0, "amount" },
    { "sethdseed", 0, "newkeypool" },
    { "getreceivedbyaddress", 1, "minconf" },
    { "getreceivedbylabel", 1, "minconf" },
    { "listreceivedbyaddress", 0, "minconf" },
    { "listreceivedbyaddress", 1, "include_empty" },
    { "listreceivedbyaddress", 2, "include_watchonly" },
    { "listreceivedbylabel", 0, "minconf" },
    { "listreceivedbylabel", 1, "include_empty" },
    { "listreceivedbylabel", 2, "include_watchonly" },
    { "getbalance", 1, "minconf" },
    { "getbalance", 2, "include_watchonly" },
    { "getbalance", 3, "avoid_reuse" },
    { "getbalance", 4, "with_tokens" },
    { "getbalances", 0, "with_tokens" },
    { "getunconfirmedbalance", 0, "with_tokens" },
    { "getblockhash", 0, "height" },
    { "getwalletinfo", 0, "with_tokens" },
    { "waitforblockheight", 0, "height" },
    { "waitforblockheight", 1, "timeout" },
    { "waitforblock", 1, "timeout" },
    { "waitfornewblock", 0, "timeout" },
    { "listtransactions", 1, "count" },
    { "listtransactions", 2, "skip" },
    { "listtransactions", 3, "include_watchonly" },
    { "listtransactions", 4, "exclude_custom_tx" },
    { "walletpassphrase", 1, "timeout" },
    { "getblocktemplate", 0, "template_request" },
    { "listsinceblock", 1, "target_confirmations" },
    { "listsinceblock", 2, "include_watchonly" },
    { "listsinceblock", 3, "include_removed" },
    { "sendmany", 1, "amounts" },
    { "sendmany", 2, "minconf" },
    { "sendmany", 4, "subtractfeefrom" },
    { "sendmany", 5 , "replaceable" },
    { "sendmany", 6 , "conf_target" },
    { "deriveaddresses", 1, "range" },
    { "scantxoutset", 1, "scanobjects" },
    { "addmultisigaddress", 0, "nrequired" },
    { "addmultisigaddress", 1, "keys" },
    { "createmultisig", 0, "nrequired" },
    { "createmultisig", 1, "keys" },
    { "listunspent", 0, "minconf" },
    { "listunspent", 1, "maxconf" },
    { "listunspent", 2, "addresses" },
    { "listunspent", 3, "include_unsafe" },
    { "listunspent", 4, "query_options" },
    { "getblock", 1, "verbosity" },
    { "getblock", 1, "verbose" },
    { "getblockheader", 1, "verbose" },
    { "getchaintxstats", 0, "nblocks" },
    { "gettransaction", 1, "include_watchonly" },
    { "getrawtransaction", 1, "verbose" },
    { "createrawtransaction", 0, "inputs" },
    { "createrawtransaction", 1, "outputs" },
    { "createrawtransaction", 2, "locktime" },
    { "createrawtransaction", 3, "replaceable" },
    { "decoderawtransaction", 1, "iswitness" },
    { "signrawtransactionwithkey", 1, "privkeys" },
    { "signrawtransactionwithkey", 2, "prevtxs" },
    { "signrawtransactionwithwallet", 1, "prevtxs" },
    { "sendrawtransaction", 1, "allowhighfees" },
    { "sendrawtransaction", 1, "maxfeerate" },
    { "testmempoolaccept", 0, "rawtxs" },
    { "testmempoolaccept", 1, "allowhighfees" },
    { "testmempoolaccept", 1, "maxfeerate" },
    { "combinerawtransaction", 0, "txs" },
    { "fundrawtransaction", 1, "options" },
    { "fundrawtransaction", 2, "iswitness" },
    { "walletcreatefundedpsbt", 0, "inputs" },
    { "walletcreatefundedpsbt", 1, "outputs" },
    { "walletcreatefundedpsbt", 2, "locktime" },
    { "walletcreatefundedpsbt", 3, "options" },
    { "walletcreatefundedpsbt", 4, "bip32derivs" },
    { "walletprocesspsbt", 1, "sign" },
    { "walletprocesspsbt", 3, "bip32derivs" },
    { "createpsbt", 0, "inputs" },
    { "createpsbt", 1, "outputs" },
    { "createpsbt", 2, "locktime" },
    { "createpsbt", 3, "replaceable" },
    { "combinepsbt", 0, "txs"},
    { "joinpsbts", 0, "txs"},
    { "finalizepsbt", 1, "extract"},
    { "converttopsbt", 1, "permitsigdata"},
    { "converttopsbt", 2, "iswitness"},
    { "gettxout", 1, "n" },
    { "gettxout", 2, "include_mempool" },
    { "gettxoutproof", 0, "txids" },
    { "lockunspent", 0, "unlock" },
    { "lockunspent", 1, "transactions" },
    { "importprivkey", 2, "rescan" },
    { "importaddress", 2, "rescan" },
    { "importaddress", 3, "p2sh" },
    { "importpubkey", 2, "rescan" },
    { "importmulti", 0, "requests" },
    { "importmulti", 1, "options" },
    { "verifychain", 0, "checklevel" },
    { "verifychain", 1, "nblocks" },
    { "getblockstats", 0, "hash_or_height" },
    { "getblockstats", 1, "stats" },
    { "pruneblockchain", 0, "height" },
    { "keypoolrefill", 0, "newsize" },
    { "getrawmempool", 0, "verbose" },
    { "estimatesmartfee", 0, "conf_target" },
    { "estimaterawfee", 0, "conf_target" },
    { "estimaterawfee", 1, "threshold" },
    { "prioritisetransaction", 1, "dummy" },
    { "prioritisetransaction", 2, "fee_delta" },
    { "setban", 2, "bantime" },
    { "setban", 3, "absolute" },
    { "setnetworkactive", 0, "state" },
    { "setwalletflag", 1, "value" },
    { "getmempoolancestors", 1, "verbose" },
    { "getmempooldescendants", 1, "verbose" },
    { "bumpfee", 1, "options" },
    { "logging", 0, "include" },
    { "logging", 1, "exclude" },
    { "disconnectnode", 1, "nodeid" },
    // Echo with conversion (For testing only)
    { "echojson", 0, "arg0" },
    { "echojson", 1, "arg1" },
    { "echojson", 2, "arg2" },
    { "echojson", 3, "arg3" },
    { "echojson", 4, "arg4" },
    { "echojson", 5, "arg5" },
    { "echojson", 6, "arg6" },
    { "echojson", 7, "arg7" },
    { "echojson", 8, "arg8" },
    { "echojson", 9, "arg9" },
    { "rescanblockchain", 0, "start_height"},
    { "rescanblockchain", 1, "stop_height"},
    { "createwallet", 1, "disable_private_keys"},
    { "createwallet", 2, "blank"},
    { "createwallet", 4, "avoid_reuse"},
    { "getnodeaddresses", 0, "count"},
    { "stop", 0, "wait" },
    { "createmasternode", 2, "inputs" },
    { "resignmasternode", 1, "inputs" },
    { "setforcedrewardaddress", 2, "inputs" },
    { "remforcedrewardaddress", 1, "inputs" },
    { "updatemasternode", 2, "inputs" },
    { "listmasternodes", 0, "pagination" },
    { "listmasternodes", 1, "verbose" },
    { "getmasternodeblocks", 0, "identifier"},
    { "getmasternodeblocks", 1, "depth"},
    { "createtoken", 0, "metadata" },
    { "createtoken", 1, "inputs"},
    { "updatetoken", 1, "metadata"},
    { "updatetoken", 2, "inputs"},
    { "listtokens", 0, "pagination" },
    { "listtokens", 1, "verbose" },
    { "minttokens", 0, "amounts" },
    { "minttokens", 1, "inputs"},
    { "utxostoaccount", 0, "amounts" },
    { "utxostoaccount", 1, "inputs" },
    { "sendutxosfrom", 2, "amount" },
    { "addpoolliquidity", 0, "from" },
    { "addpoolliquidity", 2, "inputs" },
    { "removepoolliquidity", 2, "inputs" },

    { "listpoolpairs", 0, "pagination" },
    { "listpoolpairs", 1, "verbose" },
    { "getpoolpair", 1, "verbose" },

    { "listaccounts", 0, "pagination" },
    { "listaccounts", 1, "verbose" },
    { "listaccounts", 2, "indexed_amounts" },
    { "listaccounts", 3, "is_mine_only" },
    { "getaccount", 1, "pagination" },
    { "getaccount", 2, "indexed_amounts" },
    { "gettokenbalances", 0, "pagination" },
    { "gettokenbalances", 1, "indexed_amounts" },
    { "gettokenbalances", 2, "symbol_lookup" },
    { "accounttoaccount", 1, "to" },
    { "accounttoaccount", 2, "inputs" },
    { "accounttoutxos", 1, "to" },
    { "accounttoutxos", 2, "inputs" },

    { "icx_createorder", 0, "order" },
    { "icx_createorder", 1, "inputs" },
    { "icx_makeoffer", 0, "offer" },
    { "icx_makeoffer", 1, "inputs" },
    { "icx_submitdfchtlc", 0, "dfchtlc" },
    { "icx_submitdfchtlc", 1, "inputs" },
    { "icx_submitexthtlc", 0, "exthtlc" },
    { "icx_submitexthtlc", 1, "inputs" },
    { "icx_claimdfchtlc", 0, "claim" },
    { "icx_claimdfchtlc", 1, "inputs" },
    { "icx_closeorder", 1, "inputs" },
    { "icx_closeoffer", 1, "inputs" },
    { "icx_listorders", 0, "by" },
    { "icx_listhtlcs", 0, "by" },

    { "setcollateraltoken", 0, "metadata" },
    { "setcollateraltoken", 1, "inputs" },
    { "listcollateraltokens", 0, "by" },
    { "setloantoken", 0, "metadata" },
    { "setloantoken", 1, "inputs" },
    { "updateloantoken", 1, "metadata" },
    { "updateloantoken", 2, "inputs" },
    { "takeloan", 0, "metadata" },
    { "takeloan", 1, "inputs" },
    { "paybackloan", 0, "metadata" },
    { "paybackloan", 1, "inputs" },
    { "createloanscheme", 0, "mincolratio" },
    { "createloanscheme", 1, "interestrate" },
    { "updateloanscheme", 0, "mincolratio" },
    { "updateloanscheme", 1, "interestrate" },
    { "updateloanscheme", 3, "ACTIVATE_AFTER_BLOCK" },
    { "destroyloanscheme", 1, "ACTIVATE_AFTER_BLOCK" },
    { "createvault", 2, "inputs" },
    { "closevault", 2, "inputs" },
    { "updatevault", 1, "parameters" },
    { "updatevault", 2, "inputs" },
    { "deposittovault", 3, "inputs" },
    { "withdrawfromvault", 3, "inputs" },
    { "placeauctionbid", 1, "index" },
    { "placeauctionbid", 4, "inputs" },
    { "listvaulthistory", 1, "options" },
    { "listvaults", 0, "options" },
    { "listvaults", 1, "pagination" },
    { "listauctions", 0, "pagination" },
    { "listauctionhistory", 1, "pagination" },
    { "estimateloan", 1, "tokens" },
    { "estimateloan", 2, "targetRatio" },
    { "estimatecollateral", 1, "targetRatio" },
    { "estimatecollateral", 2, "tokens" },
    { "estimatevault", 0, "collateralAmounts" },
    { "estimatevault", 1, "loanAmounts" },

    { "spv_sendrawtx", 0, "rawtx" },
    { "spv_createanchor", 0, "inputs" },
    { "spv_createanchor", 2, "send" },
    { "spv_createanchor", 3, "feerate" },
    { "spv_estimateanchorcost", 0, "feerate" },
    { "spv_rescan", 0, "height" },
    { "spv_gettxconfirmations", 0, "txhash" },
    { "spv_setlastheight", 0, "height" },
    { "spv_listanchors", 0, "minBtcHeight" },
    { "spv_listanchors", 1, "maxBtcHeight" },
    { "spv_listanchors", 2, "minConfs" },
    { "spv_listanchors", 3, "maxConfs" },
    { "spv_listanchors", 4, "startBtcHeight" },
    { "spv_listanchors", 5, "limit" },
    { "spv_sendtoaddress", 1, "amount" },
    { "spv_sendtoaddress", 2, "feerate" },
    { "spv_listreceivedbyaddress", 0, "minconf" },

    { "createpoolpair", 0, "metadata" },
    { "createpoolpair", 1, "inputs" },
    { "updatepoolpair", 0, "metadata" },
    { "updatepoolpair", 1, "inputs" },
    { "poolswap", 0, "metadata" },
    { "poolswap", 1, "inputs" },
    { "compositeswap", 0, "metadata" },
    { "compositeswap", 1, "inputs" },
    { "testpoolswap", 0, "metadata"},
    { "listpoolshares", 0, "pagination" },
    { "listpoolshares", 1, "verbose" },
    { "listpoolshares", 2, "is_mine_only" },

    { "listaccounthistory", 1, "options" },
    { "listburnhistory", 0, "options" },
    { "accounthistorycount", 1, "options" },

    { "setgov", 0, "variables" },
    { "setgov", 1, "inputs" },

    { "setgovheight", 0, "variables" },
    { "setgovheight", 1, "height" },
    { "setgovheight", 2, "inputs" },

    { "isappliedcustomtx", 1, "blockHeight" },
    { "sendtokenstoaddress", 0, "from" },
    { "sendtokenstoaddress", 1, "to" },
    { "getanchorteams", 0, "blockHeight" },
    { "getactivemasternodecount", 0, "blockCount" },
    { "appointoracle", 1, "pricefeeds" },
    { "appointoracle", 2, "weightage" },
    { "appointoracle", 3, "inputs" },
    { "updateoracle", 2, "pricefeeds" },
    { "updateoracle", 3, "weightage" },
    { "updateoracle", 4, "inputs" },
    { "removeoracle", 1, "inputs" },
    { "setoracledata", 1, "timestamp" },
    { "setoracledata", 2, "prices" },
    { "setoracledata", 3, "inputs" },
    { "listoracles", 0, "pagination" },
    { "listlatestrawprices", 0, "request" },
    { "listlatestrawprices", 1, "pagination" },
    { "listprices", 0, "pagination" },
    { "getprice", 0, "request" },
    { "listfixedintervalprices", 0, "pagination" },

    { "spv_claimhtlc", 3, "feerate" },
    { "spv_refundhtlc", 2, "feerate" },
    { "spv_refundhtlcall", 1, "feerate" },
    { "decodecustomtx", 1, "iswitness" },

    { "setmockcheckpoint", 0, "height" },
};
// clang-format on

class CRPCConvertTable
{
private:
    std::set<std::pair<std::string, int>> members;
    std::set<std::pair<std::string, std::string>> membersByName;

public:
    CRPCConvertTable();

    bool convert(const std::string& method, int idx) {
        return (members.count(std::make_pair(method, idx)) > 0);
    }
    bool convert(const std::string& method, const std::string& name) {
        return (membersByName.count(std::make_pair(method, name)) > 0);
    }
};

CRPCConvertTable::CRPCConvertTable()
{
    const unsigned int n_elem =
        (sizeof(vRPCConvertParams) / sizeof(vRPCConvertParams[0]));

    for (unsigned int i = 0; i < n_elem; i++) {
        members.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                      vRPCConvertParams[i].paramIdx));
        membersByName.insert(std::make_pair(vRPCConvertParams[i].methodName,
                                            vRPCConvertParams[i].paramName));
    }
}

static CRPCConvertTable rpcCvtTable;

/** Non-RFC4627 JSON parser, accepts internal values (such as numbers, true, false, null)
 * as well as objects and arrays.
 */
UniValue ParseNonRFCJSONValue(const std::string& strVal)
{
    UniValue jVal;
    if (!jVal.read(std::string("[")+strVal+std::string("]")) ||
        !jVal.isArray() || jVal.size()!=1)
        throw std::runtime_error(std::string("Error parsing JSON:")+strVal);
    return jVal[0];
}

UniValue RPCConvertValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VARR);

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        if (!rpcCvtTable.convert(strMethod, idx)) {
            // insert string value directly
            params.push_back(strVal);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.push_back(ParseNonRFCJSONValue(strVal));
        }
    }

    return params;
}

UniValue RPCConvertNamedValues(const std::string &strMethod, const std::vector<std::string> &strParams)
{
    UniValue params(UniValue::VOBJ);

    for (const std::string &s: strParams) {
        size_t pos = s.find('=');
        if (pos == std::string::npos) {
            throw(std::runtime_error("No '=' in named argument '"+s+"', this needs to be present for every argument (even if it is empty)"));
        }

        std::string name = s.substr(0, pos);
        std::string value = s.substr(pos+1);

        if (!rpcCvtTable.convert(strMethod, name)) {
            // insert string value directly
            params.pushKV(name, value);
        } else {
            // parse string as JSON, insert bool/number/object/etc. value
            params.pushKV(name, ParseNonRFCJSONValue(value));
        }
    }

    return params;
}

// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_RPC_REQUEST_H
#define DEFI_RPC_REQUEST_H

#include <string>
#include <univalue.h>
#include <masternodes/coinselect.h>
#include <util/system.h>

UniValue JSONRPCRequestObj(const std::string& strMethod, const UniValue& params, const UniValue& id);
UniValue JSONRPCReplyObj(const UniValue& result, const UniValue& error, const UniValue& id);
std::string JSONRPCReply(const UniValue& result, const UniValue& error, const UniValue& id);
UniValue JSONRPCError(int code, const std::string& message);

/** Generate a new RPC authentication cookie and write it to disk */
bool GenerateAuthCookie(std::string *cookie_out);
/** Read the RPC authentication cookie from disk */
bool GetAuthCookie(std::string *cookie_out);
/** Delete RPC authentication cookie from disk */
void DeleteAuthCookie();
/** Parse JSON-RPC batch reply into a vector */
std::vector<UniValue> JSONRPCProcessBatchReply(const UniValue &in, size_t num);


struct RPCMetadata {
    public:
    CoinSelectionOptions coinSelectOpts;

    static RPCMetadata CreateDefault() {
        RPCMetadata m;
        FromArgs(m, gArgs);
        return m;
    }

    static void SetupArgs(ArgsManager& args) {
        CoinSelectionOptions::SetupArgs(args);
    }

    static void FromArgs(RPCMetadata &m, ArgsManager& args) {
        CoinSelectionOptions::FromArgs(m.coinSelectOpts, args);
    }

    static void FromHTTPHeaderFunc(RPCMetadata &m, const HTTPHeaderQueryFunc headerFunc) {
        CoinSelectionOptions::FromHTTPHeaderFunc(m.coinSelectOpts, headerFunc);
    }

    static void ToHTTPHeaderFunc(const RPCMetadata& m, const HTTPHeaderWriterFunc writer) {
        CoinSelectionOptions::ToHTTPHeaderFunc(m.coinSelectOpts, writer);
    }
};
    // UniValue metadata;

class JSONRPCRequest
{
public:
    UniValue id;
    std::string strMethod;
    UniValue params;
    bool fHelp;
    std::string URI;
    std::string authUser;
    std::string peerAddr;
    RPCMetadata metadata;

    JSONRPCRequest() : id(NullUniValue), params(NullUniValue), fHelp(false) {}
    void parse(const UniValue& valRequest);
};

#endif // DEFI_RPC_REQUEST_H

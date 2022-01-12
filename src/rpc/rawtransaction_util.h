// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_RPC_RAWTRANSACTION_UTIL_H
#define DEFI_RPC_RAWTRANSACTION_UTIL_H

#include <script/standard.h> // CTxDestination

#include <map>
#include <masternodes/res.h>
#include <rpc/request.h>
#include <masternodes/balances.h>

class FillableSigningProvider;
class UniValue;
struct CMutableTransaction;
class Coin;
class COutPoint;

namespace interfaces {
class Chain;
}

std::pair<std::string, std::string> SplitAmount(std::string const & output);

ResVal<std::pair<CAmount, std::string>> ParseTokenAmount(std::string const & tokenAmount);
ResVal<CTokenAmount> GuessTokenAmount(interfaces::Chain const & chain,std::string const & tokenAmount);

CScript DecodeScript(std::string const& str);
CTokenAmount DecodeAmount(interfaces::Chain const & chain, UniValue const& amountUni, std::string const& name);
CBalances DecodeAmounts(interfaces::Chain const & chain, UniValue const& amountsUni, std::string const& name);
CAccounts DecodeRecipients(interfaces::Chain const & chain, UniValue const& sendTo);

/**
 * Sign a transaction with the given keystore and previous transactions
 *
 * @param  mtx           The transaction to-be-signed
 * @param  prevTxs       Array of previous txns outputs that tx depends on but may not yet be in the block chain
 * @param  keystore      Temporary keystore containing signing keys
 * @param  coins         Map of unspent outputs - coins in mempool and current chain UTXO set, may be extended by previous txns outputs after call
 * @param  tempKeystore  Whether to use temporary keystore
 * @param  hashType      The signature hash type
 * @returns JSON object with details of signed transaction
 */
UniValue SignTransaction(CMutableTransaction& mtx, const UniValue& prevTxs, FillableSigningProvider* keystore, std::map<COutPoint, Coin>& coins, bool tempKeystore, const UniValue& hashType);

/** Create a transaction from univalue parameters */
CMutableTransaction ConstructTransaction(const UniValue& inputs_in, const UniValue& outputs_in, const UniValue& locktime, bool rbf, interfaces::Chain & chain);

struct CParserResults {
    CScript& address;
    uint256& txid;
};

int DecodeScriptTxId(const std::string& str, CParserResults result);

#endif // DEFI_RPC_RAWTRANSACTION_UTIL_H

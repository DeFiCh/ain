// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/rawtransaction_util.h>

#include <coins.h>
#include <core_io.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <masternodes/tokens.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/request.h>
#include <rpc/util.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <tinyformat.h>
#include <univalue.h>
#include <util/rbf.h>
#include <util/strencodings.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <string>

std::pair<std::string, std::string> SplitAmount(std::string const & output)
{
    const unsigned char TOKEN_SPLITTER = '@';
    size_t pos = output.rfind(TOKEN_SPLITTER);
    std::string token_id = (pos != std::string::npos) ? output.substr(pos+1) : "";
    std::string amount = (pos != std::string::npos) ? output.substr(0, pos) : output;
    return { amount, token_id };
}

ResVal<std::pair<CAmount, std::string>> ParseTokenAmount(std::string const & tokenAmount)
{
    const auto strs = SplitAmount(tokenAmount);

    CAmount amount{0};
    if (!ParseFixedPoint(strs.first, 8, &amount))
        return Res::ErrCode(RPC_TYPE_ERROR, "Invalid amount");
    if (amount <= 0) {
        return Res::ErrCode(RPC_TYPE_ERROR, "Amount out of range"); // keep it for tests compatibility
    }
    return {{amount, strs.second}, Res::Ok()};
}

ResVal<CTokenAmount> GuessTokenAmount(interfaces::Chain const & chain, std::string const & tokenAmount)
{
    const auto parsed = ParseTokenAmount(tokenAmount);
    if (!parsed.ok) {
        return parsed;
    }
    DCT_ID tokenId;
    try {
        // try to parse it as a number, in a case DCT_ID was written
        tokenId.v = (uint32_t) std::stoul(parsed.val->second);
        return {{tokenId, parsed.val->first}, Res::Ok()};
    } catch (...) {
        // assuming it's token symbol, read DCT_ID from DB
        std::unique_ptr<CToken> token = chain.existTokenGuessId(parsed.val->second, tokenId);
        if (!token) {
            return Res::Err("Invalid Defi token: %s", parsed.val->second);
        }
        return {{tokenId, parsed.val->first}, Res::Ok()};
    }
}


// decodes either base58/bech32 address, or a hex format
CScript DecodeScript(std::string const& str)
{
    if (IsHex(str)) {
        const auto raw = ParseHex(str);
        CScript result{raw.begin(), raw.end()};
        txnouttype dummy;
        if (IsStandard(result, dummy)) {
            return result;
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "recipient script (" + str + ") does not solvable/non-standard");
    }
    const auto dest = DecodeDestination(str);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "recipient (" + str + ") does not refer to any valid address");
    }
    return GetScriptForDestination(dest);
}

int DecodeScriptTxId(const std::string& str, CParserResults result)
{
    if (IsHex(str)) {
        auto hex = ParseHex(str);
        CScript address{hex.begin(), hex.end()};
        txnouttype dummy;
        if (IsStandard(address, dummy)) {
            result.address = address;
            return 0;
        }
        if (hex.size() == 32) {
            std::reverse(hex.begin(), hex.end());
            result.txid = uint256{hex};
            return 1;
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "not solvable/non-standard address neither txid");
    }
    result.address = DecodeScript(str);
    return 0;
}

CTokenAmount DecodeAmount(interfaces::Chain const & chain, UniValue const& amountUni, std::string const& name)
{
    // decode amounts
    std::string strAmount;
    if (amountUni.isArray()) { // * amounts
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, name + ": expected single amount");
    } else if (amountUni.isNum()) { // legacy format for '0' token
        strAmount = amountUni.getValStr() + "@" + DCT_ID{0}.ToString();
    } else { // only 1 amount
        strAmount = amountUni.get_str();
    }
    return GuessTokenAmount(chain, strAmount).ValOrException([name](int code, const std::string& msg) -> UniValue {
        return JSONRPCError(code, name + ": " + msg);
    });
}

CBalances DecodeAmounts(interfaces::Chain const & chain, UniValue const& amountsUni, std::string const& name)
{
    // decode amounts
    CBalances amounts;
    if (amountsUni.isArray()) { // * amounts
        for (const auto& amountUni : amountsUni.get_array().getValues()) {
            amounts.Add(DecodeAmount(chain, amountUni, name));
        }
    } else {
        amounts.Add(DecodeAmount(chain, amountsUni, name));
    }
    return amounts;
}

// decodes recipients from formats:
// "addr": 123.0,
// "addr": "123.0@0",
// "addr": "123.0@DFI",
// "addr": ["123.0@DFI", "123.0@0", ...]
CAccounts DecodeRecipients(interfaces::Chain const & chain, UniValue const& sendTo)
{
    CAccounts recipients;
    for (const std::string& addr : sendTo.getKeys()) {
        // decode recipient
        const auto recipient = DecodeScript(addr);
        if (recipients.find(recipient) != recipients.end()) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, addr + ": duplicate recipient");
        }
        // decode amounts and substitute
        recipients[recipient] = DecodeAmounts(chain, sendTo[addr], addr);
    }
    return recipients;
}

CMutableTransaction ConstructTransaction(const UniValue& inputs_in, const UniValue& outputs_in, const UniValue& locktime, bool rbf, interfaces::Chain & chain)
{
    if (inputs_in.isNull() || outputs_in.isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = inputs_in.get_array();
    const bool outputs_is_obj = outputs_in.isObject();
    UniValue outputs = outputs_is_obj ? outputs_in.get_obj() : outputs_in.get_array();

    auto locked_chain = chain.lock();
    const auto txVersion = GetTransactionVersion(locked_chain->getHeight().value_or(-1));
    CMutableTransaction rawTx(txVersion);

    if (!locktime.isNull()) {
        int64_t nLockTime = locktime.get_int64();
        if (nLockTime < 0 || nLockTime > LOCKTIME_MAX)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

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

        uint32_t nSequence;
        if (rbf) {
            nSequence = MAX_BIP125_RBF_SEQUENCE; /* CTxIn::SEQUENCE_FINAL - 2 */
        } else if (rawTx.nLockTime) {
            nSequence = CTxIn::SEQUENCE_FINAL - 1;
        } else {
            nSequence = CTxIn::SEQUENCE_FINAL;
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > CTxIn::SEQUENCE_FINAL) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    if (!outputs_is_obj) {
        // Translate array of key-value pairs into dict
        UniValue outputs_dict = UniValue(UniValue::VOBJ);
        for (size_t i = 0; i < outputs.size(); ++i) {
            const UniValue& output = outputs[i];
            if (!output.isObject()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, key-value pair not an object as expected");
            }
            if (output.size() != 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, key-value pair must contain exactly one key");
            }
            outputs_dict.pushKVs(output);
        }
        outputs = std::move(outputs_dict);
    }

    // Duplicate checking
    std::set<CTxDestination> destinations;
    bool has_data{false};

    for (const std::string& name_ : outputs.getKeys()) {
        if (name_ == "data") {
            if (has_data) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, duplicate key: data");
            }
            has_data = true;
            std::vector<unsigned char> data = ParseHexV(outputs[name_].getValStr(), "Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination = DecodeDestination(name_); // it will be more clear to decode it straight to the CScript, but lets keep destination for tests compatibility
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Defi address: ") + name_);
            }
            if (!destinations.insert(destination).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }
            CScript scriptPubKey = GetScriptForDestination(destination);

            auto amounts = DecodeAmounts(chain, outputs[name_], name_);
            for (auto const & kv : amounts.balances) {
                CTxOut out(kv.second, scriptPubKey, kv.first);
                rawTx.vout.push_back(out);
            }
        }
    }

    if (rbf && rawTx.vin.size() > 0 && !SignalsOptInRBF(CTransaction(rawTx))) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter combination: Sequence number(s) contradict replaceable option");
    }

    return rawTx;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hash.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    UniValue witness(UniValue::VARR);
    for (unsigned int i = 0; i < txin.scriptWitness.stack.size(); i++) {
        witness.push_back(HexStr(txin.scriptWitness.stack[i].begin(), txin.scriptWitness.stack[i].end()));
    }
    entry.pushKV("witness", witness);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

UniValue SignTransaction(CMutableTransaction& mtx, const UniValue& prevTxsUnival, FillableSigningProvider* keystore, std::map<COutPoint, Coin>& coins, bool is_temp_keystore, const UniValue& hashType)
{
    // Add previous txouts given in the RPC call:
    if (!prevTxsUnival.isNull()) {
        UniValue prevTxs = prevTxsUnival.get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); ++idx) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject()) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");
            }

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");
            }

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                auto coin = coins.find(out);
                if (coin != coins.end() && !coin->second.IsSpent() && coin->second.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin->second.out.scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = MAX_MONEY;
                if (prevOut.exists("amount")) {
                    newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
                newcoin.nHeight = 1;
                coins[out] = std::move(newcoin);
            }

            // if redeemScript and private keys were given, add redeemScript to the keystore so it can be signed
            if (is_temp_keystore && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                        {"witnessScript", UniValueType(UniValue::VSTR)},
                    }, true);
                UniValue rs = find_value(prevOut, "redeemScript");
                if (!rs.isNull()) {
                    std::vector<unsigned char> rsData(ParseHexV(rs, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    keystore->AddCScript(redeemScript);
                    // Automatically also add the P2WSH wrapped version of the script (to deal with P2SH-P2WSH).
                    // This is only for compatibility, it is encouraged to use the explicit witnessScript field instead.
                    keystore->AddCScript(GetScriptForWitness(redeemScript));
                }
                UniValue ws = find_value(prevOut, "witnessScript");
                if (!ws.isNull()) {
                    std::vector<unsigned char> wsData(ParseHexV(ws, "witnessScript"));
                    CScript witnessScript(wsData.begin(), wsData.end());
                    keystore->AddCScript(witnessScript);
                    // Automatically also add the P2WSH wrapped version of the script (to deal with P2SH-P2WSH).
                    keystore->AddCScript(GetScriptForWitness(witnessScript));
                }
                if (rs.isNull() && ws.isNull()) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing redeemScript/witnessScript");
                }
            }
        }
    }

    int nHashType = ParseSighashString(hashType);

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        auto coin = coins.find(txin.prevout);
        if (coin == coins.end() || coin->second.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coin->second.out.scriptPubKey;
        const CAmount& amount = coin->second.out.nValue;

        SignatureData sigdata = DataFromTransaction(mtx, i, coin->second.out);
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size())) {
            ProduceSignature(*keystore, MutableTransactionSignatureCreator(&mtx, i, amount, nHashType), prevPubKey, sigdata);
        }

        UpdateInput(txin, sigdata);

        // amount must be specified for valid segwit signature
        if (amount == MAX_MONEY && !txin.scriptWitness.IsNull()) {
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing amount for %s", coin->second.out.ToString()));
        }

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
            } else {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(CTransaction(mtx)));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }

    return result;
}


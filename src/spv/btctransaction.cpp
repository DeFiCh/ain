// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <spv/btctransaction.h>

#include <hash.h>
#include <streams.h>
#include <tinyformat.h>
#include <util/strencodings.h>

std::string CBtcOutPoint::ToString() const
{
    return strprintf("CBtcOutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}

CBtcTxIn::CBtcTxIn(CBtcOutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CBtcTxIn::CBtcTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = CBtcOutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CBtcTxIn::ToString() const
{
    std::string str;
    str += "CBtcTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CBtcTxOut::CBtcTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

std::string CBtcTxOut::ToString() const
{
    return strprintf("CBtcTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

CMutableBtcTransaction::CMutableBtcTransaction() : nVersion(CBtcTransaction::CURRENT_VERSION), nLockTime(0) {}
CMutableBtcTransaction::CMutableBtcTransaction(const CBtcTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nLockTime(tx.nLockTime) {}

uint256 CMutableBtcTransaction::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_BTC_TRANSACTION_NO_WITNESS);
}

uint256 CBtcTransaction::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_BTC_TRANSACTION_NO_WITNESS);
}

uint256 CBtcTransaction::ComputeWitnessHash() const
{
    if (!HasWitness()) {
        return hash;
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CBtcTransaction::CBtcTransaction() : vin(), vout(), nVersion(CBtcTransaction::CURRENT_VERSION), nLockTime(0), hash{}, m_witness_hash{} {}
CBtcTransaction::CBtcTransaction(const CMutableBtcTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nLockTime(tx.nLockTime), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}
CBtcTransaction::CBtcTransaction(CMutableBtcTransaction&& tx) : vin(std::move(tx.vin)), vout(std::move(tx.vout)), nVersion(tx.nVersion), nLockTime(tx.nLockTime), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}

CAmount CBtcTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (const auto& tx_out : vout) {
        nValueOut += tx_out.nValue;
        if (!MoneyRange(tx_out.nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nValueOut;
}

unsigned int CBtcTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, PROTOCOL_VERSION);
}

std::string CBtcTransaction::ToString() const
{
    std::string str;
    str += strprintf("CBtcTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime);
    for (const auto& tx_in : vin)
        str += "    " + tx_in.ToString() + "\n";
    for (const auto& tx_in : vin)
        str += "    " + tx_in.scriptWitness.ToString() + "\n";
    for (const auto& tx_out : vout)
        str += "    " + tx_out.ToString() + "\n";
    return str;
}

std::string EncodeHexBtcTx(const CBtcTransaction& tx, const int serializeFlags)
{
    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION | serializeFlags);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());
}

// Check that all of the input and output scripts of a transaction contains valid opcodes
static bool CheckTxScriptsSanity(const CMutableBtcTransaction& tx)
{
    // Check input scripts for non-coinbase txs
    if (!CBtcTransaction(tx).IsCoinBase()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            if (!tx.vin[i].scriptSig.HasValidOps() || tx.vin[i].scriptSig.size() > MAX_SCRIPT_SIZE) {
                return false;
            }
        }
    }
    // Check output scripts
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        if (!tx.vout[i].scriptPubKey.HasValidOps() || tx.vout[i].scriptPubKey.size() > MAX_SCRIPT_SIZE) {
            return false;
        }
    }

    return true;
}

bool DecodeHexBtcTx(CMutableBtcTransaction& tx, const std::string& hex_tx, bool try_no_witness, bool try_witness)
{
    if (!IsHex(hex_tx)) {
        return false;
    }

    std::vector<unsigned char> txData(ParseHex(hex_tx));

    if (try_no_witness) {
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_BTC_TRANSACTION_NO_WITNESS);
        try {
            ssData >> tx;
            if (ssData.eof() && (!try_witness || CheckTxScriptsSanity(tx))) {
                return true;
            }
        } catch (const std::exception&) {
            // Fall through.
        }
    }

    if (try_witness) {
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        try {
            ssData >> tx;
            if (ssData.empty()) {
                return true;
            }
        } catch (const std::exception&) {
            // Fall through.
        }
    }

    return false;
}


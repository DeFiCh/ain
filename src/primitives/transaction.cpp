// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
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

bool CTxOut::SERIALIZE_FORCED_TO_OLD_IN_TESTS = false;

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = std::move(scriptPubKeyIn);
    nTokenId = DCT_ID{0};
}

CTxOut::CTxOut(const CAmount & nValueIn, CScript scriptPubKeyIn, DCT_ID nTokenIdIn)
{
    nValue = nValueIn;
    scriptPubKey = std::move(scriptPubKeyIn);
    nTokenId = nTokenIdIn;
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, nTokenId=%s, scriptPubKey=%s)", nValue / COIN, nValue % COIN, nTokenId.ToString(), HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::TX_VERSION_2), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(int32_t version) : nVersion(version), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nLockTime(tx.nLockTime) {}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH,
        nVersion < CTransaction::TOKENS_MIN_VERSION ?
        SERIALIZE_TRANSACTION_NO_WITNESS | SERIALIZE_TRANSACTION_NO_TOKENS : SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH,
        nVersion < CTransaction::TOKENS_MIN_VERSION ?
        SERIALIZE_TRANSACTION_NO_WITNESS | SERIALIZE_TRANSACTION_NO_TOKENS : SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeWitnessHash() const
{
    if (!HasWitness()) {
        return hash;
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CTransaction::CTransaction() : vin(), vout(), nVersion(CTransaction::TX_VERSION_2), nLockTime(0), hash{}, m_witness_hash{} {}
CTransaction::CTransaction(int32_t version) : vin(), vout(), nVersion(version), nLockTime(0), hash{}, m_witness_hash{} {}
CTransaction::CTransaction(const CMutableTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nLockTime(tx.nLockTime), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}
CTransaction::CTransaction(CMutableTransaction&& tx) : vin(std::move(tx.vin)), vout(std::move(tx.vout)), nVersion(tx.nVersion), nLockTime(tx.nLockTime), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}

CAmount CTransaction::GetValueOut(uint32_t mintingOutputsStart, DCT_ID nTokenId) const
{
    CAmount nValueOut = 0;
    for (uint32_t i = 0; i < (uint32_t) vout.size() && i < mintingOutputsStart; i++) {
        const auto& tx_out = vout[i];
        if (tx_out.nTokenId == nTokenId) {
            nValueOut += tx_out.nValue;
            if (!MoneyRange(tx_out.nValue) || !MoneyRange(nValueOut))
                throw std::runtime_error(std::string(__func__) + ": value out of range");
        }
    }
    return nValueOut;
}

TAmounts CTransaction::GetValuesOut(uint32_t mintingOutputsStart) const
{
    TAmounts nValuesOut;
    for (uint32_t i = 0; i < (uint32_t) vout.size() && i < mintingOutputsStart; i++) {
        const auto& tx_out = vout[i];
        nValuesOut[tx_out.nTokenId] += tx_out.nValue;
        if (!MoneyRange(tx_out.nValue) || !MoneyRange(nValuesOut[tx_out.nTokenId]))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nValuesOut;
}

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, PROTOCOL_VERSION);
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
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

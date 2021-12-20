// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <chainparams.h>
#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <masternodes/mn_checks.h> // For CExpirationAndVersion

/// @todo refactor it to unify txs!!! (need to restart blockchain)
const std::vector<unsigned char> DfAnchorFinalizeTxMarker = {'D', 'f', 'A', 'f'};
const std::vector<unsigned char> DfAnchorFinalizeTxMarkerPlus = {'D', 'f', 'A', 'P'};

bool CheckTransaction(const CTransaction& tx, CValidationState &state, bool fCheckDuplicateInputs)
{
    /// @note we don't check minted token's outputs nor auth here!
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (tx.vout.empty())
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs - note that this check is slow so we skip it in CheckBlock
    if (fCheckDuplicateInputs) {
        std::set<COutPoint> vInOutPoints;
        for (const auto& txin : tx.vin)
        {
            if (!vInOutPoints.insert(txin.prevout).second)
                return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
        }
    }

    if (tx.IsCoinBase())
    {
        std::vector<unsigned char> dummy;
        if (IsAnchorRewardTx(tx, dummy) || IsAnchorRewardTxPlus(tx, dummy))
            return true;
        if (tx.vin[0].scriptSig.size() < 2 || (tx.vin[0].scriptSig.size() > 100))
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

bool ParseScriptByMarker(CScript const & script,
                         const std::vector<unsigned char> & marker,
                         std::vector<unsigned char> & metadata,
                         bool& hasAdditionalOpcodes,
                         bool& hasAdditionalOpcodesGW,
                         CExpirationAndVersion* customTxParams)
{
    opcodetype opcode;
    auto pc = script.begin();
    if (!script.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!script.GetOp(pc, opcode, metadata)
    || (opcode > OP_PUSHDATA1 && opcode != OP_PUSHDATA2 && opcode != OP_PUSHDATA4)
    || metadata.size() < marker.size() + 1
    || memcmp(&metadata[0], &marker[0], marker.size()) != 0) {
        return false;
    }

    // Check that no more opcodes are found in the script
    std::vector<unsigned char> expirationAndVersion;
    if (script.GetOp(pc, opcode, expirationAndVersion)) {
        hasAdditionalOpcodes = true;
        if (expirationAndVersion.size() == sizeof(uint32_t) + sizeof(uint8_t)) {
            if (customTxParams) {
                VectorReader stream(SER_DISK, CLIENT_VERSION, expirationAndVersion, 0);
                stream >> *customTxParams;
            }
        } else {
            hasAdditionalOpcodesGW = true;
        }
        if (pc != script.end()) {
            hasAdditionalOpcodesGW = true;
        }
    }

    metadata.erase(metadata.begin(), metadata.begin() + marker.size());
    return true;
}

bool IsAnchorRewardTx(CTransaction const & tx, std::vector<unsigned char> & metadata, uint32_t height)
{
    if (!tx.IsCoinBase() || tx.vout.size() != 2 || tx.vout[0].nValue != 0) {
        return false;
    }
    bool hasAdditionalOpcodes{false};
    bool hasAdditionalOpcodesGW{false};
    const bool fortCanning{height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight)};
    const bool greatWorld{height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)};
    const auto result = ParseScriptByMarker(tx.vout[0].scriptPubKey, DfAnchorFinalizeTxMarker, metadata, hasAdditionalOpcodes, hasAdditionalOpcodesGW);
    if (fortCanning && !greatWorld && hasAdditionalOpcodes) {
        return false;
    } else if (greatWorld && hasAdditionalOpcodesGW) {
        return false;
    }
    return result;
}

bool IsAnchorRewardTxPlus(CTransaction const & tx, std::vector<unsigned char> & metadata, uint32_t height)
{
    if (!tx.IsCoinBase() || tx.vout.size() != 2 || tx.vout[0].nValue != 0) {
        return false;
    }
    bool hasAdditionalOpcodes{false};
    bool hasAdditionalOpcodesGW{false};
    const bool fortCanning{height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight)};
    const bool greatWorld{height >= static_cast<uint32_t>(Params().GetConsensus().GreatWorldHeight)};
    const auto result = ParseScriptByMarker(tx.vout[0].scriptPubKey, DfAnchorFinalizeTxMarkerPlus, metadata, hasAdditionalOpcodes, hasAdditionalOpcodesGW);
    if (fortCanning && !greatWorld && hasAdditionalOpcodes) {
        return false;
    } else if (greatWorld && hasAdditionalOpcodesGW) {
        return false;
    }
    return result;
}

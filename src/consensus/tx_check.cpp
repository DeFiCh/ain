// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <primitives/transaction.h>
#include <consensus/validation.h>

/// @todo refactor it to unify txs!!! (need to restart blockchain)
const std::vector<unsigned char> DfCriminalTxMarker = {'D', 'f', 'C', 'r'};
const std::vector<unsigned char> DfAnchorFinalizeTxMarker = {'D', 'f', 'A', 'f'};

bool CheckTransaction(const CTransaction& tx, CValidationState &state, const TAmounts& maxMoney, bool fCheckDuplicateInputs)
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
    TAmounts nValues;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-vout-negative");
        auto& val = nValues[txout.nTokenId];
        auto res = SafeAdd(val, txout.nValue);
        if (!res)
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        val = res;
        auto it = maxMoney.find(txout.nTokenId);
        if (it == maxMoney.end())
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-txout-unknown-token");
        if (!MoneyRange(val, it->second))
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
        if (IsAnchorRewardTx(tx, dummy) || IsCriminalProofTx(tx, dummy))
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

bool IsCriminalProofTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (!tx.IsCoinBase() || tx.vout.size() != 1 || tx.vout[0].nValue != 0) {
        return false;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfCriminalTxMarker.size() + 1 ||
        memcmp(&metadata[0], &DfCriminalTxMarker[0], DfCriminalTxMarker.size()) != 0) {
        return false;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfCriminalTxMarker.size());
    return true;
}

bool IsAnchorRewardTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (!tx.IsCoinBase() || tx.vout.size() != 2 || tx.vout[0].nValue != 0) {
        return false;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfAnchorFinalizeTxMarker.size() + 1 ||
        memcmp(&metadata[0], &DfAnchorFinalizeTxMarker[0], DfAnchorFinalizeTxMarker.size()) != 0) {
        return false;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfAnchorFinalizeTxMarker.size());
    return true;
}


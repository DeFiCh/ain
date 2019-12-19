#include "mn_checks.h"
#include "masternodes.h"

#include "arith_uint256.h"
#include "chainparams.h"
#include "logging.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "univalue/include/univalue.h"
#include "streams.h"

#include <algorithm>
#include <sstream>

using namespace std;

CPubKey GetPubkeyFromScriptSig(CScript const & scriptSig)
{
    CScript::const_iterator pc = scriptSig.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    // Signature first, then pubkey. I think, that in all cases it will be OP_PUSHDATA1, but...
    if (!scriptSig.GetOp(pc, opcode, data) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4))
    {
        return CPubKey();
    }
    if (!scriptSig.GetOp(pc, opcode, data) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4))
    {
        return CPubKey();
    }
    return CPubKey(data);
}

bool HasAuth(CTransaction const & tx, CKeyID const & auth)
{
    for (auto input : tx.vin)
    {
        if (input.scriptWitness.IsNull()) {
            if (GetPubkeyFromScriptSig(input.scriptSig).GetID() == auth)
               return true;
        }
        else
        {
            /// @todo EXTEND IT TO SUPPORT WITNESS!!
            auto test = CPubKey(input.scriptWitness.stack.back());
            auto addr = test.GetID();
            (void) addr;
            std::cout << addr.ToString();

            if (test.GetID() == auth)
               return true;
        }
    }
    return false;
}

bool CheckMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, Consensus::Params const & consensusParams, int height, bool isCheck)
{
    bool result = true;
    if (tx.IsCoinBase())
    {
        return true;
    }

    if (tx.vout.size() > 0)
    {
        // Check if it is masternode tx with metadata
        std::vector<unsigned char> metadata;
        MasternodesTxType guess = GuessMasternodeTxType(tx, metadata);
        switch (guess)
        {
            case MasternodesTxType::CreateMasternode:
                result = result && CheckCreateMasternodeTx(mnview, tx, height, metadata, isCheck);
            break;
            case MasternodesTxType::ResignMasternode:
                result = result && CheckResignMasternodeTx(mnview, tx, height, metadata, isCheck);
            break;
            default:
                break;
        }
    }
    // We are always accept blocks (but skip failed txs) and fails only if it only check (but not real block processing)
    return isCheck ? result : true;
}

/*
 * Checks if given tx is 'txCreateMasternode'. Creates new MN if all checks are passed
 * Issued by: any
 */
bool CheckCreateMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, int height, std::vector<unsigned char> const & metadata, bool isCheck)
{
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetMnCreationFee(height) ||
        tx.vout[1].nValue != GetMnCollateralAmount()
        )
    {
        return false;
    }
    CMasternode node(tx, height, metadata);
    if (node.ownerAuthAddress.IsNull() || node.operatorAuthAddress.IsNull())
    {
        return false;
    }
    bool result = mnview.OnMasternodeCreate(tx.GetHash(), node);
    if (!isCheck)
    {
        LogPrintf("MN %s: Creation by tx %s at block %d\n", result ? "APPLYED" : "SKIPPED", tx.GetHash().GetHex(), height);
    }
    return result;
}

bool CheckResignMasternodeTx(CMasternodesViewCache & mnview, CTransaction const & tx, int height, const std::vector<unsigned char> & metadata, bool isCheck)
{
    uint256 nodeId(metadata);
    auto const node = mnview.ExistMasternode(nodeId);
    if (!node || node->resignHeight != -1 || node->resignTx != uint256() || !HasAuth(tx, node->ownerAuthAddress))
    {
        /// @todo @maxb more verbose? at least, auth?
        return false;
    }

    bool result = mnview.OnMasternodeResign(nodeId, tx.GetHash(), height);
    if (!isCheck)
    {
        LogPrintf("MN %s: Resign by tx %s at block %d\n", result ? "APPLYED" : "SKIPPED", tx.GetHash().GetHex(), height);
    }
    return result;
}

/*
 * Checks all inputs for collateral.
 */
bool CheckInputsForCollateralSpent(CMasternodesViewCache & mnview, CTransaction const & tx, int height, bool isCheck)
{
    bool total(true);
    for (uint32_t i = 0; i < tx.vin.size() && total; ++i)
    {
        COutPoint const & prevout = tx.vin[i].prevout;
        // Checks if it was collateral output.
        if (prevout.n == 1 && mnview.ExistMasternode(prevout.hash))
        {
            // i - unused
            bool result = mnview.CanSpend(prevout.hash, height);

            if (!isCheck)
            {
                LogPrintf("MN %s: Spent collateral by tx %s for %s at block %d\n", result ? "APPROVED" : "DENIED", tx.GetHash().GetHex(), prevout.hash.GetHex(), height);
            }
            total = total && result;
        }
    }
    return total;
}

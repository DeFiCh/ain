#include <consensus/validation.h>
#include <core_io.h>
#include <libain.hpp>
#include <masternodes/masternodes.h>
#include <rpc/blockchain.h>
#include <rpc/libain_wrapper.hpp>
#include <rpc/util.h>
#include <sync.h>
#include <validation.h>

int GetRPCSerializationFlags() {
    return 0; // FIXME: Pass RPCSerializationFlags() as option from Rust
}

double FromAmount(CAmount amount) {
    bool sign = amount < 0;
    auto n_abs = (sign ? -amount : amount);
    return n_abs / (double)COIN;
}

void SetRewardFromAmount(CommunityAccountType t, CAmount amount, NonUtxo* nonutxo)
{
    auto value = FromAmount(amount);
    switch (t) {
    case CommunityAccountType::IncentiveFunding:
        nonutxo->incentive_funding = value;
        break;
    case CommunityAccountType::AnchorReward:
        nonutxo->anchor_reward = value;
        break;
    case CommunityAccountType::Loan:
        nonutxo->loan = value;
        break;
    case CommunityAccountType::Options:
        nonutxo->options = value;
        break;
    case CommunityAccountType::Unallocated:
        nonutxo->burnt = value;
        break;
    default:
        nonutxo->unknown = value;
    }
}

static int ComputeNextBlockAndDepth(const CBlockIndex* tip, const CBlockIndex* blockindex, const CBlockIndex*& next)
{
    next = tip->GetAncestor(blockindex->nHeight + 1);
    if (next && next->pprev == blockindex) {
        return tip->nHeight - blockindex->nHeight + 1;
    }
    next = nullptr;
    return blockindex == tip ? 1 : -1;
}

void setScriptPubKey(const CScript& scriptPubKey, bool fIncludeHex, PubKey* result)
{
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;

    result->field_asm = ScriptToAsmStr(scriptPubKey);
    if (fIncludeHex)
        result->hex = HexStr(scriptPubKey.begin(), scriptPubKey.end());

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        result->field_type = GetTxnOutputType(type);
        return;
    }

    result->req_sigs = nRequired;
    result->field_type = GetTxnOutputType(type);

    for (const CTxDestination& addr : addresses) {
        result->addresses.push_back(EncodeDestination(addr));
    }
}

void setTransaction(const CTransaction& tx, const uint256& hashBlock, bool include_hex, int serialize_flags, RawTransaction* result)
{
    result->txid = tx.GetHash().GetHex();
    result->hash = tx.GetWitnessHash().GetHex();
    result->version = tx.nVersion;
    result->size = (uint32_t)::GetSerializeSize(tx, PROTOCOL_VERSION);
    result->vsize = (GetTransactionWeight(tx) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
    result->weight = GetTransactionWeight(tx);
    result->locktime = (uint32_t)tx.nLockTime;

    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        Vin vin;

        if (tx.IsCoinBase())
            vin.coinbase = HexStr(txin.scriptSig.begin(), txin.scriptSig.end());
        else {
            vin.txid = txin.prevout.hash.GetHex();
            vin.vout = (uint32_t)txin.prevout.n;
            vin.script_sig.field_asm = ScriptToAsmStr(txin.scriptSig, true);
            vin.script_sig.hex = HexStr(txin.scriptSig.begin(), txin.scriptSig.end());
            if (!tx.vin[i].scriptWitness.IsNull()) {
                for (const auto& item : tx.vin[i].scriptWitness.stack) {
                    vin.txinwitness.push_back(HexStr(item.begin(), item.end()));
                }
            }
        }
        vin.sequence = (uint64_t)txin.nSequence;
        result->vin.push_back(vin);
    }

    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];
        Vout vout;

        vout.value = FromAmount(txout.nValue);
        vout.n = (uint64_t)i;

        setScriptPubKey(txout.scriptPubKey, true, &vout.script_pub_key);
        // Start to print tokenId start from version TOKENS_MIN_VERSION
        if (tx.nVersion >= CTransaction::TOKENS_MIN_VERSION) {
            vout.token_id = (uint64_t)txout.nTokenId.v;
        }
        result->vout.push_back(vout);
    }

    if (!hashBlock.IsNull())
        result->blockhash = hashBlock.GetHex();

    if (include_hex) {
        result->hex = EncodeHexTx(tx, serialize_flags); // The hex-encoded transaction. Used the name "hex" to be consistent with the verbose output of "getrawtransaction".
    }
}

void setBlock(const CBlock& block, const CBlockIndex* tip, const CBlockIndex* blockindex, bool txDetails, Block* result)
{
    // Serialize passed information without accessing chain state of the active chain!
    AssertLockNotHeld(cs_main); // For performance reasons

    result->hash = blockindex->GetBlockHash().GetHex();
    const CBlockIndex* pnext;
    result->confirmations = (int64_t)::ComputeNextBlockAndDepth(tip, blockindex, pnext);
    result->strippedsize = (uint64_t)::GetSerializeSize(block, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    result->size = (uint64_t)::GetSerializeSize(block, PROTOCOL_VERSION);
    result->weight = (uint64_t)::GetBlockWeight(block);
    result->height = (uint64_t)blockindex->nHeight;

    CKeyID minter;
    block.ExtractMinterKey(minter);
    auto id = pcustomcsview->GetMasternodeIdByOperator(minter);
    if (id) {
        result->masternode = id->ToString();
        auto mn = pcustomcsview->GetMasternode(*id);
        if (mn) {
            auto dest = mn->operatorType == 1 ? CTxDestination(PKHash(minter)) : CTxDestination(WitnessV0KeyHash(minter));
            result->minter = EncodeDestination(dest);
        }
    }
    result->minted_blocks = blockindex->mintedBlocks;
    result->stake_modifier = blockindex->stakeModifier.ToString();
    result->version = (uint64_t)block.nVersion;
    result->version_hex = strprintf("%08x", block.nVersion);
    result->merkleroot = block.hashMerkleRoot.GetHex();

    if (blockindex->nHeight >= Params().GetConsensus().AMKHeight) {
        CAmount blockReward = GetBlockSubsidy(blockindex->nHeight, Params().GetConsensus());
        NonUtxo nonutxo;

        if (blockindex->nHeight >= Params().GetConsensus().EunosHeight) {
            CAmount burnt{0};
            for (const auto& kv : Params().GetConsensus().newNonUTXOSubsidies) {
                CAmount subsidy = CalculateCoinbaseReward(blockReward, kv.second);

                if (kv.first == CommunityAccountType::AnchorReward) {
                    SetRewardFromAmount(kv.first, subsidy, &nonutxo);
                } else {
                    burnt += subsidy; // Everything else goes into burnt
                }
            }

            // Add burnt total
            SetRewardFromAmount(CommunityAccountType::Unallocated, burnt, &nonutxo);
        } else {
            for (const auto& kv : Params().GetConsensus().nonUtxoBlockSubsidies) {
                // Anchor and LP incentive
                SetRewardFromAmount(kv.first, blockReward * kv.second / COIN, &nonutxo);
            }
        }

        result->nonutxo.push_back(nonutxo);
    }

    result->time = (uint64_t)block.GetBlockTime();
    result->mediantime = (uint64_t)blockindex->GetMedianTimePast();
    result->bits = strprintf("%08x", block.nBits);
    result->difficulty = GetDifficulty(blockindex);
    result->chainwork = blockindex->nChainWork.GetHex();
    result->n_tx = (uint64_t)blockindex->nTx;

    if (blockindex->pprev)
        result->previous_block_hash = blockindex->pprev->GetBlockHash().GetHex();
    if (pnext)
        result->next_block_hash = pnext->GetBlockHash().GetHex();

    for (const auto& tx : block.vtx) {
        Transaction txn;
        if (txDetails) {
            setTransaction(*tx, uint256(), true, GetRPCSerializationFlags(), &txn.raw);
        } else {
            txn.hash = tx->GetHash().GetHex();
        }
        result->tx.push_back(txn);
    }
}

void GetBestBlockHash(BlockResult& result)
{
    LOCK(cs_main);
    result.hash = ::ChainActive().Tip()->GetBlockHash().GetHex();
}

void GetBlock(BlockInput& block_input, BlockResult& result)
{
    uint256 hash(ParseHashS(std::string(block_input.blockhash), "blockhash"));

    CBlock block;
    const CBlockIndex* pblockindex;
    const CBlockIndex* tip;
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);
        tip = ::ChainActive().Tip();

        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        block = GetBlockChecked(pblockindex);
    }

    if (block_input.verbosity == 0) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION | GetRPCSerializationFlags());
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        result.hash = strHex;
    }

    setBlock(block, tip, pblockindex, block_input.verbosity > 1, &result.block);
}

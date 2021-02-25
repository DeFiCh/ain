// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/criminals.h>
#include <masternodes/masternodes.h>

static const unsigned int DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL = 100;

const unsigned char CMintedHeadersView::MintedHeaders ::prefix = 'h';
const unsigned char CCriminalProofsView::Proofs       ::prefix = 'm';

struct DBMNBlockHeadersKey
{
    uint256 masternodeID;
    uint64_t mintedBlocks;
    uint256 blockHash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(masternodeID);
        READWRITE(mintedBlocks);
        READWRITE(blockHash);
    }
};

void CMintedHeadersView::WriteMintedBlockHeader(const uint256 & txid, const uint64_t mintedBlocks, const uint256 & hash, const CBlockHeader & blockHeader, bool fIsFakeNet)
{
    if (fIsFakeNet) {
        return;
    }
    // directly!
    WriteBy<MintedHeaders>(DBMNBlockHeadersKey{txid, mintedBlocks, hash}, blockHeader);
}

bool CMintedHeadersView::FetchMintedHeaders(const uint256 & txid, const uint64_t mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders, bool fIsFakeNet)
{
    if (fIsFakeNet) {
        return false;
    }

    blockHeaders.clear();
    ForEach<MintedHeaders,DBMNBlockHeadersKey,CBlockHeader>([&txid, &mintedBlocks, &blockHeaders] (DBMNBlockHeadersKey const & key, CLazySerialize<CBlockHeader> blockHeader) {
        if (key.masternodeID == txid &&
            key.mintedBlocks == mintedBlocks) {

            blockHeaders.emplace(key.blockHash, blockHeader.get());
            return true; // continue
        }
        return false; // break!
    }, DBMNBlockHeadersKey{ txid, mintedBlocks, uint256{} } );

    return true;
}

void CMintedHeadersView::EraseMintedBlockHeader(const uint256 & txid, const uint64_t mintedBlocks, const uint256 & hash)
{
    // directly!
    EraseBy<MintedHeaders>(DBMNBlockHeadersKey{txid, mintedBlocks, hash});
}

void CCriminalProofsView::AddCriminalProof(const uint256 & id, const CBlockHeader & blockHeader, const CBlockHeader & conflictBlockHeader) {
    WriteBy<Proofs>(id, CDoubleSignFact{blockHeader, conflictBlockHeader});
    LogPrintf("Add criminal proof for node %s, blocks: %s, %s\n", id.ToString(), blockHeader.GetHash().ToString(), conflictBlockHeader.GetHash().ToString());
}

void CCriminalProofsView::RemoveCriminalProofs(const uint256 & mnId) {
    // in fact, only one proof
    EraseBy<Proofs>(mnId);
    LogPrintf("Criminals: erase proofs for node %s\n", mnId.ToString());
}

CCriminalProofsView::CMnCriminals CCriminalProofsView::GetUnpunishedCriminals() {

    CMnCriminals result;
    ForEach<Proofs, uint256, CDoubleSignFact>([&result] (uint256 const & id, CLazySerialize<CDoubleSignFact> proof) {

        // matching with already punished. and this is the ONLY measure!
        auto node = pcustomcsview->GetMasternode(id); // assert?
        if (node && node->banTx.IsNull()) {
            result.emplace(id, proof.get());
        }
        return true; // continue
    });
    return result;

}

bool IsDoubleSignRestricted(uint64_t height1, uint64_t height2)
{
    return (std::max(height1, height2) - std::min(height1, height2)) <= DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL;
}

bool IsDoubleSigned(CBlockHeader const & oneHeader, CBlockHeader const & twoHeader, CKeyID & minter)
{
    // not necessary to check if such masternode exists or active. this is proof by itself!
    CKeyID firstKey, secondKey;
    if (!oneHeader.ExtractMinterKey(firstKey) || !twoHeader.ExtractMinterKey(secondKey)) {
        return false;
    }

    if (IsDoubleSignRestricted(oneHeader.height, twoHeader.height) &&
        firstKey == secondKey &&
        oneHeader.mintedBlocks == twoHeader.mintedBlocks &&
        oneHeader.GetHash() != twoHeader.GetHash()
        ) {
        minter = firstKey;
        return true;
    }
    return false;
}

/** Global variable that holds CCriminalsView (should be protected by cs_main) */
std::unique_ptr<CCriminalsView> pcriminals;

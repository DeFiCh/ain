// Copyright (c) 2019 DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CRIMINALS_H
#define DEFI_MASTERNODES_CRIMINALS_H

#include <flushablestorage.h>
#include <primitives/block.h>


class CCustomCSView;
extern std::unique_ptr<CCustomCSView> pcustomcsview;

class CDoubleSignFact
{
public:
    CBlockHeader blockHeader;
    CBlockHeader conflictBlockHeader;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(blockHeader);
        READWRITE(conflictBlockHeader);
    }

    friend bool operator==(CDoubleSignFact const & a, CDoubleSignFact const & b);
    friend bool operator!=(CDoubleSignFact const & a, CDoubleSignFact const & b);
};


class CMintedHeadersView : public virtual CStorageView
{
public:
    void WriteMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader, bool fIsFakeNet);
    bool FetchMintedHeaders(uint256 const & txid, uint64_t const mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders, bool fIsFakeNet);
    void EraseMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash);

    struct MintedHeaders { static const unsigned char prefix; };
};


class CCriminalProofsView : public virtual CStorageView
{
public:
    void AddCriminalProof(uint256 const & id, CBlockHeader const & blockHeader, CBlockHeader const & conflictBlockHeader);
    void RemoveCriminalProofs(uint256 const & mnId);

    using CMnCriminals = std::map<uint256, CDoubleSignFact>; // nodeId, two headers
    CMnCriminals GetUnpunishedCriminals();

    struct Proofs { static const unsigned char prefix; };
};

// "off-chain" data, should be written directly
class CCriminalsView
        : public CMintedHeadersView
        , public CCriminalProofsView
{
public:
    CCriminalsView(const fs::path& dbName, std::size_t cacheSize, bool fMemory = false, bool fWipe = false)
        : CStorageView(new CStorageLevelDB(dbName, cacheSize, fMemory, fWipe))
    {}

};

/** Global variable that holds CCriminalsView (should be protected by cs_main) */
extern std::unique_ptr<CCriminalsView> pcriminals;

bool IsDoubleSignRestricted(uint64_t height1, uint64_t height2);
bool IsDoubleSigned(CBlockHeader const & oneHeader, CBlockHeader const & twoHeader, CKeyID & minter);

#endif // DEFI_MASTERNODES_CRIMINALS_H

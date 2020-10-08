// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MN_CHECKS_H
#define DEFI_MASTERNODES_MN_CHECKS_H

#include <consensus/params.h>
#include <masternodes/masternodes.h>
#include <vector>
#include <cstring>

class CBlock;
class CTransaction;
class CTxMemPool;
class CCoinsViewCache;

class CCustomCSView;

static const std::vector<unsigned char> DfTxMarker = {'D', 'f', 'T', 'x'};  // 44665478

enum CustomTxErrCodes : uint32_t {
    NotSpecified = 0,
//    NotCustomTx  = 1,
    NotEnoughBalance = 1024,
    Fatal = uint32_t(1) << 31 // not allowed to fail
};

enum class CustomTxType : unsigned char
{
    None = 0,
    // masternodes:
    CreateMasternode    = 'C',
    ResignMasternode    = 'R',
    // custom tokens:
    CreateToken         = 'T',
    MintToken           = 'M',
    UpdateToken         = 'N',
    // dex orders - just not to overlap in future
//    CreateOrder         = 'O',
//    DestroyOrder        = 'E',
//    MatchOrders         = 'A',
    // accounts
    UtxosToAccount     = 'U',
    AccountToUtxos     = 'b',
    AccountToAccount  = 'B'
};

inline CustomTxType CustomTxCodeToType(unsigned char ch) {
    char const txtypes[] = "CRTMDNUbB";
    if (memchr(txtypes, ch, strlen(txtypes)))
        return static_cast<CustomTxType>(ch);
    else
        return CustomTxType::None;
}

inline bool NotAllowedToFail(CustomTxType txType) {
    return txType == CustomTxType::MintToken || txType == CustomTxType::AccountToUtxos;
}

template<typename Stream>
inline void Serialize(Stream& s, CustomTxType txType)
{
    Serialize(s, static_cast<unsigned char>(txType));
}

template<typename Stream>
inline void Unserialize(Stream& s, CustomTxType & txType) {
    unsigned char ch;
    Unserialize(s, ch);

    txType = CustomTxCodeToType(ch);
}

bool HasAuth(CTransaction const & tx, CKeyID const & auth);
bool HasAuth(CTransaction const & tx, CCoinsViewCache const & coins, CScript const & auth);
bool HasCollateralAuth(CTransaction const & tx, CCoinsViewCache const & coins, uint256 const & collateralTx);

Res ApplyCustomTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, const Consensus::Params& consensusParams, uint32_t height, bool isCheck = true);
//! Deep check (and write)
Res ApplyCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata);
Res ApplyResignMasternodeTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata);

Res ApplyCreateTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);
Res ApplyUpdateTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);
Res ApplyMintTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);

Res ApplyUtxosToAccountTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);
Res ApplyAccountToUtxosTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);
Res ApplyAccountToAccountTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);

ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView & mnview, CTransaction const & tx, int height, uint256 const & prevStakeModifier, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams);

bool IsMempooledCustomTxCreate(const CTxMemPool& pool, const uint256 & txid);

// @todo refactor header functions
/*
 * Checks if given tx is probably one of 'CustomTx', returns tx type and serialized metadata in 'data'
*/
inline CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (tx.vout.size() == 0)
    {
        return CustomTxType::None;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN)
    {
        return CustomTxType::None;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfTxMarker.size() + 1 ||     // i don't know how much exactly, but at least MnTxSignature + type prefix
        memcmp(&metadata[0], &DfTxMarker[0], DfTxMarker.size()) != 0)
    {
        return CustomTxType::None;
    }
    auto txType = CustomTxCodeToType(metadata[DfTxMarker.size()]);
    metadata.erase(metadata.begin(), metadata.begin() + DfTxMarker.size() + 1);
    return txType;
}

inline boost::optional<std::vector<unsigned char>> GetMintTokenMetadata(const CTransaction & tx)
{
    std::vector<unsigned char> metadata;
    if (GuessCustomTxType(tx, metadata) == CustomTxType::MintToken) {
        return metadata;
    }
    return {};
}

inline boost::optional<std::vector<unsigned char>> GetAccountToUtxosMetadata(const CTransaction & tx)
{
    std::vector<unsigned char> metadata;
    if (GuessCustomTxType(tx, metadata) == CustomTxType::AccountToUtxos) {
        return metadata;
    }
    return {};
}

inline boost::optional<CAccountToUtxosMessage> GetAccountToUtxosMsg(const CTransaction & tx)
{
    const auto metadata = GetAccountToUtxosMetadata(tx);
    if (metadata) {
        CAccountToUtxosMessage msg;
        try {
            CDataStream ss(*metadata, SER_NETWORK, PROTOCOL_VERSION);
            ss >> msg;
        } catch (...) {
            return {};
        }
        return msg;
    }
    return {};
}

inline TAmounts GetNonMintedValuesOut(const CTransaction & tx)
{
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetAccountToUtxosMsg(tx);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValuesOut(mintingOutputsStart);
}

inline CAmount GetNonMintedValueOut(const CTransaction & tx, DCT_ID tokenID)
{
    uint32_t mintingOutputsStart = std::numeric_limits<uint32_t>::max();
    const auto accountToUtxos = GetAccountToUtxosMsg(tx);
    if (accountToUtxos) {
        mintingOutputsStart = accountToUtxos->mintingOutputsStart;
    }
    return tx.GetValueOut(mintingOutputsStart, tokenID);
}

#endif // DEFI_MASTERNODES_MN_CHECKS_H

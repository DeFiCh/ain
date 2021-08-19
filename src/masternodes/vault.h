#ifndef DEFI_MASTERNODES_VAULT_H
#define DEFI_MASTERNODES_VAULT_H

#include <amount.h>
#include <uint256.h>

#include <flushablestorage.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <script/script.h>

// use vault's creation tx for ID
using CVaultId = uint256;

struct CVaultMessage {
    CScript ownerAddress;
    std::string schemeId;
    bool isUnderLiquidation{false};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ownerAddress);
        READWRITE(schemeId);
        READWRITE(isUnderLiquidation);
    }
};

struct CUpdateVaultMessage {
    CVaultId vaultId;
    CScript ownerAddress;
    std::string schemeId;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vaultId);
        READWRITE(ownerAddress);
        READWRITE(schemeId);
    }
};

struct CAuctionData {
    uint32_t batchCount;
    CAmount liquidationPenalty;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(batchCount);
        READWRITE(liquidationPenalty);
    }
};

struct CAuctionBatch {
    CBalances collaterals;
    CTokenAmount loanAmount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(collaterals);
        READWRITE(loanAmount);
    }
};

class CVaultView : public virtual CStorageView
{
public:
    Res StoreVault(const CVaultId&, const CVaultMessage&);
    ResVal<CVaultMessage> GetVault(const CVaultId&) const;
    Res UpdateVault(const CVaultId& vaultId, const CVaultMessage& newVault);
    void ForEachVault(std::function<bool(const CVaultId&, const CVaultMessage&)> callback);

    Res AddVaultCollateral(const CVaultId& vaultId, CTokenAmount amount);
    Res SubVaultCollateral(const CVaultId& vaultId, CTokenAmount amount);
    boost::optional<CBalances> GetVaultCollaterals(const CVaultId& vaultId);
    void ForEachVaultCollateral(std::function<bool(const CVaultId&, const CBalances&)> callback);

    Res StoreAuction(const CVaultId& vaultId, uint32_t height, const CAuctionData& data);
    Res EraseAuction(const CVaultId& vaultId, uint32_t height);
    Res StoreAuctionBatch(const CVaultId& vaultId, uint32_t id, const CAuctionBatch& batch);
    Res EraseAuctionBatch(const CVaultId& vaultId, uint32_t id);
    boost::optional<CAuctionBatch> GetAuctionBatch(const CVaultId& vaultId, uint32_t id);
    void ForEachVaultAuction(std::function<bool(const CVaultId&, const CAuctionData&)> callback, uint32_t height);

    using COwnerTokenAmount = std::pair<CScript, CTokenAmount>;
    Res StoreAuctionBid(const CVaultId& vaultId, uint32_t id, COwnerTokenAmount amount);
    Res EraseAuctionBid(const CVaultId& vaultId, uint32_t id);
    boost::optional<COwnerTokenAmount> GetAuctionBid(const CVaultId& vaultId, uint32_t id);

    struct VaultKey { static const unsigned char prefix; };
    struct CollateralKey { static const unsigned char prefix; };
    struct AuctionBatchKey { static const unsigned char prefix; };
    struct AuctionHeightKey { static const unsigned char prefix; };
    struct AuctionBidKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_VAULT_H

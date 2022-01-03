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

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ownerAddress);
        READWRITE(schemeId);
    }
};

struct CVaultData : public CVaultMessage {
    bool isUnderLiquidation;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITEAS(CVaultMessage, *this);
        READWRITE(isUnderLiquidation);
    }
};

struct CCloseVaultMessage {
    CVaultId vaultId;
    CScript to;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vaultId);
        READWRITE(to);
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

struct CDepositToVaultMessage {
    CVaultId vaultId;
    CScript from;
    CTokenAmount amount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vaultId);
        READWRITE(from);
        READWRITE(amount);
    }
};

struct CWithdrawFromVaultMessage {
    CVaultId vaultId;
    CScript to;
    CTokenAmount amount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vaultId);
        READWRITE(to);
        READWRITE(amount);
    }
};

struct CAuctionBidMessage {
    CVaultId vaultId;
    uint32_t index;
    CScript from;
    CTokenAmount amount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vaultId);
        READWRITE(index);
        READWRITE(from);
        READWRITE(amount);
    }
};

struct CAuctionData {
    uint32_t batchCount;
    uint32_t liquidationHeight; // temporary
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
    CAmount loanInterest;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(collaterals);
        READWRITE(loanAmount);
        READWRITE(loanInterest);
    }
};

class CVaultView : public virtual CStorageView
{
public:
    Res StoreVault(const CVaultId&, const CVaultData&);
    Res EraseVault(const CVaultId&);
    std::optional<CVaultData> GetVault(const CVaultId&) const;
    Res UpdateVault(const CVaultId& vaultId, const CVaultMessage& newVault);
    void ForEachVault(std::function<bool(const CVaultId&, const CVaultData&)> callback, const CVaultId& start = {}, const CScript& ownerAddress = {});

    Res AddVaultCollateral(const CVaultId& vaultId, CTokenAmount amount);
    Res SubVaultCollateral(const CVaultId& vaultId, CTokenAmount amount);
    std::optional<CBalances> GetVaultCollaterals(const CVaultId& vaultId);
    void ForEachVaultCollateral(std::function<bool(const CVaultId&, const CBalances&)> callback);

    Res StoreAuction(const CVaultId& vaultId, const CAuctionData& data);
    Res EraseAuction(const CVaultId& vaultId, uint32_t height);
    std::optional<CAuctionData> GetAuction(const CVaultId& vaultId, uint32_t height);
    Res StoreAuctionBatch(const CVaultId& vaultId, uint32_t id, const CAuctionBatch& batch);
    Res EraseAuctionBatch(const CVaultId& vaultId, uint32_t id);
    std::optional<CAuctionBatch> GetAuctionBatch(const CVaultId& vaultId, uint32_t id);
    void ForEachVaultAuction(std::function<bool(const CVaultId&, const CAuctionData&)> callback, uint32_t height, const CVaultId& vaultId = {});

    using COwnerTokenAmount = std::pair<CScript, CTokenAmount>;
    Res StoreAuctionBid(const CVaultId& vaultId, uint32_t id, COwnerTokenAmount amount);
    Res EraseAuctionBid(const CVaultId& vaultId, uint32_t id);
    std::optional<COwnerTokenAmount> GetAuctionBid(const CVaultId& vaultId, uint32_t id);

    struct VaultKey         { static constexpr uint8_t prefix() { return 0x20; } };
    struct OwnerVaultKey    { static constexpr uint8_t prefix() { return 0x21; } };
    struct CollateralKey    { static constexpr uint8_t prefix() { return 0x22; } };
    struct AuctionBatchKey  { static constexpr uint8_t prefix() { return 0x23; } };
    struct AuctionHeightKey { static constexpr uint8_t prefix() { return 0x24; } };
    struct AuctionBidKey    { static constexpr uint8_t prefix() { return 0x25; } };
};

#endif // DEFI_MASTERNODES_VAULT_H

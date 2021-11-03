
#include <chainparams.h>
#include <masternodes/vault.h>

struct CAuctionKey {
    CVaultId vaultId;
    uint32_t height;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(WrapBigEndian(height));
        READWRITE(vaultId);
    }
};

Res CVaultView::StoreVault(const CVaultId& vaultId, const CVaultData& vault)
{
    WriteBy<VaultKey>(vaultId, vault);
    WriteBy<OwnerVaultKey>(std::make_pair(vault.ownerAddress, vaultId), '\0');
    return Res::Ok();
}

Res CVaultView::EraseVault(const CVaultId& vaultId)
{
    auto vault = GetVault(vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", vaultId.GetHex());
    }
    EraseBy<VaultKey>(vaultId);
    EraseBy<CollateralKey>(vaultId);
    EraseBy<OwnerVaultKey>(std::make_pair(vault->ownerAddress, vaultId));
    return Res::Ok();
}

std::optional<CVaultData> CVaultView::GetVault(const CVaultId& vaultId) const
{
    return ReadBy<VaultKey, CVaultData>(vaultId);
}

Res CVaultView::UpdateVault(const CVaultId& vaultId, const CVaultMessage& newVault)
{
    auto vault = GetVault(vaultId);
    if (!vault) {
        return Res::Err("Vault <%s> not found", vaultId.GetHex());
    }

    EraseBy<OwnerVaultKey>(std::make_pair(vault->ownerAddress, vaultId));

    vault->ownerAddress = newVault.ownerAddress;
    vault->schemeId = newVault.schemeId;

    return StoreVault(vaultId, *vault);
}

void CVaultView::ForEachVault(std::function<bool(const CVaultId&, const CVaultData&)> callback, const CVaultId& start, const CScript& ownerAddress)
{
    if (ownerAddress.empty()) {
        ForEach<VaultKey, CVaultId, CVaultData>(callback, start);
    } else {
        ForEach<OwnerVaultKey, std::pair<CScript, CVaultId>, char>([&](const std::pair<CScript, CVaultId>& key, const char) {
            return callback(key.second, *GetVault(key.second));
        }, std::make_pair(ownerAddress, start));
    }
}

Res CVaultView::AddVaultCollateral(const CVaultId& vaultId, CTokenAmount amount)
{
    CBalances amounts;
    ReadBy<CollateralKey>(vaultId, amounts);
    auto res = amounts.Add(amount);
    if (!res) {
        return res;
    }
    if (!amounts.balances.empty()) {
        WriteBy<CollateralKey>(vaultId, amounts);
    }
    return Res::Ok();
}

Res CVaultView::SubVaultCollateral(const CVaultId& vaultId, CTokenAmount amount)
{
    auto amounts = GetVaultCollaterals(vaultId);
    if (!amounts || !amounts->Sub(amount)) {
        return Res::Err("Collateral for vault <%s> not found", vaultId.GetHex());
    }
    if (amounts->balances.empty()) {
        EraseBy<CollateralKey>(vaultId);
    } else {
        WriteBy<CollateralKey>(vaultId, *amounts);
    }
    return Res::Ok();
}

std::optional<CBalances> CVaultView::GetVaultCollaterals(const CVaultId& vaultId)
{
    return ReadBy<CollateralKey, CBalances>(vaultId);
}

void CVaultView::ForEachVaultCollateral(std::function<bool(const CVaultId&, const CBalances&)> callback)
{
    ForEach<CollateralKey, CVaultId, CBalances>(callback);
}

Res CVaultView::StoreAuction(const CVaultId& vaultId, const CAuctionData& data)
{
    WriteBy<AuctionHeightKey>(CAuctionKey{vaultId, data.liquidationHeight}, data);
    return Res::Ok();
}

Res CVaultView::EraseAuction(const CVaultId& vaultId, uint32_t height)
{
    auto it = LowerBound<AuctionHeightKey>(CAuctionKey{vaultId, height});
    for (; it.Valid(); it.Next()) {
        if (it.Key().vaultId == vaultId) {
            CAuctionData data = it.Value();
            for (uint32_t i = 0; i < data.batchCount; i++) {
                EraseAuctionBid(vaultId, i);
                EraseAuctionBatch(vaultId, i);
            }
            EraseBy<AuctionHeightKey>(it.Key());
            return Res::Ok();
        }
    }
    return Res::Err("Auction for vault <%s> not found", vaultId.GetHex());
}

std::optional<CAuctionData> CVaultView::GetAuction(const CVaultId& vaultId, uint32_t height)
{
    auto it = LowerBound<AuctionHeightKey>(CAuctionKey{vaultId, height});
    for (; it.Valid(); it.Next()) {
        if (it.Key().vaultId == vaultId) {
            CAuctionData data = it.Value();
            data.liquidationHeight = it.Key().height;
            return data;
        }
    }
    return {};
}

Res CVaultView::StoreAuctionBatch(const CVaultId& vaultId, uint32_t id, const CAuctionBatch& batch)
{
    WriteBy<AuctionBatchKey>(std::make_pair(vaultId, id), batch);
    return Res::Ok();
}

Res CVaultView::EraseAuctionBatch(const CVaultId& vaultId, uint32_t id)
{
    EraseBy<AuctionBatchKey>(std::make_pair(vaultId, id));
    return Res::Ok();
}

std::optional<CAuctionBatch> CVaultView::GetAuctionBatch(const CVaultId& vaultId, uint32_t id)
{
    return ReadBy<AuctionBatchKey, CAuctionBatch>(std::make_pair(vaultId, id));
}

void CVaultView::ForEachVaultAuction(std::function<bool(const CVaultId&, const CAuctionData&)> callback, uint32_t height, const CVaultId& vaultId)
{
    ForEach<AuctionHeightKey, CAuctionKey, CAuctionData>([&](const CAuctionKey& auction, CAuctionData data) {
        data.liquidationHeight = auction.height;
        return callback(auction.vaultId, data);
    }, CAuctionKey{vaultId, height});
}

Res CVaultView::StoreAuctionBid(const CVaultId& vaultId, uint32_t id, COwnerTokenAmount amount)
{
    WriteBy<AuctionBidKey>(std::make_pair(vaultId, id), amount);
    return Res::Ok();
}

Res CVaultView::EraseAuctionBid(const CVaultId& vaultId, uint32_t id)
{
    EraseBy<AuctionBidKey>(std::make_pair(vaultId, id));
    return Res::Ok();
}

std::optional<CVaultView::COwnerTokenAmount> CVaultView::GetAuctionBid(const CVaultId& vaultId, uint32_t id)
{
    return ReadBy<AuctionBidKey, COwnerTokenAmount>(std::make_pair(vaultId, id));
}

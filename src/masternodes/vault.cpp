
#include <chainparams.h>
#include <masternodes/vault.h>

Res CVaultView::StoreVault(const CVaultId& vaultId, const CVaultMessage& vault)
{
    if (!WriteBy<VaultKey>(vaultId, vault)) {
        return Res::Err("Failed to create new vault <%s>", vaultId.GetHex());
    }

    return Res::Ok();
}

ResVal<CVaultMessage> CVaultView::GetVault(const CVaultId& vaultId) const
{
    CVaultMessage vault{};
    if (!ReadBy<VaultKey>(vaultId, vault)) {
        return Res::Err("vault <%s> not found", vaultId.GetHex());
    }
    return ResVal<CVaultMessage>(vault, Res::Ok());
}

Res CVaultView::UpdateVault(const CVaultId& vaultId, const CVaultMessage& newVault)
{
    CVaultMessage vault{};
    if (!ReadBy<VaultKey>(vaultId, vault)) {
        return Res::Err("vault <%s> not found", vaultId.GetHex());
    }

    vault.ownerAddress = newVault.ownerAddress;
    vault.schemeId = newVault.schemeId;

    if (!WriteBy<VaultKey>(vaultId, vault)) {
        return Res::Err("failed to save vault <%s>", vaultId.GetHex());
    }

    return Res::Ok();
}

void CVaultView::ForEachVault(std::function<bool(const CVaultId&, const CVaultMessage&)> callback)
{
    ForEach<VaultKey, CVaultId, CVaultMessage>(callback);
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

boost::optional<CBalances> CVaultView::GetVaultCollaterals(const CVaultId& vaultId)
{
    return ReadBy<CollateralKey, CBalances>(vaultId);
}

void CVaultView::ForEachVaultCollateral(std::function<bool(const CVaultId&, const CBalances&)> callback)
{
    ForEach<CollateralKey, CVaultId, CBalances>(callback);
}

Res CVaultView::StoreAuction(const CVaultId& vaultId, uint32_t height, const CAuctionData& data)
{
    auto auctionHeight = height + Params().GetConsensus().blocksCollateralAuction();
    WriteBy<AuctionHeightKey>(std::make_pair(auctionHeight, vaultId), data);
    return Res::Ok();
}

Res CVaultView::EraseAuction(const CVaultId& vaultId, uint32_t height)
{
    auto it = LowerBound<AuctionHeightKey>(std::make_pair(height, vaultId));
    for (; it.Valid(); it.Next()) {
        if (it.Key().second == vaultId) {
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

boost::optional<CAuctionBatch> CVaultView::GetAuctionBatch(const CVaultId& vaultId, uint32_t id)
{
    return ReadBy<AuctionBatchKey, CAuctionBatch>(std::make_pair(vaultId, id));
}

void CVaultView::ForEachVaultAuction(std::function<bool(const CVaultId&, const CAuctionData&)> callback, uint32_t height)
{
    ForEach<AuctionHeightKey, std::pair<uint32_t, CVaultId>, CAuctionData>([&](const std::pair<uint32_t, CVaultId>& pair, const CAuctionData& data) {
        if (pair.first != height) {
            return false;
        }
        return callback(pair.second, data);
    }, std::make_pair(height, CVaultId{}));
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

boost::optional<CVaultView::COwnerTokenAmount> CVaultView::GetAuctionBid(const CVaultId& vaultId, uint32_t id)
{
    return ReadBy<AuctionBidKey, COwnerTokenAmount>(std::make_pair(vaultId, id));
}

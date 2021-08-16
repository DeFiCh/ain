
#include <chainparams.h>
#include <masternodes/vault.h>

const unsigned char CVaultView::VaultKey                                  ::prefix = 0x30;
const unsigned char CVaultView::CollateralKey                             ::prefix = 0x31;

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
    WriteBy<CollateralKey>(vaultId, amounts);
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

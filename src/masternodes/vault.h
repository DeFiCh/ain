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

    struct VaultKey { static const unsigned char prefix; };
    struct CollateralKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_VAULT_H

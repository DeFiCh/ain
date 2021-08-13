#ifndef DEFI_MASTERNODES_LOAN_H
#define DEFI_MASTERNODES_LOAN_H

#include <amount.h>
#include <uint256.h>

#include <flushablestorage.h>
#include <masternodes/res.h>
#include <script/script.h>
class CLoanSetCollateralToken
{
public:
    DCT_ID idToken{UINT_MAX};
    CAmount factor;
    uint256 priceFeedTxid;
    uint32_t activateAfterBlock = 0;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(idToken);
        READWRITE(factor);
        READWRITE(priceFeedTxid);
        READWRITE(activateAfterBlock);
    }
};

class CLoanSetCollateralTokenImplementation : public CLoanSetCollateralToken
{
public:
    uint256 creationTx;
    int32_t creationHeight = -1;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanSetCollateralToken, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CLoanSetCollateralTokenMessage : public CLoanSetCollateralToken {
    using CLoanSetCollateralToken::CLoanSetCollateralToken;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanSetCollateralToken, *this);
    }
};

struct CLoanSchemeData
{
    uint32_t ratio;
    CAmount rate;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ratio);
        READWRITE(rate);
    }
};

struct CLoanScheme : public CLoanSchemeData
{
    std::string identifier;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanSchemeData,*this);
        READWRITE(identifier);
    }
};

struct CLoanSchemeMessage : public CLoanScheme
{
    uint64_t update{0};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanScheme,*this);
        READWRITE(update);
    }
};

struct CDefaultLoanSchemeMessage
{
    std::string identifier;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(identifier);
    }
};

struct CDestroyLoanSchemeMessage : public CDefaultLoanSchemeMessage
{
    uint64_t height{0};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CDefaultLoanSchemeMessage, *this);
        READWRITE(height);
    }
};

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

class CLoanView : public virtual CStorageView {
public:
    using CollateralTokenKey = std::pair<DCT_ID, uint32_t>;
    using CLoanSetCollateralTokenImpl = CLoanSetCollateralTokenImplementation;

    std::unique_ptr<CLoanSetCollateralTokenImpl> GetLoanSetCollateralToken(uint256 const & txid) const;
    Res LoanCreateSetCollateralToken(CLoanSetCollateralTokenImpl const & collToken);
    void ForEachLoanSetCollateralToken(std::function<bool (CollateralTokenKey const &, uint256 const &)> callback, CollateralTokenKey const & start = {{0},0});
    std::unique_ptr<CLoanSetCollateralTokenImpl> HasLoanSetCollateralToken(CollateralTokenKey const & key);

    Res StoreLoanScheme(const CLoanSchemeMessage& loanScheme);
    Res StoreDefaultLoanScheme(const std::string& loanSchemeID);
    Res StoreDelayedLoanScheme(const CLoanSchemeMessage& loanScheme);
    Res StoreDelayedDestroyScheme(const CDestroyLoanSchemeMessage& loanScheme);
    Res EraseLoanScheme(const std::string& loanSchemeID);
    void EraseDelayedLoanScheme(const std::string& loanSchemeID, uint64_t height);
    void EraseDelayedDestroyScheme(const std::string& loanSchemeID);
    boost::optional<std::string> GetDefaultLoanScheme();
    boost::optional<CLoanSchemeData> GetLoanScheme(const std::string& loanSchemeID);
    boost::optional<uint64_t> GetDestroyLoanScheme(const std::string& loanSchemeID);
    void ForEachLoanScheme(std::function<bool (const std::string&, const CLoanSchemeData&)> callback);
    void ForEachDelayedLoanScheme(std::function<bool (const std::pair<std::string, uint64_t>&, const CLoanSchemeMessage&)> callback);
    void ForEachDelayedDestroyScheme(std::function<bool (const std::string&, const uint64_t&)> callback);

    struct LoanSetCollateralTokenCreationTx { static const unsigned char prefix; };
    struct LoanSetCollateralTokenKey { static const unsigned char prefix; };
    struct LoanSchemeKey { static const unsigned char prefix; };
    struct DefaultLoanSchemeKey { static const unsigned char prefix; };
    struct DelayedLoanSchemeKey { static const unsigned char prefix; };
    struct DestroyLoanSchemeKey { static const unsigned char prefix; };

};

class CVaultView : public virtual CStorageView
{
public:
    Res StoreVault(const CVaultId&, const CVaultMessage&);
    ResVal<CVaultMessage> GetVault(const CVaultId&) const;
    Res UpdateVault(const CVaultId& vaultId, const CVaultMessage& newVault);
    void ForEachVault(std::function<bool(const CVaultId&, const CVaultMessage&)> callback);
    struct VaultKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_LOAN_H

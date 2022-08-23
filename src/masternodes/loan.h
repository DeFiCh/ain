#ifndef DEFI_MASTERNODES_LOAN_H
#define DEFI_MASTERNODES_LOAN_H

#include <amount.h>
#include <uint256.h>

#include <flushablestorage.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <masternodes/oracles.h>
#include <masternodes/vault.h>
#include <script/script.h>


class CLoanSetCollateralToken
{
public:
    DCT_ID idToken{UINT_MAX};
    CAmount factor;
    CTokenCurrencyPair fixedIntervalPriceId;
    uint32_t activateAfterBlock = 0;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(idToken);
        READWRITE(factor);
        READWRITE(fixedIntervalPriceId);
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

class CLoanSetLoanToken
{
public:
    std::string symbol;
    std::string name;
    CTokenCurrencyPair fixedIntervalPriceId;
    bool mintable = true;
    CAmount interest = 0;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(symbol);
        READWRITE(name);
        READWRITE(fixedIntervalPriceId);
        READWRITE(mintable);
        READWRITE(interest);
    }
};

class CLoanSetLoanTokenImplementation : public CLoanSetLoanToken
{
public:
    uint256 creationTx;
    int32_t creationHeight = -1;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanSetLoanToken, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CLoanSetLoanTokenMessage : public CLoanSetLoanToken {
    using CLoanSetLoanToken::CLoanSetLoanToken;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanSetLoanToken, *this);
    }
};

struct CLoanUpdateLoanTokenMessage : public CLoanSetLoanToken {
    using CLoanSetLoanToken::CLoanSetLoanToken;

    uint256 tokenTx;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanSetLoanToken, *this);
        READWRITE(tokenTx);
    }
};

struct CollateralTokenKey
{
    DCT_ID id;
    uint32_t height;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(id);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(height));
            height = ~height;
        } else {
            uint32_t height_ = ~height;
            READWRITE(WrapBigEndian(height_));
        }
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

struct CLoanSchemeCreation : public CLoanScheme
{
    uint256 schemeCreationTxid;
};

struct CLoanSchemeMessage : public CLoanScheme
{
    uint64_t updateHeight{0};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CLoanScheme,*this);
        READWRITE(updateHeight);
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
    uint64_t destroyHeight{0};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CDefaultLoanSchemeMessage, *this);
        READWRITE(destroyHeight);
    }
};

struct CNegativeInterest
{
    bool negative{};
    base_uint<128> amount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(negative);
        READWRITE(amount);
    }
};

struct CInterestRate
{
    uint32_t height;
    CAmount interestPerBlock;
    CAmount interestToHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(height);
        READWRITE(interestPerBlock);
        READWRITE(interestToHeight);
    }
};

struct CInterestRateV2
{
    uint32_t height;
    base_uint<128> interestPerBlock;
    base_uint<128> interestToHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(height);
        READWRITE(interestPerBlock);
        READWRITE(interestToHeight);
    }
};

struct CInterestRateV3
{
    uint32_t height;
    CNegativeInterest interestPerBlock;
    CNegativeInterest interestToHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(height);
        READWRITE(interestPerBlock);
        READWRITE(interestToHeight);
    }
};

inline CInterestRate ConvertInterestRateToV1(const CInterestRateV2& rate2)
{
    CInterestRate rate1{};
    rate1.height = rate2.height;
    rate1.interestPerBlock = rate2.interestPerBlock.GetLow64();
    rate1.interestToHeight = rate2.interestToHeight.GetLow64();

    return rate1;
}

inline CInterestRate ConvertInterestRateToV1(const CInterestRateV3& rate3)
{
    CInterestRate rate1{};
    rate1.height = rate3.height;
    rate1.interestPerBlock = rate3.interestPerBlock.amount.GetLow64();
    rate1.interestToHeight = rate3.interestToHeight.amount.GetLow64();

    return rate1;
}

inline CInterestRateV2 ConvertInterestRateToV2(const CInterestRate& rate1)
{
    CInterestRateV2 rate2{};
    rate2.height = rate1.height;
    rate2.interestPerBlock = rate1.interestPerBlock;
    rate2.interestToHeight = rate1.interestToHeight;

    return rate2;
}

inline CInterestRateV2 ConvertInterestRateToV2(const CInterestRateV3& rate3)
{
    CInterestRateV2 rate2{};
    rate2.height = rate3.height;
    rate2.interestPerBlock = rate3.interestPerBlock.amount;
    rate2.interestToHeight = rate3.interestToHeight.amount;

    return rate2;
}

inline CInterestRateV3 ConvertInterestRateToV3(const CInterestRate& rate1)
{
    CInterestRateV3 rate3{};
    rate3.height = rate1.height;
    rate3.interestPerBlock.amount = rate1.interestPerBlock;
    rate3.interestToHeight.amount = rate1.interestToHeight;

    return rate3;
}

inline CInterestRateV3 ConvertInterestRateToV3(const CInterestRateV2& rate2)
{
    CInterestRateV3 rate3{};
    rate3.height = rate2.height;
    rate3.interestPerBlock = {false, rate2.interestPerBlock};
    rate3.interestToHeight = {false, rate2.interestToHeight};

    return rate3;
}

inline const auto InterestAddition = [](const CNegativeInterest &a, const CNegativeInterest &b) {
    CNegativeInterest interest;
    if (a.negative == b.negative) {
        interest.amount = a.amount + b.amount;
        interest.negative = b.negative;
    } else {
        if (a.amount > b.amount) {
            interest.amount = a.amount - b.amount;
            interest.negative = a.negative;
        } else {
            interest.amount = b.amount - a.amount;
            interest.negative = !a.negative;
        }
    }
    return interest;
};

static const CAmount HIGH_PRECISION_SCALER = COIN * COIN; // 1,0000,0000,0000,0000

CAmount TotalInterest(const CInterestRateV3& rate, const uint32_t height);
CAmount InterestPerBlock(const CInterestRateV3& rate, uint32_t height);
CNegativeInterest TotalInterestCalculation(const CInterestRateV3& rate, const uint32_t height);
CAmount CeilInterest(const base_uint<128>& value, uint32_t height);

std::string GetInterestPerBlockHighPrecisionString(const CNegativeInterest& value);
std::optional<std::string> TryGetInterestPerBlockHighPrecisionString(const CNegativeInterest& value);

base_uint<128> InterestPerBlockCalculationV2(CAmount amount, CAmount tokenInterest, CAmount schemeInterest);
CNegativeInterest InterestPerBlockCalculationV3(CAmount amount, CAmount tokenInterest, CAmount schemeInterest);

class CLoanTakeLoanMessage
{
public:
    CVaultId vaultId;
    CScript to;
    CBalances amounts;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vaultId);
        READWRITE(to);
        READWRITE(amounts);
    }
};

class CLoanPaybackLoanMessage
{
public:
    CVaultId vaultId;
    CScript from;
    CBalances amounts;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vaultId);
        READWRITE(from);
        READWRITE(amounts);
    }
};

class CLoanPaybackLoanV2Message
{
public:
    CVaultId vaultId;
    CScript from;
    std::map<DCT_ID, CBalances> loans;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vaultId);
        READWRITE(from);
        READWRITE(loans);
    }
};

class CLoanView : public virtual CStorageView {
public:
    using CLoanSetCollateralTokenImpl = CLoanSetCollateralTokenImplementation;
    using CLoanSetLoanTokenImpl = CLoanSetLoanTokenImplementation;

    std::optional<CLoanSetCollateralTokenImpl> GetLoanCollateralToken(uint256 const & txid) const;
    Res CreateLoanCollateralToken(CLoanSetCollateralTokenImpl const & collToken);
    Res EraseLoanCollateralToken(const CLoanSetCollateralTokenImpl& collToken);
    void ForEachLoanCollateralToken(std::function<bool (CollateralTokenKey const &, uint256 const &)> callback, CollateralTokenKey const & start = {DCT_ID{0}, UINT_MAX});
    std::optional<CLoanSetCollateralTokenImpl> HasLoanCollateralToken(CollateralTokenKey const & key);

    std::optional<CLoanSetLoanTokenImpl> GetLoanToken(uint256 const & txid) const;
    [[nodiscard]] virtual std::optional<CLoanSetLoanTokenImpl> GetLoanTokenByID(DCT_ID const & id) const = 0;
    Res SetLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id);
    Res UpdateLoanToken(CLoanSetLoanTokenImpl const & loanToken, DCT_ID const & id);
    Res EraseLoanToken(const DCT_ID& id);
    void ForEachLoanToken(std::function<bool (DCT_ID const &, CLoanSetLoanTokenImpl const &)> callback, DCT_ID const & start = {0});

    Res StoreLoanScheme(const CLoanSchemeMessage& loanScheme);
    Res StoreDefaultLoanScheme(const std::string& loanSchemeID);
    Res StoreDelayedLoanScheme(const CLoanSchemeMessage& loanScheme);
    Res StoreDelayedDestroyScheme(const CDestroyLoanSchemeMessage& loanScheme);
    Res EraseLoanScheme(const std::string& loanSchemeID);
    void EraseDelayedLoanScheme(const std::string& loanSchemeID, uint64_t height);
    void EraseDelayedDestroyScheme(const std::string& loanSchemeID);
    std::optional<std::string> GetDefaultLoanScheme();
    std::optional<CLoanSchemeData> GetLoanScheme(const std::string& loanSchemeID);
    std::optional<uint64_t> GetDestroyLoanScheme(const std::string& loanSchemeID);
    void ForEachLoanScheme(std::function<bool (const std::string&, const CLoanSchemeData&)> callback);
    void ForEachDelayedLoanScheme(std::function<bool (const std::pair<std::string, uint64_t>&, const CLoanSchemeMessage&)> callback);
    void ForEachDelayedDestroyScheme(std::function<bool (const std::string&, const uint64_t&)> callback);

    Res DeleteInterest(const CVaultId& vaultId, uint32_t height);
    void EraseInterestDirect(const CVaultId& vaultId, DCT_ID id, uint32_t height);
    std::optional<CInterestRateV3> GetInterestRate(const CVaultId& vaultId, const DCT_ID id, const uint32_t height);
    void WriteInterestRate(const std::pair<CVaultId, DCT_ID>& pair, const CInterestRateV3& rate, uint32_t height);
    Res StoreInterest(const uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, const DCT_ID id, const CAmount tokenInterest, const CAmount loanIncreased);
    Res EraseInterest(const uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, const DCT_ID id, const CAmount loanDecreased, const CAmount interestDecreased);
    void WipeInterest(const uint32_t height, const CVaultId& vaultId, const std::string& loanSchemeID, const DCT_ID id);
    void ForEachVaultInterest(std::function<bool(const CVaultId&, DCT_ID, CInterestRate)> callback, const CVaultId& vaultId = {}, DCT_ID id = {});
    void ForEachVaultInterestV2(std::function<bool(const CVaultId&, DCT_ID, CInterestRateV2)> callback, const CVaultId& vaultId = {}, DCT_ID id = {});
    void ForEachVaultInterestV3(std::function<bool(const CVaultId&, DCT_ID, CInterestRateV3)> callback, const CVaultId& vaultId = {}, DCT_ID id = {});
    void RevertInterestRateToV1();
    void RevertInterestRateToV2();
    void MigrateInterestRateToV2(CVaultView &view, uint32_t height);
    void MigrateInterestRateToV3(CVaultView &view, uint32_t height);

    Res AddLoanToken(const CVaultId& vaultId, CTokenAmount amount);
    Res SubLoanToken(const CVaultId& vaultId, CTokenAmount amount);
    std::optional<CBalances> GetLoanTokens(const CVaultId& vaultId);
    void ForEachLoanTokenAmount(std::function<bool (const CVaultId&,  const CBalances&)> callback);

    Res SetLoanLiquidationPenalty(CAmount penalty);
    CAmount GetLoanLiquidationPenalty();

    [[nodiscard]] virtual std::optional<CLoanSetLoanTokenImplementation> GetLoanTokenFromAttributes(const DCT_ID& id) const = 0;
    [[nodiscard]] virtual std::optional<CLoanSetCollateralTokenImpl> GetCollateralTokenFromAttributes(const DCT_ID& id) const = 0;

    struct LoanSetCollateralTokenCreationTx { static constexpr uint8_t prefix() { return 0x10; } };
    struct LoanSetCollateralTokenKey        { static constexpr uint8_t prefix() { return 0x11; } };
    struct LoanSetLoanTokenCreationTx       { static constexpr uint8_t prefix() { return 0x12; } };
    struct LoanSetLoanTokenKey              { static constexpr uint8_t prefix() { return 0x13; } };
    struct LoanSchemeKey                    { static constexpr uint8_t prefix() { return 0x14; } };
    struct DefaultLoanSchemeKey             { static constexpr uint8_t prefix() { return 0x15; } };
    struct DelayedLoanSchemeKey             { static constexpr uint8_t prefix() { return 0x16; } };
    struct DestroyLoanSchemeKey             { static constexpr uint8_t prefix() { return 0x17; } };
    struct LoanInterestByVault              { static constexpr uint8_t prefix() { return 0x18; } };
    struct LoanTokenAmount                  { static constexpr uint8_t prefix() { return 0x19; } };
    struct LoanLiquidationPenalty           { static constexpr uint8_t prefix() { return 0x1A; } };
    struct LoanInterestV2ByVault            { static constexpr uint8_t prefix() { return 0x1B; } };
    struct LoanInterestV3ByVault            { static constexpr uint8_t prefix() { return 0x1C; } };
};

#endif // DEFI_MASTERNODES_LOAN_H

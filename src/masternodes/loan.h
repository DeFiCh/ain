#ifndef DEFI_MASTERNODES_LOAN_H
#define DEFI_MASTERNODES_LOAN_H

#include <amount.h>
#include <uint256.h>

#include <flushablestorage.h>
#include <masternodes/res.h>

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

class CLoanView : public virtual CStorageView {
public:
    using CollateralTokenKey = std::pair<DCT_ID, uint32_t>;
    using CLoanSetCollateralTokenImpl = CLoanSetCollateralTokenImplementation;

    std::unique_ptr<CLoanSetCollateralTokenImpl> GetLoanSetCollateralToken(uint256 const & txid) const;
    Res LoanCreateSetCollateralToken(CLoanSetCollateralTokenImpl const & collToken);
    void ForEachLoanSetCollateralToken(std::function<bool (CollateralTokenKey const &, uint256 const &)> callback, CollateralTokenKey const & start = {{0},0});
    std::unique_ptr<CLoanSetCollateralTokenImpl> HasLoanSetCollateralToken(CollateralTokenKey const & key);

    struct LoanSetCollateralTokenCreationTx { static const unsigned char prefix; };
    struct LoanSetCollateralTokenKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_LOAN_H

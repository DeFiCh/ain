#ifndef DEFI_MASTERNODES_LOAN_H
#define DEFI_MASTERNODES_LOAN_H

#include <tuple>

#include <amount.h>
#include <pubkey.h>
#include <uint256.h>
#include <script/script.h>

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
    using CLoanSetCollateralTokenImpl = CLoanSetCollateralTokenImplementation;

    std::unique_ptr<CLoanSetCollateralTokenImpl> GetLoanSetCollateralToken(uint256 const & txid) const;
    Res LoanCreateSetCollateralToken(CLoanSetCollateralTokenImpl const & collToken);
    void ForEachLoanSetCollateralToken(std::function<bool (DCT_ID const &, uint256 const &)> callback, DCT_ID const & token = {0});

    struct LoanSetCollateralTokenCreationTx { static const unsigned char prefix; };
    struct LoanSetCollateralTokenKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_LOAN_H

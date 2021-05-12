#ifndef DEFI_MASTERNODES_ICXORDER_H
#define DEFI_MASTERNODES_ICXORDER_H

#include <tuple>

#include <amount.h>
#include <pubkey.h>
#include <uint256.h>
#include <script/script.h>

#include <flushablestorage.h>
#include <masternodes/res.h>

class CICXOrder
{
public:
    static const uint32_t DEFAULT_EXPIRY; // default period in blocks after order automatically expires
    static const uint8_t TYPE_INTERNAL; // type for DFI/BTC orders
    static const uint8_t TYPE_EXTERNAL; // type for BTC/DFI orders
    static const uint8_t STATUS_OPEN;
    static const uint8_t STATUS_CLOSED; // manually close status
    static const uint8_t STATUS_FILLED; // completely filled status
    static const uint8_t STATUS_EXPIRED; // offer expired status
    static const std::string CHAIN_BTC;
    static const std::string TOKEN_BTC; // name of BTC token on DFC

    uint8_t orderType; //is maker buying or selling DFC asset to know which htlc to come first
    DCT_ID idToken; // used for DFT/BTC
    CScript ownerAddress; //address for DFI token for fees, and in case of DFC/BTC order for DFC asset
    CPubKey receivePubkey; // address of BTC pubkey in case of BTC/DFC order
    CAmount amountFrom; // amount of asset that is sold
    CAmount amountToFill; // how much is left to fill the order
    CAmount orderPrice; // price of asset buying in asset selling
    uint32_t expiry; // when the order exipres in number of blocks

    CICXOrder()
        : orderType(0)
        , idToken({std::numeric_limits<uint32_t>::max()})
        , amountFrom(0)
        , amountToFill(0)
        , orderPrice(0)
        , expiry(DEFAULT_EXPIRY)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderType);
        READWRITE(VARINT(idToken.v));
        READWRITE(ownerAddress);
        READWRITE(receivePubkey);
        READWRITE(amountFrom);
        READWRITE(amountToFill);
        READWRITE(orderPrice);
        READWRITE(expiry);
    }
};

class CICXOrderImplemetation : public CICXOrder
{
public:
    uint256 creationTx;
    uint256 closeTx;
    int32_t creationHeight;
    int32_t closeHeight;

    CICXOrderImplemetation()
        : CICXOrder()
        , creationHeight(-1)
        , closeHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXOrder, *this);
        READWRITE(creationTx);
        READWRITE(closeTx);
        READWRITE(creationHeight);
        READWRITE(closeHeight);
    }
};

struct CICXCreateOrderMessage : public CICXOrder {
    using CICXOrder::CICXOrder;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXOrder, *this);
    }
};

class CICXMakeOffer
{
public:
    static const uint32_t DEFAULT_EXPIRY; // default period in blocks after offer automatically expires
    static const uint32_t MAKER_DEPOSIT_REFUND_TIMEOUT; // minimum period in DFC blocks in which 2nd HTLC must be created, otherwise makerDeposit is refunded to maker
    static const uint8_t STATUS_OPEN;
    static const uint8_t STATUS_CLOSED;
    static const uint8_t STATUS_EXPIRED;
    static const int64_t DEFAULT_TAKER_FEE_PER_BTC;

    uint256 orderTx; // txid for which order is the offer
    CAmount amount; // amount of asset to swap
    CScript ownerAddress; //address for DFI token for fees, and in case of BTC/DFC order for DFC asset
    CPubKey receivePubkey; // address or BTC pubkey in case of DFC/BTC order
    uint32_t expiry; // when the offer exipres in number of blocks
    CAmount takerFee;

    CICXMakeOffer()
        : amount(0)
        , expiry(DEFAULT_EXPIRY)
        , takerFee(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderTx);
        READWRITE(amount);
        READWRITE(ownerAddress);
        READWRITE(receivePubkey);
        READWRITE(expiry);
        READWRITE(takerFee);
    }
};

class CICXMakeOfferImplemetation : public CICXMakeOffer
{
public:
    uint256 creationTx;
    uint256 closeTx;
    int32_t creationHeight;
    int32_t closeHeight;


    CICXMakeOfferImplemetation()
        : CICXMakeOffer()
        , creationHeight(-1)
        , closeHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXMakeOffer, *this);
        READWRITE(creationTx);
        READWRITE(closeTx);
        READWRITE(creationHeight);
        READWRITE(closeHeight);
    }
};

struct CICXMakeOfferMessage : public CICXMakeOffer {
    using CICXMakeOffer::CICXMakeOffer;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXMakeOffer, *this);
    }
};

class CICXSubmitDFCHTLC
{
public:
    static const uint32_t MINIMUM_TIMEOUT; // minimum period in blocks after htlc automatically timeouts and funds are returned to owner when it is first htlc
    static const uint32_t MINIMUM_2ND_TIMEOUT; // minimum period in blocks after htlc automatically timeouts and funds are returned to owner when it is second htlc
    static const uint8_t STATUS_OPEN;
    static const uint8_t STATUS_CLAIMED;
    static const uint8_t STATUS_REFUNDED;
    static const uint8_t STATUS_EXPIRED;

    // This tx is acceptance of the offer, HTLC tx and evidence of HTLC on DFC in the same time. It is a CustomTx on DFC chain
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount; // amount that is put in HTLC
    uint256 hash; // hash for the hash lock part
    uint32_t timeout; // timeout (absolute in blocks) for timelock part

    CICXSubmitDFCHTLC()
        : offerTx(uint256())
        , amount(0)
        , hash()
        , timeout(MINIMUM_TIMEOUT)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(offerTx);
        READWRITE(amount);
        READWRITE(hash);
        READWRITE(timeout);
    }
};

class CICXSubmitDFCHTLCImplemetation : public CICXSubmitDFCHTLC
{
public:
    uint256 creationTx;
    int32_t creationHeight;

    CICXSubmitDFCHTLCImplemetation()
        : CICXSubmitDFCHTLC()
        , creationHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXSubmitDFCHTLC, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CICXSubmitDFCHTLCMessage : public CICXSubmitDFCHTLC {
    using CICXSubmitDFCHTLC::CICXSubmitDFCHTLC;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXSubmitDFCHTLC, *this);
    }
};

class CICXSubmitEXTHTLC
{
public:
    static const uint32_t MINIMUM_TIMEOUT; // default period in blocks after htlc timeouts when it is first htlc
    static const uint32_t MINIMUM_2ND_TIMEOUT; // default period in blocks after htlc timeouts when it is second htlc
    static const uint8_t STATUS_OPEN;
    static const uint8_t STATUS_EXPIRED;


    // This tx is acceptance of the offer and evidence of HTLC on external chain in the same time. It is a CustomTx on DFC chain
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount;
    uint256 hash;
    std::string htlcscriptAddress;
    CPubKey ownerPubkey;
    uint32_t timeout;

    CICXSubmitEXTHTLC()
        : amount(0)
        , timeout(0)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(offerTx);
        READWRITE(amount);
        READWRITE(hash);
        READWRITE(htlcscriptAddress);
        READWRITE(ownerPubkey);
        READWRITE(timeout);
    }
};

class CICXSubmitEXTHTLCImplemetation : public CICXSubmitEXTHTLC
{
public:
    uint256 creationTx;
    int32_t creationHeight;

    CICXSubmitEXTHTLCImplemetation()
        : CICXSubmitEXTHTLC()
        , creationHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXSubmitEXTHTLC, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CICXSubmitEXTHTLCMessage : public CICXSubmitEXTHTLC {
    using CICXSubmitEXTHTLC::CICXSubmitEXTHTLC;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXSubmitEXTHTLC, *this);
    }
};

class CICXClaimDFCHTLC
{
public:
    uint256 dfchtlcTx; // txid of claiming DFC HTLC
    std::vector<unsigned char> seed; // secret for the hash to claim htlc

    CICXClaimDFCHTLC()
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(dfchtlcTx);
        READWRITE(seed);
    }
};

class CICXClaimDFCHTLCImplemetation : public CICXClaimDFCHTLC
{
public:
    uint256 creationTx;
    int32_t creationHeight;

    CICXClaimDFCHTLCImplemetation()
        : CICXClaimDFCHTLC()
        , creationHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXClaimDFCHTLC, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CICXClaimDFCHTLCMessage : public CICXClaimDFCHTLC {
    using CICXClaimDFCHTLC::CICXClaimDFCHTLC;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXClaimDFCHTLC, *this);
    }
};

class CICXCloseOrder
{
public:
    uint256 orderTx; //txid of order which will be closed

    CICXCloseOrder()
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderTx);
    }
};

class CICXCloseOrderImplemetation : public CICXCloseOrder
{
public:
    uint256 creationTx;
    int32_t creationHeight;

    CICXCloseOrderImplemetation()
        : CICXCloseOrder()
        , creationHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXCloseOrder, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CICXCloseOrderMessage : public CICXCloseOrder {
    using CICXCloseOrder::CICXCloseOrder;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXCloseOrder, *this);
    }
};

class CICXCloseOffer
{
public:
    uint256 offerTx; //txid of offer which will be closed

    CICXCloseOffer()
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(offerTx);
    }
};

class CICXCloseOfferImplemetation : public CICXCloseOffer
{
public:
    uint256 creationTx;
    int32_t creationHeight;

    CICXCloseOfferImplemetation()
        : CICXCloseOffer()
        , creationHeight(-1)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXCloseOffer, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

struct CICXCloseOfferMessage : public CICXCloseOffer {
    using CICXCloseOffer::CICXCloseOffer;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXCloseOffer, *this);
    }
};

class CICXOrderView : public virtual CStorageView {
public:
    static const CAmount DEFAULT_DFI_BTC_PRICE;

    typedef std::pair<DCT_ID,uint256> OrderKey;
    typedef std::pair<uint256,uint256> TxidPairKey;
    typedef std::pair<uint32_t,uint256> StatusKey;

    using CICXOrderImpl = CICXOrderImplemetation;
    using CICXMakeOfferImpl = CICXMakeOfferImplemetation;
    using CICXSubmitDFCHTLCImpl = CICXSubmitDFCHTLCImplemetation;
    using CICXSubmitEXTHTLCImpl = CICXSubmitEXTHTLCImplemetation;
    using CICXClaimDFCHTLCImpl = CICXClaimDFCHTLCImplemetation;
    using CICXCloseOrderImpl = CICXCloseOrderImplemetation;
    using CICXCloseOfferImpl = CICXCloseOfferImplemetation;

    //Order
    std::unique_ptr<CICXOrderImpl> GetICXOrderByCreationTx(uint256 const & txid) const;
    Res ICXCreateOrder(CICXOrderImpl const & order);
    Res ICXUpdateOrder(CICXOrderImpl const & order);
    Res ICXCloseOrderTx(CICXOrderImpl const & order, uint8_t const);
    void ForEachICXOrderOpen(std::function<bool (OrderKey const &, uint8_t)> callback, DCT_ID const & pair = {0});
    void ForEachICXOrderClose(std::function<bool (OrderKey const &, uint8_t)> callback, DCT_ID const & pair = {0});
    void ForEachICXOrderExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);

    //MakeOffer
    std::unique_ptr<CICXMakeOfferImpl> GetICXMakeOfferByCreationTx(uint256 const & txid) const;
    ResVal<uint256> ICXMakeOffer(CICXMakeOfferImpl const & makeoffer);
    Res ICXCloseMakeOfferTx(CICXMakeOfferImpl const & order, uint8_t const);
    void ForEachICXMakeOfferOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & ordertxid = uint256());
    void ForEachICXMakeOfferClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & ordertxid = uint256());
    void ForEachICXMakeOfferExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);

    //SubmitDFCHTLC
    std::unique_ptr<CICXSubmitDFCHTLCImpl> GetICXSubmitDFCHTLCByCreationTx(uint256 const & txid) const;
    ResVal<uint256> ICXSubmitDFCHTLC(CICXSubmitDFCHTLCImpl const & dfchtlc);
    Res ICXCloseDFCHTLC(CICXSubmitDFCHTLCImpl const & dfchtlc, uint8_t const);
    void ForEachICXSubmitDFCHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitDFCHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitDFCHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);
    std::unique_ptr<CICXSubmitDFCHTLCImpl> HasICXSubmitDFCHTLCOpen(uint256 const & offertxid);

    //SubmitEXTHTLC
    std::unique_ptr<CICXSubmitEXTHTLCImpl> GetICXSubmitEXTHTLCByCreationTx(uint256 const & txid) const;
    ResVal<uint256> ICXSubmitEXTHTLC(CICXSubmitEXTHTLCImpl const & dfchtlc);
    Res ICXCloseEXTHTLC(CICXSubmitEXTHTLCImpl const & exthtlc, uint8_t const);
    void ForEachICXSubmitEXTHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitEXTHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitEXTHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);
    std::unique_ptr<CICXSubmitEXTHTLCImpl> HasICXSubmitEXTHTLCOpen(uint256 const & offertxid);

    //ClaimDFCHTLC
    std::unique_ptr<CICXClaimDFCHTLCImpl> GetICXClaimDFCHTLCByCreationTx(uint256 const & txid) const;
    ResVal<uint256> ICXClaimDFCHTLC(CICXClaimDFCHTLCImpl const & claimdfchtlc, CICXOrderImpl const & order);
    void ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());

    //CloseOrder
    std::unique_ptr<CICXCloseOrderImpl> GetICXCloseOrderByCreationTx(uint256 const & txid) const;
    ResVal<uint256> ICXCloseOrder(CICXCloseOrderImpl const & closeorder);

    //CloseOrder
    std::unique_ptr<CICXCloseOfferImpl> GetICXCloseOfferByCreationTx(uint256 const & txid) const;
    ResVal<uint256> ICXCloseOffer(CICXCloseOfferImpl const & closeoffer);

    // ICX_TAKERFEE_PER_BTC
    Res ICXSetTakerFeePerBTC(CAmount amount);
    CAmount ICXGetTakerFeePerBTC();

    struct ICXOrderCreationTx { static const unsigned char prefix; };
    struct ICXMakeOfferCreationTx { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCCreationTx { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCCreationTx { static const unsigned char prefix; };
    struct ICXClaimDFCHTLCCreationTx { static const unsigned char prefix; };
    struct ICXCloseOrderCreationTx { static const unsigned char prefix; };
    struct ICXCloseOfferCreationTx { static const unsigned char prefix; };

    struct ICXOrderOpenKey { static const unsigned char prefix; };
    struct ICXOrderCloseKey { static const unsigned char prefix; };
    struct ICXMakeOfferOpenKey { static const unsigned char prefix; };
    struct ICXMakeOfferCloseKey { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCOpenKey { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCCloseKey { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCOpenKey { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCCloseKey { static const unsigned char prefix; };
    struct ICXClaimDFCHTLCKey { static const unsigned char prefix; };

    struct ICXOrderStatus { static const unsigned char prefix; };
    struct ICXOfferStatus { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCStatus { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCStatus { static const unsigned char prefix; };

    struct ICXVariables { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_ICXORDER_H

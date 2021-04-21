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
    static const int DEFAULT_EXPIRY;
    static const int TYPE_INTERNAL;
    static const int TYPE_EXTERNAL;
    static const int STATUS_OPEN;
    static const int STATUS_CLOSED;
    static const int STATUS_EXPIRED;

    //! basic properties
    uint8_t orderType; //is maker buying or selling DFC asset to know which htlc to come first
    DCT_ID idToken; // used for DFT/BTC
    std::string chain; // used for BTC/DFT
    CScript ownerAddress; //address of token asset in case of DFC/BTC order
    CAmount amountFrom; // amount of asset that is selled
    CAmount amountToFill; // how much is left to fill the order
    CAmount orderPrice; 
    uint32_t expiry; // when the order exipreis in number of blocks

    CICXOrder()
        : orderType(0)
        , idToken({std::numeric_limits<uint32_t>::max()})
        , chain()
        , ownerAddress()
        , amountFrom(0)
        , amountToFill(0)
        , orderPrice(0)
        , expiry(DEFAULT_EXPIRY)
    {}
    virtual ~CICXOrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderType);
        READWRITE(VARINT(idToken.v));
        READWRITE(chain);
        READWRITE(ownerAddress);
        READWRITE(amountFrom);
        READWRITE(amountToFill);
        READWRITE(orderPrice);
        READWRITE(expiry);
    }
};

class CICXOrderImplemetation : public CICXOrder
{
public:
    //! tx related properties
    uint256 creationTx;
    uint256 closeTx;
    int32_t creationHeight; 
    int32_t closeHeight;
    int32_t expireHeight;

    CICXOrderImplemetation()
        : CICXOrder()
        , creationTx()
        , closeTx()
        , creationHeight(-1)
        , closeHeight(-1)
        , expireHeight(-1)
    {}
    ~CICXOrderImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXOrder, *this);
        READWRITE(creationTx);
        READWRITE(closeTx);
        READWRITE(creationHeight);
        READWRITE(closeHeight);
        READWRITE(expireHeight);
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
    static const int STATUS_OPEN;

    //! basic properties
    uint256 orderTx; // txid for which order is the offer
    CAmount amount; // amount of asset to swap
    std::vector<uint8_t> receiveDestination; // address or pubkey of receiving asset

    CICXMakeOffer()
        : orderTx(uint256())
        , amount(0)
        , receiveDestination()
    {}
    virtual ~CICXMakeOffer() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderTx);
        READWRITE(amount);
        READWRITE(receiveDestination);
    }
};

class CICXMakeOfferImplemetation : public CICXMakeOffer
{
public:
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 

    CICXMakeOfferImplemetation()
        : CICXMakeOffer()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CICXMakeOfferImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXMakeOffer, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
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
    static const int DEFAULT_TIMEOUT;
    static const int STATUS_OPEN;
    static const int STATUS_CLAIMED;
    static const int STATUS_REFUNDED;

    // This tx is acceptance of the offer, HTLC tx and evidence of HTLC on DFC in the same time. It is a CustomTx on DFC chain
    //! basic properties
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount; // amount that is put in HTLC
    std::vector<uint8_t> receiveDestination; // address or pubkey of receiving asset
    uint256 hash; // hash for the hash lock part
    uint32_t timeout; // timeout (absolute in blocks) for timelock part

    CICXSubmitDFCHTLC()
        : offerTx(uint256())
        , amount(0)
        , receiveDestination()
        , hash()
        , timeout(DEFAULT_TIMEOUT)
    {}
    virtual ~CICXSubmitDFCHTLC() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(offerTx);
        READWRITE(amount);
        READWRITE(receiveDestination);
        READWRITE(hash);
        READWRITE(timeout);
    }
};

class CICXSubmitDFCHTLCImplemetation : public CICXSubmitDFCHTLC
{
public:
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 
    int32_t expireHeight;

    CICXSubmitDFCHTLCImplemetation()
        : CICXSubmitDFCHTLC()
        , creationTx()
        , creationHeight(-1)
        , expireHeight(-1)
    {}
    ~CICXSubmitDFCHTLCImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXSubmitDFCHTLC, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
        READWRITE(expireHeight);
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
    static const int STATUS_OPEN;
    static const int STATUS_EXPIRED;

    // This tx is acceptance of the offer and evidence of HTLC on external chain in the same time. It is a CustomTx on DFC chain
    //! basic properties
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount;
    uint256 hash; 
    std::string htlcscriptAddress;
    CPubKey ownerPubkey;
    uint32_t timeout;

    CICXSubmitEXTHTLC()
        : offerTx(uint256())
        , amount(0)
        , hash()
        , htlcscriptAddress()
        , ownerPubkey()
        , timeout(0)
    {}
    virtual ~CICXSubmitEXTHTLC() = default;

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
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 

    CICXSubmitEXTHTLCImplemetation()
        : CICXSubmitEXTHTLC()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CICXSubmitEXTHTLCImplemetation() override = default;

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
    // This tx is acceptance of the offer, HTLC tx and evidence of HTLC on DFC in the same time. It is a CustomTx on DFC chain
    //! basic properties
    uint256 dfchtlcTx; // txid of claiming DFC HTLC
    std::vector<unsigned char> seed; // secret for the hash to claim htlc

    CICXClaimDFCHTLC()
        : dfchtlcTx(uint256())
        , seed()
    {}
    virtual ~CICXClaimDFCHTLC() = default;

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
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 

    CICXClaimDFCHTLCImplemetation()
        : CICXClaimDFCHTLC()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CICXClaimDFCHTLCImplemetation() override = default;

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
    //! basic properties
    uint256 orderTx; //txid of order which will be closed

    CICXCloseOrder()
        : orderTx(uint256())
    {}
    virtual ~CICXCloseOrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderTx);
    }
};

class CICXCloseOrderImplemetation : public CICXCloseOrder
{
public:
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 

    CICXCloseOrderImplemetation()
        : CICXCloseOrder()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CICXCloseOrderImplemetation() override = default;

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

class CICXOrderView : public virtual CStorageView {
public:
    typedef std::pair<DCT_ID,std::string> AssetPair;
    typedef std::pair<uint8_t,AssetPair> StatusAsset;
    typedef std::pair<StatusAsset,uint256> OrderKey;

    typedef std::pair<uint256,uint256> TxidPairKey;

    typedef std::pair<uint32_t,uint256> StatusTxid;
    typedef std::pair<StatusTxid,uint256> StatusTxidKey;

    typedef std::pair<uint32_t,uint256> StatusKey;

    using CICXOrderImpl = CICXOrderImplemetation;
    using CICXMakeOfferImpl = CICXMakeOfferImplemetation;
    using CICXSubmitDFCHTLCImpl = CICXSubmitDFCHTLCImplemetation;
    using CICXSubmitEXTHTLCImpl = CICXSubmitEXTHTLCImplemetation;
    using CICXClaimDFCHTLCImpl = CICXClaimDFCHTLCImplemetation;
    using CICXCloseOrderImpl = CICXCloseOrderImplemetation;

    //Order
    std::unique_ptr<CICXOrderImpl> GetICXOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXCreateOrder(const CICXOrderImpl& order);
    ResVal<uint256> ICXCloseOrderTx(const CICXOrderImpl& order, const uint8_t);
    void ForEachICXOrder(std::function<bool (OrderKey const &, uint8_t)> callback, StatusAsset const & pair=StatusAsset());    
    void ForEachICXOrderExpired(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height=0);    
    
    //MakeOffer
    std::unique_ptr<CICXMakeOfferImpl> GetICXMakeOfferByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXMakeOffer(const CICXMakeOfferImpl& makeoffer, const CICXOrderImpl & order);
    void ForEachICXMakeOffer(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & ordertxid=uint256());

    //SubmitDFCHTLC
    std::unique_ptr<CICXSubmitDFCHTLCImpl> GetICXSubmitDFCHTLCByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXSubmitDFCHTLC(const CICXSubmitDFCHTLCImpl& dfchtlc);
    Res ICXRefundDFCHTLC(CICXSubmitDFCHTLCImpl& dfchtlc);
    void ForEachICXSubmitDFCHTLC(std::function<bool (StatusTxidKey const &, uint8_t)> callback, StatusTxid const & offertxid=StatusTxid());
    void ForEachICXSubmitDFCHTLCExpired(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height=0);

    //SubmitEXTHTLC
    std::unique_ptr<CICXSubmitEXTHTLCImpl> GetICXSubmitEXTHTLCByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXSubmitEXTHTLC(const CICXSubmitEXTHTLCImpl& dfchtlc);
    void ForEachICXSubmitEXTHTLC(std::function<bool (StatusTxidKey const &, uint8_t)> callback, StatusTxid const & offertxid=StatusTxid());
    
    //ClaimDFCHTLC
    std::unique_ptr<CICXClaimDFCHTLCImpl> GetICXClaimDFCHTLCByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXClaimDFCHTLC(const CICXClaimDFCHTLCImpl& claimdfchtlc, const CICXSubmitDFCHTLCImpl& dfchtlc);
    void ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, CLazySerialize<CICXClaimDFCHTLCImpl>)> callback, uint256 const & ordertxid=uint256());

    //CloseOrder
    std::unique_ptr<CICXCloseOrderImpl> GetICXCloseOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXCloseOrder(const CICXCloseOrderImpl& closeorder);

    struct ICXOrderCreationTx { static const unsigned char prefix; };
    struct ICXOrderKey { static const unsigned char prefix; };
    struct ICXOrderStatus { static const unsigned char prefix; };
    struct ICXMakeOfferCreationTx { static const unsigned char prefix; };
    struct ICXMakeOfferKey { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCCreationTx { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCKey { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCStatus { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCCreationTx { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCKey { static const unsigned char prefix; };
    struct ICXClaimDFCHTLCCreationTx { static const unsigned char prefix; };
    struct ICXClaimDFCHTLCKey { static const unsigned char prefix; };
    struct ICXCloseOrderCreationTx { static const unsigned char prefix; };
    struct ICXCloseOrderKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_ICXORDER_H

#ifndef DEFI_MASTERNODES_ICXORDER_H
#define DEFI_MASTERNODES_ICXORDER_H

#include <amount.h>
#include <pubkey.h>
#include <uint256.h>

#include <flushablestorage.h>
#include <masternodes/res.h>

class CICXOrder
{
public:
    static const int DEFAULT_ICXORDER_EXPIRY = 2880;
    static const int ICXORDER_TYPE_INTERNAL = 1;
    static const int ICXORDER_TYPE_EXTERNAL = 0;

    //! basic properties
    std::string ownerAddress; //address of account asset
    DCT_ID idTokenFrom; // used for DFT/BTC
    DCT_ID idTokenTo; // used fot BTC/DFT
    std::string chainFrom; // used for BTC/DFT
    std::string chainTo; // used for DFT/BTC
    uint8_t orderType; //is maker buying or selling DFC asset to know which htlc to come first
    CAmount amountFrom; // amount of asset that is selled
    CAmount amountToFill; // how much is left to fill the order
    CAmount orderPrice; 
    uint32_t expiry; // when the order exipreis in number of blocks

    CICXOrder()
        : ownerAddress("")
        , idTokenFrom({std::numeric_limits<uint32_t>::max()})
        , idTokenTo({std::numeric_limits<uint32_t>::max()})
        , chainFrom("")
        , chainTo("")
        , orderType(ICXORDER_TYPE_INTERNAL)
        , amountFrom(0)
        , amountToFill(0)
        , orderPrice(0)
        , expiry(DEFAULT_ICXORDER_EXPIRY)
    {}
    virtual ~CICXOrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ownerAddress);
        READWRITE(VARINT(idTokenFrom.v));
        READWRITE(VARINT(idTokenTo.v));
        READWRITE(chainFrom);
        READWRITE(chainTo);
        READWRITE(orderType);
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

    CICXOrderImplemetation()
        : CICXOrder()
        , creationTx()
        , closeTx()
        , creationHeight(-1)
        , closeHeight(-1)
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
    }
};

class CICXMakeOffer
{
public:
    //! basic properties
    uint256 orderTx; // txid for which order is the offer
    CAmount amount; // amount of asset to swap
    std::string receiveAddress; //address of account asset in case of BTC/DFI
    std::string receivePubKey; // pubkey for BTC htlc  in case of DFI/BTC

    CICXMakeOffer()
        : orderTx(uint256())
        , amount(0)
        , receiveAddress("")
        , receivePubKey("")
    {}
    virtual ~CICXMakeOffer() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderTx);
        READWRITE(amount);
        READWRITE(receiveAddress);
        READWRITE(receivePubKey);
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

class CICXSubmitDFCHTLC
{
public:
    static const int ICXSUBMITDFCHTLC_MAX_TIMEOUT = 1440;

    // This tx is acceptance of the offer, HTLC tx and evidence of HTLC on DFC in the same time. It is a CustomTx on DFC chain
    //! basic properties
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount; // amount that is put in HTLC
    std::string receivePubKey; // pubkey for BTC htlc  in case of DFI/BTC
    std::string receiveAddress; //address of account asset in case of BTC/DFI
    uint256 hash; // hash for the hash lock part
    uint32_t timeout; // timeout (absolute in blocks) for timelock part

    CICXSubmitDFCHTLC()
        : offerTx(uint256())
        , amount(0)
        , receivePubKey("")
        , receiveAddress("")
        , hash()
        , timeout(0)
    {}
    virtual ~CICXSubmitDFCHTLC() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(offerTx);
        READWRITE(amount);
        READWRITE(receivePubKey);
        READWRITE(receiveAddress);
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

    CICXSubmitDFCHTLCImplemetation()
        : CICXSubmitDFCHTLC()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CICXSubmitDFCHTLCImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CICXSubmitDFCHTLC, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

class CICXSubmitEXTHTLC
{
public:
    static const int ICXSUBMITEXTHTLC_MAX_TIMEOUT = 144;

    // This tx is acceptance of the offer and evidence of HTLC on external chain in the same time. It is a CustomTx on DFC chain
    //! basic properties
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount;
    uint256 hash; 
    std::string htlcscriptAddress;
    std::string ownerPubkey;
    uint32_t timeout;
    

    CICXSubmitEXTHTLC()
        : offerTx(uint256())
        , amount(0)
        , hash()
        , htlcscriptAddress("")
        , ownerPubkey("")
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

class CICXClaimDFCHTLC
{
public:
    // This tx is acceptance of the offer, HTLC tx and evidence of HTLC on DFC in the same time. It is a CustomTx on DFC chain
    //! basic properties
    uint256 dfchtlcTx; // txid for which offer is this HTLC
    CAmount amount; // amount that is put in HTLC
    uint256 seed; // secret for the hash to claim htlc

    CICXClaimDFCHTLC()
        : dfchtlcTx(uint256())
        , amount(0)
        , seed()
    {}
    virtual ~CICXClaimDFCHTLC() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(dfchtlcTx);
        READWRITE(amount);
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

class CICXOrderView : public virtual CStorageView {
public:
    typedef std::pair<DCT_ID,std::string> AssetPair;
    typedef std::pair<AssetPair,uint256> OrderKey;
    typedef std::pair<uint256,uint256> TxidPairKey;


    using CICXOrderImpl = CICXOrderImplemetation;
    using CICXMakeOfferImpl = CICXMakeOfferImplemetation;
    using CICXSubmitDFCHTLCImpl = CICXSubmitDFCHTLCImplemetation;
    using CICXSubmitEXTHTLCImpl = CICXSubmitEXTHTLCImplemetation;
    using CICXClaimDFCHTLCImpl = CICXClaimDFCHTLCImplemetation;
    using CICXCloseOrderImpl = CICXCloseOrderImplemetation;

    //Order
    std::unique_ptr<CICXOrderImpl> GetICXOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXCreateOrder(const CICXOrderImpl& order);
    ResVal<uint256> ICXCloseOrderTx(const CICXOrderImpl& order);
    void ForEachICXOrder(std::function<bool (OrderKey const &, CLazySerialize<CICXOrderImpl>)> callback, AssetPair const & pair=AssetPair());    
    
    //MakeOffer
    std::unique_ptr<CICXMakeOfferImpl> GetICXMakeOfferByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXMakeOffer(const CICXMakeOfferImpl& makeoffer, const CICXOrderImpl & order);
    void ForEachICXMakeOffer(std::function<bool (TxidPairKey const &, CLazySerialize<CICXMakeOfferImpl>)> callback, uint256 const & ordertxid=uint256());

    //SubmitDFCHTLC
    std::unique_ptr<CICXSubmitDFCHTLCImpl> GetICXSubmitDFCHTLCByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXSubmitDFCHTLC(const CICXSubmitDFCHTLCImpl& dfchtlc);
    void ForEachICXSubmitDFCHTLC(std::function<bool (TxidPairKey const &, CLazySerialize<CICXSubmitDFCHTLCImpl>)> callback, uint256 const & ordertxid=uint256());

    //SubmitEXTHTLC
    std::unique_ptr<CICXSubmitEXTHTLCImpl> GetICXSubmitEXTHTLCByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXSubmitEXTHTLC(const CICXSubmitEXTHTLCImpl& dfchtlc);
    void ForEachICXSubmitEXTHTLC(std::function<bool (TxidPairKey const &, CLazySerialize<CICXSubmitEXTHTLCImpl>)> callback, uint256 const & ordertxid=uint256());

    //ClaimDFCHTLC
    std::unique_ptr<CICXClaimDFCHTLCImpl> GetICXClaimDFCHTLCByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXClaimDFCHTLC(const CICXClaimDFCHTLCImpl& dfchtlc);
    void ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, CLazySerialize<CICXClaimDFCHTLCImpl>)> callback, uint256 const & ordertxid=uint256());

    //CloseOrder
    std::unique_ptr<CICXCloseOrderImpl> GetICXCloseOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> ICXCloseOrder(const CICXCloseOrderImpl& closeorder, const CICXOrderImpl& order);
    void ForEachICXClosedOrder(std::function<bool (OrderKey const &, CLazySerialize<CICXOrderImpl>)> callback, AssetPair const & pair=AssetPair());

    struct ICXOrderCreationTx { static const unsigned char prefix; };
    struct ICXOrderKey { static const unsigned char prefix; };
    struct ICXMakeOfferCreationTx { static const unsigned char prefix; };
    struct ICXMakeOfferKey { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCCreationTx { static const unsigned char prefix; };
    struct ICXSubmitDFCHTLCKey { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCCreationTx { static const unsigned char prefix; };
    struct ICXSubmitEXTHTLCKey { static const unsigned char prefix; };
    struct ICXClaimDFCHTLCCreationTx { static const unsigned char prefix; };
    struct ICXClaimDFCHTLCKey { static const unsigned char prefix; };
    struct ICXCloseOrderCreationTx { static const unsigned char prefix; };
    struct ICXCloseOrderKey { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_ICXORDER_H

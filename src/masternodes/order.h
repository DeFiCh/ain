#ifndef DEFI_MASTERNODES_ORDER_H
#define DEFI_MASTERNODES_ORDER_H

#include <amount.h>
#include <uint256.h>

#include <flushablestorage.h>
#include <masternodes/res.h>

class COrder
{
public:
    static const int DEFAULT_ORDER_EXPIRY = 2880;
    static const int DEFAULT_OPTION_DFI = 800000000;

    //! basic properties
    std::string ownerAddress;
    DCT_ID idTokenFrom;
    DCT_ID idTokenTo;
    CAmount amountFrom;
    CAmount amountToFill;
    CAmount orderPrice;
    uint32_t expiry;
    CAmount optionDFI;

    COrder()
        : ownerAddress("")
        , idTokenFrom({0})
        , idTokenTo({0})
        , amountFrom(0)
        , amountToFill(0)
        , orderPrice(0)
        , expiry(DEFAULT_ORDER_EXPIRY)
        , optionDFI(DEFAULT_OPTION_DFI)
    {}
    virtual ~COrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ownerAddress);
        READWRITE(VARINT(idTokenFrom.v));
        READWRITE(VARINT(idTokenTo.v));
        READWRITE(amountFrom);
        READWRITE(amountToFill);
        READWRITE(orderPrice);
        READWRITE(expiry);
        READWRITE(optionDFI);
    }
};

class COrderImplemetation : public COrder
{
public:
    //! tx related properties
    uint256 creationTx;
    uint256 closeTx;
    int32_t creationHeight; 
    int32_t closeHeight;

    COrderImplemetation()
        : COrder()
        , creationTx()
        , closeTx()
        , creationHeight(-1)
        , closeHeight(-1)
    {}
    ~COrderImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(COrder, *this);
        READWRITE(creationTx);
        READWRITE(closeTx);
        READWRITE(creationHeight);
        READWRITE(closeHeight);
    }
};

class CFulfillOrder
{
public:
    //! basic properties
    std::string ownerAddress;
    uint256 orderTx;
    CAmount amount;

    CFulfillOrder()
        : ownerAddress("")
        , orderTx(uint256())
        , amount(0)
    {}
    virtual ~CFulfillOrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(ownerAddress);
        READWRITE(orderTx);
        READWRITE(amount);
    }
};

class CFulfillOrderImplemetation : public CFulfillOrder
{
public:
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 

    CFulfillOrderImplemetation()
        : CFulfillOrder()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CFulfillOrderImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CFulfillOrder, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

class CCloseOrder
{
public:
    //! basic properties
    uint256 orderTx;

    CCloseOrder()
        : orderTx(uint256())
    {}
    virtual ~CCloseOrder() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderTx);
    }
};

class CCloseOrderImplemetation : public CCloseOrder
{
public:
    //! tx related properties
    uint256 creationTx;
    int32_t creationHeight; 

    CCloseOrderImplemetation()
        : CCloseOrder()
        , creationTx()
        , creationHeight(-1)
    {}
    ~CCloseOrderImplemetation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CCloseOrder, *this);
        READWRITE(creationTx);
        READWRITE(creationHeight);
    }
};

class COrderView : public virtual CStorageView {
public:
    typedef std::pair<DCT_ID,DCT_ID> TokenPair;
    typedef std::pair<TokenPair,uint256> TokenPairKey;
    typedef std::pair<uint256,uint256> FulfillOrderId;

    using COrderImpl = COrderImplemetation;
    using CFulfillOrderImpl = CFulfillOrderImplemetation;
    using CCloseOrderImpl = CCloseOrderImplemetation;

    std::unique_ptr<COrderImpl> GetOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> CreateOrder(const COrderImpl& order);
    ResVal<uint256> CloseOrderTx(const COrderImpl& order);
    void ForEachOrder(std::function<bool (TokenPairKey const &, CLazySerialize<COrderImpl>)> callback, TokenPair const & pair=TokenPair());    
    
    std::unique_ptr<CFulfillOrderImpl> GetFulfillOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> FulfillOrder(const CFulfillOrderImpl& fillorder, const COrderImpl & order);
    void ForEachFulfillOrder(std::function<bool (FulfillOrderId const &, CLazySerialize<CFulfillOrderImpl>)> callback, uint256 const & ordertxid=uint256());

    std::unique_ptr<CCloseOrderImpl> GetCloseOrderByCreationTx(const uint256 & txid) const;
    ResVal<uint256> CloseOrder(const CCloseOrderImpl& closeorder);
    void ForEachClosedOrder(std::function<bool (TokenPairKey const &, CLazySerialize<COrderImpl>)> callback, TokenPair const & pair=TokenPair());


    struct OrderCreationTx { static const unsigned char prefix; };
    struct OrderCreationTxId { static const unsigned char prefix; };
    struct FulfillCreationTx { static const unsigned char prefix; };
    struct FulfillOrderTxid { static const unsigned char prefix; };
    struct CloseCreationTx { static const unsigned char prefix; };
    struct OrderCloseTx { static const unsigned char prefix; };
};

#endif // DEFI_MASTERNODES_ORDER_H

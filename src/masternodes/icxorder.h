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
    static constexpr uint32_t DEFAULT_EXPIRY = 2880;
    static constexpr uint8_t TYPE_INTERNAL = 1; // type for DFI/BTC orders
    static constexpr uint8_t TYPE_EXTERNAL = 2; // type for BTC/DFI orders
    static constexpr uint8_t STATUS_OPEN = 0;
    static constexpr uint8_t STATUS_CLOSED = 1; // manually close status
    static constexpr uint8_t STATUS_FILLED = 2; // completely filled status
    static constexpr uint8_t STATUS_EXPIRED = 3; // offer expired status
    inline static const std::string CHAIN_BTC = "BTC";
    inline static const std::string TOKEN_BTC = "BTC";

    uint8_t orderType = 0; //is maker buying or selling DFC asset to know which htlc to come first
    DCT_ID idToken{UINT_MAX}; // used for DFT/BTC
    CScript ownerAddress; //address for DFI token for fees, and in case of DFC/BTC order for DFC asset
    CPubKey receivePubkey; // address of BTC pubkey in case of BTC/DFC order
    CAmount amountFrom = 0; // amount of asset that is sold
    CAmount amountToFill = 0; // how much is left to fill the order
    CAmount orderPrice = 0; // price of asset buying in asset selling
    uint32_t expiry = DEFAULT_EXPIRY; // when the order exipres in number of blocks

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(orderType);
        READWRITE(idToken);
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
    int32_t creationHeight = -1;
    int32_t closeHeight = -1;

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
    static constexpr uint32_t DEFAULT_EXPIRY = 10; // default period in blocks after offer automatically expires
    static constexpr uint32_t EUNOSPAYA_DEFAULT_EXPIRY = 20;
    static constexpr uint32_t MAKER_DEPOSIT_REFUND_TIMEOUT = 100; // minimum period in DFC blocks in which 2nd HTLC must be created, otherwise makerDeposit is refunded to maker
    static constexpr uint8_t STATUS_OPEN = 0;
    static constexpr uint8_t STATUS_CLOSED = 1;
    static constexpr uint8_t STATUS_EXPIRED = 2;
    static constexpr int64_t DEFAULT_TAKER_FEE_PER_BTC = 3 * COIN / 1000;

    uint256 orderTx; // txid for which order is the offer
    CAmount amount = 0; // amount of asset to swap
    CScript ownerAddress; //address for DFI token for fees, and in case of BTC/DFC order for DFC asset
    CPubKey receivePubkey; // address or BTC pubkey in case of DFC/BTC order
    uint32_t expiry = 0; // when the offer exipres in number of blocks
    CAmount takerFee = 0;

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
    int32_t creationHeight = -1;
    int32_t closeHeight = -1;

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
    static constexpr uint32_t MINIMUM_TIMEOUT = 500; // minimum period in blocks after htlc automatically timeouts and funds are returned to owner when it is first htlc
    static constexpr uint32_t EUNOSPAYA_MINIMUM_TIMEOUT = 1440;
    static constexpr uint32_t MINIMUM_2ND_TIMEOUT = 250; // minimum period in blocks after htlc automatically timeouts and funds are returned to owner when it is second htlc
    static constexpr uint32_t EUNOSPAYA_MINIMUM_2ND_TIMEOUT = 480;
    static constexpr uint8_t STATUS_OPEN = 0;
    static constexpr uint8_t STATUS_CLAIMED = 1;
    static constexpr uint8_t STATUS_REFUNDED = 2;
    static constexpr uint8_t STATUS_EXPIRED = 3;

    // This tx is acceptance of the offer, HTLC tx and evidence of HTLC on DFC in the same time. It is a CustomTx on DFC chain
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount = 0; // amount that is put in HTLC
    uint256 hash; // hash for the hash lock part
    uint32_t timeout = 0; // timeout (absolute in blocks) for timelock part

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
    int32_t creationHeight = -1;

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
    static constexpr uint32_t MINIMUM_TIMEOUT = 30; // default period in blocks after htlc timeouts when it is first htlc
    static constexpr uint32_t EUNOSPAYA_MINIMUM_TIMEOUT = 72;
    static constexpr uint32_t MINIMUM_2ND_TIMEOUT = 15; // default period in blocks after htlc timeouts when it is second htlc
    static constexpr uint32_t EUNOSPAYA_MINIMUM_2ND_TIMEOUT = 24;
    static constexpr uint32_t BTC_BLOCKS_IN_DFI_BLOCKS = 16; // number of BTC blocks in DFI blocks period
    static constexpr uint32_t EUNOSPAYA_BTC_BLOCKS_IN_DFI_BLOCKS = 20; // number of BTC blocks in DFI blocks period
    static constexpr uint8_t STATUS_OPEN = 0;
    static constexpr uint8_t STATUS_CLOSED = 1;
    static constexpr uint8_t STATUS_EXPIRED = 3;

    // This tx is acceptance of the offer and evidence of HTLC on external chain in the same time. It is a CustomTx on DFC chain
    uint256 offerTx; // txid for which offer is this HTLC
    CAmount amount = 0;
    uint256 hash;
    std::string htlcscriptAddress;
    CPubKey ownerPubkey;
    uint32_t timeout = 0;

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
    int32_t creationHeight = -1;

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
    int32_t creationHeight = -1;

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
    int32_t creationHeight = -1;

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
    int32_t creationHeight = -1;

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
    static constexpr CAmount DEFAULT_DFI_BTC_PRICE = 15000;

    using OrderKey = std::pair<DCT_ID, uint256>;
    using TxidPairKey = std::pair<uint256, uint256>;
    using StatusKey = std::pair<uint32_t, uint256>;

    using CICXOrderImpl = CICXOrderImplemetation;
    using CICXMakeOfferImpl = CICXMakeOfferImplemetation;
    using CICXSubmitDFCHTLCImpl = CICXSubmitDFCHTLCImplemetation;
    using CICXSubmitEXTHTLCImpl = CICXSubmitEXTHTLCImplemetation;
    using CICXClaimDFCHTLCImpl = CICXClaimDFCHTLCImplemetation;
    using CICXCloseOrderImpl = CICXCloseOrderImplemetation;
    using CICXCloseOfferImpl = CICXCloseOfferImplemetation;

    //Order
    std::optional<CICXOrderImpl> GetICXOrderByCreationTx(uint256 const & txid) const;
    uint8_t GetICXOrderStatus(OrderKey const & key) const;
    Res ICXCreateOrder(CICXOrderImpl const & order);
    Res ICXUpdateOrder(CICXOrderImpl const & order);
    Res ICXCloseOrderTx(CICXOrderImpl const & order, uint8_t const);
    void ForEachICXOrderOpen(std::function<bool (OrderKey const &, uint8_t)> callback, DCT_ID const & pair = {0});
    void ForEachICXOrderClose(std::function<bool (OrderKey const &, uint8_t)> callback, DCT_ID const & pair = {0});
    void ForEachICXOrderExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);
    std::optional<CICXOrderImpl> HasICXOrderOpen(DCT_ID const & tokenId, uint256 const & ordertxid);

    //MakeOffer
    std::optional<CICXMakeOfferImpl> GetICXMakeOfferByCreationTx(uint256 const & txid) const;
    uint8_t GetICXMakeOfferStatus(TxidPairKey const & key) const;
    Res ICXMakeOffer(CICXMakeOfferImpl const & makeoffer);
    Res ICXUpdateMakeOffer(CICXMakeOfferImpl const & makeoffer);
    Res ICXCloseMakeOfferTx(CICXMakeOfferImpl const & order, uint8_t const);
    void ForEachICXMakeOfferOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & ordertxid = uint256());
    void ForEachICXMakeOfferClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & ordertxid = uint256());
    void ForEachICXMakeOfferExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);
    std::optional<CICXMakeOfferImpl> HasICXMakeOfferOpen(uint256 const & ordertxid, uint256 const & offertxid);

    //SubmitDFCHTLC
    std::optional<CICXSubmitDFCHTLCImpl> GetICXSubmitDFCHTLCByCreationTx(uint256 const & txid) const;
    Res ICXSubmitDFCHTLC(CICXSubmitDFCHTLCImpl const & dfchtlc);
    Res ICXCloseDFCHTLC(CICXSubmitDFCHTLCImpl const & dfchtlc, uint8_t const);
    void ForEachICXSubmitDFCHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitDFCHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitDFCHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);
    std::optional<CICXSubmitDFCHTLCImpl> HasICXSubmitDFCHTLCOpen(uint256 const & offertxid);
    bool ExistedICXSubmitDFCHTLC(uint256 const & offertxid, bool isPreEunosPaya);


    //SubmitEXTHTLC
    std::optional<CICXSubmitEXTHTLCImpl> GetICXSubmitEXTHTLCByCreationTx(uint256 const & txid) const;
    Res ICXSubmitEXTHTLC(CICXSubmitEXTHTLCImpl const & dfchtlc);
    Res ICXCloseEXTHTLC(CICXSubmitEXTHTLCImpl const & exthtlc, uint8_t const);
    void ForEachICXSubmitEXTHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitEXTHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());
    void ForEachICXSubmitEXTHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height = 0);
    std::optional<CICXSubmitEXTHTLCImpl> HasICXSubmitEXTHTLCOpen(uint256 const & offertxid);
    bool ExistedICXSubmitEXTHTLC(uint256 const & offertxid, bool isPreEunosPaya);

    //ClaimDFCHTLC
    std::optional<CICXClaimDFCHTLCImpl> GetICXClaimDFCHTLCByCreationTx(uint256 const & txid) const;
    Res ICXClaimDFCHTLC(CICXClaimDFCHTLCImpl const & claimdfchtlc, uint256 const & offertxid, CICXOrderImpl const & order);
    void ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid = uint256());

    //CloseOrder
    std::optional<CICXCloseOrderImpl> GetICXCloseOrderByCreationTx(uint256 const & txid) const;
    Res ICXCloseOrder(CICXCloseOrderImpl const & closeorder);

    //CloseOrder
    std::optional<CICXCloseOfferImpl> GetICXCloseOfferByCreationTx(uint256 const & txid) const;
    Res ICXCloseOffer(CICXCloseOfferImpl const & closeoffer);

    // ICX_TAKERFEE_PER_BTC
    Res ICXSetTakerFeePerBTC(CAmount amount);
    CAmount ICXGetTakerFeePerBTC();

    struct ICXOrderCreationTx         { static constexpr uint8_t prefix() { return '1'; } };
    struct ICXMakeOfferCreationTx     { static constexpr uint8_t prefix() { return '2'; } };
    struct ICXSubmitDFCHTLCCreationTx { static constexpr uint8_t prefix() { return '3'; } };
    struct ICXSubmitEXTHTLCCreationTx { static constexpr uint8_t prefix() { return '4'; } };
    struct ICXClaimDFCHTLCCreationTx  { static constexpr uint8_t prefix() { return '5'; } };
    struct ICXCloseOrderCreationTx    { static constexpr uint8_t prefix() { return '6'; } };
    struct ICXCloseOfferCreationTx    { static constexpr uint8_t prefix() { return '7'; } };

    struct ICXOrderOpenKey            { static constexpr uint8_t prefix() { return 0x01; } };
    struct ICXOrderCloseKey           { static constexpr uint8_t prefix() { return 0x02; } };
    struct ICXMakeOfferOpenKey        { static constexpr uint8_t prefix() { return 0x03; } };
    struct ICXMakeOfferCloseKey       { static constexpr uint8_t prefix() { return 0x04; } };
    struct ICXSubmitDFCHTLCOpenKey    { static constexpr uint8_t prefix() { return 0x05; } };
    struct ICXSubmitDFCHTLCCloseKey   { static constexpr uint8_t prefix() { return 0x06; } };
    struct ICXSubmitEXTHTLCOpenKey    { static constexpr uint8_t prefix() { return 0x07; } };
    struct ICXSubmitEXTHTLCCloseKey   { static constexpr uint8_t prefix() { return 0x08; } };
    struct ICXClaimDFCHTLCKey         { static constexpr uint8_t prefix() { return 0x09; } };

    struct ICXOrderStatus             { static constexpr uint8_t prefix() { return 0x0A; } };
    struct ICXOfferStatus             { static constexpr uint8_t prefix() { return 0x0B; } };
    struct ICXSubmitDFCHTLCStatus     { static constexpr uint8_t prefix() { return 0x0C; } };
    struct ICXSubmitEXTHTLCStatus     { static constexpr uint8_t prefix() { return 0x0D; } };

    struct ICXVariables               { static constexpr uint8_t prefix() { return 0x0F; } };
};

#endif // DEFI_MASTERNODES_ICXORDER_H

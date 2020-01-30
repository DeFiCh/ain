//
//  BRPaymentProtocol.h
//
//  Created by Aaron Voisine on 9/7/15.
//  Copyright (c) 2015 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef BRPaymentProtocol_h
#define BRPaymentProtocol_h

#include "BRTransaction.h"
#include "BRAddress.h"
#include "BRKey.h"
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// BIP70 payment protocol: https://github.com/b_itcoin/bips/blob/master/bip-0070.mediawiki
// BIP75 payment protocol encryption: https://github.com/b_itcoin/bips/blob/master/bip-0075.mediawiki

typedef struct {
    char *network; // main / test / regtest, default is "main"
    BRTxOutput *outputs; // where to send payments, outputs[n].amount defaults to 0
    size_t outCount;
    uint64_t time; // request creation time, seconds since unix epoch, optional
    uint64_t expires; // when this request should be considered invalid, optional
    char *memo; // human-readable description of request for the customer, optional
    char *paymentURL; // url to send payment and get payment ack, optional
    uint8_t *merchantData; // arbitrary data to include in the payment message, optional
    size_t merchDataLen;
} BRPaymentProtocolDetails;

// returns a newly allocated details struct that must be freed by calling BRPaymentProtocolDetailsFree()
BRPaymentProtocolDetails *BRPaymentProtocolDetailsNew(const char *network, const BRTxOutput outputs[], size_t outCount,
                                                      uint64_t time, uint64_t expires, const char *memo,
                                                      const char *paymentURL, const uint8_t *merchantData,
                                                      size_t merchDataLen);

// buf must contain a serialized details struct
// returns a details struct that must be freed by calling BRPaymentProtocolDetailsFree()
BRPaymentProtocolDetails *BRPaymentProtocolDetailsParse(const uint8_t *buf, size_t bufLen);

// writes serialized details struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolDetailsSerialize(const BRPaymentProtocolDetails *details, uint8_t *buf, size_t bufLen);

// frees memory allocated for details struct
void BRPaymentProtocolDetailsFree(BRPaymentProtocolDetails *details);

typedef struct {
    uint32_t version; // default is 1
    char *pkiType; // none / x509+sha256 / x509+sha1, default is "none"
    uint8_t *pkiData; // depends on pkiType, optional
    size_t pkiDataLen;
    BRPaymentProtocolDetails *details; // required
    uint8_t *signature; // pki-dependent signature, optional
    size_t sigLen;
} BRPaymentProtocolRequest;

// returns a newly allocated request struct that must be freed by calling BRPaymentProtocolRequestFree()
BRPaymentProtocolRequest *BRPaymentProtocolRequestNew(uint32_t version, const char *pkiType, const uint8_t *pkiData,
                                                      size_t pkiDataLen, BRPaymentProtocolDetails *details,
                                                      const uint8_t *signature, size_t sigLen);

// buf must contain a serialized request struct
// returns a request struct that must be freed by calling BRPaymentProtocolRequestFree()
BRPaymentProtocolRequest *BRPaymentProtocolRequestParse(const uint8_t *buf, size_t bufLen);

// writes serialized request struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolRequestSerialize(const BRPaymentProtocolRequest *req, uint8_t *buf, size_t bufLen);

// writes the DER encoded certificate corresponding to index to cert
// returns the number of bytes written to cert, or the total certLen needed if cert is NULL
// returns 0 if index is out-of-bounds
size_t BRPaymentProtocolRequestCert(const BRPaymentProtocolRequest *req, uint8_t *cert, size_t certLen, size_t idx);

// writes the hash of the request to md needed to sign or verify the request
// returns the number of bytes written, or the total mdLen needed if md is NULL
size_t BRPaymentProtocolRequestDigest(BRPaymentProtocolRequest *req, uint8_t *md, size_t mdLen);

// frees memory allocated for request struct
void BRPaymentProtocolRequestFree(BRPaymentProtocolRequest *req);

typedef struct {
    uint8_t *merchantData; // from request->details->merchantData, optional
    size_t merchDataLen;
    BRTransaction **transactions; // array of signed BRTransaction struct references to satisfy outputs from details
    size_t txCount;
    BRTxOutput *refundTo; // where to send refunds, if a refund is necessary, refundTo[n].amount defaults to 0
    size_t refundToCount;
    char *memo; // human-readable message for the merchant, optional
} BRPaymentProtocolPayment;

// returns a newly allocated payment struct that must be freed by calling BRPaymentProtocolPaymentFree()
BRPaymentProtocolPayment *BRPaymentProtocolPaymentNew(const uint8_t *merchantData, size_t merchDataLen,
                                                      BRTransaction *transactions[], size_t txCount,
                                                      const uint64_t refundToAmounts[],
                                                      const BRAddress refundToAddresses[], size_t refundToCount,
                                                      const char *memo);

// buf must contain a serialized payment struct
// returns a payment struct that must be freed by calling BRPaymentProtocolPaymentFree()
BRPaymentProtocolPayment *BRPaymentProtocolPaymentParse(const uint8_t *buf, size_t bufLen);

// writes serialized payment struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolPaymentSerialize(const BRPaymentProtocolPayment *payment, uint8_t *buf, size_t bufLen);

// frees memory allocated for payment struct (does not call BRTransactionFree() on transactions)
void BRPaymentProtocolPaymentFree(BRPaymentProtocolPayment *payment);

typedef struct {
    BRPaymentProtocolPayment *payment; // payment message that triggered this ack, required
    char *memo; // human-readable message for customer, optional
} BRPaymentProtocolACK;

// returns a newly allocated ACK struct that must be freed by calling BRPaymentProtocolACKFree()
BRPaymentProtocolACK *BRPaymentProtocolACKNew(BRPaymentProtocolPayment *payment, const char *memo);

// buf must contain a serialized ACK struct
// returns an ACK struct that must be freed by calling BRPaymentProtocolACKFree()
BRPaymentProtocolACK *BRPaymentProtocolACKParse(const uint8_t *buf, size_t bufLen);

// writes serialized ACK struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolACKSerialize(const BRPaymentProtocolACK *ack, uint8_t *buf, size_t bufLen);

// frees memory allocated for ACK struct
void BRPaymentProtocolACKFree(BRPaymentProtocolACK *ack);

typedef struct {
    BRKey senderPubKey; // sender's public key, required
    uint64_t amount; // amount is integer-number-of-satoshis, defaults to 0
    char *pkiType; // none / x509+sha256, default is "none"
    uint8_t *pkiData; // depends on pkiType, optional
    size_t pkiDataLen;
    char *memo; // human-readable description of invoice request for the receiver, optional
    char *notifyUrl; // URL to notify on encrypted payment request ready, optional
    uint8_t *signature; // pki-dependent signature, optional
    size_t sigLen;
} BRPaymentProtocolInvoiceRequest;

// returns a newly allocated invoice request struct that must be freed by calling BRPaymentProtocolInvoiceRequestFree()
BRPaymentProtocolInvoiceRequest *BRPaymentProtocolInvoiceRequestNew(BRKey *senderPubKey, uint64_t amount,
                                                                    const char *pkiType, uint8_t *pkiData,
                                                                    size_t pkiDataLen, const char *memo,
                                                                    const char *notifyUrl, const uint8_t *signature,
                                                                    size_t sigLen);
    
// buf must contain a serialized invoice request
// returns an invoice request struct that must be freed by calling BRPaymentProtocolInvoiceRequestFree()
BRPaymentProtocolInvoiceRequest *BRPaymentProtocolInvoiceRequestParse(const uint8_t *buf, size_t bufLen);
    
// writes serialized invoice request to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolInvoiceRequestSerialize(BRPaymentProtocolInvoiceRequest *req, uint8_t *buf, size_t bufLen);
    
// writes the DER encoded certificate corresponding to index to cert
// returns the number of bytes written to cert, or the total certLen needed if cert is NULL
// returns 0 if index is out-of-bounds
size_t BRPaymentProtocolInvoiceRequestCert(const BRPaymentProtocolInvoiceRequest *req, uint8_t *cert, size_t certLen,
                                           size_t idx);
    
// writes the hash of the request to md needed to sign or verify the request
// returns the number of bytes written, or the total mdLen needed if md is NULL
size_t BRPaymentProtocolInvoiceRequestDigest(BRPaymentProtocolInvoiceRequest *req, uint8_t *md, size_t mdLen);

// frees memory allocated for invoice request struct
void BRPaymentProtocolInvoiceRequestFree(BRPaymentProtocolInvoiceRequest *req);

typedef enum {
    BRPaymentProtocolMessageTypeUnknown = 0,
    BRPaymentProtocolMessageTypeInvoiceRequest = 1,
    BRPaymentProtocolMessageTypeRequest = 2,
    BRPaymentProtocolMessageTypePayment = 3,
    BRPaymentProtocolMessageTypeACK = 4
} BRPaymentProtocolMessageType;

typedef struct {
    BRPaymentProtocolMessageType msgType; // message type of message, required
    uint8_t *message; // serialized payment protocol message, required
    size_t msgLen;
    uint64_t statusCode; // payment protocol status code, optional
    char *statusMsg; // human-readable payment protocol status message, optional
    uint8_t *identifier; // unique key to identify entire exchange, optional (should use sha256 of invoice request)
    size_t identLen;
} BRPaymentProtocolMessage;

// returns a newly allocated message struct that must be freed by calling BRPaymentProtocolMessageFree()
BRPaymentProtocolMessage *BRPaymentProtocolMessageNew(BRPaymentProtocolMessageType msgType, const uint8_t *message,
                                                      size_t msgLen, uint64_t statusCode, const char *statusMsg,
                                                      const uint8_t *identifier, size_t identLen);
    
// buf must contain a serialized message
// returns an message struct that must be freed by calling BRPaymentProtocolMessageFree()
BRPaymentProtocolMessage *BRPaymentProtocolMessageParse(const uint8_t *buf, size_t bufLen);
    
// writes serialized message struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolMessageSerialize(const BRPaymentProtocolMessage *msg, uint8_t *buf, size_t bufLen);
    
// frees memory allocated for message struct
void BRPaymentProtocolMessageFree(BRPaymentProtocolMessage *msg);
    
typedef struct {
    BRPaymentProtocolMessageType msgType; // message type of decrypted message, required
    uint8_t *message; // encrypted payment protocol message, required
    size_t msgLen;
    BRKey receiverPubKey; // receiver's public key, required
    BRKey senderPubKey; // sender's public key, required
    uint64_t nonce; // microseconds since epoch, required
    uint8_t *signature; // signature over the full encrypted message with sender/receiver ec key respectively, optional
    size_t sigLen;
    uint8_t *identifier; // unique key to identify entire exchange, optional (should use sha256 of invoice request)
    size_t identLen;
    uint64_t statusCode; // payment protocol status code, optional
    char *statusMsg; // human-readable payment protocol status message, optional
} BRPaymentProtocolEncryptedMessage;

// returns a newly allocated encrypted message struct that must be freed with BRPaymentProtocolEncryptedMessageFree()
// message is the un-encrypted serialized payment protocol message
// one of either receiverKey or senderKey must contain a private key, and the other must contain only a public key
BRPaymentProtocolEncryptedMessage *BRPaymentProtocolEncryptedMessageNew(BRPaymentProtocolMessageType msgType,
                                                                        const uint8_t *message, size_t msgLen,
                                                                        BRKey *receiverKey, BRKey *senderKey,
                                                                        uint64_t nonce,
                                                                        const uint8_t *identifier, size_t identLen,
                                                                        uint64_t statusCode, const char *statusMsg);
    
// buf must contain a serialized encrytped message
// returns an encrypted message struct that must be freed by calling BRPaymentProtocolEncryptedMessageFree()
BRPaymentProtocolEncryptedMessage *BRPaymentProtocolEncryptedMessageParse(const uint8_t *buf, size_t bufLen);
    
// writes serialized encrypted message to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolEncryptedMessageSerialize(BRPaymentProtocolEncryptedMessage *msg, uint8_t *buf, size_t bufLen);

int BRPaymentProtocolEncryptedMessageVerify(BRPaymentProtocolEncryptedMessage *msg, BRKey *pubKey);

size_t BRPaymentProtocolEncryptedMessageDecrypt(BRPaymentProtocolEncryptedMessage *msg, uint8_t *out, size_t outLen,
                                                BRKey *privKey);

// frees memory allocated for encrypted message struct
void BRPaymentProtocolEncryptedMessageFree(BRPaymentProtocolEncryptedMessage *msg);

#ifdef __cplusplus
}
#endif

#endif // BRPaymentProtocol_h

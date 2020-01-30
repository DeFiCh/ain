//
//  BRPaymentProtocol.c
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

#include "BRPaymentProtocol.h"
#include "BRCrypto.h"
#include "BRArray.h"
#include <string.h>
#include <inttypes.h>
#include <stdio.h>

// BIP70 payment protocol: https://github.com/b_itcoin/bips/blob/master/bip-0070.mediawiki
// BIP75 payment protocol encryption: https://github.com/b_itcoin/bips/blob/master/bip-0075.mediawiki

#define PROTOBUF_VARINT   0 // int32, int64, uint32, uint64, sint32, sint64, bool, enum
#define PROTOBUF_64BIT    1 // fixed64, sfixed64, double
#define PROTOBUF_LENDELIM 2 // string, bytes, embedded messages, packed repeated fields
#define PROTOBUF_32BIT    5 // fixed32, sfixed32, float

typedef struct {
    uint8_t *defaults;
    uint8_t *unknown;
} ProtoBufContext;

static uint64_t _ProtoBufVarInt(const uint8_t *buf, size_t bufLen, size_t *off)
{
    uint64_t varInt = 0;
    uint8_t b = 0x80;
    size_t i = 0;
    
    while ((b & 0x80) && buf && *off < bufLen) {
        b = buf[(*off)++];
        varInt += (uint64_t)(b & 0x7f) << 7*i++;
    }
    
    return (b & 0x80) ? 0 : varInt;
}

static void _ProtoBufSetVarInt(uint8_t *buf, size_t bufLen, uint64_t i, size_t *off)
{
    uint8_t b;
    
    do {
        b = i & 0x7f;
        i >>= 7;
        if (i > 0) b |= 0x80;
        if (buf && *off + 1 <= bufLen) buf[*off] = b;
        (*off)++;
    } while (i > 0);
}

static const uint8_t *_ProtoBufLenDelim(const uint8_t *buf, size_t *len, size_t *off)
{
    const uint8_t *data = NULL;
    size_t dataLen = (size_t)_ProtoBufVarInt(buf, *len, off);
    
    if (buf && *off + dataLen <= *len) data = &buf[*off];
    *off += dataLen;
    *len = dataLen;
    return data;
}

static void _ProtoBufSetLenDelim(uint8_t *buf, size_t bufLen, const void *data, size_t dataLen, size_t *off)
{
    if (data || dataLen == 0) {
        _ProtoBufSetVarInt(buf, bufLen, dataLen, off);
        if (buf && *off + dataLen <= bufLen) memcpy(&buf[*off], data, dataLen);
        *off += dataLen;
    }
}

// the following fixed int functions are not used by payment protocol, and only work for parsing/serializing unknown
// fields - the values returned or set are unconverted raw byte values
static uint64_t _ProtoBufFixed(const uint8_t *buf, size_t bufLen, size_t *off, size_t size)
{
    uint64_t i = 0;
    
    if (buf && *off + size <= bufLen && size <= sizeof(i)) memcpy(&i, &buf[*off], size);
    *off += size;
    return i;
}

static void _ProtoBufSetFixed(uint8_t *buf, size_t bufLen, uint64_t i, size_t *off, size_t size)
{
    if (buf && *off + size <= bufLen && size <= sizeof(i)) memcpy(&buf[*off], &i, size);
    *off += size;
}

// sets either i or data depending on field type, and returns field key
static uint64_t _ProtoBufField(uint64_t *i, const uint8_t **data, const uint8_t *buf, size_t *len, size_t *off)
{
    uint64_t varInt = 0, fixedInt = 0, key = _ProtoBufVarInt(buf, *len, off);
    const uint8_t *lenDelim = NULL;
    
    switch (key & 0x07) {
        case PROTOBUF_VARINT: varInt = _ProtoBufVarInt(buf, *len, off); if (i) *i = varInt; break;
        case PROTOBUF_64BIT: fixedInt = _ProtoBufFixed(buf, *len, off, sizeof(uint64_t)); if (i) *i = fixedInt; break;
        case PROTOBUF_LENDELIM: lenDelim = _ProtoBufLenDelim(buf, len, off); if (data) *data = lenDelim; break;
        case PROTOBUF_32BIT: fixedInt = _ProtoBufFixed(buf, *len, off, sizeof(uint32_t)); if (i) *i = fixedInt; break;
        default: break;
    }
    
    return key;
}

static void _ProtoBufString(char **str, const void *data, size_t dataLen)
{
    if (data || dataLen == 0) {
        if (! *str) array_new(*str, dataLen + 1);
        array_clear(*str);
        array_add_array(*str, (const char *)data, dataLen);
        array_add(*str, '\0');
    }
}

static void _ProtoBufSetString(uint8_t *buf, size_t bufLen, const char *str, uint64_t key, size_t *off)
{
    size_t strLen = (str) ? strlen(str) : 0;
    
    _ProtoBufSetVarInt(buf, bufLen, (key << 3) | PROTOBUF_LENDELIM, off);
    _ProtoBufSetLenDelim(buf, bufLen, str, strLen, off);
}

static size_t _ProtoBufBytes(uint8_t **bytes, const void *data, size_t dataLen)
{
    if (data || dataLen == 0) {
        if (! *bytes) array_new(*bytes, dataLen);
        array_clear(*bytes);
        array_add_array(*bytes, (const uint8_t *)data, dataLen);
    }
    
    return (*bytes) ? array_count(*bytes) : 0;
}

static void _ProtoBufSetBytes(uint8_t *buf, size_t bufLen, const uint8_t *bytes, size_t bytesLen, uint64_t key,
                              size_t *off)
{
    _ProtoBufSetVarInt(buf, bufLen, (key << 3) | PROTOBUF_LENDELIM, off);
    _ProtoBufSetLenDelim(buf, bufLen, bytes, bytesLen, off);
}

static void _ProtoBufSetInt(uint8_t *buf, size_t bufLen, uint64_t i, uint64_t key, size_t *off)
{
    _ProtoBufSetVarInt(buf, bufLen, (key << 3) | PROTOBUF_VARINT, off);
    _ProtoBufSetVarInt(buf, bufLen, i, off);
}

static void _ProtoBufUnknown(uint8_t **unknown, uint64_t key, uint64_t i, const void *data, size_t dataLen)
{
    size_t bufLen = 10 + ((key & 0x07) == PROTOBUF_LENDELIM ? dataLen : 0);
    uint8_t _buf[(bufLen <= 0x1000) ? bufLen : 0], *buf = (bufLen <= 0x1000) ? _buf : malloc(bufLen);
    size_t off = 0, o = 0, l;
    uint64_t k;
    
    assert(buf != NULL);
    _ProtoBufSetVarInt(buf, bufLen, key, &off);
    
    switch (key & 0x07) {
        case PROTOBUF_VARINT: _ProtoBufSetVarInt(buf, bufLen, i, &off); break;
        case PROTOBUF_64BIT: _ProtoBufSetFixed(buf, bufLen, i, &off, sizeof(uint64_t)); break;
        case PROTOBUF_LENDELIM: _ProtoBufSetLenDelim(buf, bufLen, data, dataLen, &off); break;
        case PROTOBUF_32BIT: _ProtoBufSetFixed(buf, bufLen, i, &off, sizeof(uint32_t)); break;
        default: break;
    }
    
    if (off < bufLen) bufLen = off;
    if (! *unknown) array_new(*unknown, bufLen);
    off = 0;
    
    while (off < array_count(*unknown)) {
        l = array_count(*unknown);
        o = off;
        k = _ProtoBufField(NULL, NULL, *unknown, &l, &off);
        if (k == key) array_rm_range(*unknown, o, off - o);
        if (k >= key) break;
    }
    
    array_insert_array(*unknown, o, buf, bufLen);
    if (buf != _buf) free(buf);
}

typedef enum {
    output_amount = 1,
    output_script = 2
} output_key;

typedef enum {
    details_network = 1,
    details_outputs = 2,
    details_time = 3,
    details_expires = 4,
    details_memo = 5,
    details_payment_url = 6,
    details_merch_data = 7
} details_key;

typedef enum {
    request_version = 1,
    request_pki_type = 2,
    request_pki_data = 3,
    request_details = 4,
    request_signature = 5
} request_key;

typedef enum {
    certificates_cert = 1
} certificates_key;

typedef enum {
    payment_merch_data = 1,
    payment_transactions = 2,
    payment_refund_to = 3,
    payment_memo = 4
} payment_key;

typedef enum {
    ack_payment = 1,
    ack_memo = 2
} ack_key;

typedef enum {
    invoice_req_sender_pk = 1,
    invoice_req_amount = 2,
    invoice_req_pki_type = 3,
    invoice_req_pki_data = 4,
    invoice_req_memo = 5,
    invoice_req_notify_url = 6,
    invoice_req_signature = 7
} invoice_req_key;

typedef enum {
    message_msg_type = 1,
    message_message = 2,
    message_status_code = 3,
    message_status_msg = 4,
    message_identifier = 5
} message_key;

typedef enum {
    encrypted_msg_msg_type = 1,
    encrypted_msg_message = 2,
    encrypted_msg_receiver_pk = 3,
    encrypted_msg_sender_pk = 4,
    encrypted_msg_nonce = 5,
    encrypted_msg_signature = 6,
    encrypted_msg_identifier = 7,
    encrypted_msg_status_code = 8,
    encrypted_msg_status_msg = 9
} encrypted_msg_key;

static BRTxOutput _BRPaymentProtocolOutput(uint64_t amount, uint8_t *script, size_t scriptLen)
{
    BRTxOutput out = BR_TX_OUTPUT_NONE;
    ProtoBufContext ctx = { NULL, NULL };
    
    assert(script != NULL || scriptLen == 0);
    
    array_new(ctx.defaults, output_script + 1);
    array_set_count(ctx.defaults, output_script + 1);
    out.amount = amount;
    BRTxOutputSetScript(&out, script, scriptLen);
    if (! out.script) array_new(out.script, sizeof(ctx));
    array_add_array(out.script, (uint8_t *)&ctx, sizeof(ctx)); // store context at end of script data
    return out;
}

static BRTxOutput _BRPaymentProtocolOutputParse(const uint8_t *buf, size_t bufLen)
{
    BRTxOutput out = BR_TX_OUTPUT_NONE;
    ProtoBufContext ctx = { NULL, NULL };
    size_t off = 0;

    array_new(ctx.defaults, output_script + 1);
    array_set_count(ctx.defaults, output_script + 1);
    out.amount = 0;
    ctx.defaults[output_amount] = 1;
    
    while (off < bufLen) {
        const uint8_t *data = NULL;
        size_t dataLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dataLen, &off);
        
        switch (key >> 3) {
            case output_amount: out.amount = i, ctx.defaults[output_amount] = 0; break;
            case output_script: BRTxOutputSetScript(&out, data, dataLen); break;
            default: _ProtoBufUnknown(&ctx.unknown, key, i, data, dataLen); break;
        }
    }

    if (! out.script) { // required
        out = BR_TX_OUTPUT_NONE;
        if (ctx.defaults) array_free(ctx.defaults);
        if (ctx.unknown) array_free(ctx.unknown);
    }
    else array_add_array(out.script, (uint8_t *)&ctx, sizeof(ctx)); // store context at end of script data

    return out;
}

static size_t _BRPaymentProtocolOutputSerialize(BRTxOutput out, uint8_t *buf, size_t bufLen)
{
    ProtoBufContext ctx;
    size_t off = 0;
    
    assert(out.script != NULL);
    
    memcpy(&ctx, &out.script[out.scriptLen], sizeof(ctx)); // context is stored at end of script data
    if (! ctx.defaults[output_amount]) _ProtoBufSetInt(buf, bufLen, out.amount, output_amount, &off);
    if (! ctx.defaults[output_script]) _ProtoBufSetBytes(buf, bufLen, out.script, out.scriptLen, output_script, &off);
    
    if (ctx.unknown) {
        if (buf && off + array_count(ctx.unknown) <= bufLen) memcpy(&buf[off], ctx.unknown, array_count(ctx.unknown));
        off += array_count(ctx.unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

static void _BRPaymentProtocolOutputFree(BRTxOutput out)
{
    ProtoBufContext ctx;

    if (out.script) {
        memcpy(&ctx, &out.script[out.scriptLen], sizeof(ctx));
        if (ctx.defaults) array_free(ctx.defaults);
        if (ctx.unknown) array_free(ctx.unknown);
        BRTxOutputSetScript(&out, NULL, 0);
    }
}

// returns a newly allocated details struct that must be freed by calling BRPaymentProtocolDetailsFree()
BRPaymentProtocolDetails *BRPaymentProtocolDetailsNew(const char *network, const BRTxOutput outputs[], size_t outCount,
                                                      uint64_t time, uint64_t expires, const char *memo,
                                                      const char *paymentURL, const uint8_t *merchantData,
                                                      size_t merchDataLen)
{
    BRPaymentProtocolDetails *details = calloc(1, sizeof(*details) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&details[1];
    
    assert(details != NULL);
    assert(outputs != NULL || outCount == 0);
    
    array_new(ctx->defaults, details_merch_data + 1);
    array_set_count(ctx->defaults, details_merch_data + 1);

    if (! network) {
        _ProtoBufString(&details->network, "main", strlen("main"));
        ctx->defaults[details_network] = 1;
    }
    else _ProtoBufString(&details->network, network, strlen(network));

    array_new(details->outputs, outCount);
    
    for (size_t i = 0; i < outCount; i++) {
        array_add(details->outputs, _BRPaymentProtocolOutput(outputs[i].amount, outputs[i].script, outputs[i].scriptLen));
    }
    
    details->time = time;
    details->expires = expires;
    if (memo) _ProtoBufString(&details->memo, memo, strlen(memo));
    if (paymentURL) _ProtoBufString(&details->paymentURL, paymentURL, strlen(paymentURL));
    if (merchantData) details->merchDataLen = _ProtoBufBytes(&details->merchantData, merchantData, merchDataLen);
    return details;
}

// buf must contain a serialized details struct
// returns a details struct that must be freed by calling BRPaymentProtocolDetailsFree()
BRPaymentProtocolDetails *BRPaymentProtocolDetailsParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolDetails *details = calloc(1, sizeof(*details) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&details[1];
    size_t off = 0;

    assert(details != NULL);
    assert(buf != NULL || bufLen == 0);

    array_new(ctx->defaults, details_merch_data + 1);
    array_set_count(ctx->defaults, details_merch_data + 1);
    ctx->defaults[details_time] = 1;
    ctx->defaults[details_expires] = 1;
    array_new(details->outputs, 1);

    while (buf && off < bufLen) {
        BRTxOutput out = BR_TX_OUTPUT_NONE;
        const uint8_t *data = NULL;
        size_t dLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dLen, &off);

        switch (key >> 3) {
            case details_network: _ProtoBufString(&details->network, data, dLen); break;
            case details_outputs: out = _BRPaymentProtocolOutputParse(data, dLen); break;
            case details_time: details->time = i, ctx->defaults[details_time] = 0; break;
            case details_expires: details->expires = i, ctx->defaults[details_expires] = 0; break;
            case details_memo: _ProtoBufString(&details->memo, data, dLen); break;
            case details_payment_url: _ProtoBufString(&details->paymentURL, data, dLen); break;
            case details_merch_data: details->merchDataLen = _ProtoBufBytes(&details->merchantData, data, dLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dLen); break;
        }

        if (out.script) array_add(details->outputs, out);
    }
    
    details->outCount = array_count(details->outputs);
    
    if (! details->network) {
        _ProtoBufString(&details->network, "main", strlen("main"));
        ctx->defaults[details_network] = 1;
    }
    
    return details;
}

// writes serialized details struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolDetailsSerialize(const BRPaymentProtocolDetails *details, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&details[1];
    size_t i, off = 0, outLen = 0x100, l;
    uint8_t *outBuf = malloc(outLen);

    assert(outBuf != NULL);
    assert(details != NULL);
    
    if (! ctx->defaults[details_network]) _ProtoBufSetString(buf, bufLen, details->network, details_network, &off);
    
    for (i = 0; i < details->outCount; i++) {
        l = _BRPaymentProtocolOutputSerialize(details->outputs[i], NULL, 0);
        if (l > outLen) outBuf = realloc(outBuf, (outLen = l));
        assert(outBuf != NULL);
        l = _BRPaymentProtocolOutputSerialize(details->outputs[i], outBuf, outLen);
        _ProtoBufSetBytes(buf, bufLen, outBuf, l, details_outputs, &off);
    }

    free(outBuf);
    if (! ctx->defaults[details_time]) _ProtoBufSetInt(buf, bufLen, details->time, details_time, &off);
    if (! ctx->defaults[details_expires]) _ProtoBufSetInt(buf, bufLen, details->expires, details_expires, &off);
    if (details->memo) _ProtoBufSetString(buf, bufLen, details->memo, details_memo, &off);
    if (details->paymentURL) _ProtoBufSetString(buf, bufLen, details->paymentURL, details_payment_url, &off);
    if (details->merchantData) _ProtoBufSetBytes(buf, bufLen, details->merchantData, details->merchDataLen,
                                                 details_merch_data, &off);

    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

// frees memory allocated for details struct
void BRPaymentProtocolDetailsFree(BRPaymentProtocolDetails *details)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&details[1];

    assert(details != NULL);
    
    if (details->network) array_free(details->network);
    for (size_t i = 0; i < details->outCount; i++) _BRPaymentProtocolOutputFree(details->outputs[i]);
    if (details->outputs) array_free(details->outputs);
    if (details->memo) array_free(details->memo);
    if (details->paymentURL) array_free(details->paymentURL);
    if (details->merchantData) array_free(details->merchantData);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
    free(details);
}

// returns a newly allocated request struct that must be freed by calling BRPaymentProtocolRequestFree()
BRPaymentProtocolRequest *BRPaymentProtocolRequestNew(uint32_t version, const char *pkiType, const uint8_t *pkiData,
                                                      size_t pkiDataLen, BRPaymentProtocolDetails *details,
                                                      const uint8_t *signature, size_t sigLen)
{
    BRPaymentProtocolRequest *req = calloc(1, sizeof(*req) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&req[1];

    assert(req != NULL);
    assert(details != NULL);
    
    array_new(ctx->defaults, request_signature + 1);
    array_set_count(ctx->defaults, request_signature + 1);

    if (! version) {
        req->version = 1;
        ctx->defaults[request_version] = 1;
    }
    else req->version = version;
    
    if (! pkiType) {
        _ProtoBufString(&req->pkiType, "none", strlen("none"));
        ctx->defaults[request_pki_type] = 1;
    }
    else _ProtoBufString(&req->pkiType, pkiType, strlen(pkiType));
    
    if (pkiData) req->pkiDataLen = _ProtoBufBytes(&req->pkiData, pkiData, pkiDataLen);
    req->details = details;
    if (signature) req->sigLen = _ProtoBufBytes(&req->signature, signature, sigLen);

    if (! req->details) { // required
        BRPaymentProtocolRequestFree(req);
        req = NULL;
    }
    
    return req;
}

// buf must contain a serialized request struct
// returns a request struct that must be freed by calling BRPaymentProtocolRequestFree()
BRPaymentProtocolRequest *BRPaymentProtocolRequestParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolRequest *req = calloc(1, sizeof(*req) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&req[1];
    size_t off = 0;
    
    assert(req != NULL);
    assert(buf != NULL || bufLen == 0);

    array_new(ctx->defaults, request_signature + 1);
    array_set_count(ctx->defaults, request_signature + 1);
    req->version = 1;
    ctx->defaults[request_version] = 1;
    
    while (buf && off < bufLen) {
        const uint8_t *data = NULL;
        size_t dataLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dataLen, &off);
        
        switch (key >> 3) {
            case request_version: req->version = (uint32_t)i, ctx->defaults[request_version] = 0; break;
            case request_pki_type: _ProtoBufString(&req->pkiType, data, dataLen); break;
            case request_pki_data: req->pkiDataLen = _ProtoBufBytes(&req->pkiData, data, dataLen); break;
            case request_details: req->details = (data) ? BRPaymentProtocolDetailsParse(data, dataLen) : NULL; break;
            case request_signature: req->sigLen = _ProtoBufBytes(&req->signature, data, dataLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dataLen); break;
        }
    }
    
    if (! req->pkiType) {
        _ProtoBufString(&req->pkiType, "none", strlen("none"));
        ctx->defaults[request_pki_type] = 1;
    }
    
    if (! req->details) { // required
        BRPaymentProtocolRequestFree(req);
        req = NULL;
    }

    return req;
}

// writes serialized request struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolRequestSerialize(const BRPaymentProtocolRequest *req, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&req[1];
    size_t off = 0;

    assert(req != NULL);
    assert(req->details != NULL);
    
    if (! ctx->defaults[request_version]) _ProtoBufSetInt(buf, bufLen, req->version, request_version, &off);
    if (! ctx->defaults[request_pki_type]) _ProtoBufSetString(buf, bufLen, req->pkiType, request_pki_type, &off);
    if (req->pkiData) _ProtoBufSetBytes(buf, bufLen, req->pkiData, req->pkiDataLen, request_pki_data, &off);

    if (req->details) {
        size_t detailsLen = BRPaymentProtocolDetailsSerialize(req->details, NULL, 0);
        uint8_t *detailsBuf = malloc(detailsLen);

        assert(detailsBuf != NULL);
        detailsLen = BRPaymentProtocolDetailsSerialize(req->details, detailsBuf, detailsLen);
        _ProtoBufSetBytes(buf, bufLen, detailsBuf, detailsLen, request_details, &off);
        free(detailsBuf);
    }
    
    if (req->signature) _ProtoBufSetBytes(buf, bufLen, req->signature, req->sigLen, request_signature, &off);

    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }

    return (! buf || off <= bufLen) ? off : 0;
}

// writes the DER encoded certificate corresponding to index to cert
// returns the number of bytes written to cert, or the total certLen needed if cert is NULL
// returns 0 if index is out-of-bounds
size_t BRPaymentProtocolRequestCert(const BRPaymentProtocolRequest *req, uint8_t *cert, size_t certLen, size_t idx)
{
    size_t off = 0, len = 0;
    
    assert(req != NULL);
    
    while (req->pkiData && off < req->pkiDataLen) {
        const uint8_t *data = NULL;
        size_t dataLen = req->pkiDataLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, req->pkiData, &dataLen, &off);
        
        if ((key >> 3) == certificates_cert && data) {
            if (idx == 0) {
                len = dataLen;
                if (cert && len <= certLen) memcpy(cert, data, len);
                break;
            }
            else idx--;
        }
    }
    
    return (idx == 0 && (! cert || len <= certLen)) ? len : 0;
}

// writes the hash of the request to md needed to sign or verify the request
// returns the number of bytes written, or the total mdLen needed if md is NULL
size_t BRPaymentProtocolRequestDigest(BRPaymentProtocolRequest *req, uint8_t *md, size_t mdLen)
{
    uint8_t *buf;
    size_t bufLen;
    
    assert(req != NULL);

    req->sigLen = 0; // set signature to 0 bytes, a signature can't sign itself
    bufLen = BRPaymentProtocolRequestSerialize(req, NULL, 0);
    buf = malloc(bufLen);
    assert(buf != NULL);
    bufLen = BRPaymentProtocolRequestSerialize(req, buf, bufLen);
    
    if (req->pkiType && strncmp(req->pkiType, "x509+sha256", strlen("x509+sha256") + 1) == 0) {
        if (md && 256/8 <= mdLen) BRSHA256(md, buf, bufLen);
        bufLen = 256/8;
    }
    else if (req->pkiType && strncmp(req->pkiType, "x509+sha1", strlen("x509+sha1") + 1) == 0) {
        if (md && 160/8 <= mdLen) BRSHA1(md, buf, bufLen);
        bufLen = 160/8;
    }
    else bufLen = 0;
    
    free(buf);
    if (req->signature) req->sigLen = array_count(req->signature);
    return (! md || bufLen <= mdLen) ? bufLen : 0;
}

// frees memory allocated for request struct
void BRPaymentProtocolRequestFree(BRPaymentProtocolRequest *req)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&req[1];

    assert(req != NULL);
    
    if (req->pkiType) array_free(req->pkiType);
    if (req->pkiData) array_free(req->pkiData);
    if (req->details) BRPaymentProtocolDetailsFree(req->details);
    if (req->signature) array_free(req->signature);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
    free(req);
}

// returns a newly allocated payment struct that must be freed by calling BRPaymentProtocolPaymentFree()
BRPaymentProtocolPayment *BRPaymentProtocolPaymentNew(const uint8_t *merchantData, size_t merchDataLen,
                                                      BRTransaction *transactions[], size_t txCount,
                                                      const uint64_t refundToAmounts[],
                                                      const BRAddress refundToAddresses[], size_t refundToCount,
                                                      const char *memo)
{
    BRPaymentProtocolPayment *payment = calloc(1, sizeof(*payment) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&payment[1];
    
    assert(payment != NULL);
    assert(transactions != NULL || txCount == 0);
    assert(refundToAmounts != NULL || refundToCount == 0);
    assert(refundToAddresses != NULL || refundToCount == 0);
    
    array_new(ctx->defaults, payment_merch_data + 1);
    array_set_count(ctx->defaults, payment_merch_data + 1);

    if (merchantData) payment->merchDataLen = _ProtoBufBytes(&payment->merchantData, merchantData, merchDataLen);
    
    if (transactions) {
        array_new(payment->transactions, txCount);
        array_add_array(payment->transactions, transactions, txCount);
        payment->txCount = txCount;
    }
    
    array_new(payment->refundTo, refundToCount);
        
    for (size_t i = 0; i < refundToCount; i++) {
        uint8_t script[BRAddressScriptPubKey(NULL, 0, refundToAddresses[i].s)];
        size_t scriptLen = BRAddressScriptPubKey(script, sizeof(script), refundToAddresses[i].s);
            
        array_add(payment->refundTo, _BRPaymentProtocolOutput(refundToAmounts[i], script, scriptLen));
    }
    
    payment->refundToCount = refundToCount;
    if (memo) _ProtoBufString(&payment->memo, memo, strlen(memo));
    return payment;
}

// buf must contain a serialized payment struct
// returns a payment struct that must be freed by calling BRPaymentProtocolPaymentFree()
BRPaymentProtocolPayment *BRPaymentProtocolPaymentParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolPayment *payment = calloc(1, sizeof(*payment) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&payment[1];
    size_t off = 0;
    
    assert(payment != NULL);
    assert(buf != NULL || bufLen == 0);
    
    array_new(ctx->defaults, payment_merch_data + 1);
    array_set_count(ctx->defaults, payment_merch_data + 1);
    array_new(payment->transactions, 1);
    array_new(payment->refundTo, 1);
    
    while (buf && off < bufLen) {
        BRTransaction *tx = NULL;
        BRTxOutput out = BR_TX_OUTPUT_NONE;
        const uint8_t *data = NULL;
        size_t dLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dLen, &off);
        
        switch (key >> 3) {
            case payment_transactions: tx = (data) ? BRTransactionParse(data, dLen) : NULL; break;
            case payment_refund_to: out = _BRPaymentProtocolOutputParse(data, dLen); break;
            case payment_memo: _ProtoBufString(&payment->memo, data, dLen); break;
            case payment_merch_data: payment->merchDataLen = _ProtoBufBytes(&payment->merchantData, data, dLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dLen); break;
        }
        
        if (tx) array_add(payment->transactions, tx);
        if (out.script) array_add(payment->refundTo, out);
    }
    
    payment->txCount = array_count(payment->transactions);
    payment->refundToCount = array_count(payment->refundTo);
    return payment;
}

// writes serialized payment struct to buf, returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolPaymentSerialize(const BRPaymentProtocolPayment *payment, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&payment[1];
    size_t off = 0, sLen = 0x100, l;
    uint8_t *sBuf = malloc(sLen);

    assert(sBuf != NULL);
    assert(payment != NULL);
    
    if (payment->merchantData) {
        _ProtoBufSetBytes(buf, bufLen, payment->merchantData, payment->merchDataLen, payment_merch_data, &off);
    }

    for (size_t i = 0; i < payment->txCount; i++) {
        l = BRTransactionSerialize(payment->transactions[i], NULL, 0);
        if (l > sLen) sBuf = realloc(sBuf, (sLen = l));
        assert(sBuf != NULL);
        l = BRTransactionSerialize(payment->transactions[i], sBuf, sLen);
        _ProtoBufSetBytes(buf, bufLen, sBuf, l, payment_transactions, &off);
    }

    for (size_t i = 0; i < payment->refundToCount; i++) {
        l = _BRPaymentProtocolOutputSerialize(payment->refundTo[i], NULL, 0);
        if (l > sLen) sBuf = realloc(sBuf, (sLen = l));
        assert(sBuf != NULL);
        l = _BRPaymentProtocolOutputSerialize(payment->refundTo[i], sBuf, l);
        _ProtoBufSetBytes(buf, bufLen, sBuf, l, payment_refund_to, &off);
    }

    free(sBuf);
    if (payment->memo) _ProtoBufSetString(buf, bufLen, payment->memo, payment_memo, &off);

    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

// frees memory allocated for payment struct (does not call BRTransactionFree() on transactions)
void BRPaymentProtocolPaymentFree(BRPaymentProtocolPayment *payment)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&payment[1];

    assert(payment != NULL);
    
    if (payment->merchantData) array_free(payment->merchantData);
    if (payment->transactions) array_free(payment->transactions);
    for (size_t i = 0; i < payment->refundToCount; i++) _BRPaymentProtocolOutputFree(payment->refundTo[i]);
    if (payment->refundTo) array_free(payment->refundTo);
    if (payment->memo) array_free(payment->memo);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
}

// returns a newly allocated ACK struct that must be freed by calling BRPaymentProtocolACKFree()
BRPaymentProtocolACK *BRPaymentProtocolACKNew(BRPaymentProtocolPayment *payment, const char *memo)
{
    BRPaymentProtocolACK *ack = calloc(1, sizeof(*ack) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&ack[1];
    
    assert(ack != NULL);
    assert(payment != NULL);
    
    array_new(ctx->defaults, ack_memo + 1);
    array_set_count(ctx->defaults, ack_memo + 1);
    ack->payment = payment;
    if (memo) _ProtoBufString(&ack->memo, memo, strlen(memo));
    
    if (! ack->payment) { // required
        BRPaymentProtocolACKFree(ack);
        ack = NULL;
    }

    return ack;
}

// buf must contain a serialized ACK struct
// returns a ACK struct that must be freed by calling BRPaymentProtocolACKFree()
BRPaymentProtocolACK *BRPaymentProtocolACKParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolACK *ack = calloc(1, sizeof(*ack) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&ack[1];
    size_t off = 0;
    
    assert(ack != NULL);
    assert(buf != NULL || bufLen == 0);
    
    array_new(ctx->defaults, ack_memo + 1);
    array_set_count(ctx->defaults, ack_memo + 1);
    
    while (buf && off < bufLen) {
        const uint8_t *data = NULL;
        size_t dataLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dataLen, &off);
        
        switch (key >> 3) {
            case ack_payment: ack->payment = (data) ? BRPaymentProtocolPaymentParse(data, dataLen) : NULL; break;
            case ack_memo: _ProtoBufString(&ack->memo, data, dataLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dataLen); break;
        }
    }
    
    if (! ack->payment) { // required
        BRPaymentProtocolACKFree(ack);
        ack = NULL;
    }
    
    return ack;
}

// writes serialized ACK struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolACKSerialize(const BRPaymentProtocolACK *ack, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&ack[1];
    size_t off = 0;
    
    assert(ack != NULL);
    assert(ack->payment != NULL);
    
    if (ack->payment) {
        size_t paymentLen = BRPaymentProtocolPaymentSerialize(ack->payment, NULL, 0);
        uint8_t *paymentBuf = malloc(paymentLen);
        
        assert(paymentBuf != NULL);
        paymentLen = BRPaymentProtocolPaymentSerialize(ack->payment, paymentBuf, paymentLen);
        _ProtoBufSetBytes(buf, bufLen, paymentBuf, paymentLen, ack_payment, &off);
        free(paymentBuf);
    }
    
    if (ack->memo) _ProtoBufSetString(buf, bufLen, ack->memo, ack_memo, &off);

    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

// frees memory allocated for ACK struct
void BRPaymentProtocolACKFree(BRPaymentProtocolACK *ack)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&ack[1];

    assert(ack != NULL);
    
    if (ack->payment) BRPaymentProtocolPaymentFree(ack->payment);
    if (ack->memo) array_free(ack->memo);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
    free(ack);
}

// returns a newly allocated invoice request struct that must be freed by calling BRPaymentProtocolInvoiceRequestFree()
BRPaymentProtocolInvoiceRequest *BRPaymentProtocolInvoiceRequestNew(BRKey *senderPubKey, uint64_t amount,
                                                                    const char *pkiType, uint8_t *pkiData,
                                                                    size_t pkiDataLen, const char *memo,
                                                                    const char *notifyUrl, const uint8_t *signature,
                                                                    size_t sigLen)
{
    BRPaymentProtocolInvoiceRequest *req = calloc(1, sizeof(*req) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&req[1];
    uint8_t pk[65];
    size_t pkLen;
    
    assert(req != NULL);
    assert(senderPubKey != NULL);
    
    array_new(ctx->defaults, invoice_req_signature + 1);
    array_set_count(ctx->defaults, invoice_req_signature + 1);
    pkLen = BRKeyPubKey(senderPubKey, pk, sizeof(pk));
    BRKeySetPubKey(&req->senderPubKey, pk, pkLen);
    req->amount = amount;

    if (! pkiType) {
        _ProtoBufString(&req->pkiType, "none", strlen("none"));
        ctx->defaults[invoice_req_pki_type] = 1;
    }
    else _ProtoBufString(&req->pkiType, pkiType, strlen(pkiType));
    
    if (pkiData) req->pkiDataLen = _ProtoBufBytes(&req->pkiData, pkiData, pkiDataLen);
    if (memo) _ProtoBufString(&req->memo, memo, strlen(memo));
    if (notifyUrl) _ProtoBufString(&req->notifyUrl, notifyUrl, strlen(notifyUrl));
    if (signature) req->sigLen = _ProtoBufBytes(&req->signature, signature, sigLen);
    return req;
}

// buf must contain a serialized invoice request
// returns an invoice request struct that must be freed by calling BRPaymentProtocolInvoiceRequestFree()
BRPaymentProtocolInvoiceRequest *BRPaymentProtocolInvoiceRequestParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolInvoiceRequest *req = calloc(1, sizeof(*req) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&req[1];
    size_t off = 0;
    int gotSenderPK = 0;
    
    assert(req != NULL);
    assert(buf != NULL || bufLen == 0);
    
    array_new(ctx->defaults, invoice_req_signature + 1);
    array_set_count(ctx->defaults, invoice_req_signature + 1);
    ctx->defaults[invoice_req_amount] = 1;
    
    while (buf && off < bufLen) {
        const uint8_t *data = NULL;
        size_t dataLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dataLen, &off);
        
        switch (key >> 3) {
            case invoice_req_sender_pk: gotSenderPK = BRKeySetPubKey(&req->senderPubKey, data, dataLen); break;
            case invoice_req_amount: req->amount = i, ctx->defaults[invoice_req_amount] = 0; break;
            case invoice_req_pki_type: _ProtoBufString(&req->pkiType, data, dataLen); break;
            case invoice_req_pki_data: req->pkiDataLen = _ProtoBufBytes(&req->pkiData, data, dataLen); break;
            case invoice_req_memo: _ProtoBufString(&req->memo, data, dataLen); break;
            case invoice_req_notify_url: _ProtoBufString(&req->notifyUrl, data, dataLen); break;
            case invoice_req_signature: req->sigLen = _ProtoBufBytes(&req->signature, data, dataLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dataLen); break;
        }
    }
    
    if (! req->pkiType) {
        _ProtoBufString(&req->pkiType, "none", strlen("none"));
        ctx->defaults[invoice_req_pki_type] = 1;
    }

    if (! gotSenderPK) { // required
        BRPaymentProtocolInvoiceRequestFree(req);
        req = NULL;
    }
    
    return req;
}

// writes serialized invoice request to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolInvoiceRequestSerialize(BRPaymentProtocolInvoiceRequest *req, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&req[1];
    uint8_t pk[65];
    size_t pkLen, off = 0;
    
    assert(req != NULL);
    
    pkLen = BRKeyPubKey(&req->senderPubKey, pk, sizeof(pk));
    _ProtoBufSetBytes(buf, bufLen, pk, pkLen, invoice_req_sender_pk, &off);
    if (! ctx->defaults[invoice_req_amount]) _ProtoBufSetInt(buf, bufLen, req->amount, invoice_req_amount, &off);
    if (! ctx->defaults[invoice_req_pki_type]) _ProtoBufSetString(buf, bufLen, req->pkiType, invoice_req_pki_type,&off);
    if (req->pkiData) _ProtoBufSetBytes(buf, bufLen, req->pkiData, req->pkiDataLen, invoice_req_pki_data, &off);
    if (req->memo) _ProtoBufSetString(buf, bufLen, req->memo, invoice_req_memo, &off);
    if (req->notifyUrl) _ProtoBufSetString(buf, bufLen, req->notifyUrl, invoice_req_notify_url, &off);
    if (req->signature) _ProtoBufSetBytes(buf, bufLen, req->signature, req->sigLen, invoice_req_signature, &off);
    
    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

// writes the DER encoded certificate corresponding to index to cert
// returns the number of bytes written to cert, or the total certLen needed if cert is NULL
// returns 0 if index is out-of-bounds
size_t BRPaymentProtocolInvoiceRequestCert(const BRPaymentProtocolInvoiceRequest *req, uint8_t *cert, size_t certLen,
                                           size_t idx)
{
    size_t off = 0, len = 0;
    
    assert(req != NULL);
    
    while (req->pkiData && off < req->pkiDataLen) {
        const uint8_t *data = NULL;
        size_t dataLen = req->pkiDataLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, req->pkiData, &dataLen, &off);
        
        if ((key >> 3) == certificates_cert && data) {
            if (idx == 0) {
                len = dataLen;
                if (cert && len <= certLen) memcpy(cert, data, len);
                break;
            }
            else idx--;
        }
    }
    
    return (idx == 0 && (! cert || len <= certLen)) ? len : 0;
}

// writes the hash of the request to md needed to sign or verify the request
// returns the number of bytes written, or the total mdLen needed if md is NULL
size_t BRPaymentProtocolInvoiceRequestDigest(BRPaymentProtocolInvoiceRequest *req, uint8_t *md, size_t mdLen)
{
    uint8_t *buf;
    size_t bufLen;
    
    assert(req != NULL);
    
    req->sigLen = 0; // set signature to 0 bytes, a signature can't sign itself
    bufLen = BRPaymentProtocolInvoiceRequestSerialize(req, NULL, 0);
    buf = malloc(bufLen);
    assert(buf != NULL);
    bufLen = BRPaymentProtocolInvoiceRequestSerialize(req, buf, bufLen);
    
    if (req->pkiType && strncmp(req->pkiType, "x509+sha256", strlen("x509+sha256") + 1) == 0) {
        if (md && 256/8 <= mdLen) BRSHA256(md, buf, bufLen);
        bufLen = 256/8;
    }
    else bufLen = 0;
    
    free(buf);
    if (req->signature) req->sigLen = array_count(req->signature);
    return (! md || bufLen <= mdLen) ? bufLen : 0;
}

// frees memory allocated for invoice request struct
void BRPaymentProtocolInvoiceRequestFree(BRPaymentProtocolInvoiceRequest *req)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&req[1];

    assert(req != NULL);
    
    if (req->pkiType) array_free(req->pkiType);
    if (req->pkiData) array_free(req->pkiData);
    if (req->memo) array_free(req->memo);
    if (req->notifyUrl) array_free(req->notifyUrl);
    if (req->signature) array_free(req->signature);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
    free(req);
}

// returns a newly allocated message struct that must be freed by calling BRPaymentProtocolMessageFree()
BRPaymentProtocolMessage *BRPaymentProtocolMessageNew(BRPaymentProtocolMessageType msgType, const uint8_t *message,
                                                      size_t msgLen, uint64_t statusCode, const char *statusMsg,
                                                      const uint8_t *identifier, size_t identLen)
{
    BRPaymentProtocolMessage *msg = calloc(1, sizeof(*msg) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&msg[1];
    
    assert(msg != NULL);
    assert(message != NULL || msgLen == 0);
    
    array_new(ctx->defaults, message_identifier + 1);
    array_set_count(ctx->defaults, message_identifier + 1);
    msg->msgType = msgType;
    if (message) msg->msgLen = _ProtoBufBytes(&msg->message, message, msgLen);
    msg->statusCode = statusCode;
    if (statusMsg) _ProtoBufString(&msg->statusMsg, statusMsg, strlen(statusMsg));
    if (identifier) msg->identLen = _ProtoBufBytes(&msg->identifier, identifier, identLen);
    
    if (! msg->message) { // required
        BRPaymentProtocolMessageFree(msg);
        msg = NULL;
    }
    
    return msg;
}

// buf must contain a serialized message
// returns an message struct that must be freed by calling BRPaymentProtocolMessageFree()
BRPaymentProtocolMessage *BRPaymentProtocolMessageParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolMessage *msg = calloc(1, sizeof(*msg) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&msg[1];
    size_t off = 0;
    int gotMsgType = 0;
    
    assert(msg != NULL);
    assert(buf != NULL || bufLen == 0);
    
    array_new(ctx->defaults, message_identifier + 1);
    array_set_count(ctx->defaults, message_identifier + 1);
    ctx->defaults[message_status_code] = 1;
    
    while (buf && off < bufLen) {
        const uint8_t *data = NULL;
        size_t dataLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dataLen, &off);
        
        switch (key >> 3) {
            case message_msg_type: msg->msgType = (BRPaymentProtocolMessageType)i, gotMsgType = 1; break;
            case message_message: msg->msgLen = _ProtoBufBytes(&msg->message, data, dataLen); break;
            case message_status_code: msg->statusCode = i, ctx->defaults[message_status_code] = 0; break;
            case message_status_msg: _ProtoBufString(&msg->statusMsg, data, dataLen); break;
            case message_identifier: msg->identLen = _ProtoBufBytes(&msg->identifier, data, dataLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dataLen); break;
        }
    }
    
    if (! gotMsgType || ! msg->message) { // required
        BRPaymentProtocolMessageFree(msg);
        msg = NULL;
    }
    
    return msg;
}

// writes serialized message struct to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolMessageSerialize(const BRPaymentProtocolMessage *msg, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&msg[1];
    size_t off = 0;
    
    assert(msg != NULL);
    assert(msg->message != NULL);
    
    _ProtoBufSetInt(buf, bufLen, msg->msgType, message_msg_type, &off);
    _ProtoBufSetBytes(buf, bufLen, msg->message, msg->msgLen, message_message, &off);
    if (! ctx->defaults[message_status_code]) _ProtoBufSetInt(buf, bufLen, msg->statusCode, message_status_code, &off);
    if (msg->statusMsg) _ProtoBufSetString(buf, bufLen, msg->statusMsg, message_status_msg, &off);
    if (msg->identifier) _ProtoBufSetBytes(buf, bufLen, msg->identifier, msg->identLen, message_identifier, &off);
    
    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

// frees memory allocated for message struct
void BRPaymentProtocolMessageFree(BRPaymentProtocolMessage *msg)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&msg[1];
    
    assert(msg != NULL);

    if (msg->message) array_free(msg->message);
    if (msg->statusMsg) array_free(msg->statusMsg);
    if (msg->identifier) array_free(msg->identifier);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
    free(msg);
}

static void _BRPaymentProtocolEncryptedMessageCEK(BRPaymentProtocolEncryptedMessage *msg, void *cek32, void *iv12,
                                                  BRKey *privKey)
{
    uint8_t secret[32], seed[512/8], K[256/8], V[256/8], pk[65], rpk[65],
            nonce[] = { msg->nonce >> 56, msg->nonce >> 48, msg->nonce >> 40, msg->nonce >> 32,
                        msg->nonce >> 24, msg->nonce >> 16, msg->nonce >> 8, msg->nonce }; // convert to big endian
    size_t pkLen = BRKeyPubKey(privKey, pk, sizeof(pk)),
           rpkLen = BRKeyPubKey(&msg->receiverPubKey, rpk, sizeof(rpk));
    BRKey *pubKey = (pkLen != rpkLen || memcmp(pk, rpk, pkLen) != 0) ? &msg->receiverPubKey : &msg->senderPubKey;

    BRKeyECDH(privKey, secret, pubKey);
    BRSHA512(seed, secret, sizeof(secret));
    mem_clean(secret, sizeof(secret));
    BRHMACDRBG(cek32, 32, K, V, BRSHA256, 256/8, seed, sizeof(seed), nonce, sizeof(nonce), NULL, 0);
    mem_clean(seed, sizeof(seed));
    BRHMACDRBG(iv12, 12, K, V, BRSHA256, 256/8, NULL, 0, NULL, 0, NULL, 0);
    mem_clean(K, sizeof(K));
    mem_clean(V, sizeof(V));
}

// returns a newly allocated encrypted message struct that must be freed by calling BRPaymentProtocolMessageFree()
// message is the un-encrypted serialized payment protocol message
// one of either receiverKey or senderKey must contain a private key, and the other must contain only a public key
BRPaymentProtocolEncryptedMessage *BRPaymentProtocolEncryptedMessageNew(BRPaymentProtocolMessageType msgType,
                                                                        const uint8_t *message, size_t msgLen,
                                                                        BRKey *receiverKey, BRKey *senderKey,
                                                                        uint64_t nonce,
                                                                        const uint8_t *identifier, size_t identLen,
                                                                        uint64_t statusCode, const char *statusMsg)
{
    BRPaymentProtocolEncryptedMessage *msg = calloc(1, sizeof(*msg) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&msg[1];
    BRKey *privKey;
    size_t pkLen, sigLen, bufLen = msgLen + 16, adLen = (statusMsg) ? 20 + strlen(statusMsg) + 1 : 20 + 1;
    char *ad = calloc(adLen, sizeof(*ad));
    uint8_t cek[32], iv[12], pk[65], sig[73], md[256/8], *buf = malloc(bufLen);
    
    assert(msg != NULL);
    assert(ad != NULL);
    assert(buf != NULL);
    assert(message != NULL || msgLen == 0);
    assert(receiverKey != NULL);
    assert(senderKey != NULL);

    size_t recPriKeyLen = BRKeyPrivKey(receiverKey, NULL, 0);
    size_t sndPriKeyLen = BRKeyPrivKey(senderKey, NULL, 0);
    assert(recPriKeyLen != 0 || sndPriKeyLen != 0);
    
    array_new(ctx->defaults, encrypted_msg_status_msg + 1);
    array_set_count(ctx->defaults, encrypted_msg_status_msg + 1);
    msg->msgType = msgType;
    pkLen = BRKeyPubKey(receiverKey, pk, sizeof(pk));
    BRKeySetPubKey(&msg->receiverPubKey, pk, pkLen);
    pkLen = BRKeyPubKey(senderKey, pk, sizeof(pk));
    BRKeySetPubKey(&msg->senderPubKey, pk, pkLen);
    msg->nonce = nonce;
    if (identifier) msg->identLen = _ProtoBufBytes(&msg->identifier, identifier, identLen);
    msg->statusCode = statusCode;
    if (statusMsg) _ProtoBufString(&msg->statusMsg, statusMsg, strlen(statusMsg));
    privKey = (BRKeyPrivKey(receiverKey, NULL, 0) != 0) ? receiverKey : senderKey;
    _BRPaymentProtocolEncryptedMessageCEK(msg, cek, iv, privKey);
    snprintf(ad, adLen, "%"PRIu64"%s", statusCode, (statusMsg) ? statusMsg : "");
    bufLen = BRChacha20Poly1305AEADEncrypt(buf, bufLen, cek, iv, message, msgLen, ad, strlen(ad));
    mem_clean(cek, sizeof(cek));
    mem_clean(iv, sizeof(iv));
    free(ad);
    msg->msgLen = _ProtoBufBytes(&msg->message, buf, bufLen);
    free(buf);
    
    if (! msg->message) { // required
        BRPaymentProtocolEncryptedMessageFree(msg);
        msg = NULL;
    }
    else {
        msg->signature = (uint8_t *)"";
        bufLen = BRPaymentProtocolEncryptedMessageSerialize(msg, NULL, 0);
        buf = malloc(bufLen);
        assert(buf != NULL);
        bufLen = BRPaymentProtocolEncryptedMessageSerialize(msg, buf, bufLen);
        msg->signature = NULL;
        BRSHA256(md, buf, bufLen);
        free(buf);
        sigLen = BRKeySign(privKey, sig, sizeof(sig), UInt256Get(md));
        msg->sigLen = _ProtoBufBytes(&msg->signature, sig, sigLen);
    }
    
    return msg;
}

// buf must contain a serialized encrytped message
// returns an encrypted message struct that must be freed by calling BRPaymentProtocolEncryptedMessageFree()
BRPaymentProtocolEncryptedMessage *BRPaymentProtocolEncryptedMessageParse(const uint8_t *buf, size_t bufLen)
{
    BRPaymentProtocolEncryptedMessage *msg = calloc(1, sizeof(*msg) + sizeof(ProtoBufContext));
    ProtoBufContext *ctx = (ProtoBufContext *)&msg[1];
    size_t off = 0;
    int gotMsgType = 0, gotNonce = 0, gotReceiverPK = 0, gotSenderPK = 0;
    
    assert(msg != NULL);
    assert(buf != NULL || bufLen == 0);
    
    array_new(ctx->defaults, encrypted_msg_status_msg + 1);
    array_set_count(ctx->defaults, encrypted_msg_status_msg + 1);
    ctx->defaults[encrypted_msg_status_code] = 1;
    
    while (buf && off < bufLen) {
        const uint8_t *data = NULL;
        size_t dataLen = bufLen;
        uint64_t i = 0, key = _ProtoBufField(&i, &data, buf, &dataLen, &off);
        
        switch (key >> 3) {
            case encrypted_msg_msg_type: msg->msgType = (BRPaymentProtocolMessageType)i, gotMsgType = 1; break;
            case encrypted_msg_message: msg->msgLen = _ProtoBufBytes(&msg->message, data, dataLen); break;
            case encrypted_msg_receiver_pk: gotReceiverPK = BRKeySetPubKey(&msg->receiverPubKey, data, dataLen); break;
            case encrypted_msg_sender_pk: gotSenderPK = BRKeySetPubKey(&msg->senderPubKey, data, dataLen); break;
            case encrypted_msg_nonce: msg->nonce = i, gotNonce = 1; break;
            case encrypted_msg_signature: msg->sigLen = _ProtoBufBytes(&msg->signature, data, dataLen); break;
            case encrypted_msg_identifier: msg->identLen = _ProtoBufBytes(&msg->identifier, data, dataLen); break;
            case encrypted_msg_status_code: msg->statusCode = i, ctx->defaults[encrypted_msg_status_code] = 0; break;
            case encrypted_msg_status_msg: _ProtoBufString(&msg->statusMsg, data, dataLen); break;
            default: _ProtoBufUnknown(&ctx->unknown, key, i, data, dataLen); break;
        }
    }
    
    if (! gotMsgType || ! msg->message || ! gotReceiverPK || ! gotSenderPK || ! gotNonce) { // required
        BRPaymentProtocolEncryptedMessageFree(msg);
        msg = NULL;
    }
    
    return msg;
}

// writes serialized encrypted message to buf and returns number of bytes written, or total bufLen needed if buf is NULL
size_t BRPaymentProtocolEncryptedMessageSerialize(BRPaymentProtocolEncryptedMessage *msg, uint8_t *buf, size_t bufLen)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&msg[1];
    uint8_t pubKey[65];
    size_t pkLen, off = 0;
    
    assert(msg != NULL);
    assert(msg->message != NULL);
    
    _ProtoBufSetInt(buf, bufLen, msg->msgType, encrypted_msg_msg_type, &off);
    _ProtoBufSetBytes(buf, bufLen, msg->message, msg->msgLen, encrypted_msg_message, &off);
    pkLen = BRKeyPubKey(&msg->receiverPubKey, pubKey, sizeof(pubKey));
    _ProtoBufSetBytes(buf, bufLen, pubKey, pkLen, encrypted_msg_receiver_pk, &off);
    pkLen = BRKeyPubKey(&msg->senderPubKey, pubKey, sizeof(pubKey));
    _ProtoBufSetBytes(buf, bufLen, pubKey, pkLen, encrypted_msg_sender_pk, &off);
    _ProtoBufSetInt(buf, bufLen, msg->nonce, encrypted_msg_nonce, &off);
    if (msg->signature) _ProtoBufSetBytes(buf, bufLen, msg->signature, msg->sigLen, encrypted_msg_signature, &off);
    if (msg->identifier) _ProtoBufSetBytes(buf, bufLen, msg->identifier, msg->identLen, encrypted_msg_identifier, &off);
    if (! ctx->defaults[encrypted_msg_status_code]) _ProtoBufSetInt(buf, bufLen, msg->statusCode,
                                                                    encrypted_msg_status_code, &off);
    if (msg->statusMsg) _ProtoBufSetString(buf, bufLen, msg->statusMsg, encrypted_msg_status_msg, &off);
    
    if (ctx->unknown) {
        if (buf && off + array_count(ctx->unknown) <= bufLen) memcpy(&buf[off], ctx->unknown,array_count(ctx->unknown));
        off += array_count(ctx->unknown);
    }
    
    return (! buf || off <= bufLen) ? off : 0;
}

int BRPaymentProtocolEncryptedMessageVerify(BRPaymentProtocolEncryptedMessage *msg, BRKey *pubKey)
{
    uint8_t md[256/8], *buf;
    size_t bufLen, sigLen;
    
    assert(msg != NULL);
    assert(msg->message != NULL);
    
    sigLen = msg->sigLen;
    msg->sigLen = 0; // set signature to zero length (a signature can't sign itself)
    bufLen = BRPaymentProtocolEncryptedMessageSerialize(msg, NULL, 0);
    buf = malloc(bufLen);
    assert(buf != NULL);
    bufLen = BRPaymentProtocolEncryptedMessageSerialize(msg, buf, bufLen);
    msg->sigLen = sigLen;
    BRSHA256(md, buf, bufLen);
    free(buf);
    return BRKeyVerify(pubKey, UInt256Get(md), msg->signature, msg->sigLen);
}

size_t BRPaymentProtocolEncryptedMessageDecrypt(BRPaymentProtocolEncryptedMessage *msg, uint8_t *out, size_t outLen,
                                                BRKey *privKey)
{
    const ProtoBufContext *ctx = (const ProtoBufContext *)&msg[1];
    uint8_t cek[32], iv[12];
    size_t adLen;
    char *ad;

    assert(msg != NULL);
    assert(msg->message != NULL);
    
    if (! out) return (msg->msgLen < 16) ? 0 : msg->msgLen - 16;

    assert(privKey != NULL);

    _BRPaymentProtocolEncryptedMessageCEK(msg, cek, iv, privKey);
    adLen = (msg->statusMsg) ? 20 + strlen(msg->statusMsg) + 1 : 20 + 1;
    ad = calloc(adLen, sizeof(*ad));
    
    if (! ctx->defaults[encrypted_msg_status_code]) {
        snprintf(ad, adLen, "%"PRIu64"%s", msg->statusCode, (msg->statusMsg) ? msg->statusMsg : "");
    }
    else if (msg->statusMsg) strncpy(ad, msg->statusMsg, adLen);
    
    outLen = BRChacha20Poly1305AEADDecrypt(out, outLen, cek, iv, msg->message, msg->msgLen, ad, strlen(ad));
    mem_clean(cek, sizeof(cek));
    mem_clean(iv, sizeof(iv));
    free(ad);
    return outLen;
}

// frees memory allocated for encrypted message struct
void BRPaymentProtocolEncryptedMessageFree(BRPaymentProtocolEncryptedMessage *msg)
{
    ProtoBufContext *ctx = (ProtoBufContext *)&msg[1];
    
    assert(msg != NULL);
    
    if (msg->message) array_free(msg->message);
    if (msg->signature) array_free(msg->signature);
    if (msg->identifier) array_free(msg->identifier);
    if (msg->statusMsg) array_free(msg->statusMsg);
    if (ctx->defaults) array_free(ctx->defaults);
    if (ctx->unknown) array_free(ctx->unknown);
    free(msg);
}

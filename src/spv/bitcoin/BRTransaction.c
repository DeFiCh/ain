//
//  BRTransaction.c
//
//  Created by Aaron Voisine on 8/31/15.
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

#include "BRTransaction.h"
#include "BRKey.h"
#include "BRAddress.h"
#include "BRArray.h"
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>

#define TX_VERSION           0x00000001
#define TX_LOCKTIME          0x00000000
#define SIGHASH_ALL          0x01 // default, sign all of the outputs
#define SIGHASH_NONE         0x02 // sign none of the outputs, I don't care where the bitcoins go
#define SIGHASH_SINGLE       0x03 // sign one of the outputs, I don't care where the other outputs go
#define SIGHASH_ANYONECANPAY 0x80 // let other people add inputs, I don't care where the rest of the bitcoins come from
#define SIGHASH_FORKID       0x40 // use BIP143 digest method (for b-cash/b-gold signatures)

void BRTxInputSetAddress(BRTxInput *input, const char *address)
{
    assert(input != NULL);
    assert(address == NULL || BRAddressIsValid(address));
    if (input->script) array_free(input->script);
    input->script = NULL;
    input->scriptLen = 0;
    memset(input->address, 0, sizeof(input->address));

    if (address) {
        strncpy(input->address, address, sizeof(input->address) - 1);
        input->scriptLen = BRAddressScriptPubKey(NULL, 0, address);
        array_new(input->script, input->scriptLen);
        array_set_count(input->script, input->scriptLen);
        BRAddressScriptPubKey(input->script, input->scriptLen, address);
    }
}

void BRTxInputSetScript(BRTxInput *input, const uint8_t *script, size_t scriptLen)
{
    assert(input != NULL);
    assert(script != NULL || scriptLen == 0);
    if (input->script) array_free(input->script);
    input->script = NULL;
    input->scriptLen = 0;
    memset(input->address, 0, sizeof(input->address));
    
    if (script) {
        input->scriptLen = scriptLen;
        array_new(input->script, scriptLen);
        array_add_array(input->script, script, scriptLen);
        BRAddressFromScriptPubKey(input->address, sizeof(input->address), script, scriptLen);
    }
}

void BRTxInputSetSignature(BRTxInput *input, const uint8_t *signature, size_t sigLen)
{
    assert(input != NULL);
    assert(signature != NULL || sigLen == 0);
    if (input->signature) array_free(input->signature);
    input->signature = NULL;
    input->sigLen = 0;
    
    if (signature) {
        input->sigLen = sigLen;
        array_new(input->signature, sigLen);
        array_add_array(input->signature, signature, sigLen);
        if (! input->address[0]) BRAddressFromScriptSig(input->address, sizeof(input->address), signature, sigLen);
    }
}

void BRTxInputSetWitness(BRTxInput *input, const uint8_t *witness, size_t witLen)
{
    assert(input != NULL);
    assert(witness != NULL || witLen == 0);
    if (input->witness) array_free(input->witness);
    input->witness = NULL;
    input->witLen = 0;
    
    if (witness) {
        input->witLen = witLen;
        array_new(input->witness, witLen);
        array_add_array(input->witness, witness, witLen);
        if (! input->address[0]) BRAddressFromWitness(input->address, sizeof(input->address), witness, witLen);
    }
}

// serializes a tx input for a signature pre-image
// set input->amount to 0 to skip serializing the input amount in non-witness signatures
static size_t _BRTxInputData(const BRTxInput *input, uint8_t *data, size_t dataLen)
{
    size_t off = 0;
    
    if (data && off + sizeof(UInt256) <= dataLen) memcpy(&data[off], &input->txHash, sizeof(UInt256)); // previous out
    off += sizeof(UInt256);
    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], input->index);
    off += sizeof(uint32_t);
    off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), input->sigLen);
    if (data && off + input->sigLen <= dataLen) memcpy(&data[off], input->signature, input->sigLen); // scriptSig
    off += input->sigLen;

    if (input->amount != 0) {
        if (data && off + sizeof(uint64_t) <= dataLen) UInt64SetLE(&data[off], input->amount);
        off += sizeof(uint64_t);
    }

    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], input->sequence);
    off += sizeof(uint32_t);
    return (! data || off <= dataLen) ? off : 0;
}

void BRTxOutputSetAddress(BRTxOutput *output, const char *address)
{
    assert(output != NULL);
    assert(address == NULL || BRAddressIsValid(address));
    if (output->script) array_free(output->script);
    output->script = NULL;
    output->scriptLen = 0;
    memset(output->address, 0, sizeof(output->address));

    if (address) {
        strncpy(output->address, address, sizeof(output->address) - 1);
        output->scriptLen = BRAddressScriptPubKey(NULL, 0, address);
        array_new(output->script, output->scriptLen);
        array_set_count(output->script, output->scriptLen);
        BRAddressScriptPubKey(output->script, output->scriptLen, address);
    }
}

void BRTxOutputSetScript(BRTxOutput *output, const uint8_t *script, size_t scriptLen)
{
    assert(output != NULL);
    if (output->script) array_free(output->script);
    output->script = NULL;
    output->scriptLen = 0;
    memset(output->address, 0, sizeof(output->address));

    if (script) {
        output->scriptLen = scriptLen;
        array_new(output->script, scriptLen);
        array_add_array(output->script, script, scriptLen);
        BRAddressFromScriptPubKey(output->address, sizeof(output->address), script, scriptLen);
    }
}

// serializes the tx output at index for a signature pre-image
// an index of SIZE_MAX will serialize all tx outputs for SIGHASH_ALL signatures
static size_t _BRTransactionOutputData(const BRTransaction *tx, uint8_t *data, size_t dataLen, size_t index)
{
    BRTxOutput *output;
    size_t i, off = 0;
    
    for (i = (index == SIZE_MAX ? 0 : index); i < tx->outCount && (index == SIZE_MAX || index == i); i++) {
        output = &tx->outputs[i];
        if (data && off + sizeof(uint64_t) <= dataLen) UInt64SetLE(&data[off], output->amount);
        off += sizeof(uint64_t);
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), output->scriptLen);
        if (data && off + output->scriptLen <= dataLen) memcpy(&data[off], output->script, output->scriptLen);
        off += output->scriptLen;
    }
    
    return (! data || off <= dataLen) ? off : 0;
}

// writes the BIP143 witness program data that needs to be hashed and signed for the tx input at index
// https://github.com/b_itcoin/bips/blob/master/bip-0143.mediawiki
// returns number of bytes written, or total len needed if data is NULL
static size_t _BRTransactionWitnessData(const BRTransaction *tx, uint8_t *data, size_t dataLen, size_t index,
                                        int hashType)
{
    BRTxInput input;
    int anyoneCanPay = (hashType & SIGHASH_ANYONECANPAY), sigHash = (hashType & 0x1f);
    size_t i, off = 0;
    uint8_t scriptCode[] = { OP_DUP, OP_HASH160, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, OP_EQUALVERIFY, OP_CHECKSIG };

    if (index >= tx->inCount) return 0;
    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], tx->version); // tx version
    off += sizeof(uint32_t);
    
    if (! anyoneCanPay) {
        uint8_t buf[(sizeof(UInt256) + sizeof(uint32_t))*tx->inCount];
        
        for (i = 0; i < tx->inCount; i++) {
            UInt256Set(&buf[(sizeof(UInt256) + sizeof(uint32_t))*i], tx->inputs[i].txHash);
            UInt32SetLE(&buf[(sizeof(UInt256) + sizeof(uint32_t))*i + sizeof(UInt256)], tx->inputs[i].index);
        }
        
        if (data && off + sizeof(UInt256) <= dataLen) BRSHA256_2(&data[off], buf, sizeof(buf)); // inputs hash
    }
    else if (data && off + sizeof(UInt256) <= dataLen) UInt256Set(&data[off], UINT256_ZERO); // anyone-can-pay
    
    off += sizeof(UInt256);
    
    if (! anyoneCanPay && sigHash != SIGHASH_SINGLE && sigHash != SIGHASH_NONE) {
        uint8_t buf[sizeof(uint32_t)*tx->inCount];
        
        for (i = 0; i < tx->inCount; i++) UInt32SetLE(&buf[sizeof(uint32_t)*i], tx->inputs[i].sequence);
        if (data && off + sizeof(UInt256) <= dataLen) BRSHA256_2(&data[off], buf, sizeof(buf)); // sequence hash
    }
    else if (data && off + sizeof(UInt256) <= dataLen) UInt256Set(&data[off], UINT256_ZERO);
    
    off += sizeof(UInt256);
    input = tx->inputs[index];
    input.signature = input.script; // TODO: handle OP_CODESEPARATOR
    input.sigLen = input.scriptLen;

    if (input.scriptLen == 22 && input.script[0] == OP_0 && input.script[1] == 20) { // P2WPKH scriptCode
        memcpy(&scriptCode[3], &input.script[2], 20);
        input.signature = scriptCode;
        input.sigLen = sizeof(scriptCode);
    }

    off += _BRTxInputData(&input, (data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0));
    
    if (sigHash != SIGHASH_SINGLE && sigHash != SIGHASH_NONE) {
        size_t bufLen = _BRTransactionOutputData(tx, NULL, 0, SIZE_MAX);
        uint8_t _buf[0x1000], *buf = (bufLen <= 0x1000) ? _buf : malloc(bufLen);
        
        bufLen = _BRTransactionOutputData(tx, buf, bufLen, SIZE_MAX);
        if (data && off + sizeof(UInt256) <= dataLen) BRSHA256_2(&data[off], buf, bufLen); // SIGHASH_ALL outputs hash
        if (buf != _buf) free(buf);
    }
    else if (sigHash == SIGHASH_SINGLE && index < tx->outCount) {
        uint8_t buf[_BRTransactionOutputData(tx, NULL, 0, index)];
        size_t bufLen = _BRTransactionOutputData(tx, buf, sizeof(buf), index);
        
        if (data && off + sizeof(UInt256) <= dataLen) BRSHA256_2(&data[off], buf, bufLen); //SIGHASH_SINGLE outputs hash
    }
    else if (data && off + sizeof(UInt256) <= dataLen) UInt256Set(&data[off], UINT256_ZERO); // SIGHASH_NONE
    
    off += sizeof(UInt256);
    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], tx->lockTime); // locktime
    off += sizeof(uint32_t);
    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], hashType); // hash type
    off += sizeof(uint32_t);
    return (! data || off <= dataLen) ? off : 0;
}

// writes the data that needs to be hashed and signed for the tx input at index
// an index of SIZE_MAX will write the entire signed transaction
// returns number of bytes written, or total dataLen needed if data is NULL
static size_t _BRTransactionData(const BRTransaction *tx, uint8_t *data, size_t dataLen, size_t index, int hashType)
{
    BRTxInput input;
    int anyoneCanPay = (hashType & SIGHASH_ANYONECANPAY), sigHash = (hashType & 0x1f), witnessFlag = 0;
    size_t i, count, len, woff, off = 0;
    
    if (hashType & SIGHASH_FORKID) return _BRTransactionWitnessData(tx, data, dataLen, index, hashType);
    if (anyoneCanPay && index >= tx->inCount) return 0;
    
    for (i = 0; index == SIZE_MAX && ! witnessFlag && i < tx->inCount; i++) {
        if (tx->inputs[i].witLen > 0) witnessFlag = 1;
    }
    
    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], tx->version); // tx version
    off += sizeof(uint32_t);
    
    if (! anyoneCanPay) {
        if (witnessFlag && data && off + 2 <= dataLen) data[off] = 0, data[off + 1] = witnessFlag;
        if (witnessFlag) off += 2;
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), tx->inCount);
        
        for (i = 0; i < tx->inCount; i++) { // inputs
            input = tx->inputs[i];
            
            if (index == i || (index == SIZE_MAX && ! input.signature)) {
                input.signature = input.script; // TODO: handle OP_CODESEPARATOR
                input.sigLen = input.scriptLen;
                if (index == i) input.amount = 0;
            }
            else if (index != SIZE_MAX) {
                input.sigLen = 0;
                if (sigHash == SIGHASH_NONE || sigHash == SIGHASH_SINGLE) input.sequence = 0;
                input.amount = 0;
            }
            else input.amount = 0;
            
            off += _BRTxInputData(&input, (data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0));
        }
    }
    else {
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), 1);
        input = tx->inputs[index];
        input.signature = input.script; // TODO: handle OP_CODESEPARATOR
        input.sigLen = input.scriptLen;
        input.amount = 0;
        off += _BRTxInputData(&input, (data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0));
    }
    
    if (sigHash != SIGHASH_SINGLE && sigHash != SIGHASH_NONE) { // SIGHASH_ALL outputs
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), tx->outCount);
        off += _BRTransactionOutputData(tx, (data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), SIZE_MAX);
    }
    else if (sigHash == SIGHASH_SINGLE && index < tx->outCount) { // SIGHASH_SINGLE outputs
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), index + 1);
        
        for (i = 0; i < index; i++)  {
            if (data && off + sizeof(uint64_t) <= dataLen) UInt64SetLE(&data[off], -1LL);
            off += sizeof(uint64_t);
            off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), 0);
        }
        
        off += _BRTransactionOutputData(tx, (data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), index);
    }
    else off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), 0); //SIGHASH_NONE outputs
    
    for (i = 0; witnessFlag && i < tx->inCount; i++) {
        input = tx->inputs[i];

        for (count = 0, woff = 0; woff < input.witLen; count++) {
            woff += BRVarInt(&input.witness[woff], input.witLen - woff, &len);
            woff += len;
        }
        
        off += BRVarIntSet((data ? &data[off] : NULL), (off <= dataLen ? dataLen - off : 0), count);
        if (data && off + input.witLen <= dataLen) memcpy(&data[off], input.witness, input.witLen);
        off += input.witLen;
    }
    
    if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], tx->lockTime); // locktime
    off += sizeof(uint32_t);
    
    if (index != SIZE_MAX) {
        if (data && off + sizeof(uint32_t) <= dataLen) UInt32SetLE(&data[off], hashType); // hash type
        off += sizeof(uint32_t);
    }
    
    return (! data || off <= dataLen) ? off : 0;
}

// returns a newly allocated empty transaction that must be freed by calling BRTransactionFree()
BRTransaction *BRTransactionNew(void)
{
    BRTransaction *tx = calloc(1, sizeof(*tx));

    assert(tx != NULL);
    tx->version = TX_VERSION;
    array_new(tx->inputs, 1);
    array_new(tx->outputs, 2);
    tx->lockTime = TX_LOCKTIME;
    tx->blockHeight = TX_UNCONFIRMED;
    return tx;
}

// returns a deep copy of tx and that must be freed by calling BRTransactionFree()
BRTransaction *BRTransactionCopy(const BRTransaction *tx)
{
    BRTransaction *cpy = BRTransactionNew();
    BRTxInput *inputs = cpy->inputs;
    BRTxOutput *outputs = cpy->outputs;
    
    assert(tx != NULL);
    *cpy = *tx;
    cpy->inputs = inputs;
    cpy->outputs = outputs;
    cpy->inCount = cpy->outCount = 0;

    for (size_t i = 0; i < tx->inCount; i++) {
        BRTransactionAddInput(cpy, tx->inputs[i].txHash, tx->inputs[i].index, tx->inputs[i].amount,
                              tx->inputs[i].script, tx->inputs[i].scriptLen,
                              tx->inputs[i].signature, tx->inputs[i].sigLen,
                              tx->inputs[i].witness, tx->inputs[i].witLen, tx->inputs[i].sequence);
    }
    
    for (size_t i = 0; i < tx->outCount; i++) {
        BRTransactionAddOutput(cpy, tx->outputs[i].amount, tx->outputs[i].script, tx->outputs[i].scriptLen);
    }

    return cpy;
}

// buf must contain a serialized tx
// retruns a transaction that must be freed by calling BRTransactionFree()
BRTransaction *BRTransactionParse(const uint8_t *buf, size_t bufLen)
{
    assert(buf != NULL || bufLen == 0);
    if (! buf) return NULL;
    
    int isSigned = 1, witnessFlag = 0;
    uint8_t *sBuf;
    size_t i, j, off = 0, witnessOff = 0, sLen = 0, len = 0, count;
    BRTransaction *tx = BRTransactionNew();
    BRTxInput *input;
    BRTxOutput *output;
    
    tx->version = (off + sizeof(uint32_t) <= bufLen) ? UInt32GetLE(&buf[off]) : 0;
    off += sizeof(uint32_t);
    tx->inCount = (size_t)BRVarInt(&buf[off], (off <= bufLen ? bufLen - off : 0), &len);
    off += len;
    if (tx->inCount == 0 && off + 1 <= bufLen) witnessFlag = buf[off++];
    
    if (witnessFlag) {
        tx->inCount = (size_t)BRVarInt(&buf[off], (off <= bufLen ? bufLen - off : 0), &len);
        off += len;
    }

    array_set_count(tx->inputs, tx->inCount);
    
    for (i = 0; off <= bufLen && i < tx->inCount; i++) {
        input = &tx->inputs[i];
        input->txHash = (off + sizeof(UInt256) <= bufLen) ? UInt256Get(&buf[off]) : UINT256_ZERO;
        off += sizeof(UInt256);
        input->index = (off + sizeof(uint32_t) <= bufLen) ? UInt32GetLE(&buf[off]) : 0;
        off += sizeof(uint32_t);
        sLen = (size_t)BRVarInt(&buf[off], (off <= bufLen ? bufLen - off : 0), &len);
        off += len;
        
        if (off + sLen <= bufLen && BRAddressFromScriptPubKey(NULL, 0, &buf[off], sLen) > 0) {
            BRTxInputSetScript(input, &buf[off], sLen);
            input->amount = (off + sLen + sizeof(uint64_t) <= bufLen) ? UInt64GetLE(&buf[off + sLen]) : 0;
            off += sizeof(uint64_t);
            isSigned = 0;
        }
        else if (off + sLen <= bufLen) BRTxInputSetSignature(input, &buf[off], sLen);
        
        off += sLen;
        if (! witnessFlag) BRTxInputSetWitness(input, &buf[off], 0); // set witness to empty byte array
        input->sequence = (off + sizeof(uint32_t) <= bufLen) ? UInt32GetLE(&buf[off]) : 0;
        off += sizeof(uint32_t);
    }
    
    tx->outCount = (size_t)BRVarInt(&buf[off], (off <= bufLen ? bufLen - off : 0), &len);
    off += len;
    array_set_count(tx->outputs, tx->outCount);
    
    for (i = 0; off <= bufLen && i < tx->outCount; i++) {
        output = &tx->outputs[i];
        output->amount = (off + sizeof(uint64_t) <= bufLen) ? UInt64GetLE(&buf[off]) : 0;
        off += sizeof(uint64_t);
        sLen = (size_t)BRVarInt(&buf[off], (off <= bufLen ? bufLen - off : 0), &len);
        off += len;
        if (off + sLen <= bufLen) BRTxOutputSetScript(output, &buf[off], sLen);
        off += sLen;
    }
    
    for (i = 0, witnessOff = off; witnessFlag && off <= bufLen && i < tx->inCount; i++) {
        input = &tx->inputs[i];
        count = (size_t)BRVarInt(&buf[off], (off <= bufLen ? bufLen - off : 0), &len);
        off += len;
        
        for (j = 0, sLen = 0; j < count; j++) {
            sLen += (size_t)BRVarInt(&buf[off + sLen], (off + sLen <= bufLen ? bufLen - (off + sLen) : 0), &len);
            sLen += len;
        }
        
        if (off + sLen <= bufLen) BRTxInputSetWitness(input, &buf[off], sLen);
        off += sLen;
    }
    
    tx->lockTime = (off + sizeof(uint32_t) <= bufLen) ? UInt32GetLE(&buf[off]) : 0;
    off += sizeof(uint32_t);
    
    if (tx->inCount == 0 || off > bufLen) {
        BRTransactionFree(tx);
        tx = NULL;
    }
    else if (isSigned && witnessFlag) {
        BRSHA256_2(&tx->wtxHash, buf, off);
        sBuf = malloc((witnessOff - 2) + sizeof(uint32_t));
        UInt32SetLE(sBuf, tx->version);
        memcpy(&sBuf[sizeof(uint32_t)], &buf[sizeof(uint32_t) + 2], witnessOff - (sizeof(uint32_t) + 2));
        UInt32SetLE(&sBuf[witnessOff - 2], tx->lockTime);
        BRSHA256_2(&tx->txHash, sBuf, (witnessOff - 2) + sizeof(uint32_t));
        free(sBuf);
    }
    else if (isSigned) {
        BRSHA256_2(&tx->txHash, buf, off);
        tx->wtxHash = tx->txHash;
    }
    
    return tx;
}

// returns number of bytes written to buf, or total bufLen needed if buf is NULL
// (tx->blockHeight and tx->timestamp are not serialized)
size_t BRTransactionSerialize(const BRTransaction *tx, uint8_t *buf, size_t bufLen)
{
    assert(tx != NULL);
    return (tx) ? _BRTransactionData(tx, buf, bufLen, SIZE_MAX, SIGHASH_ALL) : 0;
}

// adds an input to tx
void BRTransactionAddInput(BRTransaction *tx, UInt256 txHash, uint32_t index, uint64_t amount,
                           const uint8_t *script, size_t scriptLen, const uint8_t *signature, size_t sigLen,
                           const uint8_t *witness, size_t witLen, uint32_t sequence)
{
    BRTxInput input = { txHash, index, "", amount, NULL, 0, NULL, 0, NULL, 0, sequence };

    assert(tx != NULL);
    assert(! UInt256IsZero(txHash));
    assert(script != NULL || scriptLen == 0);
    assert(signature != NULL || sigLen == 0);
    assert(witness != NULL || witLen == 0);
    
    if (tx) {
        if (script) BRTxInputSetScript(&input, script, scriptLen);
        if (signature) BRTxInputSetSignature(&input, signature, sigLen);
        if (witness) BRTxInputSetWitness(&input, witness, witLen);
        array_add(tx->inputs, input);
        tx->inCount = array_count(tx->inputs);
    }
}

// adds an output to tx
void BRTransactionAddOutput(BRTransaction *tx, uint64_t amount, const uint8_t *script, size_t scriptLen)
{
    BRTxOutput output = { "", amount, NULL, 0 };
    
    assert(tx != NULL);
    assert(script != NULL || scriptLen == 0);
    
    if (tx) {
        BRTxOutputSetScript(&output, script, scriptLen);
        array_add(tx->outputs, output);
        tx->outCount = array_count(tx->outputs);
    }
}

// shuffles order of tx outputs
void BRTransactionShuffleOutputs(BRTransaction *tx)
{
    assert(tx != NULL);
    
    for (uint32_t i = 0; tx && i + 1 < tx->outCount; i++) { // fischer-yates shuffle
        uint32_t j = i + BRRand((uint32_t)tx->outCount - i);
        BRTxOutput t;
        
        if (j != i) {
            t = tx->outputs[i];
            tx->outputs[i] = tx->outputs[j];
            tx->outputs[j] = t;
        }
    }
}

// size in bytes if signed, or estimated size assuming compact pubkey sigs
size_t BRTransactionSize(const BRTransaction *tx)
{
    BRTxInput *input;
    size_t size, witSize = 0;

    assert(tx != NULL);
    size = (tx) ? 8 + BRVarIntSize(tx->inCount) + BRVarIntSize(tx->outCount) : 0;
    
    for (size_t i = 0; tx && i < tx->inCount; i++) {
        input = &tx->inputs[i];
        
        if (input->signature && input->witness) {
            size += sizeof(UInt256) + sizeof(uint32_t) + BRVarIntSize(input->sigLen) + input->sigLen + sizeof(uint32_t);
            witSize += input->witLen;
        }
        else if (input->script && input->scriptLen > 0 && input->script[0] == OP_0) { // estimated P2WPKH signature size
            witSize += TX_INPUT_SIZE;
        }
        else size += TX_INPUT_SIZE; // estimated P2PKH signature size
    }
    
    for (size_t i = 0; tx && i < tx->outCount; i++) {
        size += sizeof(uint64_t) + BRVarIntSize(tx->outputs[i].scriptLen) + tx->outputs[i].scriptLen;
    }
    
    if (witSize > 0) witSize += 2 + tx->inCount;
    return size + witSize;
}

// virtual transaction size as defined by BIP141: https://github.com/b_itcoin/bips/blob/master/bip-0141.mediawiki
size_t BRTransactionVSize(const BRTransaction *tx)
{
    BRTxInput *input;
    size_t size, witSize = 0;
    
    assert(tx != NULL);
    size = (tx) ? 8 + BRVarIntSize(tx->inCount) + BRVarIntSize(tx->outCount) : 0;
    
    for (size_t i = 0; i < tx->inCount; i++) {
        input = &tx->inputs[i];
        
        if (input->signature && input->witness) {
            size += sizeof(UInt256) + sizeof(uint32_t) + BRVarIntSize(input->sigLen) + input->sigLen + sizeof(uint32_t);
            witSize += tx->inputs[i].witLen;
        }
        else if (input->script && input->scriptLen > 0 && input->script[0] == OP_0) { // estimated P2WPKH signature size
            witSize += TX_INPUT_SIZE;
        }
        else size += TX_INPUT_SIZE; // estimated P2PKH signature size
    }
    
    for (size_t i = 0; i < tx->outCount; i++) {
        size += sizeof(uint64_t) + BRVarIntSize(tx->outputs[i].scriptLen) + tx->outputs[i].scriptLen;
    }
    
    if (witSize > 0) witSize += 2 + tx->inCount;
    return (size*4 + witSize + 3)/4;
}

// minimum transaction fee needed for tx to relay across the bitcoin network (defid 0.12 default min-relay fee-rate)
uint64_t BRTransactionStandardFee(const BRTransaction *tx)
{
    assert(tx != NULL);
    return BRTransactionVSize(tx)*TX_FEE_PER_KB/1000;
}

// checks if all signatures exist, but does not verify them
int BRTransactionIsSigned(const BRTransaction *tx)
{
    assert(tx != NULL);
    
    for (size_t i = 0; tx && i < tx->inCount; i++) {
        if (! tx->inputs[i].signature || ! tx->inputs[i].witness) return 0;
    }

    return (tx) ? 1 : 0;
}

// adds signatures to any inputs with NULL signatures that can be signed with any keys
// forkId is 0 for bitcoin, 0x40 for b-cash, 0x4f for b-gold
// returns true if tx is signed
int BRTransactionSign(BRTransaction *tx, int forkId, BRKey keys[], size_t keysCount)
{
    UInt160 pkh[keysCount];
    size_t i, j;
    
    assert(tx != NULL);
    assert(keys != NULL || keysCount == 0);
    
    for (i = 0; tx && i < keysCount; i++) {
        pkh[i] = BRKeyHash160(&keys[i]);
    }
    
    for (i = 0; tx && i < tx->inCount; i++) {
        BRTxInput *input = &tx->inputs[i];
        const uint8_t *hash = BRScriptPKH(input->script, input->scriptLen);
        
        j = 0;
        while (j < keysCount && (! hash || ! UInt160Eq(pkh[j], UInt160Get(hash)))) j++;
        if (j >= keysCount) continue;
        
        const uint8_t *elems[BRScriptElements(NULL, 0, input->script, input->scriptLen)];
        size_t elemsCount = BRScriptElements(elems, sizeof(elems)/sizeof(*elems), input->script, input->scriptLen);
        uint8_t pubKey[BRKeyPubKey(&keys[j], NULL, 0)];
        size_t pkLen = BRKeyPubKey(&keys[j], pubKey, sizeof(pubKey));
        uint8_t sig[73], script[1 + sizeof(sig) + 1 + sizeof(pubKey)];
        size_t sigLen, scriptLen;
        UInt256 md = UINT256_ZERO;
        
        if (elemsCount == 2 && *elems[0] == OP_0 && *elems[1] == 20) { // pay-to-witness-pubkey-hash
            uint8_t data[_BRTransactionWitnessData(tx, NULL, 0, i, forkId | SIGHASH_ALL)];
            size_t dataLen = _BRTransactionWitnessData(tx, data, sizeof(data), i, forkId | SIGHASH_ALL);
            
            BRSHA256_2(&md, data, dataLen);
            sigLen = BRKeySign(&keys[j], sig, sizeof(sig) - 1, md);
            sig[sigLen++] = forkId | SIGHASH_ALL;
            scriptLen = BRScriptPushData(script, sizeof(script), sig, sigLen);
            scriptLen += BRScriptPushData(&script[scriptLen], sizeof(script) - scriptLen, pubKey, pkLen);
            BRTxInputSetSignature(input, script, 0);
            BRTxInputSetWitness(input, script, scriptLen);
        }
        else if (elemsCount >= 2 && *elems[elemsCount - 2] == OP_EQUALVERIFY) { // pay-to-pubkey-hash
            uint8_t data[_BRTransactionData(tx, NULL, 0, i, forkId | SIGHASH_ALL)];
            size_t dataLen = _BRTransactionData(tx, data, sizeof(data), i, forkId | SIGHASH_ALL);
            
            BRSHA256_2(&md, data, dataLen);
            sigLen = BRKeySign(&keys[j], sig, sizeof(sig) - 1, md);
            sig[sigLen++] = forkId | SIGHASH_ALL;
            scriptLen = BRScriptPushData(script, sizeof(script), sig, sigLen);
            scriptLen += BRScriptPushData(&script[scriptLen], sizeof(script) - scriptLen, pubKey, pkLen);
            BRTxInputSetSignature(input, script, scriptLen);
            BRTxInputSetWitness(input, script, 0);
        }
        else { // pay-to-pubkey
            uint8_t data[_BRTransactionData(tx, NULL, 0, i, forkId | SIGHASH_ALL)];
            size_t dataLen = _BRTransactionData(tx, data, sizeof(data), i, forkId | SIGHASH_ALL);

            BRSHA256_2(&md, data, dataLen);
            sigLen = BRKeySign(&keys[j], sig, sizeof(sig) - 1, md);
            sig[sigLen++] = forkId | SIGHASH_ALL;
            scriptLen = BRScriptPushData(script, sizeof(script), sig, sigLen);
            BRTxInputSetSignature(input, script, scriptLen);
            BRTxInputSetWitness(input, script, 0);
        }
    }
    
    if (tx && BRTransactionIsSigned(tx)) {
        uint8_t data[BRTransactionSerialize(tx, NULL, 0)];
        size_t len = BRTransactionSerialize(tx, data, sizeof(data));
        BRTransaction *t = BRTransactionParse(data, len);
        
        if (t) tx->txHash = t->txHash, tx->wtxHash = t->wtxHash;
        if (t) BRTransactionFree(t);
        return 1;
    }
    else return 0;
}

// true if tx meets IsStandard() rules: https://b_itcoin.org/en/developer-guide#standard-transactions
int BRTransactionIsStandard(const BRTransaction *tx)
{
    int r = 1;
    
    // TODO: XXX implement
    
    return r;
}

// frees memory allocated for tx
void BRTransactionFree(BRTransaction *tx)
{
    assert(tx != NULL);
    
    if (tx) {
        for (size_t i = 0; i < tx->inCount; i++) {
            BRTxInputSetScript(&tx->inputs[i], NULL, 0);
            BRTxInputSetSignature(&tx->inputs[i], NULL, 0);
            BRTxInputSetWitness(&tx->inputs[i], NULL, 0);
        }

        for (size_t i = 0; i < tx->outCount; i++) {
            BRTxOutputSetScript(&tx->outputs[i], NULL, 0);
        }

        array_free(tx->outputs);
        array_free(tx->inputs);
        free(tx);
    }
}

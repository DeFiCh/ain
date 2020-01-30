//
//  BRAddress.h
//
//  Created by Aaron Voisine on 9/18/15.
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

#ifndef BRAddress_h
#define BRAddress_h

#include "BRCrypto.h"
#include <string.h>
#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// bitcoin script opcodes: https://en.b_itcoin.it/wiki/Script#Constants
#define OP_0           0x00
#define OP_PUSHDATA1   0x4c
#define OP_PUSHDATA2   0x4d
#define OP_PUSHDATA4   0x4e
#define OP_1NEGATE     0x4f
#define OP_1           0x51
#define OP_16          0x60
#define OP_DUP         0x76
#define OP_EQUAL       0x87
#define OP_EQUALVERIFY 0x88
#define OP_HASH160     0xa9
#define OP_CHECKSIG    0xac

// reads a varint from buf and stores its length in intLen if intLen is non-NULL
// returns the varint value
uint64_t BRVarInt(const uint8_t *buf, size_t bufLen, size_t *intLen);

// writes i to buf as a varint and returns the number of bytes written, or bufLen needed if buf is NULL
size_t BRVarIntSet(uint8_t *buf, size_t bufLen, uint64_t i);

// returns the number of bytes needed to encode i as a varint
size_t BRVarIntSize(uint64_t i);

// parses script and writes an array of pointers to the script elements (opcodes and data pushes) to elems
// returns the number of elements written, or elemsCount needed if elems is NULL
size_t BRScriptElements(const uint8_t *elems[], size_t elemsCount, const uint8_t *script, size_t scriptLen);

// given a data push script element, returns a pointer to the start of the data and writes its length to dataLen
const uint8_t *BRScriptData(const uint8_t *elem, size_t *dataLen);

// writes a data push script element to script
// returns the number of bytes written, or scriptLen needed if script is NULL
size_t BRScriptPushData(uint8_t *script, size_t scriptLen, const uint8_t *data, size_t dataLen);

// returns a pointer to the 20byte pubkey hash, or NULL if none
const uint8_t *BRScriptPKH(const uint8_t *script, size_t scriptLen);
    
typedef struct {
    char s[75];
} BRAddress;

#define BR_ADDRESS_NONE ((const BRAddress) {\
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"\
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" })

// writes the bitcoin address for a scriptPubKey to addr
// returns the number of bytes written, or addrLen needed if addr is NULL
size_t BRAddressFromScriptPubKey(char *addr, size_t addrLen, const uint8_t *script, size_t scriptLen);

// writes the bitcoin address for a scriptSig to addr
// returns the number of bytes written, or addrLen needed if addr is NULL
size_t BRAddressFromScriptSig(char *addr, size_t addrLen, const uint8_t *script, size_t scriptLen);

// writes the bitcoin address for a witness to addr
// returns the number of bytes written, or addrLen needed if addr is NULL
size_t BRAddressFromWitness(char *addr, size_t addrLen, const uint8_t *witness, size_t witLen);

// writes the bech32 pay-to-witness-pubkey-hash address for a hash160 to addr
// returns the number of bytes written, or addrLen needed if addr is NULL
size_t BRAddressFromHash160(char *addr, size_t addrLen, const void *md20);

// writes the scriptPubKey for addr to script
// returns the number of bytes written, or scriptLen needed if script is NULL
size_t BRAddressScriptPubKey(uint8_t *script, size_t scriptLen, const char *addr);

// writes the 20 byte hash160 of addr to md20 and returns true on success
int BRAddressHash160(void *md20, const char *addr);

// returns true if addr is a valid bitcoin address
int BRAddressIsValid(const char *addr);

// returns a hash value for addr suitable for use in a hashtable
inline static size_t BRAddressHash(const void *addr)
{
    return BRMurmur3_32(addr, strlen((const char *)addr), 0);
}

// true if addr and otherAddr are equal
inline static int BRAddressEq(const void *addr, const void *otherAddr)
{
    return (addr == otherAddr || strncmp((const char *)addr, (const char *)otherAddr, sizeof(BRAddress)) == 0);
}

#ifdef __cplusplus
}
#endif

#endif // BRAddress_h

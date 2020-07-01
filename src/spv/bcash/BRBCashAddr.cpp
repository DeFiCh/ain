//
//  BRBCashAddr.c
//  breadwallet-core
//
//  Created by Aaron Voisine on 1/20/18.
//  Copyright (c) 2018 breadwallet LLC
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

#include "BRBCashAddr.h"
#include "support/BRAddress.h"
#include "support/BRBase58.h"
#include "support/BRCrypto.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

// b-cash address format: https://github.com/bitcoincashorg/spec/blob/master/cashaddr.md

#define BCASH_PUBKEY_ADDRESS 28
#define BCASH_SCRIPT_ADDRESS 40

#define BITCOIN_PUBKEY_ADDRESS      0
#define BITCOIN_SCRIPT_ADDRESS      5
#define BITCOIN_PUBKEY_ADDRESS_TEST 111
#define BITCOIN_SCRIPT_ADDRESS_TEST 196

#define polymod(x) ((((x) & 0x07ffffffff) << 5) ^ (-(((x) >> 35) & 1) & 0x98f2bc8e61) ^\
    (-(((x) >> 36) & 1) & 0x79b76d99e2) ^ (-(((x) >> 37) & 1) & 0xf33e5fb3c4) ^\
    (-(((x) >> 38) & 1) & 0xae2eabe2a8) ^ (-(((x) >> 39) & 1) & 0x1e4f43e470))

// returns the number of bytes written to data21 (maximum of 21)
static size_t _BRBCashAddrDecode(char *hrp12, uint8_t *data21, const char *addr)
{
    size_t i, j, bufLen, addrLen = (addr) ? strlen(addr) : 0, sep = addrLen;
    uint64_t x, chk = 1;
    uint8_t c, buf[22], upper = 0, lower = 0;
    
    assert(hrp12 != NULL);
    assert(data21 != NULL);
    assert(addr != NULL);
    
    for (i = 0; i < addrLen; i++) {
        if (addr[i] < 33 || addr[i] > 126) return 0;
        if (islower(addr[i])) lower = 1;
        if (isupper(addr[i])) upper = 1;
    }
    
    while (sep > 0 && addr[sep] != ':') sep--;
    if (sep > 11 || sep + 34 + 8 > addrLen || (upper && lower)) return 0;
    for (i = 0; i < sep; i++) chk = polymod(chk) ^ (addr[i] & 0x1f);
    chk = polymod(chk);
    memset(buf, 0, sizeof(buf));
    
    for (i = sep + 1, j = 0; i < addrLen; i++, j++) {
        switch (tolower(addr[i])) {
            case 'q': c = 0;  break; case 'p': c = 1;  break; case 'z': c = 2;  break; case 'r': c = 3;  break;
            case 'y': c = 4;  break; case '9': c = 5;  break; case 'x': c = 6;  break; case '8': c = 7;  break;
            case 'g': c = 8;  break; case 'f': c = 9;  break; case '2': c = 10; break; case 't': c = 11; break;
            case 'v': c = 12; break; case 'd': c = 13; break; case 'w': c = 14; break; case '0': c = 15; break;
            case 's': c = 16; break; case '3': c = 17; break; case 'j': c = 18; break; case 'n': c = 19; break;
            case '5': c = 20; break; case '4': c = 21; break; case 'k': c = 22; break; case 'h': c = 23; break;
            case 'c': c = 24; break; case 'e': c = 25; break; case '6': c = 26; break; case 'm': c = 27; break;
            case 'u': c = 28; break; case 'a': c = 29; break; case '7': c = 30; break; case 'l': c = 31; break;
            default: return 0; // invalid bech32 digit
        }
        
        chk = polymod(chk) ^ c;
        if (j >= 35 || i + 8 >= addrLen) continue;
        x = (j % 8)*5 - ((j % 8)*5/8)*8;
        buf[(j/8)*5 + (j % 8)*5/8] |= (c << 3) >> x;
        if (x > 3) buf[(j/8)*5 + (j % 8)*5/8 + 1] |= c << (11 - x);
    }
    
    bufLen = (addrLen - (sep + 8))*5/8;
    if (hrp12 == NULL || data21 == NULL || chk != 1 || bufLen != 21) return 0;
    for (i = 0; i < sep; i++) hrp12[i] = tolower(addr[i]);
    hrp12[sep] = '\0';
    memcpy(data21, buf, bufLen);
    return bufLen;
}

// returns the number of bytes written to addr55 (maximum of 55)
static size_t _BRBCashAddrEncode(char *addr55, const char *hrp, const uint8_t data[], size_t dataLen)
{
    static const char chars[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    char addr[55];
    uint64_t x, chk = 1;
    uint8_t a, b = 0, c = 0;
    size_t i, j;
    
    assert(addr55 != NULL);
    assert(hrp != NULL);
    assert(data != NULL);
    assert(dataLen == 21);
    
    for (i = 0; hrp && hrp[i]; i++) {
        if (i > 12 || hrp[i] < 33 || hrp[i] > 126 || isupper(hrp[i])) return 0;
        chk = polymod(chk) ^ (hrp[i] & 0x1f);
        addr[i] = hrp[i];
    }
    
    chk = polymod(chk);
    addr[i++] = ':';
    if (i < 1 || data == NULL || dataLen != 21) return 0;
    
    for (j = 0; j <= dataLen; j++) {
        a = b, b = (j < dataLen) ? data[j] : 0;
        x = (j % 5)*8 - ((j % 5)*8/5)*5;
        c = ((a << (5 - x)) | (b >> (3 + x))) & 0x1f;
        if (j < dataLen || j % 5 > 0) chk = polymod(chk) ^ c, addr[i++] = chars[c];
        if (x >= 2) c = (b >> (x - 2)) & 0x1f;
        if (x >= 2 && j < dataLen) chk = polymod(chk) ^ c, addr[i++] = chars[c];
    }
    
    for (j = 0; j < 8; j++) chk = polymod(chk);
    chk ^= 1;
    for (j = 0; j < 8; ++j) addr[i++] = chars[(chk >> ((7 - j)*5)) & 0x1f];
    addr[i++] = '\0';
    memcpy(addr55, addr, i);
    return i;
}

// returns the number of bytes written to bitcoinAddr36 (maximum of 36)
size_t BRBCashAddrDecode(char *bitcoinAddr36, const char *bCashAddr)
{
    uint8_t data[21], ver = UINT8_MAX;
    char bchaddr[55] = "bitcoincash:", bchtest[55] = "bchtest:", bchreg[55] = "bchreg:",
         BCHaddr[55] = "BITCOINCASH:", BCHtest[55] = "BCHTEST:", BCHreg[55] = "BCHREG:", hrp[12];
    
    assert(bitcoinAddr36 != NULL);
    assert(bCashAddr != NULL);
    
    if (_BRBCashAddrDecode(hrp, data, bCashAddr) == 21) {
        if (strcmp(hrp, "bitcoincash") == 0) {
            if (data[0] == 0x00) ver = BITCOIN_PUBKEY_ADDRESS;
            if (data[0] == 0x08) ver = BITCOIN_SCRIPT_ADDRESS;
        }
        else if (strcmp(hrp, "bchtest") == 0 || strcmp(hrp, "bchreg") == 0) {
            if (data[0] == 0x00) ver = BITCOIN_PUBKEY_ADDRESS_TEST;
            if (data[0] == 0x08) ver = BITCOIN_SCRIPT_ADDRESS_TEST;
        }
    }
    else if (BRBase58CheckDecode(data, sizeof(data), bCashAddr) == 21) {
        if (data[0] == BCASH_PUBKEY_ADDRESS) ver = BITCOIN_PUBKEY_ADDRESS;
        if (data[0] == BCASH_SCRIPT_ADDRESS) ver = BITCOIN_SCRIPT_ADDRESS;
    }
    else { // try adding various address prefixes
        strncpy(&bchaddr[12], bCashAddr, 42), bchaddr[54] = '\0';
        strncpy(&bchtest[8], bCashAddr, 46), bchtest[54] = '\0';
        strncpy(&bchreg[7], bCashAddr, 47), bchreg[54] = '\0';
        strncpy(&BCHaddr[12], bCashAddr, 42), BCHaddr[54] = '\0';
        strncpy(&BCHtest[8], bCashAddr, 46), BCHtest[54] = '\0';
        strncpy(&BCHreg[7], bCashAddr, 47), BCHreg[54] = '\0';

        if (_BRBCashAddrDecode(hrp, data, bchaddr) == 21 || _BRBCashAddrDecode(hrp, data, BCHaddr) == 21) {
            if (data[0] == 0x00) ver = BITCOIN_PUBKEY_ADDRESS;
            if (data[0] == 0x08) ver = BITCOIN_SCRIPT_ADDRESS;
        }
        else if (_BRBCashAddrDecode(hrp, data, bchtest) == 21 || _BRBCashAddrDecode(hrp, data, BCHtest) == 21 ||
                 _BRBCashAddrDecode(hrp, data, bchreg) == 21 || _BRBCashAddrDecode(hrp, data, BCHreg) == 21) {
            if (data[0] == 0x00) ver = BITCOIN_PUBKEY_ADDRESS_TEST;
            if (data[0] == 0x08) ver = BITCOIN_SCRIPT_ADDRESS_TEST;
        }
    }

    data[0] = ver;
    return (ver != UINT8_MAX) ? BRBase58CheckEncode(bitcoinAddr36, 36, data, 21) : 0;
}

// returns the number of bytes written to bCashAddr55 (maximum of 55)
size_t BRBCashAddrEncode(char *bCashAddr55, const char *bitcoinAddr)
{
    uint8_t data[21], ver = 0;
    const char *hrp = NULL;
    
    assert(bCashAddr55 != NULL);
    assert(bitcoinAddr != NULL);
    if (BRBase58CheckDecode(data, sizeof(data), bitcoinAddr) != 21) return 0;
    if (data[0] == BITCOIN_PUBKEY_ADDRESS) ver = 0x00, hrp = "bitcoincash";
    if (data[0] == BITCOIN_SCRIPT_ADDRESS) ver = 0x08, hrp = "bitcoincash";
    if (data[0] == BITCOIN_PUBKEY_ADDRESS_TEST) ver = 0x00, hrp = "bchtest";
    if (data[0] == BITCOIN_SCRIPT_ADDRESS_TEST) ver = 0x08, hrp = "bchtest";
    data[0] = ver;
    return _BRBCashAddrEncode(bCashAddr55, hrp, data, 21);
}


//
//  BRBech32.c
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

#include "BRBech32.h"
#include "BRAddress.h"
#include "BRCrypto.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

// bech32 address format: https://github.com/b_itcoin/bips/blob/master/bip-0173.mediawiki

#define polymod(x) ((((x) & 0x1ffffff) << 5) ^ (-(((x) >> 25) & 1) & 0x3b6a57b2) ^\
                    (-(((x) >> 26) & 1) & 0x26508e6d) ^ (-(((x) >> 27) & 1) & 0x1ea119fa) ^\
                    (-(((x) >> 28) & 1) & 0x3d4233dd) ^ (-(((x) >> 29) & 1) & 0x2a1462b3))

// returns the number of bytes written to data42 (maximum of 42)
size_t BRBech32Decode(char *hrp84, uint8_t *data42, const char *addr)
{
    size_t i, j, bufLen, addrLen, sep;
    uint32_t x, chk = 1;
    uint8_t c, ver = 0xff, buf[52], upper = 0, lower = 0;

    assert(hrp84 != NULL);
    assert(data42 != NULL);
    assert(addr != NULL);
    
    for (i = 0; addr && addr[i]; i++) {
        if (addr[i] < 33 || addr[i] > 126) return 0;
        if (islower(addr[i])) lower = 1;
        if (isupper(addr[i])) upper = 1;
    }

    addrLen = sep = i;
    while (sep > 0 && addr[sep] != '1') sep--;
    if (addrLen < 8 || addrLen > 90 || sep < 1 || sep + 2 + 6 > addrLen || (upper && lower)) return 0;
    for (i = 0; i < sep; i++) chk = polymod(chk) ^ (tolower(addr[i]) >> 5);
    chk = polymod(chk);
    for (i = 0; i < sep; i++) chk = polymod(chk) ^ (addr[i] & 0x1f);
    memset(buf, 0, sizeof(buf));

    for (i = sep + 1, j = -1; i < addrLen; i++, j++) {
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
        if (j == -1) ver = c;
        if (j == -1 || i + 6 >= addrLen) continue;
        x = (j % 8)*5 - ((j % 8)*5/8)*8;
        buf[(j/8)*5 + (j % 8)*5/8] |= (c << 3) >> x;
        if (x > 3) buf[(j/8)*5 + (j % 8)*5/8 + 1] |= c << (11 - x);
    }
    
    bufLen = (addrLen - (sep + 2 + 6))*5/8;
    if (hrp84 == NULL || data42 == NULL || chk != 1 || ver > 16 || bufLen < 2 || bufLen > 40) return 0;
    assert(sep < 84);
    for (i = 0; i < sep; i++) hrp84[i] = tolower(addr[i]);
    hrp84[sep] = '\0';
    data42[0] = (ver == 0) ? OP_0 : ver + OP_1 - 1;
    data42[1] = bufLen;
    assert(bufLen <= 40);
    memcpy(&data42[2], buf, bufLen);
    return 2 + bufLen;
}

// data must contain a valid BIP141 witness program
// returns the number of bytes written to addr91 (maximum of 91)
size_t BRBech32Encode(char *addr91, const char *hrp, const uint8_t data[])
{
    static const char chars[] = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    char addr[91];
    uint32_t x, chk = 1;
    uint8_t ver, a, b = 0, c = 0;
    size_t i, j, len;

    assert(addr91 != NULL);
    assert(hrp != NULL);
    assert(data != NULL);
    
    for (i = 0; hrp && hrp[i]; i++) {
        if (i > 83 || hrp[i] < 33 || hrp[i] > 126 || isupper(hrp[i])) return 0;
        chk = polymod(chk) ^ (hrp[i] >> 5);
        addr[i] = hrp[i];
    }
    
    chk = polymod(chk);
    for (j = 0; j < i; j++) chk = polymod(chk) ^ (hrp[j] & 0x1f);
    addr[i++] = '1';
    if (i < 1 || data == NULL || (data[0] > OP_0 && data[0] < OP_1)) return 0;
    ver = (data[0] >= OP_1) ? data[0] + 1 - OP_1 : 0;
    len = data[1];
    if (ver > 16 || len < 2 || len > 40 || i + 1 + len + 6 >= 91) return 0;
    chk = polymod(chk) ^ ver;
    addr[i++] = chars[ver];
    
    for (j = 0; j <= len; j++) {
        a = b, b = (j < len) ? data[2 + j] : 0;
        x = (j % 5)*8 - ((j % 5)*8/5)*5;
        c = ((a << (5 - x)) | (b >> (3 + x))) & 0x1f;
        if (j < len || j % 5 > 0) chk = polymod(chk) ^ c, addr[i++] = chars[c];
        if (x >= 2) c = (b >> (x - 2)) & 0x1f;
        if (x >= 2 && j < len) chk = polymod(chk) ^ c, addr[i++] = chars[c];
    }
    
    for (j = 0; j < 6; j++) chk = polymod(chk);
    chk ^= 1;
    for (j = 0; j < 6; ++j) addr[i++] = chars[(chk >> ((5 - j)*5)) & 0x1f];
    addr[i++] = '\0';
    memcpy(addr91, addr, i);
    return i;
}


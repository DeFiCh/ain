//
//  BRBase58.c
//  breadwallet-core
//
//  Created by Aaron Voisine on 9/15/15.
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

#include "BRBase58.h"
#include "BRCrypto.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// base58 and base58check encoding: https://en.b_itcoin.it/wiki/Base58Check_encoding

// returns the number of characters written to str including NULL terminator, or total strLen needed if str is NULL
size_t BRBase58Encode(char *str, size_t strLen, const uint8_t *data, size_t dataLen)
{
    static const char chars[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    size_t i, j, len, zcount = 0;
    
    assert(data != NULL);
    while (zcount < dataLen && data && data[zcount] == 0) zcount++; // count leading zeroes

    uint8_t buf[(dataLen - zcount)*138/100 + 1]; // log(256)/log(58), rounded up
    
    memset(buf, 0, sizeof(buf));
    
    for (i = zcount; data && i < dataLen; i++) {
        uint32_t carry = data[i];
        
        for (j = sizeof(buf); j > 0; j--) {
            carry += (uint32_t)buf[j - 1] << 8;
            buf[j - 1] = carry % 58;
            carry /= 58;
        }
        
        var_clean(&carry);
    }
    
    i = 0;
    while (i < sizeof(buf) && buf[i] == 0) i++; // skip leading zeroes
    len = (zcount + sizeof(buf) - i) + 1;

    if (str && len <= strLen) {
        while (zcount-- > 0) *(str++) = chars[0];
        while (i < sizeof(buf)) *(str++) = chars[buf[i++]];
        *str = '\0';
    }
    
    mem_clean(buf, sizeof(buf));
    return (! str || len <= strLen) ? len : 0;
}

// returns the number of bytes written to data, or total dataLen needed if data is NULL
size_t BRBase58Decode(uint8_t *data, size_t dataLen, const char *str)
{
    size_t i = 0, j, len, zcount = 0;
    
    assert(str != NULL);
    while (str && *str == '1') str++, zcount++; // count leading zeroes
    
    uint8_t buf[(str) ? strlen(str)*733/1000 + 1 : 0]; // log(58)/log(256), rounded up
    
    memset(buf, 0, sizeof(buf));
    
    while (str && *str) {
        uint32_t carry = *(const uint8_t *)(str++);
        
        switch (carry) {
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                carry -= '1';
                break;
                
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G': case 'H':
                carry += 9 - 'A';
                break;
                
            case 'J': case 'K': case 'L': case 'M': case 'N':
                carry += 17 - 'J';
                break;
                
            case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y':
            case 'Z':
                carry += 22 - 'P';
                break;
                
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i': case 'j':
            case 'k':
                carry += 33 - 'a';
                break;
                
            case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u': case 'v':
            case 'w': case 'x': case 'y': case 'z':
                carry += 44 - 'm';
                break;
                
            default:
                carry = UINT32_MAX;
        }
        
        if (carry >= 58) break; // invalid base58 digit
        
        for (j = sizeof(buf); j > 0; j--) {
            carry += (uint32_t)buf[j - 1]*58;
            buf[j - 1] = carry & 0xff;
            carry >>= 8;
        }
        
        var_clean(&carry);
    }
    
    while (i < sizeof(buf) && buf[i] == 0) i++; // skip leading zeroes
    len = zcount + sizeof(buf) - i;

    if (data && len <= dataLen) {
        if (zcount > 0) memset(data, 0, zcount);
        memcpy(&data[zcount], &buf[i], sizeof(buf) - i);
    }

    mem_clean(buf, sizeof(buf));
    return (! data || len <= dataLen) ? len : 0;
}

// returns the number of characters written to str including NULL terminator, or total strLen needed if str is NULL
size_t BRBase58CheckEncode(char *str, size_t strLen, const uint8_t *data, size_t dataLen)
{
    size_t len = 0, bufLen = dataLen + 256/8;
    uint8_t _buf[0x1000], *buf = (bufLen <= 0x1000) ? _buf : malloc(bufLen);

    assert(buf != NULL);
    assert(data != NULL || dataLen == 0);

    if (data || dataLen == 0) {
        memcpy(buf, data, dataLen);
        BRSHA256_2(&buf[dataLen], data, dataLen);
        len = BRBase58Encode(str, strLen, buf, dataLen + 4);
    }
    
    mem_clean(buf, bufLen);
    if (buf != _buf) free(buf);
    return len;
}

// returns the number of bytes written to data, or total dataLen needed if data is NULL
size_t BRBase58CheckDecode(uint8_t *data, size_t dataLen, const char *str)
{
    size_t len, bufLen = (str) ? strlen(str) : 0;
    uint8_t md[256/8], _buf[0x1000], *buf = (bufLen <= 0x1000) ? _buf : malloc(bufLen);

    assert(str != NULL);
    assert(buf != NULL);
    len = BRBase58Decode(buf, bufLen, str);
    
    if (len >= 4) {
        len -= 4;
        BRSHA256_2(md, buf, len);
        if (memcmp(&buf[len], md, sizeof(uint32_t)) != 0) len = 0; // verify checksum
        if (data && len <= dataLen) memcpy(data, buf, len);
    }
    else len = 0;
    
    mem_clean(buf, bufLen);
    if (buf != _buf) free(buf);
    return (! data || len <= dataLen) ? len : 0;
}

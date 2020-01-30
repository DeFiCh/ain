//
//  BRBase58.h
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

#ifndef BRBase58_h
#define BRBase58_h

#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// base58 and base58check encoding: https://en.b_itcoin.it/wiki/Base58Check_encoding

// returns the number of characters written to str including NULL terminator, or total strLen needed if str is NULL
size_t BRBase58Encode(char *str, size_t strLen, const uint8_t *data, size_t dataLen);

// returns the number of bytes written to data, or total dataLen needed if data is NULL
size_t BRBase58Decode(uint8_t *data, size_t dataLen, const char *str);

// returns the number of characters written to str including NULL terminator, or total strLen needed if str is NULL
size_t BRBase58CheckEncode(char *str, size_t strLen, const uint8_t *data, size_t dataLen);

// returns the number of bytes written to data, or total dataLen needed if data is NULL
size_t BRBase58CheckDecode(uint8_t *data, size_t dataLen, const char *str);

#ifdef __cplusplus
}
#endif

#endif // BRBase58_h

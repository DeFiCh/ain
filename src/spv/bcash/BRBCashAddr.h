//
//  BRBCashAddr.h
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

#ifndef BRBCashAddr_h
#define BRBCashAddr_h

#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// b-cash address format: https://github.com/bitcoincashorg/spec/blob/master/cashaddr.md

// returns the number of bytes written to bitcoinAddr36 (maximum of 36)
size_t BRBCashAddrDecode(char *bitcoinAddr36, const char *bCashAddr);

// returns the number of bytes written to bCashAddr55 (maximum of 55)
size_t BRBCashAddrEncode(char *bCashAddr55, const char *bitcoinAddr);

#ifdef __cplusplus
}
#endif

#endif // BRBCashAddr_h

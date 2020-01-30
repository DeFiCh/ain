//
//  BRBech32.h
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

#ifndef BRBech32_h
#define BRBech32_h

#include <stddef.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// bech32 address format: https://github.com/b_itcoin/bips/blob/master/bip-0173.mediawiki

// returns the number of bytes written to data42 (maximum of 42)
size_t BRBech32Decode(char *hrp84, uint8_t *data42, const char *addr);

// data must contain a valid BIP141 witness program
// returns the number of bytes written to addr91 (maximum of 91)
size_t BRBech32Encode(char *addr91, const char *hrp, const uint8_t data[]);

#ifdef __cplusplus
}
#endif

#endif // BRBech32_h

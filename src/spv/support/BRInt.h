//
//  BRInt.h
//
//  Created by Aaron Voisine on 8/16/15.
//  Copyright (c) 2015 breadwallet LLC.
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

#ifndef BRInt_h
#define BRInt_h

#include "BRLargeInt.h"

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif


#define UINT128_ZERO ((const UInt128) { .u64 = { 0, 0 } })
#define UINT160_ZERO ((const UInt160) { .u32 = { 0, 0, 0, 0, 0 } })
#define UINT256_ZERO ((const UInt256) { .u64 = { 0, 0, 0, 0 } })
#define UINT512_ZERO ((const UInt512) { .u64 = { 0, 0, 0, 0, 0, 0, 0, 0 } })

// hex encoding/decoding

#define u256hex(u) ((const char[]) {\
    _hexc((u).u8[ 0] >> 4), _hexc((u).u8[ 0]), _hexc((u).u8[ 1] >> 4), _hexc((u).u8[ 1]),\
    _hexc((u).u8[ 2] >> 4), _hexc((u).u8[ 2]), _hexc((u).u8[ 3] >> 4), _hexc((u).u8[ 3]),\
    _hexc((u).u8[ 4] >> 4), _hexc((u).u8[ 4]), _hexc((u).u8[ 5] >> 4), _hexc((u).u8[ 5]),\
    _hexc((u).u8[ 6] >> 4), _hexc((u).u8[ 6]), _hexc((u).u8[ 7] >> 4), _hexc((u).u8[ 7]),\
    _hexc((u).u8[ 8] >> 4), _hexc((u).u8[ 8]), _hexc((u).u8[ 9] >> 4), _hexc((u).u8[ 9]),\
    _hexc((u).u8[10] >> 4), _hexc((u).u8[10]), _hexc((u).u8[11] >> 4), _hexc((u).u8[11]),\
    _hexc((u).u8[12] >> 4), _hexc((u).u8[12]), _hexc((u).u8[13] >> 4), _hexc((u).u8[13]),\
    _hexc((u).u8[14] >> 4), _hexc((u).u8[14]), _hexc((u).u8[15] >> 4), _hexc((u).u8[15]),\
    _hexc((u).u8[16] >> 4), _hexc((u).u8[16]), _hexc((u).u8[17] >> 4), _hexc((u).u8[17]),\
    _hexc((u).u8[18] >> 4), _hexc((u).u8[18]), _hexc((u).u8[19] >> 4), _hexc((u).u8[19]),\
    _hexc((u).u8[20] >> 4), _hexc((u).u8[20]), _hexc((u).u8[21] >> 4), _hexc((u).u8[21]),\
    _hexc((u).u8[22] >> 4), _hexc((u).u8[22]), _hexc((u).u8[23] >> 4), _hexc((u).u8[23]),\
    _hexc((u).u8[24] >> 4), _hexc((u).u8[24]), _hexc((u).u8[25] >> 4), _hexc((u).u8[25]),\
    _hexc((u).u8[26] >> 4), _hexc((u).u8[26]), _hexc((u).u8[27] >> 4), _hexc((u).u8[27]),\
    _hexc((u).u8[28] >> 4), _hexc((u).u8[28]), _hexc((u).u8[29] >> 4), _hexc((u).u8[29]),\
    _hexc((u).u8[30] >> 4), _hexc((u).u8[30]), _hexc((u).u8[31] >> 4), _hexc((u).u8[31]), '\0' })

#define uint256(s) ((const UInt256) { .u8 = {\
    (_hexu((s)[ 0]) << 4) | _hexu((s)[ 1]), (_hexu((s)[ 2]) << 4) | _hexu((s)[ 3]),\
    (_hexu((s)[ 4]) << 4) | _hexu((s)[ 5]), (_hexu((s)[ 6]) << 4) | _hexu((s)[ 7]),\
    (_hexu((s)[ 8]) << 4) | _hexu((s)[ 9]), (_hexu((s)[10]) << 4) | _hexu((s)[11]),\
    (_hexu((s)[12]) << 4) | _hexu((s)[13]), (_hexu((s)[14]) << 4) | _hexu((s)[15]),\
    (_hexu((s)[16]) << 4) | _hexu((s)[17]), (_hexu((s)[18]) << 4) | _hexu((s)[19]),\
    (_hexu((s)[20]) << 4) | _hexu((s)[21]), (_hexu((s)[22]) << 4) | _hexu((s)[23]),\
    (_hexu((s)[24]) << 4) | _hexu((s)[25]), (_hexu((s)[26]) << 4) | _hexu((s)[27]),\
    (_hexu((s)[28]) << 4) | _hexu((s)[29]), (_hexu((s)[30]) << 4) | _hexu((s)[31]),\
    (_hexu((s)[32]) << 4) | _hexu((s)[33]), (_hexu((s)[34]) << 4) | _hexu((s)[35]),\
    (_hexu((s)[36]) << 4) | _hexu((s)[37]), (_hexu((s)[38]) << 4) | _hexu((s)[39]),\
    (_hexu((s)[40]) << 4) | _hexu((s)[41]), (_hexu((s)[42]) << 4) | _hexu((s)[43]),\
    (_hexu((s)[44]) << 4) | _hexu((s)[45]), (_hexu((s)[46]) << 4) | _hexu((s)[47]),\
    (_hexu((s)[48]) << 4) | _hexu((s)[49]), (_hexu((s)[50]) << 4) | _hexu((s)[51]),\
    (_hexu((s)[52]) << 4) | _hexu((s)[53]), (_hexu((s)[54]) << 4) | _hexu((s)[55]),\
    (_hexu((s)[56]) << 4) | _hexu((s)[57]), (_hexu((s)[58]) << 4) | _hexu((s)[59]),\
    (_hexu((s)[60]) << 4) | _hexu((s)[61]), (_hexu((s)[62]) << 4) | _hexu((s)[63]) } })

#define _hexc(u) ((char) (((u) & 0x0f) + ((((u) & 0x0f) <= 9) ? '0' : 'a' - 0x0a)))
#define _hexu(c) (((c) >= '0' && (c) <= '9') ? (c) - '0' : ((c) >= 'a' && (c) <= 'f') ? (c) - ('a' - 0x0a) :\
                  ((c) >= 'A' && (c) <= 'F') ? (c) - ('A' - 0x0a) : -1)

// unaligned memory access helpers

typedef union { uint8_t u8[16/8]; } _u16;
typedef union { uint8_t u8[32/8]; } _u32;
typedef union { uint8_t u8[64/8]; } _u64;
typedef union { uint8_t u8[128/8]; } _u128;
typedef union { uint8_t u8[160/8]; } _u160;
typedef union { uint8_t u8[256/8]; } _u256;


inline static void UInt16SetBE(void *b2, uint16_t u)
{
    *(_u16*)b2 = (_u16) { (uint8_t)((u >> 8) & 0xff), (uint8_t)(u & 0xff) };
}

inline static void UInt16SetLE(void *b2, uint16_t u)
{
    *(_u16*)b2 = (_u16) { (uint8_t)(u & 0xff), (uint8_t)((u >> 8) & 0xff) };
}

inline static void UInt32SetBE(void *b4, uint32_t u)
{
    *(_u32*)b4 =
        (_u32) { (uint8_t)((u >> 24) & 0xff), (uint8_t)((u >> 16) & 0xff), (uint8_t)((u >> 8) & 0xff), (uint8_t)(u & 0xff) };
}

inline static void UInt32SetLE(void *b4, uint32_t u)
{
    *(_u32*)b4 =
        (_u32) { (uint8_t)(u & 0xff), (uint8_t)((u >> 8) & 0xff), (uint8_t)((u >> 16) & 0xff), (uint8_t)((u >> 24) & 0xff) };
}

inline static void UInt64SetBE(void *b8, uint64_t u)
{
    *(_u64*)b8 =
        (_u64) { (uint8_t)((u >> 56) & 0xff), (uint8_t)((u >> 48) & 0xff), (uint8_t)((u >> 40) & 0xff), (uint8_t)((u >> 32) & 0xff),
                 (uint8_t)((u >> 24) & 0xff), (uint8_t)((u >> 16) & 0xff), (uint8_t)((u >> 8) & 0xff), (uint8_t)(u & 0xff) };
}

inline static void UInt64SetLE(void *b8, uint64_t u)
{
    *(_u64*)b8 =
        (_u64) { (uint8_t)(u & 0xff), (uint8_t)((u >> 8) & 0xff), (uint8_t)((u >> 16) & 0xff), (uint8_t)((u >> 24) & 0xff),
                 (uint8_t)((u >> 32) & 0xff), (uint8_t)((u >> 40) & 0xff), (uint8_t)((u >> 48) & 0xff), (uint8_t)((u >> 56) & 0xff) };
}

inline static void UInt128Set(void *b16, UInt128 u)
{
    *(_u128*)b16 =
        (_u128) { u.u8[0], u.u8[1], u.u8[2],  u.u8[3],  u.u8[4],  u.u8[5],  u.u8[6],  u.u8[7],
                  u.u8[8], u.u8[9], u.u8[10], u.u8[11], u.u8[12], u.u8[13], u.u8[14], u.u8[15] };
}

inline static void UInt160Set(void *b20, UInt160 u)
{
    *(_u160*)b20 =
        (_u160) { u.u8[0],  u.u8[1],  u.u8[2],  u.u8[3],  u.u8[4],  u.u8[5],  u.u8[6],  u.u8[7],
                  u.u8[8],  u.u8[9],  u.u8[10], u.u8[11], u.u8[12], u.u8[13], u.u8[14], u.u8[15],
                  u.u8[16], u.u8[17], u.u8[18], u.u8[19] };
}

inline static void UInt256Set(void *b32, UInt256 u)
{
    *(_u256*)b32 =
        (_u256) { u.u8[0],  u.u8[1],  u.u8[2],  u.u8[3],  u.u8[4],  u.u8[5],  u.u8[6],  u.u8[7],
                  u.u8[8],  u.u8[9],  u.u8[10], u.u8[11], u.u8[12], u.u8[13], u.u8[14], u.u8[15],
                  u.u8[16], u.u8[17], u.u8[18], u.u8[19], u.u8[20], u.u8[21], u.u8[22], u.u8[23],
                  u.u8[24], u.u8[25], u.u8[26], u.u8[27], u.u8[28], u.u8[29], u.u8[30], u.u8[31] };
}

inline static uint16_t UInt16GetBE(const void *b2)
{
    return (((uint16_t)((const uint8_t *)b2)[0] << 8) | ((uint16_t)((const uint8_t *)b2)[1]));
}

inline static uint16_t UInt16GetLE(const void *b2)
{
    return (((uint16_t)((const uint8_t *)b2)[1] << 8) | ((uint16_t)((const uint8_t *)b2)[0]));
}

inline static uint32_t UInt32GetBE(const void *b4)
{
    return (((uint32_t)((const uint8_t *)b4)[0] << 24) | ((uint32_t)((const uint8_t *)b4)[1] << 16) |
            ((uint32_t)((const uint8_t *)b4)[2] << 8)  | ((uint32_t)((const uint8_t *)b4)[3]));
}

inline static uint32_t UInt32GetLE(const void *b4)
{
    return (((uint32_t)((const uint8_t *)b4)[3] << 24) | ((uint32_t)((const uint8_t *)b4)[2] << 16) |
            ((uint32_t)((const uint8_t *)b4)[1] << 8)  | ((uint32_t)((const uint8_t *)b4)[0]));
}

inline static uint64_t UInt64GetBE(const void *b8)
{
    return (((uint64_t)((const uint8_t *)b8)[0] << 56) | ((uint64_t)((const uint8_t *)b8)[1] << 48) |
            ((uint64_t)((const uint8_t *)b8)[2] << 40) | ((uint64_t)((const uint8_t *)b8)[3] << 32) |
            ((uint64_t)((const uint8_t *)b8)[4] << 24) | ((uint64_t)((const uint8_t *)b8)[5] << 16) |
            ((uint64_t)((const uint8_t *)b8)[6] << 8)  | ((uint64_t)((const uint8_t *)b8)[7]));
}

inline static uint64_t UInt64GetLE(const void *b8)
{
    return (((uint64_t)((const uint8_t *)b8)[7] << 56) | ((uint64_t)((const uint8_t *)b8)[6] << 48) |
            ((uint64_t)((const uint8_t *)b8)[5] << 40) | ((uint64_t)((const uint8_t *)b8)[4] << 32) |
            ((uint64_t)((const uint8_t *)b8)[3] << 24) | ((uint64_t)((const uint8_t *)b8)[2] << 16) |
            ((uint64_t)((const uint8_t *)b8)[1] << 8)  | ((uint64_t)((const uint8_t *)b8)[0]));
}

inline static UInt128 UInt128Get(const void *b16)
{
    return (UInt128) { .u8 = {
        ((const uint8_t *)b16)[0],  ((const uint8_t *)b16)[1],  ((const uint8_t *)b16)[2],  ((const uint8_t *)b16)[3],
        ((const uint8_t *)b16)[4],  ((const uint8_t *)b16)[5],  ((const uint8_t *)b16)[6],  ((const uint8_t *)b16)[7],
        ((const uint8_t *)b16)[8],  ((const uint8_t *)b16)[9],  ((const uint8_t *)b16)[10], ((const uint8_t *)b16)[11],
        ((const uint8_t *)b16)[12], ((const uint8_t *)b16)[13], ((const uint8_t *)b16)[14], ((const uint8_t *)b16)[15]
    } };
}

inline static UInt160 UInt160Get(const void *b20)
{
    return (UInt160) { .u8 = {
        ((const uint8_t *)b20)[0],  ((const uint8_t *)b20)[1],  ((const uint8_t *)b20)[2],  ((const uint8_t *)b20)[3],
        ((const uint8_t *)b20)[4],  ((const uint8_t *)b20)[5],  ((const uint8_t *)b20)[6],  ((const uint8_t *)b20)[7],
        ((const uint8_t *)b20)[8],  ((const uint8_t *)b20)[9],  ((const uint8_t *)b20)[10], ((const uint8_t *)b20)[11],
        ((const uint8_t *)b20)[12], ((const uint8_t *)b20)[13], ((const uint8_t *)b20)[14], ((const uint8_t *)b20)[15],
        ((const uint8_t *)b20)[16], ((const uint8_t *)b20)[17], ((const uint8_t *)b20)[18], ((const uint8_t *)b20)[19]
    } };
}

inline static UInt256 UInt256Get(const void *b32)
{
    return (UInt256) { .u8 = {
        ((const uint8_t *)b32)[0],  ((const uint8_t *)b32)[1],  ((const uint8_t *)b32)[2],  ((const uint8_t *)b32)[3],
        ((const uint8_t *)b32)[4],  ((const uint8_t *)b32)[5],  ((const uint8_t *)b32)[6],  ((const uint8_t *)b32)[7],
        ((const uint8_t *)b32)[8],  ((const uint8_t *)b32)[9],  ((const uint8_t *)b32)[10], ((const uint8_t *)b32)[11],
        ((const uint8_t *)b32)[12], ((const uint8_t *)b32)[13], ((const uint8_t *)b32)[14], ((const uint8_t *)b32)[15],
        ((const uint8_t *)b32)[16], ((const uint8_t *)b32)[17], ((const uint8_t *)b32)[18], ((const uint8_t *)b32)[19],
        ((const uint8_t *)b32)[20], ((const uint8_t *)b32)[21], ((const uint8_t *)b32)[22], ((const uint8_t *)b32)[23],
        ((const uint8_t *)b32)[24], ((const uint8_t *)b32)[25], ((const uint8_t *)b32)[26], ((const uint8_t *)b32)[27],
        ((const uint8_t *)b32)[28], ((const uint8_t *)b32)[29], ((const uint8_t *)b32)[30], ((const uint8_t *)b32)[31]
    } };
}

#ifdef __cplusplus
}
#endif

#endif // BRInt_h

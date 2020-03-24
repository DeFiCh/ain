//
//  BRCrypto.c
//
//  Created by Aaron Voisine on 8/8/15.
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

#include "BRCrypto.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// endian swapping
#if __BIG_ENDIAN__ || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define be32(x) (x)
#define le32(x) ((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | (((x) & 0xff0000) >> 8) | (((x) & 0xff000000) >> 24))
#define be64(x) (x)
#define le64(x) ((union { uint32_t u32[2]; uint64_t u64; }) { le32((uint32_t)(x)), le32((uint32_t)((x) >> 32)) }.u64)
#elif __LITTLE_ENDIAN__ || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define le32(x) (x)
#define be32(x) ((((x) & 0xff) << 24) | (((x) & 0xff00) << 8) | (((x) & 0xff0000) >> 8) | (((x) & 0xff000000) >> 24))
#define le64(x) (x)
#define be64(x) ((union { uint32_t u32[2]; uint64_t u64; }) { be32((uint32_t)((x) >> 32)), be32((uint32_t)(x)) }.u64)
#else // unknown endianess
#define be32(x) ((union { uint8_t u8[4]; uint32_t u32; }) { (x) >> 24, (x) >> 16, (x) >> 8, (x) }.u32)
#define le32(x) ((union { uint8_t u8[4]; uint32_t u32; }) { (x), (x) >> 8, (x) >> 16, (x) >> 24 }.u32)
#define be64(x) ((union { uint32_t u32[2]; uint64_t u64; }) { be32((uint32_t)((x) >> 32)), be32((uint32_t)(x)) }.u64)
#define le64(x) ((union { uint32_t u32[2]; uint64_t u64; }) { le32((uint32_t)(x)), le32((uint32_t)((x) >> 32)) }.u64)
#endif

// bitwise left rotation
#define rol32(a, b) (((a) << (b)) | ((a) >> (32 - (b))))

// basic sha1 functions
#define f1(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define f2(x, y, z) ((x) ^ (y) ^ (z))
#define f3(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))

// basic sha1 operation
#define sha1(x, y, z) (t = rol32(a, 5) + (x) + e + (y) + (z), e = d, d = c, c = rol32(b, 30), b = a, a = t)

static void _BRSHA1Compress(uint32_t *r, uint32_t *x)
{
    int i = 0;
    uint32_t a = r[0], b = r[1], c = r[2], d = r[3], e = r[4], t;
    
    for (; i < 16; i++) sha1(f1(b, c, d), 0x5a827999, (x[i] = be32(x[i])));
    for (; i < 20; i++) sha1(f1(b, c, d), 0x5a827999, (x[i] = rol32(x[i - 3] ^ x[i - 8] ^ x[i - 14] ^ x[i - 16], 1)));
    for (; i < 40; i++) sha1(f2(b, c, d), 0x6ed9eba1, (x[i] = rol32(x[i - 3] ^ x[i - 8] ^ x[i - 14] ^ x[i - 16], 1)));
    for (; i < 60; i++) sha1(f3(b, c, d), 0x8f1bbcdc, (x[i] = rol32(x[i - 3] ^ x[i - 8] ^ x[i - 14] ^ x[i - 16], 1)));
    for (; i < 80; i++) sha1(f2(b, c, d), 0xca62c1d6, (x[i] = rol32(x[i - 3] ^ x[i - 8] ^ x[i - 14] ^ x[i - 16], 1)));
    
    r[0] += a, r[1] += b, r[2] += c, r[3] += d, r[4] += e;
    var_clean(&a, &b, &c, &d, &e, &t);
}

// sha-1 - not recommended for cryptographic use
void BRSHA1(void *md20, const void *data, size_t dataLen)
{
    size_t i;
    uint32_t x[80], buf[] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 }; // initial buffer values
    
    assert(md20 != NULL);
    assert(data != NULL || dataLen == 0);
    
    for (i = 0; i < dataLen; i += 64) { // process data in 64 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 64 < dataLen) ? 64 : dataLen - i);
        if (i + 64 > dataLen) break;
        _BRSHA1Compress(buf, x);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 64 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 56) _BRSHA1Compress(buf, x), memset(x, 0, 64); // length goes to next block
    x[14] = be32((uint32_t)(dataLen >> 29)), x[15] = be32((uint32_t)(dataLen << 3)); // append length in bits
    _BRSHA1Compress(buf, x); // finalize
    for (i = 0; i < 5; i++) buf[i] = be32(buf[i]); // endian swap
    memcpy(md20, buf, 20); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

// bitwise right rotation
#define ror32(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

// basic sha2 functions
#define ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

// basic sha256 functions
#define s0(x) (ror32((x), 2) ^ ror32((x), 13) ^ ror32((x), 22))
#define s1(x) (ror32((x), 6) ^ ror32((x), 11) ^ ror32((x), 25))
#define s2(x) (ror32((x), 7) ^ ror32((x), 18) ^ ((x) >> 3))
#define s3(x) (ror32((x), 17) ^ ror32((x), 19) ^ ((x) >> 10))

static void _BRSHA256Compress(uint32_t *r, const uint32_t *x)
{
    static const uint32_t k[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    
    int i;
    uint32_t a = r[0], b = r[1], c = r[2], d = r[3], e = r[4], f = r[5], g = r[6], h = r[7], t1, t2, w[64];
    
    for (i = 0; i < 16; i++) w[i] = be32(x[i]);
    for (; i < 64; i++) w[i] = s3(w[i - 2]) + w[i - 7] + s2(w[i - 15]) + w[i - 16];
    
    for (i = 0; i < 64; i++) {
        t1 = h + s1(e) + ch(e, f, g) + k[i] + w[i];
        t2 = s0(a) + maj(a, b, c);
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    
    r[0] += a, r[1] += b, r[2] += c, r[3] += d, r[4] += e, r[5] += f, r[6] += g, r[7] += h;
    var_clean(&a, &b, &c, &d, &e, &f, &g, &h, &t1, &t2);
    mem_clean(w, sizeof(w));
}

void BRSHA224(void *md28, const void *data, size_t dataLen) {
    size_t i;
    uint32_t x[16], buf[] = { 0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511,
                              0x64f98fa7, 0xbefa4fa4 }; // initial buffer values

    assert(md28 != NULL);
    assert(data != NULL || dataLen == 0);

    for (i = 0; i < dataLen; i += 64) { // process data in 64 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 64 < dataLen) ? 64 : dataLen - i);
        if (i + 64 > dataLen) break;
        _BRSHA256Compress(buf, x);
    }

    memset((uint8_t *)x + (dataLen - i), 0, 64 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 56) _BRSHA256Compress(buf, x), memset(x, 0, 64); // length goes to next block
    x[14] = be32((uint32_t)(dataLen >> 29)), x[15] = be32((uint32_t)(dataLen << 3)); // append length in bits
    _BRSHA256Compress(buf, x); // finalize
    for (i = 0; i < 7; i++) buf[i] = be32(buf[i]); // endian swap
    memcpy(md28, buf, 28); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

void BRSHA256(void *md32, const void *data, size_t dataLen)
{
    size_t i;
    uint32_t x[16], buf[] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
                              0x1f83d9ab, 0x5be0cd19 }; // initial buffer values
    
    assert(md32 != NULL);
    assert(data != NULL || dataLen == 0);

    for (i = 0; i < dataLen; i += 64) { // process data in 64 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 64 < dataLen) ? 64 : dataLen - i);
        if (i + 64 > dataLen) break;
        _BRSHA256Compress(buf, x);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 64 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 56) _BRSHA256Compress(buf, x), memset(x, 0, 64); // length goes to next block
    x[14] = be32((uint32_t)(dataLen >> 29)), x[15] = be32((uint32_t)(dataLen << 3)); // append length in bits
    _BRSHA256Compress(buf, x); // finalize
    for (i = 0; i < 8; i++) buf[i] = be32(buf[i]); // endian swap
    memcpy(md32, buf, 32); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

// double-sha-256 = sha-256(sha-256(x))
void BRSHA256_2(void *md32, const void *data, size_t dataLen)
{
    uint8_t t[32];

    assert(md32 != NULL);
    assert(data != NULL || dataLen == 0);
    BRSHA256(t, data, dataLen);
    BRSHA256(md32, t, sizeof(t));
}

// bitwise right rotation
#define ror64(a, b) (((a) >> (b)) | ((a) << (64 - (b))))

// basic sha512 opeartions
#define S0(x) (ror64((x), 28) ^ ror64((x), 34) ^ ror64((x), 39))
#define S1(x) (ror64((x), 14) ^ ror64((x), 18) ^ ror64((x), 41))
#define S2(x) (ror64((x), 1) ^ ror64((x), 8) ^ ((x) >> 7))
#define S3(x) (ror64((x), 19) ^ ror64((x), 61) ^ ((x) >> 6))

static void _BRSHA512Compress(uint64_t *r, const uint64_t *x)
{
    static const uint64_t k[] = {
        0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538,
        0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe,
        0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235,
        0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65,
        0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab,
        0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725,
        0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed,
        0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b,
        0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218,
        0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53,
        0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373,
        0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec,
        0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c,
        0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6,
        0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc,
        0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817
    };
    
    int i;
    uint64_t a = r[0], b = r[1], c = r[2], d = r[3], e = r[4], f = r[5], g = r[6], h = r[7], t1, t2, w[80];
    
    for (i = 0; i < 16; i++) w[i] = be64(x[i]);
    for (; i < 80; i++) w[i] = S3(w[i - 2]) + w[i - 7] + S2(w[i - 15]) + w[i - 16];
    
    for (i = 0; i < 80; i++) {
        t1 = h + S1(e) + ch(e, f, g) + k[i] + w[i];
        t2 = S0(a) + maj(a, b, c);
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    
    r[0] += a, r[1] += b, r[2] += c, r[3] += d, r[4] += e, r[5] += f, r[6] += g, r[7] += h;
    var_clean(&a, &b, &c, &d, &e, &f, &g, &h, &t1, &t2);
    mem_clean(w, sizeof(w));
}

void BRSHA384(void *md48, const void *data, size_t dataLen)
{
    size_t i;
    uint64_t x[16], buf[] = { 0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939,
                              0x67332667ffc00b31, 0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4 };
    
    assert(md48 != NULL);
    assert(data != NULL || dataLen == 0);

    for (i = 0; i < dataLen; i += 128) { // process data in 128 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 128 < dataLen) ? 128 : dataLen - i);
        if (i + 128 > dataLen) break;
        _BRSHA512Compress(buf, x);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 128 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 112) _BRSHA512Compress(buf, x), memset(x, 0, 128); // length goes to next block
    x[14] = 0, x[15] = be64((uint64_t)dataLen*8); // append length in bits
    _BRSHA512Compress(buf, x); // finalize
    for (i = 0; i < 6; i++) buf[i] = be64(buf[i]); // endian swap
    memcpy(md48, buf, 48); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

void BRSHA512(void *md64, const void *data, size_t dataLen)
{
    size_t i;
    uint64_t x[16], buf[] = { 0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
                              0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179 };
    
    assert(md64 != NULL);
    assert(data != NULL || dataLen == 0);

    for (i = 0; i < dataLen; i += 128) { // process data in 128 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 128 < dataLen) ? 128 : dataLen - i);
        if (i + 128 > dataLen) break;
        _BRSHA512Compress(buf, x);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 128 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 112) _BRSHA512Compress(buf, x), memset(x, 0, 128); // length goes to next block
    x[14] = 0, x[15] = be64((uint64_t)dataLen*8); // append length in bits
    _BRSHA512Compress(buf, x); // finalize
    for (i = 0; i < 8; i++) buf[i] = be64(buf[i]); // endian swap
    memcpy(md64, buf, 64); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

// basic ripemd functions
#define f(x, y, z) ((x) ^ (y) ^ (z))
#define g(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define h(x, y, z) (((x) | ~(y)) ^ (z))
#define i(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define j(x, y, z) ((x) ^ ((y) | ~(z)))

// basic ripemd operation
#define rmd(a, b, c, d, e, f, g, h, i, j) ((a) = rol32((f) + (b) + le32(c) + (d), (e)) + (g), (f) = (g), (g) = (h),\
                                           (h) = rol32((i), 10), (i) = (j), (j) = (a))

static void _BRRMDCompress(uint32_t *r, const uint32_t *x)
{
    // left line
    static const int rl1[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, // round 1, id
                     rl2[] = { 7, 4, 13, 1, 10, 6, 15, 3, 12, 0, 9, 5, 2, 14, 11, 8 }, // round 2, rho
                     rl3[] = { 3, 10, 14, 4, 9, 15, 8, 1, 2, 7, 0, 6, 13, 11, 5, 12 }, // round 3, rho^2
                     rl4[] = { 1, 9, 11, 10, 0, 8, 12, 4, 13, 3, 7, 15, 14, 5, 6, 2 }, // round 4, rho^3
                     rl5[] = { 4, 0, 5, 9, 7, 12, 2, 10, 14, 1, 3, 8, 11, 6, 15, 13 }; // round 5, rho^4
    // right line
    static const int rr1[] = { 5, 14, 7, 0, 9, 2, 11, 4, 13, 6, 15, 8, 1, 10, 3, 12 }, // round 1, pi
                     rr2[] = { 6, 11, 3, 7, 0, 13, 5, 10, 14, 15, 8, 12, 4, 9, 1, 2 }, // round 2, rho pi
                     rr3[] = { 15, 5, 1, 3, 7, 14, 6, 9, 11, 8, 12, 2, 10, 0, 4, 13 }, // round 3, rho^2 pi
                     rr4[] = { 8, 6, 4, 1, 3, 11, 15, 0, 5, 12, 2, 13, 9, 7, 10, 14 }, // round 4, rho^3 pi
                     rr5[] = { 12, 15, 10, 4, 1, 5, 8, 7, 6, 2, 13, 14, 0, 3, 9, 11 }; // round 5, rho^4 pi
    // left line shifts
    static const int sl1[] = { 11, 14, 15, 12, 5, 8, 7, 9, 11, 13, 14, 15, 6, 7, 9, 8 }, // round 1
                     sl2[] = { 7, 6, 8, 13, 11, 9, 7, 15, 7, 12, 15, 9, 11, 7, 13, 12 }, // round 2
                     sl3[] = { 11, 13, 6, 7, 14, 9, 13, 15, 14, 8, 13, 6, 5, 12, 7, 5 }, // round 3
                     sl4[] = { 11, 12, 14, 15, 14, 15, 9, 8, 9, 14, 5, 6, 8, 6, 5, 12 }, // round 4
                     sl5[] = { 9, 15, 5, 11, 6, 8, 13, 12, 5, 12, 13, 14, 11, 8, 5, 6 }; // round 5
    // right line shifts
    static const int sr1[] = { 8, 9, 9, 11, 13, 15, 15, 5, 7, 7, 8, 11, 14, 14, 12, 6 }, // round 1
                     sr2[] = { 9, 13, 15, 7, 12, 8, 9, 11, 7, 7, 12, 7, 6, 15, 13, 11 }, // round 2
                     sr3[] = { 9, 7, 15, 11, 8, 6, 6, 14, 12, 13, 5, 14, 13, 13, 7, 5 }, // round 3
                     sr4[] = { 15, 5, 8, 11, 14, 14, 6, 14, 6, 9, 12, 9, 12, 5, 15, 8 }, // round 4
                     sr5[] = { 8, 5, 12, 9, 12, 5, 14, 6, 8, 13, 6, 5, 15, 13, 11, 11 }; // round 5

    int i;
    uint32_t al = r[0], bl = r[1], cl = r[2], dl = r[3], el = r[4], ar = al, br = bl, cr = cl, dr = dl, er = el, t;
    
    for (i = 0; i < 16; i++) rmd(t, f(bl, cl, dl), x[rl1[i]], 0x00000000, sl1[i], al, el, dl, cl, bl); // round 1 left
    for (i = 0; i < 16; i++) rmd(t, j(br, cr, dr), x[rr1[i]], 0x50a28be6, sr1[i], ar, er, dr, cr, br); // round 1 right
    for (i = 0; i < 16; i++) rmd(t, g(bl, cl, dl), x[rl2[i]], 0x5a827999, sl2[i], al, el, dl, cl, bl); // round 2 left
    for (i = 0; i < 16; i++) rmd(t, i(br, cr, dr), x[rr2[i]], 0x5c4dd124, sr2[i], ar, er, dr, cr, br); // round 2 right
    for (i = 0; i < 16; i++) rmd(t, h(bl, cl, dl), x[rl3[i]], 0x6ed9eba1, sl3[i], al, el, dl, cl, bl); // round 3 left
    for (i = 0; i < 16; i++) rmd(t, h(br, cr, dr), x[rr3[i]], 0x6d703ef3, sr3[i], ar, er, dr, cr, br); // round 3 right
    for (i = 0; i < 16; i++) rmd(t, i(bl, cl, dl), x[rl4[i]], 0x8f1bbcdc, sl4[i], al, el, dl, cl, bl); // round 4 left
    for (i = 0; i < 16; i++) rmd(t, g(br, cr, dr), x[rr4[i]], 0x7a6d76e9, sr4[i], ar, er, dr, cr, br); // round 4 right
    for (i = 0; i < 16; i++) rmd(t, j(bl, cl, dl), x[rl5[i]], 0xa953fd4e, sl5[i], al, el, dl, cl, bl); // round 5 left
    for (i = 0; i < 16; i++) rmd(t, f(br, cr, dr), x[rr5[i]], 0x00000000, sr5[i], ar, er, dr, cr, br); // round 5 right
    
    t = r[1] + cl + dr; // final result for r[0]
    r[1] = r[2] + dl + er, r[2] = r[3] + el + ar, r[3] = r[4] + al + br, r[4] = r[0] + bl + cr, r[0] = t; // combine
    var_clean(&al, &bl, &cl, &dl, &el, &ar, &br, &cr, &dr, &er, &t);
}

// ripemd-160: http://homes.esat.kuleuven.be/~bosselae/ripemd160.html
void BRRMD160(void *md20, const void *data, size_t dataLen)
{
    size_t i;
    uint32_t x[16], buf[] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 }; // initial buffer values
    
    assert(md20 != NULL);
    assert(data != NULL || dataLen == 0);

    for (i = 0; i <= dataLen; i += 64) { // process data in 64 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 64 < dataLen) ? 64 : dataLen - i);
        if (i + 64 > dataLen) break;
        _BRRMDCompress(buf, x);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 64 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 56) _BRRMDCompress(buf, x), memset(x, 0, 64); // length goes to next block
    x[14] = le32((uint32_t)(dataLen << 3)), x[15] = le32((uint32_t)(dataLen >> 29)); // append length in bits
    _BRRMDCompress(buf, x); // finalize
    for (i = 0; i < 5; i++) buf[i] = le32(buf[i]); // endian swap
    memcpy(md20, buf, 20); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

// bitcoin hash-160 = ripemd-160(sha-256(x))
void BRHash160(void *md20, const void *data, size_t datalen)
{
    uint8_t t[32];
    
    assert(md20 != NULL);
    assert(data != NULL || datalen == 0);
    
    BRSHA256(t, data, datalen);
    BRRMD160(md20, t, sizeof(t));
}

// bitwise left rotation
#define rol64(a, b) ((a) << (b) ^ ((a) >> (64 - (b))))

static void _BRSHA3Compress(uint64_t *r, const uint64_t *x, size_t blockSize)
{
    static const uint64_t k[] = { // keccak round constants
        0x0000000000000001, 0x0000000000008082, 0x800000000000808a, 0x8000000080008000, 0x000000000000808b,
        0x0000000080000001, 0x8000000080008081, 0x8000000000008009, 0x000000000000008a, 0x0000000000000088,
        0x0000000080008009, 0x000000008000000a, 0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
        0x8000000000008003, 0x8000000000008002, 0x8000000000000080, 0x000000000000800a, 0x800000008000000a,
        0x8000000080008081, 0x8000000000008080, 0x0000000080000001, 0x8000000080008008
    };
    
    size_t i, j;
    uint64_t a[5], b[5], r0, r1;
    
    for (i = 0; i < blockSize/sizeof(uint64_t); i++) r[i] ^= le64(x[i]);
    
    for (i = 0; i < 24; i++) { // permute r
        // theta(r)
        for (j = 0; j < 5; j++) a[j] = r[j] ^ r[j + 5] ^ r[j + 10] ^ r[j + 15] ^ r[j + 20];
        b[0] = rol64(a[1], 1) ^ a[4], b[1] = rol64(a[2], 1) ^ a[0], b[2] = rol64(a[3], 1) ^ a[1];
        b[3] = rol64(a[4], 1) ^ a[2], b[4] = rol64(a[0], 1) ^ a[3];
        for (j = 0; j < 5; j++) r[j] ^= b[j], r[j + 5] ^= b[j], r[j + 10] ^= b[j], r[j + 15] ^= b[j], r[j + 20] ^= b[j];
        
        // rho(r)
        r[1] = rol64(r[1], 1), r[2] = rol64(r[2], 62), r[3] = rol64(r[3], 28), r[4] = rol64(r[4], 27);
        r[5] = rol64(r[5], 36), r[6] = rol64(r[6], 44), r[7] = rol64(r[7], 6), r[8] = rol64(r[8], 55);
        r[9] = rol64(r[9], 20), r[10] = rol64(r[10], 3), r[11] = rol64(r[11], 10), r[12] = rol64(r[12], 43);
        r[13] = rol64(r[13], 25), r[14] = rol64(r[14], 39), r[15] = rol64(r[15], 41), r[16] = rol64(r[16], 45);
        r[17] = rol64(r[17], 15), r[18] = rol64(r[18], 21), r[19] = rol64(r[19], 8), r[20] = rol64(r[20], 18);
        r[21] = rol64(r[21], 2), r[22] = rol64(r[22], 61), r[23] = rol64(r[23], 56), r[24] = rol64(r[24], 14);
        
        // pi(r)
        r1 = r[1], r[1] = r[6], r[6] = r[9], r[9] = r[22], r[22] = r[14], r[14] = r[20], r[20] = r[2], r[2] = r[12],
        r[12] = r[13], r[13] = r[19], r[19] = r[23], r[23] = r[15], r[15] = r[4], r[4] = r[24], r[24] = r[21];
        r[21] = r[8], r[8] = r[16], r[16] = r[5], r[5] = r[3], r[3] = r[18], r[18] = r[17], r[17] = r[11], r[11] = r[7];
        r[7] = r[10], r[10] = r1; // r[0] left as is
        
        for (j = 0; j < 25; j += 5) { // chi(r)
            r0 = r[0 + j], r1 = r[1 + j], r[0 + j] ^= ~r1 & r[2 + j], r[1 + j] ^= ~r[2 + j] & r[3 + j];
            r[2 + j] ^= ~r[3 + j] & r[4 + j], r[3 + j] ^= ~r[4 + j] & r0, r[4 + j] ^= ~r0 & r1;
        }
        
        *r ^= k[i]; // iota(r, i)
    }
    
    mem_clean(a, sizeof(a));
    mem_clean(b, sizeof(b));
    var_clean(&r0, &r1);
}

// sha3-256: http://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.202.pdf
void BRSHA3_256(void *md32, const void *data, size_t dataLen)
{
    size_t i;
    uint64_t x[17], buf[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    
    assert(md32 != NULL);
    assert(data != NULL || dataLen == 0);
    
    for (i = 0; i <= dataLen; i += 136) { // process data in 136 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 136 < dataLen) ? 136 : dataLen - i);
        if (i + 136 > dataLen) break;
        _BRSHA3Compress(buf, x, 136);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 136 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] |= 0x06; // append padding
    ((uint8_t *)x)[135] |= 0x80;
    _BRSHA3Compress(buf, x, 136); // finalize
    for (i = 0; i < 4; i++) buf[i] = le64(buf[i]); // endian swap
    memcpy(md32, buf, 32); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

// keccak-256: https://keccak.team/files/Keccak-submission-3.pdf
void BRKeccak256(void *md32, const void *data, size_t dataLen)
{
    size_t i;
    uint64_t x[17], buf[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    
    assert(md32 != NULL);
    assert(data != NULL || dataLen == 0);
    
    for (i = 0; i <= dataLen; i += 136) { // process data in 136 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 136 < dataLen) ? 136 : dataLen - i);
        if (i + 136 > dataLen) break;
        _BRSHA3Compress(buf, x, 136);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 136 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] |= 0x01; // append padding
    ((uint8_t *)x)[135] |= 0x80;
    _BRSHA3Compress(buf, x, 136); // finalize
    for (i = 0; i < 4; i++) buf[i] = le64(buf[i]); // endian swap
    memcpy(md32, buf, 32); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

// basic md5 functions
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

// basic md5 operation
#define md5(f, a, b, c, d, x, k, s, t) ((a) += f((b), (c), (d)) + le32(x) + (k), (a) = rol32(a, s), (a) += (b),\
                                        (t) = (d), (d) = (c), (c) = (b), (b) = (a), (a) = (t))

static void _BRMD5Compress(uint32_t *r, const uint32_t *x)
{
    static const uint32_t k[] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
    };

    static const int s[] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };
    
    int i = 0;
    uint32_t a = r[0], b = r[1], c = r[2], d = r[3], t;
    
    for (; i < 16; i++) md5(F, a, b, c, d, x[i], k[i], s[i % 4], t);
    for (; i < 32; i++) md5(G, a, b, c, d, x[(5*i + 1) % 16], k[i], s[4 + (i % 4)], t);
    for (; i < 48; i++) md5(H, a, b, c, d, x[(3*i + 5) % 16], k[i], s[8 + (i % 4)], t);
    for (; i < 64; i++) md5(I, a, b, c, d, x[(7*i) % 16], k[i], s[12 + (i % 4)], t);
    
    r[0] += a, r[1] += b, r[2] += c, r[3] += d;
    var_clean(&a, &b, &c, &d, &t);
}

// md5 - for non-cyptographic use only
void BRMD5(void *md16, const void *data, size_t dataLen)
{
    size_t i;
    uint32_t x[16], buf[] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 }; // initial buffer values
    
    assert(md16 != NULL);
    assert(data != NULL || dataLen == 0);

    for (i = 0; i <= dataLen; i += 64) { // process data in 64 byte blocks
        memcpy(x, (const uint8_t *)data + i, (i + 64 < dataLen) ? 64 : dataLen - i);
        if (i + 64 > dataLen) break;
        _BRMD5Compress(buf, x);
    }
    
    memset((uint8_t *)x + (dataLen - i), 0, 64 - (dataLen - i)); // clear remainder of x
    ((uint8_t *)x)[dataLen - i] = 0x80; // append padding
    if (dataLen - i >= 56) _BRMD5Compress(buf, x), memset(x, 0, 64); // length goes to next block
    x[14] = le32((uint32_t)(dataLen << 3)), x[15] = le32((uint32_t)(dataLen >> 29)); // append length in bits
    _BRMD5Compress(buf, x); // finalize
    for (i = 0; i < 4; i++) buf[i] = le32(buf[i]); // endian swap
    memcpy(md16, buf, 16); // write to md
    mem_clean(x, sizeof(x));
    mem_clean(buf, sizeof(buf));
}

#define C1 0xcc9e2d51
#define C2 0x1b873593

// basic mumurHash3 operation
#define fmix32(h) ((h) ^= (h) >> 16, (h) *= 0x85ebca6b, (h) ^= (h) >> 13, (h) *= 0xc2b2ae35, (h) ^= (h) >> 16)

// murmurHash3 (x86_32): https://code.google.com/p/smhasher/ - for non-cryptographic use only
uint32_t BRMurmur3_32(const void *data, size_t dataLen, uint32_t seed)
{
    const uint8_t *d = data;
    uint32_t h = seed, k = 0;
    size_t i, count = dataLen/4;
    
    assert(data != NULL || dataLen == 0);
    
    for (i = 0; i < count*4; i += 4) {
        k = (((uint32_t)d[i + 3] << 24) | ((uint32_t)d[i + 2] << 16) |
             ((uint32_t)d[i + 1] <<  8) | ((uint32_t)d[i]))*C1;
        k = rol32(k, 15)*C2;
        h ^= k;
        h = rol32(h, 13)*5 + 0xe6546b64;
    }
    
    k = 0;
    
    switch (dataLen & 3) {
        case 3: k ^= d[i + 2] << 16; // fall through
        case 2: k ^= d[i + 1] << 8;  // fall through
        case 1: k ^= d[i], k *= C1, h ^= rol32(k, 15)*C2;
    }
    
    h ^= dataLen;
    fmix32(h);
    return h;
}

#define sipround(a, b, c, d) a += b, b = rol64(b, 13) ^ a, a = rol64(a, 32), c += d, d = rol64(d, 16) ^ c,\
                             a += d, d = rol64(d, 21) ^ a, c += b, b = rol64(b, 17) ^ c, c = rol64(c, 32)

// sipHash-64: https://131002.net/siphash
uint64_t BRSip64(const void *key16, const void *data, size_t dataLen)
{
    uint64_t x, a = 0x736f6d6570736575, b = 0x646f72616e646f6d, c = 0x6c7967656e657261, d = 0x7465646279746573;
    size_t i, j;
    
    memcpy(&x, key16, sizeof(x));
    a ^= le64(x), c ^= le64(x);
    memcpy(&x, (const uint8_t *)key16 + sizeof(x), sizeof(x));
    b ^= le64(x), d ^= le64(x);
    
    for (i = 0; i + 7 < dataLen; i += sizeof(x)) {
        memcpy(&x, (uint8_t *)data + i, sizeof(x));
        d ^= le64(x);
        for (j = 0; j < 2; j++) sipround(a, b, c, d);
        a ^= le64(x);
    }
    
    x = (uint64_t)dataLen << 56;
    for (j = 0; i + j < dataLen; j++) x |= ((uint64_t)((uint8_t *)data)[i + j]) << j*8;
    d ^= x;
    for (i = 0; i < 2; i++) sipround(a, b, c, d);
    a ^= x, c ^= 0xff;
    for (i = 0; i < 4; i++) sipround(a, b, c, d);
    x = a ^ b ^ c ^ d;
    return le64(x);
}

// HMAC(key, data) = hash((key xor opad) || hash((key xor ipad) || data))
// opad = 0x5c5c5c...5c5c
// ipad = 0x363636...3636
void BRHMAC(void *mac, void (*hash)(void *, const void *, size_t), size_t hashLen, const void *key, size_t keyLen,
            const void *data, size_t dataLen)
{
    size_t i, blockLen = (hashLen > 32) ? 128 : 64;
    uint8_t k[hashLen];
    uint64_t kipad[(blockLen + dataLen)/sizeof(uint64_t) + 1], kopad[(blockLen + hashLen)/sizeof(uint64_t) + 1];
    
    assert(mac != NULL);
    assert(hash != NULL);
    assert(hashLen > 0 && (hashLen % 4) == 0);
    assert(key != NULL || keyLen == 0);
    assert(data != NULL || dataLen == 0);
    
    if (keyLen > blockLen) hash(k, key, keyLen), key = k, keyLen = sizeof(k);
    memset(kipad, 0, blockLen);
    memcpy(kipad, key, keyLen);
    for (i = 0; i < blockLen/sizeof(uint64_t); i++) kipad[i] ^= 0x3636363636363636;
    memset(kopad, 0, blockLen);
    memcpy(kopad, key, keyLen);
    for (i = 0; i < blockLen/sizeof(uint64_t); i++) kopad[i] ^= 0x5c5c5c5c5c5c5c5c;
    memcpy(&kipad[blockLen/sizeof(uint64_t)], data, dataLen);
    hash(&kopad[blockLen/sizeof(uint64_t)], kipad, blockLen + dataLen);
    hash(mac, kopad, blockLen + hashLen);
    
    mem_clean(k, sizeof(k));
    mem_clean(kipad, blockLen);
    mem_clean(kopad, blockLen);
}

// hmac-drbg with no prediction resistance or additional input
// K and V must point to buffers of size hashLen, and ps (personalization string) may be NULL
// to generate additional drbg output, use K and V from the previous call, and set seed, nonce and ps to NULL
void BRHMACDRBG(void *out, size_t outLen, void *K, void *V, void (*hash)(void *, const void *, size_t), size_t hashLen,
                const void *seed, size_t seedLen, const void *nonce, size_t nonceLen, const void *ps, size_t psLen)
{
    size_t i, bufLen = hashLen + 1 + seedLen + nonceLen + psLen;
    uint8_t buf[bufLen];
    
    assert(out != NULL || outLen == 0);
    assert(K != NULL);
    assert(V != NULL);
    assert(hash != NULL);
    assert(hashLen > 0 && (hashLen % 4) == 0);
    assert(seed != NULL || seedLen == 0);
    assert(nonce != NULL || nonceLen == 0);
    assert(ps != NULL || psLen == 0);
    
    if (seed || nonce || ps) { // K = [0x00, 0x00, ... 0x00], V = [0x01, 0x01, ... 0x01]
        for (i = 0; i < hashLen; i++) ((uint8_t *)K)[i] = 0x00, ((uint8_t *)V)[i] = 0x01;
    }
    
    memcpy(buf, V, hashLen);
    buf[hashLen] = 0x00;
    memcpy(&buf[hashLen + 1], seed, seedLen);
    memcpy(&buf[hashLen + 1 + seedLen], nonce, nonceLen);
    memcpy(&buf[hashLen + 1 + seedLen + nonceLen], ps, psLen);
    BRHMAC(K, hash, hashLen, K, hashLen, buf, bufLen); // K = HMAC(K, V || 0x00 || entropy || nonce || ps)
    BRHMAC(V, hash, hashLen, K, hashLen, V, hashLen);  // V = HMAC(K, V)
    
    if (seed || nonce || ps) {
        memcpy(buf, V, hashLen);
        buf[hashLen] = 0x01;
        BRHMAC(K, hash, hashLen, K, hashLen, buf, bufLen); // K = HMAC(K, V || 0x01 || entropy || nonce || ps)
        BRHMAC(V, hash, hashLen, K, hashLen, V, hashLen);  // V = HMAC(K, V)
    }
    
    mem_clean(buf, bufLen);
    
    for (i = 0; i*hashLen < outLen; i++) {
        BRHMAC(V, hash, hashLen, K, hashLen, V, hashLen); // V = HMAC(K, V)
        memcpy((uint8_t *)out + i*hashLen, V, (i*hashLen + hashLen <= outLen) ? hashLen : outLen % hashLen);
    }
}

static void _BRPoly1305Compress(uint32_t h[5], const void *key32, const void *data, size_t dataLen, int final)
{
    uint32_t x[4], b, t0, t1, t2, t3, t4, r0, r1, r2, r3, r4;
    uint64_t d0, d1, d2, d3, d4;

    // r &= 0xffffffc0ffffffc0ffffffc0fffffff
    memcpy(x, key32, 16);
    t0 = le32(x[0]), t1 = le32(x[1]), t2 = le32(x[2]), t3 = le32(x[3]);
    r0 = t0 & 0x03ffffff, r1 = ((t0 >> 26) | (t1 << 6)) & 0x03ffff03, r2 = ((t1 >> 20) | (t2 << 12)) & 0x03ffc0ff;
    r3 = ((t2 >> 14) | (t3 << 18)) & 0x03f03fff, r4 = (t3 >> 8) & 0x000fffff;
    
    for (size_t i = 0; i < dataLen; i += 16) { // process data in 16 byte blocks
        if (i + 16 > dataLen) {
            memcpy(x, (const uint8_t *)data + i, dataLen - i);
            memset((uint8_t *)x + (dataLen - i), 0, 16 - (dataLen - i)); // clear remainder of x
            ((uint8_t *)x)[dataLen - i] = 1; // append padding
        }
        else memcpy(x, (const uint8_t *)data + i, 16);
        
        // h += x
        t0 = le32(x[0]), t1 = le32(x[1]), t2 = le32(x[2]), t3 = le32(x[3]);
        h[0] += t0 & 0x03ffffff, h[1] += ((t0 >> 26) | (t1 << 6)) & 0x03ffffff;
        h[2] += ((t1 >> 20) | (t2 << 12)) & 0x03ffffff, h[3] += ((t2 >> 14) | (t3 << 18)) & 0x03ffffff;
        h[4] += (t3 >> 8) | ((i + 16 <= dataLen) ? (1 << 24) : 0);
    
        // h *= r
        d0 = (uint64_t)h[0]*r0 + (uint64_t)h[1]*r4*5 + (uint64_t)h[2]*r3*5 + (uint64_t)h[3]*r2*5 + (uint64_t)h[4]*r1*5;
        d1 = (uint64_t)h[0]*r1 + (uint64_t)h[1]*r0 + (uint64_t)h[2]*r4*5 + (uint64_t)h[3]*r3*5 + (uint64_t)h[4]*r2*5;
        d2 = (uint64_t)h[0]*r2 + (uint64_t)h[1]*r1 + (uint64_t)h[2]*r0 + (uint64_t)h[3]*r4*5 + (uint64_t)h[4]*r3*5;
        d3 = (uint64_t)h[0]*r3 + (uint64_t)h[1]*r2 + (uint64_t)h[2]*r1 + (uint64_t)h[3]*r0 + (uint64_t)h[4]*r4*5;
        d4 = (uint64_t)h[0]*r4 + (uint64_t)h[1]*r3 + (uint64_t)h[2]*r2 + (uint64_t)h[3]*r1 + (uint64_t)h[4]*r0;
        
        // (partial) h %= p
        d1 += (uint32_t)(d0 >> 26), h[1] = d1 & 0x03ffffff, d2 += (uint32_t)(d1 >> 26), h[2] = d2 & 0x03ffffff;
        d3 += (uint32_t)(d2 >> 26), h[3] = d3 & 0x03ffffff, d4 += (uint32_t)(d3 >> 26), h[4] = d4 & 0x03ffffff;
        h[0] = (d0 & 0x03ffffff) + (uint32_t)(d4 >> 26)*5, h[1] += h[0] >> 26, h[0] &= 0x03ffffff;
    }
    
    if (final) {
        // fully carry h
        h[2] += h[1] >> 26, h[1] &= 0x03ffffff, h[3] += h[2] >> 26, h[2] &= 0x03ffffff, h[4] += h[3] >> 26;
        h[3] &= 0x03ffffff, h[0] += (h[4] >> 26)*5, h[4] &= 0x03ffffff, h[1] += h[0] >> 26, h[0] &= 0x03ffffff;
        
        // compute h + -p
        t0 = h[0] + 5, t1 = h[1] + (t0 >> 26), t0 &= 0x03ffffff, t2 = h[2] + (t1 >> 26), t1 &= 0x03ffffff;
        t3 = h[3] + (t2 >> 26), t2 &= 0x03ffffff, t4 = h[4] + (t3 >> 26) - (1 << 26), t3 &= 0x03ffffff;
        
        // select h if h < p, or h + -p if h >= p
        b = (t4 >> 31) - 1, h[0] = (h[0] & ~b) | (t0 & b), h[1] = (h[1] & ~b) | (t1 & b);
        h[2] = (h[2] & ~b) | (t2 & b), h[3] = (h[3] & ~b) | (t3 & b), h[4] = (h[4] & ~b) | (t4 & b);
        
        // h = h % (2^128)
        h[0] = (h[0] | (h[1] << 26)) & 0x0ffffffff, h[1] = ((h[1] >> 6) | (h[2] << 20)) & 0x0ffffffff;
        h[2] = ((h[2] >> 12) | (h[3] << 14)) & 0x0ffffffff, h[3] = ((h[3] >> 18) | (h[4] << 8)) & 0x0ffffffff;
        
        // mac = (h + pad) % (2^128)
        memcpy(x, (const uint8_t *)key32 + 16, 16);
        d0 = (uint64_t)h[0] + le32(x[0]), d1 = (uint64_t)h[1] + le32(x[1]) + (d0 >> 32);
        d2 = (uint64_t)h[2] + le32(x[2]) + (d1 >> 32), d3 = (uint64_t)h[3] + le32(x[3]) + (d2 >> 32);
        h[0] = le32((uint32_t)d0), h[1] = le32((uint32_t)d1), h[2] = le32((uint32_t)d2), h[3] = le32((uint32_t)d3);
    }
    
    var_clean(&d0, &d1, &d2, &d3, &d4);
    mem_clean(x, sizeof(x));
    var_clean(&b, &t0, &t1, &t2, &t3, &t4, &r0, &r1, &r2, &r3, &r4);
}

// poly1305 authenticator: https://tools.ietf.org/html/rfc7539
// NOTE: must use constant time mem comparison when verifying mac to defend against timing attacks
void BRPoly1305(void *mac16, const void *key32, const void *data, size_t dataLen)
{
    uint32_t h[5] = { 0, 0, 0, 0, 0 };
    
    assert(mac16 != NULL);
    assert(data != NULL || dataLen == 0);
    assert(key32 != NULL);
    
    _BRPoly1305Compress(h, key32, data, dataLen, 1);
    memcpy(mac16, h, 16);
    mem_clean(h, sizeof(h));
}

// basic chacha quarter round operation
#define qr(a, b, c, d) ((a) += (b), (d) = rol32((d) ^ (a), 16), (c) += (d), (b) = rol32((b) ^ (c), 12),\
                        (a) += (b), (d) = rol32((d) ^ (a), 8), (c) += (d), (b) = rol32((b) ^ (c), 7))

// chacha20 stream cipher: https://cr.yp.to/chacha.html
void BRChacha20(void *out, const void *key32, const void *iv8, const void *data, size_t dataLen, uint64_t counter)
{
    static const char sigma[16] = "expand 32-byte k";
    uint32_t b[16], s[16], x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15;
    size_t i, j;
    
    assert(out != NULL || dataLen == 0);
    assert(data != NULL || dataLen == 0);
    assert(key32 != NULL);
    assert(iv8 != NULL);
    
    memcpy(s, sigma, 16);
    memcpy(&s[4], key32, 32);
    s[12] = le32((uint32_t)counter);
    s[13] = le32(counter >> 32);
    memcpy(&s[14], iv8, 8);
    for (i = 0; i < 16; i++) s[i] = le32(s[i]);

    for (i = 0; i < dataLen; i++) {
        if (i % 64 == 0) {
            x0 = s[0], x1 = s[1], x2 = s[2], x3 = s[3], x4 = s[4], x5 = s[5], x6 = s[6], x7 = s[7];
            x8 = s[8], x9 = s[9], x10 = s[10], x11 = s[11], x12 = s[12], x13 = s[13], x14 = s[14], x15 = s[15];
            
            for (j = 0; j < 10; j++) {
                qr(x0, x4, x8, x12), qr(x1, x5, x9, x13), qr(x2, x6, x10, x14), qr(x3, x7, x11, x15);
                qr(x0, x5, x10, x15), qr(x1, x6, x11, x12), qr(x2, x7, x8, x13), qr(x3, x4, x9, x14);
            }
            
            b[0] = le32(s[0] + x0), b[1] = le32(s[1] + x1), b[2] = le32(s[2] + x2), b[3] = le32(s[3] + x3);
            b[4] = le32(s[4] + x4), b[5] = le32(s[5] + x5), b[6] = le32(s[6] + x6), b[7] = le32(s[7] + x7);
            b[8] = le32(s[8] + x8), b[9] = le32(s[9] + x9), b[10] = le32(s[10] + x10), b[11] = le32(s[11] + x11);
            b[12] = le32(s[12] + x12), b[13] = le32(s[13] + x13), b[14] = le32(s[14] + x14), b[15] = le32(s[15] + x15);

            s[12]++;
            if (s[12] == 0) s[13]++;
        }
        
        ((uint8_t *)out)[i] = ((const uint8_t *)data)[i] ^ ((uint8_t *)b)[i % 64];
    }
    
    var_clean(&x0, &x1, &x2, &x3, &x4, &x5, &x6, &x7, &x8, &x9, &x10, &x11, &x12, &x13, &x14, &x15);
    mem_clean(s, sizeof(s));
    mem_clean(b, sizeof(b));
}

// chacha20-poly1305 authenticated encryption with associated data (AEAD): https://tools.ietf.org/html/rfc7539
size_t BRChacha20Poly1305AEADEncrypt(void *out, size_t outLen, const void *key32, const void *nonce12,
                                     const void *data, size_t dataLen, const void *ad, size_t adLen)
{
    const void *iv = (const uint8_t *)nonce12 + 4;
    uint64_t counter = 0, macKey[4] = { 0, 0, 0, 0 }, pad[2] = { 0, 0 };
    uint32_t h[5] = { 0, 0, 0, 0, 0 };

    if (! out) return dataLen + 16;
    if (outLen < dataLen + 16 || dataLen/64 >= UINT32_MAX) return 0;

    assert(key32 != NULL);
    assert(nonce12 != NULL);
    assert(data != NULL || dataLen == 0);
    assert(ad != NULL || adLen == 0);
    
    memcpy(&((uint32_t *)&counter)[1], nonce12, sizeof(uint32_t));    
    BRChacha20(macKey, key32, iv, macKey, sizeof(macKey), le64(counter));
    _BRPoly1305Compress(h, macKey, ad, (adLen/16)*16, 0);
    memcpy(pad, (const uint8_t *)ad + (adLen/16)*16, adLen % 16);
    if (adLen % 16) _BRPoly1305Compress(h, macKey, pad, 16, 0);
    BRChacha20(out, key32, iv, data, dataLen, le64(counter) + 1);
    _BRPoly1305Compress(h, macKey, out, (dataLen/16)*16, 0);
    pad[0] = pad[1] = 0;
    memcpy(pad, (const uint8_t *)out + (dataLen/16)*16, dataLen % 16);
    if (dataLen % 16) _BRPoly1305Compress(h, macKey, pad, 16, 0);
    pad[0] = le64(adLen);
    pad[1] = le64(dataLen);
    _BRPoly1305Compress(h, macKey, pad, 16, 1);
    mem_clean(macKey, sizeof(macKey));
    memcpy((uint8_t *)out + dataLen, h, 16);
    return dataLen + 16;
}

size_t BRChacha20Poly1305AEADDecrypt(void *out, size_t outLen, const void *key32, const void *nonce12,
                                     const void *data, size_t dataLen, const void *ad, size_t adLen)
{
    const void *iv = (const uint8_t *)nonce12 + 4;
    uint64_t counter = 0, macKey[4] = { 0, 0, 0, 0 }, pad[2] = { 0, 0 };
    uint32_t h[5] = { 0, 0, 0, 0, 0 }, mac[4];
    
    if (! out) return (dataLen < 16) ? 0 : dataLen - 16;
    if (dataLen < 16 || (dataLen - 16)/64 >= UINT32_MAX || outLen + 16 < dataLen) return 0;
    
    assert(key32 != NULL);
    assert(nonce12 != NULL);
    assert(data != NULL || dataLen == 0);
    assert(ad != NULL || adLen == 0);

    outLen = dataLen - 16;
    memcpy(&((uint32_t *)&counter)[1], nonce12, sizeof(uint32_t));
    BRChacha20(macKey, key32, iv, macKey, sizeof(macKey), le64(counter));
    _BRPoly1305Compress(h, macKey, ad, (adLen/16)*16, 0);
    memcpy(pad, (const uint8_t *)ad + (adLen/16)*16, adLen % 16);
    if (adLen % 16) _BRPoly1305Compress(h, macKey, pad, 16, 0);
    _BRPoly1305Compress(h, macKey, data, (outLen/16)*16, 0);
    pad[0] = pad[1] = 0;
    memcpy(pad, (const uint8_t *)data + (outLen/16)*16, outLen % 16);
    if (outLen % 16) _BRPoly1305Compress(h, macKey, pad, 16, 0);
    pad[0] = le64(adLen);
    pad[1] = le64(outLen);
    _BRPoly1305Compress(h, macKey, pad, 16, 1);
    mem_clean(macKey, sizeof(macKey));
    memcpy(mac, (const uint8_t *)data + outLen, 16);
    if ((mac[0] ^ h[0]) | (mac[1] ^ h[1]) | (mac[2] ^ h[2]) | (mac[3] ^ h[3]) != 0) outLen = 0; // constant time compare
    BRChacha20(out, key32, iv, data, outLen, le64(counter) + 1);
    return outLen;
}

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t sboxi[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

#define xt(x) (((x) << 1) ^ ((((x) >> 7) & 1)*0x1b))

static void _BRAESExpandKey(uint8_t k[256], const void *key, size_t kl)
{
    uint8_t r = 1;
    size_t i, j, rounds = kl/4 + 6;
    
    memcpy(k, key, kl);
    
    for (i = kl; i <= 16*rounds; i += kl) {
        k[i] = k[i - kl] ^ sbox[k[i - 3]] ^ r, k[i + 1] = k[i + 1 - kl] ^ sbox[k[i - 2]];
        k[i + 2] = k[i + 2 - kl] ^ sbox[k[i - 1]], k[i + 3] = k[i + 3 - kl] ^ sbox[k[i - 4]], r = xt(r);
        for (j = i + 4; j < i + kl; j++) k[j] = k[j - kl] ^ ((kl == 32 && (j % 16) < 4) ? sbox[k[j - 4]] : k[j - 4]);
    }
}

static void _BRAESCipher(uint8_t x[16], const uint8_t k[256], size_t kl)
{
    uint8_t a, b, c, d, e;
    size_t i, j, rounds = kl/4 + 6;
    
    for (j = 0; j < 16; j++) x[j] ^= k[j]; // first add round key
    
    for (i = 0; i < rounds; i++) {
        for (j = 0; j < 16; j++) x[j] = sbox[x[j]]; // sub bytes
        
        // shift rows
        a = x[1], x[1] = x[5], x[5] = x[9], x[9] = x[13], x[13] = a, a = x[10], x[10] = x[2], x[2] = a;
        a = x[3], x[3] = x[15], x[15] = x[11], x[11] = x[7], x[7] = a, a = x[14], x[14] = x[6], x[6] = a;
        
        for (j = 0; i < rounds - 1 && j < 16; j += 4) { // mix columns
            a = x[j], b = x[j + 1], c = x[j + 2], d = x[j + 3], e = a ^ b ^ c ^ d;
            x[j] ^= e ^ xt(a ^ b), x[j + 1] ^= e ^ xt(b ^ c), x[j + 2] ^= e ^ xt(c ^ d), x[j + 3] ^= e ^ xt(d ^ a);
        }
        
        for (j = 0; j < 16; j++) x[j] ^= k[(i + 1)*16 + j]; // add round key
    }
    
    var_clean(&a, &b, &c, &d, &e);
}

static void _BRAESDecipher(uint8_t x[16], const uint8_t k[256], size_t kl)
{
    uint8_t a, b, c, d, e, f, g;
    size_t i, j, rounds = kl/4 + 6;
    
    for (j = 0; j < 16; j++) x[j] ^= k[rounds*16 + j]; // first add round key
    
    for (i = rounds; i > 0; i--) {
        // unshift rows
        a = x[1], x[1] = x[13], x[13] = x[9], x[9] = x[5], x[5] = a, a = x[2], x[2] = x[10], x[10] = a;
        a = x[3], x[3] = x[7], x[7] = x[11], x[11] = x[15], x[15] = a, a = x[6], x[6] = x[14], x[14] = a;
        
        for (j = 0; j < 16; j++) x[j] = sboxi[x[j]]; // unsub bytes
        
        for (j = 0; j < 16; j++) x[j] ^= k[(i - 1)*16 + j]; // add round key
        
        for (j = 0; i > 1 && j < 16; j += 4) { // unmix columns
            a = x[j], b = x[j + 1], c = x[j + 2], d = x[j + 3], e = a ^ b ^ c ^ d;
            f = e ^ xt(xt(xt(e) ^ a ^ c)), g = e ^ xt(xt(xt(e) ^ b ^ d));
            x[j] ^= f ^ xt(a ^ b), x[j + 1] ^= g ^ xt(b ^ c), x[j + 2] ^= f ^ xt(c ^ d), x[j + 3] ^= g ^ xt(d ^ a);
        }
    }
    
    var_clean(&a, &b, &c, &d, &e, &f, &g);
}

// aes-ecb block cipher
void BRAESECBEncrypt(void *buf16, const void *key, size_t keyLen)
{
    uint8_t k[256];
    
    assert(buf16 != NULL);
    assert(key != NULL);
    assert(keyLen == 16 || keyLen == 24 || keyLen == 32);
    
    _BRAESExpandKey(k, key, keyLen);
    _BRAESCipher(buf16, k, keyLen);
    mem_clean(k, sizeof(k));
}

void BRAESECBDecrypt(void *buf16, const void *key, size_t keyLen)
{
    uint8_t k[256];
    
    assert(buf16 != NULL);
    assert(key != NULL);
    assert(keyLen == 16 || keyLen == 24 || keyLen == 32);
    
    _BRAESExpandKey(k, key, keyLen);
    _BRAESDecipher(buf16, k, keyLen);
    mem_clean(k, sizeof(k));
}

// aes-ctr stream cipher encrypt/decrypt
void BRAESCTR(void *out, const void *key, size_t keyLen, const void *iv16, const void *data, size_t dataLen)
{
    uint8_t x[16], iv[16], k[256];
    size_t off, i;
    
    assert(out != NULL);
    assert(key != NULL);
    assert(keyLen == 16 || keyLen == 24 || keyLen == 32);
    assert(iv16 != NULL);
    assert(data != NULL || dataLen == 0);
    
    memcpy(iv, iv16, 16);
    _BRAESExpandKey(k, key, keyLen);
    
    for (off = 0; off < dataLen; off++) {
        if ((off % 16) == 0) { // generate xor compliment
            memcpy(x, iv, 16);
            _BRAESCipher(x, k, keyLen);
            i = 16;
            do { iv[--i]++; } while (iv[i] == 0 && i > 0); // increment iv with overflow
        }
        
        ((uint8_t *)out)[off] = (((uint8_t *)data)[off] ^ x[off % 16]);
    }
    
    mem_clean(k, sizeof(k));
    mem_clean(x, sizeof(x));
}
// aes-ctr stream cipher encrypt/decrypt
void BRAESCTR_OFFSET(void *out, size_t outLen, const void *key, size_t keyLen, void *iv16, const void *data, size_t dataLen)
{
    uint8_t x[16], iv[16], k[256];
    size_t off, i, outIdx = 0;
    
    assert(out != NULL);
    assert(key != NULL);
    assert(keyLen == 16 || keyLen == 24 || keyLen == 32);
    assert(iv16 != NULL);
    assert(data != NULL || dataLen == 0);
    
    memcpy(iv, iv16, 16);
    _BRAESExpandKey(k, key, keyLen);
    
    for (off = (dataLen - outLen); off < dataLen; off++, outIdx++) {
        if ((off % 16) == 0) { // generate xor compliment
            memcpy(x, iv, 16);
            _BRAESCipher(x, k, keyLen);
            i = 16;
            do { iv[--i]++; } while (iv[i] == 0 && i > 0); // increment iv with overflow
        }
        ((uint8_t *)out)[outIdx] = (((uint8_t *)data)[outIdx] ^ x[outIdx % 16]);
    }
    memcpy(iv16, iv, 16);
    mem_clean(k, sizeof(k));
    mem_clean(x, sizeof(x));
}



// dk = T1 || T2 || ... || Tdklen/hlen
// Ti = U1 xor U2 xor ... xor Urounds
// U1 = hmac_hash(pw, salt || be32(i))
// U2 = hmac_hash(pw, U1)
// ...
// Urounds = hmac_hash(pw, Urounds-1)
void BRPBKDF2(void *dk, size_t dkLen, void (*hash)(void *, const void *, size_t), size_t hashLen,
              const void *pw, size_t pwLen, const void *salt, size_t saltLen, unsigned rounds)
{
    uint8_t s[saltLen + sizeof(uint32_t)];
    uint32_t i, j, U[hashLen/sizeof(uint32_t)], T[hashLen/sizeof(uint32_t)];
    
    assert(dk != NULL || dkLen == 0);
    assert(hash != NULL);
    assert(hashLen > 0 && (hashLen % 4) == 0);
    assert(pw != NULL || pwLen == 0);
    assert(salt != NULL || saltLen == 0);
    assert(rounds > 0);
    
    memcpy(s, salt, saltLen);
    
    for (i = 0; i < (dkLen + hashLen - 1)/hashLen; i++) {
        j = be32(i + 1);
        memcpy(s + saltLen, &j, sizeof(j));
        BRHMAC(U, hash, hashLen, pw, pwLen, s, sizeof(s)); // U1 = hmac_hash(pw, salt || be32(i))
        memcpy(T, U, sizeof(U));
        
        for (unsigned r = 1; r < rounds; r++) {
            BRHMAC(U, hash, hashLen, pw, pwLen, U, sizeof(U)); // Urounds = hmac_hash(pw, Urounds-1)
            for (j = 0; j < hashLen/sizeof(uint32_t); j++) T[j] ^= U[j]; // Ti = U1 ^ U2 ^ ... ^ Urounds
        }
        
        // dk = T1 || T2 || ... || Tdklen/hlen
        memcpy((uint8_t *)dk + i*hashLen, T, (i*hashLen + hashLen <= dkLen) ? hashLen : dkLen % hashLen);
    }
    
    mem_clean(s, sizeof(s));
    mem_clean(U, sizeof(U));
    mem_clean(T, sizeof(T));
}

// salsa20/8 stream cipher: http://cr.yp.to/snuffle.html
static void _salsa20_8(uint32_t b[16])
{
    uint32_t x0 = b[0], x1 = b[1], x2 = b[2],  x3 = b[3],  x4 = b[4],  x5 = b[5],  x6 = b[6],  x7 = b[7],
             x8 = b[8], x9 = b[9], xa = b[10], xb = b[11], xc = b[12], xd = b[13], xe = b[14], xf = b[15];
    
    for (unsigned i = 0; i < 8; i += 2) {
        // operate on columns
        x4 ^= rol32(x0 + xc, 7), x8 ^= rol32(x4 + x0, 9), xc ^= rol32(x8 + x4, 13), x0 ^= rol32(xc + x8, 18);
        x9 ^= rol32(x5 + x1, 7), xd ^= rol32(x9 + x5, 9), x1 ^= rol32(xd + x9, 13), x5 ^= rol32(x1 + xd, 18);
        xe ^= rol32(xa + x6, 7), x2 ^= rol32(xe + xa, 9), x6 ^= rol32(x2 + xe, 13), xa ^= rol32(x6 + x2, 18);
        x3 ^= rol32(xf + xb, 7), x7 ^= rol32(x3 + xf, 9), xb ^= rol32(x7 + x3, 13), xf ^= rol32(xb + x7, 18);
        
        // operate on rows
        x1 ^= rol32(x0 + x3, 7), x2 ^= rol32(x1 + x0, 9), x3 ^= rol32(x2 + x1, 13), x0 ^= rol32(x3 + x2, 18);
        x6 ^= rol32(x5 + x4, 7), x7 ^= rol32(x6 + x5, 9), x4 ^= rol32(x7 + x6, 13), x5 ^= rol32(x4 + x7, 18);
        xb ^= rol32(xa + x9, 7), x8 ^= rol32(xb + xa, 9), x9 ^= rol32(x8 + xb, 13), xa ^= rol32(x9 + x8, 18);
        xc ^= rol32(xf + xe, 7), xd ^= rol32(xc + xf, 9), xe ^= rol32(xd + xc, 13), xf ^= rol32(xe + xd, 18);
    }
    
    b[0] += x0, b[1] += x1, b[2] += x2,  b[3] += x3,  b[4] += x4,  b[5] += x5,  b[6] += x6,  b[7] += x7;
    b[8] += x8, b[9] += x9, b[10] += xa, b[11] += xb, b[12] += xc, b[13] += xd, b[14] += xe, b[15] += xf;
}

static void _blockmix_salsa8(uint64_t *dest, const uint64_t *src, uint64_t *b, unsigned r)
{
    memcpy(b, &src[(2*r - 1)*8], 64);
    
    for (unsigned i = 0; i < 2*r; i += 2) {
        for (unsigned j = 0; j < 8; j++) b[j] ^= src[i*8 + j];
        _salsa20_8((uint32_t *)b);
        memcpy(&dest[i*4], b, 64);
        for (unsigned j = 0; j < 8; j++) b[j] ^= src[i*8 + 8 + j];
        _salsa20_8((uint32_t *)b);
        memcpy(&dest[i*4 + r*8], b, 64);
    }
}

// scrypt key derivation: http://www.tarsnap.com/scrypt.html
void BRScrypt(void *dk, size_t dkLen, const void *pw, size_t pwLen, const void *salt, size_t saltLen,
              unsigned n, unsigned r, unsigned p)
{
    uint64_t x[16*r], y[16*r], z[8], *v = malloc(128*r*n), m;
    uint32_t b[32*r*p];
    
    assert(v != NULL);
    assert(dk != NULL || dkLen == 0);
    assert(pw != NULL || pwLen == 0);
    assert(salt != NULL || saltLen == 0);
    assert(n > 0);
    assert(r > 0);
    assert(p > 0);
    
    BRPBKDF2(b, sizeof(b), BRSHA256, 256/8, pw, pwLen, salt, saltLen, 1);
    
    for (int i = 0; i < p; i++) {
        for (unsigned j = 0; j < 32*r; j++) ((uint32_t *)x)[j] = le32(b[i*32*r + j]);
        
        for (unsigned j = 0; j < n; j += 2) {
            memcpy(&v[j*(16*r)], x, 128*r);
            _blockmix_salsa8(y, x, z, r);
            memcpy(&v[(j + 1)*(16*r)], y, 128*r);
            _blockmix_salsa8(x, y, z, r);
        }
        
        for (unsigned j = 0; j < n; j += 2) {
            m = le64(x[(2*r - 1)*8]) & (n - 1);
            for (unsigned k = 0; k < 16*r; k++) x[k] ^= v[m*(16*r) + k];
            _blockmix_salsa8(y, x, z, r);
            m = le64(y[(2*r - 1)*8]) & (n - 1);
            for (unsigned k = 0; k < 16*r; k++) y[k] ^= v[m*(16*r) + k];
            _blockmix_salsa8(x, y, z, r);
        }
        
        for (unsigned j = 0; j < 32*r; j++) b[i*32*r + j] = le32(((uint32_t *)x)[j]);
    }
    
    BRPBKDF2(dk, dkLen, BRSHA256, 256/8, pw, pwLen, b, sizeof(b), 1);
    mem_clean(b, sizeof(b));
    mem_clean(x, sizeof(x));
    mem_clean(y, sizeof(y));
    mem_clean(z, sizeof(z));
    mem_clean(v, 128*r*n);
    free(v);
}

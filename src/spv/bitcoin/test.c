//
//  test.c
//
//  Created by Aaron Voisine on 8/14/15.
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
#include "BRBloomFilter.h"
#include "BRMerkleBlock.h"
#include "BRWallet.h"
#include "BRKey.h"
#include "BRBIP38Key.h"
#include "BRKeyECIES.h"
#include "BRAddress.h"
#include "BRBase58.h"
#include "BRBech32.h"
#include "BRBIP39Mnemonic.h"
#include "BRBIP39WordsEn.h"
#include "BRPeer.h"
#include "BRPeerManager.h"
#include "BRChainParams.h"
#include "bcash/BRBCashParams.h"
#include "bcash/BRBCashAddr.h"
#include "BRPaymentProtocol.h"
#include "BRInt.h"
#include "BRArray.h"
#include "BRSet.h"
#include "BRTransaction.h"
#include "BRWalletManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SKIP_BIP38 1

#ifdef __ANDROID__
#include <android/log.h>
#define fprintf(...) __android_log_print(ANDROID_LOG_ERROR, "bread", _va_rest(__VA_ARGS__, NULL))
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "bread", __VA_ARGS__)
#define _va_first(first, ...) first
#define _va_rest(first, ...) __VA_ARGS__
#endif

#if BITCOIN_TESTNET
#define BR_CHAIN_PARAMS BRTestNetParams
#else
#define BR_CHAIN_PARAMS BRMainNetParams
#endif

int BRIntsTests()
{
    // test endianess
    
    int r = 1;
    union {
        uint8_t u8[8];
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
    } x = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    
    if (UInt16GetBE(&x) != 0x0102) r = 0, fprintf(stderr, "***FAILED*** %s: UInt16GetBE() test\n", __func__);
    if (UInt16GetLE(&x) != 0x0201) r = 0, fprintf(stderr, "***FAILED*** %s: UInt16GetLE() test\n", __func__);
    if (UInt32GetBE(&x) != 0x01020304) r = 0, fprintf(stderr, "***FAILED*** %s: UInt32GetBE() test\n", __func__);
    if (UInt32GetLE(&x) != 0x04030201) r = 0, fprintf(stderr, "***FAILED*** %s: UInt32GetLE() test\n", __func__);
    if (UInt64GetBE(&x) != 0x0102030405060708)
        r = 0, fprintf(stderr, "***FAILED*** %s: UInt64GetBE() test\n", __func__);
    if (UInt64GetLE(&x) != 0x0807060504030201)
        r = 0, fprintf(stderr, "***FAILED*** %s: UInt64GetLE() test\n", __func__);

    UInt16SetBE(&x, 0x0201);
    if (x.u8[0] != 0x02 || x.u8[1] != 0x01) r = 0, fprintf(stderr, "***FAILED*** %s: UInt16SetBE() test\n", __func__);

    UInt16SetLE(&x, 0x0201);
    if (x.u8[0] != 0x01 || x.u8[1] != 0x02) r = 0, fprintf(stderr, "***FAILED*** %s: UInt16SetLE() test\n", __func__);

    UInt32SetBE(&x, 0x04030201);
    if (x.u8[0] != 0x04 || x.u8[1] != 0x03 || x.u8[2] != 0x02 || x.u8[3] != 0x01)
        r = 0, fprintf(stderr, "***FAILED*** %s: UInt32SetBE() test\n", __func__);

    UInt32SetLE(&x, 0x04030201);
    if (x.u8[0] != 0x01 || x.u8[1] != 0x02 || x.u8[2] != 0x03 || x.u8[3] != 0x04)
        r = 0, fprintf(stderr, "***FAILED*** %s: UInt32SetLE() test\n", __func__);

    UInt64SetBE(&x, 0x0807060504030201);
    if (x.u8[0] != 0x08 || x.u8[1] != 0x07 || x.u8[2] != 0x06 || x.u8[3] != 0x05 ||
        x.u8[4] != 0x04 || x.u8[5] != 0x03 || x.u8[6] != 0x02 || x.u8[7] != 0x01)
        r = 0, fprintf(stderr, "***FAILED*** %s: UInt64SetBE() test\n", __func__);

    UInt64SetLE(&x, 0x0807060504030201);
    if (x.u8[0] != 0x01 || x.u8[1] != 0x02 || x.u8[2] != 0x03 || x.u8[3] != 0x04 ||
        x.u8[4] != 0x05 || x.u8[5] != 0x06 || x.u8[6] != 0x07 || x.u8[7] != 0x08)
        r = 0, fprintf(stderr, "***FAILED*** %s: UInt64SetLE() test\n", __func__);
    
    return r;
}

int BRArrayTests()
{
    int r = 1;
    int *a = NULL, b[] = { 1, 2, 3 }, c[] = { 3, 2 };
    
    array_new(a, 0);                // [ ]
    if (array_count(a) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: array_new() test\n", __func__);

    array_add(a, 0);                // [ 0 ]
    if (array_count(a) != 1 || a[0] != 0) r = 0, fprintf(stderr, "***FAILED*** %s: array_add() test\n", __func__);

    array_add_array(a, b, 3);       // [ 0, 1, 2, 3 ]
    if (array_count(a) != 4 || a[3] != 3) r = 0, fprintf(stderr, "***FAILED*** %s: array_add_array() test\n", __func__);

    array_insert(a, 0, 1);          // [ 1, 0, 1, 2, 3 ]
    if (array_count(a) != 5 || a[0] != 1) r = 0, fprintf(stderr, "***FAILED*** %s: array_insert() test\n", __func__);

    array_insert_array(a, 0, c, 2); // [ 3, 2, 1, 0, 1, 2, 3 ]
    if (array_count(a) != 7 || a[0] != 3)
        r = 0, fprintf(stderr, "***FAILED*** %s: array_insert_array() test\n", __func__);

    array_rm_range(a, 0, 4);        // [ 1, 2, 3 ]
    if (array_count(a) != 3 || a[0] != 1) r = 0, fprintf(stderr, "***FAILED*** %s: array_rm_range() test\n", __func__);

    printf("\n");
    for (size_t i = 0; i < array_count(a); i++) {
        printf("%i, ", a[i]);       // 1, 2, 3,
    }
    printf("\n");

    array_insert_array(a, 3, c, 2); // [ 1, 2, 3, 3, 2 ]
    if (array_count(a) != 5 || a[4] != 2)
        r = 0, fprintf(stderr, "***FAILED*** %s: array_insert_array() test 2\n", __func__);
    
    array_insert(a, 5, 1);          // [ 1, 2, 3, 3, 2, 1 ]
    if (array_count(a) != 6 || a[5] != 1) r = 0, fprintf(stderr, "***FAILED*** %s: array_insert() test 2\n", __func__);
    
    array_rm(a, 0);                 // [ 2, 3, 3, 2, 1 ]
    if (array_count(a) != 5 || a[0] != 2) r = 0, fprintf(stderr, "***FAILED*** %s: array_rm() test\n", __func__);

    array_rm_last(a);               // [ 2, 3, 3, 2 ]
    if (array_count(a) != 4 || a[0] != 2) r = 0, fprintf(stderr, "***FAILED*** %s: array_rm_last() test\n", __func__);
    
    array_clear(a);                 // [ ]
    if (array_count(a) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: array_clear() test\n", __func__);

    array_free(a);
    
    printf("                                    ");
    return r;
}

//inline static int compare_int(void *info, const void *a, const void *b)
//{
//    if (*(int *)a < *(int *)b) return -1;
//    if (*(int *)a > *(int *)b) return 1;
//    return 0;
//}

inline static size_t hash_int(const void *i)
{
    return (size_t)((0x811C9dc5 ^ *(const unsigned *)i)*0x01000193); // (FNV_OFFSET xor i)*FNV_PRIME
}

inline static int eq_int(const void *a, const void *b)
{
    return (*(const int *)a == *(const int *)b);
}

int BRSetTests()
{
    int r = 1;
    int i, x[1000];
    BRSet *s = BRSetNew(hash_int, eq_int, 0);
    
    for (i = 0; i < 1000; i++) {
        x[i] = i;
        BRSetAdd(s, &x[i]);
    }
    
    if (BRSetCount(s) != 1000) r = 0, fprintf(stderr, "***FAILED*** %s: BRSetAdd() test\n", __func__);
    
    for (i = 999; i >= 0; i--) {
        if (*(int *)BRSetGet(s, &i) != i) r = 0, fprintf(stderr, "***FAILED*** %s: BRSetGet() test %d\n", __func__, i);
    }
    
    for (i = 0; i < 500; i++) {
        if (*(int *)BRSetRemove(s, &i) != i)
            r = 0, fprintf(stderr, "***FAILED*** %s: BRSetRemove() test %d\n", __func__, i);
    }

    if (BRSetCount(s) != 500) r = 0, fprintf(stderr, "***FAILED*** %s: BRSetCount() test 1\n", __func__);

    for (i = 999; i >= 500; i--) {
        if (*(int *)BRSetRemove(s, &i) != i)
            r = 0, fprintf(stderr, "***FAILED*** %s: BRSetRemove() test %d\n", __func__, i);
    }

    if (BRSetCount(s) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRSetCount() test 2\n", __func__);
    
    return r;
}

int BRBase58Tests()
{
    int r = 1;
    char *s;
    
    s = "#&$@*^(*#!^"; // test bad input
    
    uint8_t buf1[BRBase58Decode(NULL, 0, s)];
    size_t len1 = BRBase58Decode(buf1, sizeof(buf1), s);

    if (len1 != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58Decode() test 1\n", __func__);

    uint8_t buf2[BRBase58Decode(NULL, 0, "")];
    size_t len2 = BRBase58Decode(buf2, sizeof(buf2), "");
    
    if (len2 != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58Decode() test 2\n", __func__);
    
    s = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    
    uint8_t buf3[BRBase58Decode(NULL, 0, s)];
    size_t len3 = BRBase58Decode(buf3, sizeof(buf3), s);
    char str3[BRBase58Encode(NULL, 0, buf3, len3)];
    
    BRBase58Encode(str3, sizeof(str3), buf3, len3);
    if (strcmp(str3, s) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58Decode() test 3\n", __func__);

    s = "1111111111111111111111111111111111111111111111111111111111111111111";

    uint8_t buf4[BRBase58Decode(NULL, 0, s)];
    size_t len4 = BRBase58Decode(buf4, sizeof(buf4), s);
    char str4[BRBase58Encode(NULL, 0, buf4, len4)];
    
    BRBase58Encode(str4, sizeof(str4), buf4, len4);
    if (strcmp(str4, s) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58Decode() test 4\n", __func__);

    s = "111111111111111111111111111111111111111111111111111111111111111111z";

    uint8_t buf5[BRBase58Decode(NULL, 0, s)];
    size_t len5 = BRBase58Decode(buf5, sizeof(buf5), s);
    char str5[BRBase58Encode(NULL, 0, buf5, len5)];
    
    BRBase58Encode(str5, sizeof(str5), buf5, len5);
    if (strcmp(str5, s) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58Decode() test 5\n", __func__);

    s = "z";
    
    uint8_t buf6[BRBase58Decode(NULL, 0, s)];
    size_t len6 = BRBase58Decode(buf6, sizeof(buf6), s);
    char str6[BRBase58Encode(NULL, 0, buf6, len6)];
    
    BRBase58Encode(str6, sizeof(str6), buf6, len6);
    if (strcmp(str6, s) != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58Decode() test 6\n", __func__);

    s = NULL;
    
    char s1[BRBase58CheckEncode(NULL, 0, (uint8_t *)s, 0)];
    size_t l1 = BRBase58CheckEncode(s1, sizeof(s1), (uint8_t *)s, 0);
    uint8_t b1[BRBase58CheckDecode(NULL, 0, s1)];
    
    l1 = BRBase58CheckDecode(b1, sizeof(b1), s1);
    if (l1 != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58CheckDecode() test 1\n", __func__);

    s = "";

    char s2[BRBase58CheckEncode(NULL, 0, (uint8_t *)s, 0)];
    size_t l2 = BRBase58CheckEncode(s2, sizeof(s2), (uint8_t *)s, 0);
    uint8_t b2[BRBase58CheckDecode(NULL, 0, s2)];
    
    l2 = BRBase58CheckDecode(b2, sizeof(b2), s2);
    if (l2 != 0) r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58CheckDecode() test 2\n", __func__);
    
    s = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    
    char s3[BRBase58CheckEncode(NULL, 0, (uint8_t *)s, 21)];
    size_t l3 = BRBase58CheckEncode(s3, sizeof(s3), (uint8_t *)s, 21);
    uint8_t b3[BRBase58CheckDecode(NULL, 0, s3)];
    
    l3 = BRBase58CheckDecode(b3, sizeof(b3), s3);
    if (l3 != 21 || memcmp(s, b3, l3) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58CheckDecode() test 3\n", __func__);

    s = "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01";
    
    char s4[BRBase58CheckEncode(NULL, 0, (uint8_t *)s, 21)];
    size_t l4 = BRBase58CheckEncode(s4, sizeof(s4), (uint8_t *)s, 21);
    uint8_t b4[BRBase58CheckDecode(NULL, 0, s4)];
    
    l4 = BRBase58CheckDecode(b4, sizeof(b4), s4);
    if (l4 != 21 || memcmp(s, b4, l4) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58CheckDecode() test 4\n", __func__);

    s = "\x05\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";
    
    char s5[BRBase58CheckEncode(NULL, 0, (uint8_t *)s, 21)];
    size_t l5 = BRBase58CheckEncode(s5, sizeof(s5), (uint8_t *)s, 21);
    uint8_t b5[BRBase58CheckDecode(NULL, 0, s5)];
    
    l5 = BRBase58CheckDecode(b5, sizeof(b5), s5);
    if (l5 != 21 || memcmp(s, b5, l5) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBase58CheckDecode() test 5\n", __func__);

    return r;
}

int BRBech32Tests()
{
    int r = 1;
    uint8_t b[52];
    char h[84];
    char *s, addr[91];
    size_t l;
    
    s = "\x00\x14\x75\x1e\x76\xe8\x19\x91\x96\xd4\x54\x94\x1c\x45\xd1\xb3\xa3\x23\xf1\x43\x3b\xd6";
    l = BRBech32Decode(h, b, "BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4");
    if (l != 22 || strcmp(h, "bc") || memcmp(s, b, l))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBech32Decode() test 1", __func__);
    
    l = BRBech32Decode(h, b, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
    if (l != 22 || strcmp(h, "bc") || memcmp(s, b, l))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBech32Decode() test 2", __func__);

    l = BRBech32Encode(addr, "bc", b);
    if (l == 0 || strcmp(addr, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBech32Encode() test 2", __func__);

    s = "\x52\x10\x75\x1e\x76\xe8\x19\x91\x96\xd4\x54\x94\x1c\x45\xd1\xb3\xa3\x23";
    l = BRBech32Decode(h, b, "bc1zw508d6qejxtdg4y5r3zarvaryvg6kdaj");
    if (l != 18 || strcmp(h, "bc") || memcmp(s, b, l))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBech32Decode() test 3", __func__);

    l = BRBech32Encode(addr, "bc", b);
    if (l == 0 || strcmp(addr, "bc1zw508d6qejxtdg4y5r3zarvaryvg6kdaj"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBech32Encode() test 3", __func__);

    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

int BRBCashAddrTests()
{
    int r = 1;
    char *s, addr[36];
    size_t l;

    s = "77047ecdd5ae988f30d68e828dad668439ad3e5ebba05680089c80f0be82d889";
    l = BRBCashAddrDecode(addr, s);
    if (l != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 1", __func__);

    // bitcoincash:P2PKH addrs

    s = "bitcoincash:qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a"; // w/ prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 2", __func__);

    s = "qpm2qsznhks23z7629mms6s4cwef74vcwvy22gdx6a"; // w/o prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "1BpEi6DfDAUFd7GtittLSdBeYJvcoaVggu"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 3", __func__);

    // bitcoincash P2SH addrs

    s = "bitcoincash:pr95sy3j9xwd2ap32xkykttr4cvcu7as4yc93ky28e"; // w/ prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "3LDsS579y7sruadqu11beEJoTjdFiFCdX4"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 4", __func__);

    s = "pr95sy3j9xwd2ap32xkykttr4cvcu7as4yc93ky28e"; // w/o prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "3LDsS579y7sruadqu11beEJoTjdFiFCdX4"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 5", __func__);

    // bchtest:P2PKH addrs

    s = "bchtest:qpm2qsznhks23z7629mms6s4cwef74vcwvqcw003ap"; // w/ prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "mrLC19Je2BuWQDkWSTriGYPyQJXKkkBmCx"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 6", __func__);

    s = "qpm2qsznhks23z7629mms6s4cwef74vcwvqcw003ap"; // w/o prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "mrLC19Je2BuWQDkWSTriGYPyQJXKkkBmCx"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 7", __func__);

    // bchtest P2SH addrs

    s = "bchtest:pr95sy3j9xwd2ap32xkykttr4cvcu7as4yuh43xaq9"; // w/ prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "2NBn5Vp3BaaPD7NGPa8dUGBJ4g5qRXq92wG"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 8", __func__);

    s = "pr95sy3j9xwd2ap32xkykttr4cvcu7as4yuh43xaq9"; // w/o prefix string
    l = BRBCashAddrDecode(addr, s);
    if (l == 0 || strcmp(addr, "2NBn5Vp3BaaPD7NGPa8dUGBJ4g5qRXq92wG"))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRBCashAddrDecode() test 9", __func__);

    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

int BRHashTests()
{
    // test sha1
    
    int r = 1;
    uint8_t md[64];
    char *s;
    
    s = "Free online SHA1 Calculator, type text here...";
    BRSHA1(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x6f\xc2\xe2\x51\x72\xcb\x15\x19\x3c\xb1\xc6\xd4\x8f\x60\x7d\x42\xc1\xd2\xa2\x15",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA1() test 1", __func__);
        
    s = "this is some text to test the sha1 implementation with more than 64bytes of data since it's internal digest "
        "buffer is 64bytes in size";
    BRSHA1(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x08\x51\x94\x65\x8a\x92\x35\xb2\x95\x1a\x83\xd1\xb8\x26\xb9\x87\xe9\x38\x5a\xa3",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA1() test 2", __func__);
        
    s = "123456789012345678901234567890123456789012345678901234567890";
    BRSHA1(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x24\x5b\xe3\x00\x91\xfd\x39\x2f\xe1\x91\xf4\xbf\xce\xc2\x2d\xcb\x30\xa0\x3a\xe6",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA1() test 3", __func__);
    
    // a message exactly 64bytes long (internal buffer size)
    s = "1234567890123456789012345678901234567890123456789012345678901234";
    BRSHA1(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\xc7\x14\x90\xfc\x24\xaa\x3d\x19\xe1\x12\x82\xda\x77\x03\x2d\xd9\xcd\xb3\x31\x03",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA1() test 4", __func__);
    
    s = ""; // empty
    BRSHA1(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\xda\x39\xa3\xee\x5e\x6b\x4b\x0d\x32\x55\xbf\xef\x95\x60\x18\x90\xaf\xd8\x07\x09",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA1() test 5", __func__);
    
    s = "a";
    BRSHA1(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x86\xf7\xe4\x37\xfa\xa5\xa7\xfc\xe1\x5d\x1d\xdc\xb9\xea\xea\xea\x37\x76\x67\xb8",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA1() test 6", __func__);

    // test sha256
    
    s = "Free online SHA256 Calculator, type text here...";
    BRSHA256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\x43\xfd\x9d\xeb\x93\xf6\xe1\x4d\x41\x82\x66\x04\x51\x4e\x3d\x78\x73\xa5\x49\xac"
                    "\x87\xae\xbe\xbf\x3d\x1c\x10\xad\x6e\xb0\x57\xd0", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA256() test 1", __func__);
        
    s = "this is some text to test the sha256 implementation with more than 64bytes of data since it's internal "
        "digest buffer is 64bytes in size";
    BRSHA256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\x40\xfd\x09\x33\xdf\x2e\x77\x47\xf1\x9f\x7d\x39\xcd\x30\xe1\xcb\x89\x81\x0a\x7e"
                    "\x47\x06\x38\xa5\xf6\x23\x66\x9f\x3d\xe9\xed\xd4", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA256() test 2", __func__);
    
    s = "123456789012345678901234567890123456789012345678901234567890";
    BRSHA256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\xde\xcc\x53\x8c\x07\x77\x86\x96\x6a\xc8\x63\xb5\x53\x2c\x40\x27\xb8\x58\x7f\xf4"
                    "\x0f\x6e\x31\x03\x37\x9a\xf6\x2b\x44\xea\xe4\x4d", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA256() test 3", __func__);
    
    // a message exactly 64bytes long (internal buffer size)
    s = "1234567890123456789012345678901234567890123456789012345678901234";
    BRSHA256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\x67\x64\x91\x96\x5e\xd3\xec\x50\xcb\x7a\x63\xee\x96\x31\x54\x80\xa9\x5c\x54\x42"
                    "\x6b\x0b\x72\xbc\xa8\xa0\xd4\xad\x12\x85\xad\x55", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA256() test 4", __func__);
    
    s = ""; // empty
    BRSHA256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24\x27\xae\x41\xe4"
                    "\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA256() test 5", __func__);
    
    s = "a";
    BRSHA256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\xca\x97\x81\x12\xca\x1b\xbd\xca\xfa\xc2\x31\xb3\x9a\x23\xdc\x4d\xa7\x86\xef\xf8"
                    "\x14\x7c\x4e\x72\xb9\x80\x77\x85\xaf\xee\x48\xbb", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA256() test 6", __func__);

    // test sha512
    
    s = "Free online SHA512 Calculator, type text here...";
    BRSHA512(md, s, strlen(s));
    if (! UInt512Eq(*(UInt512 *)"\x04\xf1\x15\x41\x35\xee\xcb\xe4\x2e\x9a\xdc\x8e\x1d\x53\x2f\x9c\x60\x7a\x84\x47"
                    "\xb7\x86\x37\x7d\xb8\x44\x7d\x11\xa5\xb2\x23\x2c\xdd\x41\x9b\x86\x39\x22\x4f\x78\x7a\x51"
                    "\xd1\x10\xf7\x25\x91\xf9\x64\x51\xa1\xbb\x51\x1c\x4a\x82\x9e\xd0\xa2\xec\x89\x13\x21\xf3",
                    *(UInt512 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA512() test 1", __func__);
    
    s = "this is some text to test the sha512 implementation with more than 128bytes of data since it's internal "
        "digest buffer is 128bytes in size";
    BRSHA512(md, s, strlen(s));
    if (! UInt512Eq(*(UInt512 *)"\x9b\xd2\xdc\x7b\x05\xfb\xbe\x99\x34\xcb\x32\x89\xb6\xe0\x6b\x8c\xa9\xfd\x7a\x55"
                    "\xe6\xde\x5d\xb7\xe1\xe4\xee\xdd\xc6\x62\x9b\x57\x53\x07\x36\x7c\xd0\x18\x3a\x44\x61\xd7"
                    "\xeb\x2d\xfc\x6a\x27\xe4\x1e\x8b\x70\xf6\x59\x8e\xbc\xc7\x71\x09\x11\xd4\xfb\x16\xa3\x90",
                    *(UInt512 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA512() test 2", __func__);
    
    s = "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567"
        "8901234567890";
    BRSHA512(md, s, strlen(s));
    if (! UInt512Eq(*(UInt512 *)"\x0d\x9a\x7d\xf5\xb6\xa6\xad\x20\xda\x51\x9e\xff\xda\x88\x8a\x73\x44\xb6\xc0\xc7"
                    "\xad\xcc\x8e\x2d\x50\x4b\x4a\xf2\x7a\xaa\xac\xd4\xe7\x11\x1c\x71\x3f\x71\x76\x95\x39\x62"
                    "\x94\x63\xcb\x58\xc8\x61\x36\xc5\x21\xb0\x41\x4a\x3c\x0e\xdf\x7d\xc6\x34\x9c\x6e\xda\xf3",
                    *(UInt512 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA512() test 3", __func__);
    
    //exactly 128bytes (internal buf size)
    s = "12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567"
        "890123456789012345678";
    BRSHA512(md, s, strlen(s));
    if (! UInt512Eq(*(UInt512 *)"\x22\x2b\x2f\x64\xc2\x85\xe6\x69\x96\x76\x9b\x5a\x03\xef\x86\x3c\xfd\x3b\x63\xdd"
                    "\xb0\x72\x77\x88\x29\x16\x95\xe8\xfb\x84\x57\x2e\x4b\xfe\x5a\x80\x67\x4a\x41\xfd\x72\xee"
                    "\xb4\x85\x92\xc9\xc7\x9f\x44\xae\x99\x2c\x76\xed\x1b\x0d\x55\xa6\x70\xa8\x3f\xc9\x9e\xc6",
                    *(UInt512 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA512() test 4", __func__);
    
    s = ""; // empty
    BRSHA512(md, s, strlen(s));
    if (! UInt512Eq(*(UInt512 *)"\xcf\x83\xe1\x35\x7e\xef\xb8\xbd\xf1\x54\x28\x50\xd6\x6d\x80\x07\xd6\x20\xe4\x05"
                    "\x0b\x57\x15\xdc\x83\xf4\xa9\x21\xd3\x6c\xe9\xce\x47\xd0\xd1\x3c\x5d\x85\xf2\xb0\xff\x83"
                    "\x18\xd2\x87\x7e\xec\x2f\x63\xb9\x31\xbd\x47\x41\x7a\x81\xa5\x38\x32\x7a\xf9\x27\xda\x3e",
                    *(UInt512 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA512() test 5", __func__);
    
    s = "a";
    BRSHA512(md, s, strlen(s));
    if (! UInt512Eq(*(UInt512 *)"\x1f\x40\xfc\x92\xda\x24\x16\x94\x75\x09\x79\xee\x6c\xf5\x82\xf2\xd5\xd7\xd2\x8e"
                    "\x18\x33\x5d\xe0\x5a\xbc\x54\xd0\x56\x0e\x0f\x53\x02\x86\x0c\x65\x2b\xf0\x8d\x56\x02\x52"
                    "\xaa\x5e\x74\x21\x05\x46\xf3\x69\xfb\xbb\xce\x8c\x12\xcf\xc7\x95\x7b\x26\x52\xfe\x9a\x75",
                    *(UInt512 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRSHA512() test 6", __func__);
    
    // test ripemd160
    
    s = "Free online RIPEMD160 Calculator, type text here...";
    BRRMD160(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x95\x01\xa5\x6f\xb8\x29\x13\x2b\x87\x48\xf0\xcc\xc4\x91\xf0\xec\xbc\x7f\x94\x5b",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRRMD160() test 1", __func__);
    
    s = "this is some text to test the ripemd160 implementation with more than 64bytes of data since it's internal "
        "digest buffer is 64bytes in size";
    BRRMD160(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x44\x02\xef\xf4\x21\x57\x10\x6a\x5d\x92\xe4\xd9\x46\x18\x58\x56\xfb\xc5\x0e\x09",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRRMD160() test 2", __func__);
    
    s = "123456789012345678901234567890123456789012345678901234567890";
    BRRMD160(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x00\x26\x3b\x99\x97\x14\xe7\x56\xfa\x5d\x02\x81\x4b\x84\x2a\x26\x34\xdd\x31\xac",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRRMD160() test 3", __func__);
    
    // a message exactly 64bytes long (internal buffer size)
    s = "1234567890123456789012345678901234567890123456789012345678901234";
    BRRMD160(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\xfa\x8c\x1a\x78\xeb\x76\x3b\xb9\x7d\x5e\xa1\x4c\xe9\x30\x3d\x1c\xe2\xf3\x34\x54",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRRMD160() test 4", __func__);
    
    s = ""; // empty
    BRRMD160(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x9c\x11\x85\xa5\xc5\xe9\xfc\x54\x61\x28\x08\x97\x7e\xe8\xf5\x48\xb2\x25\x8d\x31",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRRMD160() test 5", __func__);
    
    s = "a";
    BRRMD160(md, s, strlen(s));
    if (! UInt160Eq(*(UInt160 *)"\x0b\xdc\x9d\x2d\x25\x6b\x3e\xe9\xda\xae\x34\x7b\xe6\xf4\xdc\x83\x5a\x46\x7f\xfe",
                    *(UInt160 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRRMD160() test 6", __func__);

    // test md5
    
    s = "Free online MD5 Calculator, type text here...";
    BRMD5(md, s, strlen(s));
    if (! UInt128Eq(*(UInt128 *)"\x0b\x3b\x20\xea\xf1\x69\x64\x62\xf5\x0d\x1a\x3b\xbd\xd3\x0c\xef",
                    *(UInt128 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRMD5() test 1", __func__);
    
    s = "this is some text to test the md5 implementation with more than 64bytes of data since it's internal digest "
          "buffer is 64bytes in size";
    BRMD5(md, s, strlen(s));
    if (! UInt128Eq(*(UInt128 *)"\x56\xa1\x61\xf2\x41\x50\xc6\x2d\x78\x57\xb7\xf3\x54\x92\x7e\xbe",
                    *(UInt128 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRMD5() test 2", __func__);
    
    s = "123456789012345678901234567890123456789012345678901234567890";
    BRMD5(md, s, strlen(s));
    if (! UInt128Eq(*(UInt128 *)"\xc5\xb5\x49\x37\x7c\x82\x6c\xc3\x71\x24\x18\xb0\x64\xfc\x41\x7e",
                    *(UInt128 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRMD5() test 3", __func__);
    
    // a message exactly 64bytes long (internal buffer size)
    s = "1234567890123456789012345678901234567890123456789012345678901234";
    BRMD5(md, s, strlen(s));
    if (! UInt128Eq(*(UInt128 *)"\xeb\x6c\x41\x79\xc0\xa7\xc8\x2c\xc2\x82\x8c\x1e\x63\x38\xe1\x65",
                    *(UInt128 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRMD5() test 4", __func__);
    
    s = ""; // empty
    BRMD5(md, s, strlen(s));
    if (! UInt128Eq(*(UInt128 *)"\xd4\x1d\x8c\xd9\x8f\x00\xb2\x04\xe9\x80\x09\x98\xec\xf8\x42\x7e",
                    *(UInt128 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRMD5() test 5", __func__);
    
    s = "a";
    BRMD5(md, s, strlen(s));
    if (! UInt128Eq(*(UInt128 *)"\x0c\xc1\x75\xb9\xc0\xf1\xb6\xa8\x31\xc3\x99\xe2\x69\x77\x26\x61",
                    *(UInt128 *)md)) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRMD5() test 6", __func__);
    
    // test sha3-256
    
    s = "";
    BRSHA3_256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\xa7\xff\xc6\xf8\xbf\x1e\xd7\x66\x51\xc1\x47\x56\xa0\x61\xd6\x62\xf5\x80\xff\x4d\xe4"
                    "\x3b\x49\xfa\x82\xd8\x0a\x4b\x80\xf8\x43\x4a", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: SHA3-256() test 7", __func__);
    
    s = "abc";
    BRSHA3_256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\x3a\x98\x5d\xa7\x4f\xe2\x25\xb2\x04\x5c\x17\x2d\x6b\xd3\x90\xbd\x85\x5f\x08\x6e\x3e"
                    "\x9d\x52\x5b\x46\xbf\xe2\x45\x11\x43\x15\x32", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: SHA3-256() test 8", __func__);
    
    s =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    BRSHA3_256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\x91\x6f\x60\x61\xfe\x87\x97\x41\xca\x64\x69\xb4\x39\x71\xdf\xdb\x28\xb1\xa3\x2d\xc3"
                    "\x6c\xb3\x25\x4e\x81\x2b\xe2\x7a\xad\x1d\x18", *(UInt256 *)md))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: SHA3-256() test 9", __func__);
    
    // test keccak-256
    
    s = "";
    BRKeccak256(md, s, strlen(s));
    if (! UInt256Eq(*(UInt256 *)"\xc5\xd2\x46\x01\x86\xf7\x23\x3c\x92\x7e\x7d\xb2\xdc\xc7\x03\xc0\xe5\x00\xb6\x53\xca"
                    "\x82\x27\x3b\x7b\xfa\xd8\x04\x5d\x85\xa4\x70", *(UInt256 *)md))
        r = 0, fprintf(stderr, "***FAILED*** %s: Keccak-256() test 1\n", __func__);

    // test murmurHash3-x86_32
    
    if (BRMurmur3_32("", 0, 0) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMurmur3_32() test 1\n", __func__);

    if (BRMurmur3_32("\xFF\xFF\xFF\xFF", 4, 0) != 0x76293b50)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMurmur3_32() test 2\n", __func__);
    
    if (BRMurmur3_32("\x21\x43\x65\x87", 4, 0x5082edee) != 0x2362f9de)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMurmur3_32() test 3\n", __func__);
    
    if (BRMurmur3_32("\x00", 1, 0) != 0x514e28b7)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMurmur3_32() test 4\n", __func__);
    
    // test sipHash-64

    const char k[] = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f";
    const char d[] = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f";

    if (BRSip64(k, d, 0) != 0x726fdb47dd0e0e31) r = 0, fprintf(stderr, "***FAILED*** %s: BRSip64() test 1\n", __func__);

    if (BRSip64(k, d, 1) != 0x74f839c593dc67fd) r = 0, fprintf(stderr, "***FAILED*** %s: BRSip64() test 2\n", __func__);

    if (BRSip64(k, d, 8) != 0x93f5f5799a932462) r = 0, fprintf(stderr, "***FAILED*** %s: BRSip64() test 3\n", __func__);

    if (BRSip64(k, d,15) != 0xa129ca6149be45e5) r = 0, fprintf(stderr, "***FAILED*** %s: BRSip64() test 4\n", __func__);

    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

int BRMacTests()
{
    int r = 1;

    // test hmac
    
    const char k1[] = "\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b\x0b",
    d1[] = "Hi There";
    uint8_t mac[64];
    
    BRHMAC(mac, BRSHA224, 224/8, k1, sizeof(k1) - 1, d1, sizeof(d1) - 1);
    if (memcmp("\x89\x6f\xb1\x12\x8a\xbb\xdf\x19\x68\x32\x10\x7c\xd4\x9d\xf3\x3f\x47\xb4\xb1\x16\x99\x12\xba\x4f\x53"
               "\x68\x4b\x22", mac, 28) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha224 test 1\n", __func__);

    BRHMAC(mac, BRSHA256, 256/8, k1, sizeof(k1) - 1, d1, sizeof(d1) - 1);
    if (memcmp("\xb0\x34\x4c\x61\xd8\xdb\x38\x53\x5c\xa8\xaf\xce\xaf\x0b\xf1\x2b\x88\x1d\xc2\x00\xc9\x83\x3d\xa7\x26"
               "\xe9\x37\x6c\x2e\x32\xcf\xf7", mac, 32) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha256 test 1\n", __func__);

    BRHMAC(mac, BRSHA384, 384/8, k1, sizeof(k1) - 1, d1, sizeof(d1) - 1);
    if (memcmp("\xaf\xd0\x39\x44\xd8\x48\x95\x62\x6b\x08\x25\xf4\xab\x46\x90\x7f\x15\xf9\xda\xdb\xe4\x10\x1e\xc6\x82"
               "\xaa\x03\x4c\x7c\xeb\xc5\x9c\xfa\xea\x9e\xa9\x07\x6e\xde\x7f\x4a\xf1\x52\xe8\xb2\xfa\x9c\xb6", mac, 48)
        != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha384 test 1\n", __func__);

    BRHMAC(mac, BRSHA512, 512/8, k1, sizeof(k1) - 1, d1, sizeof(d1) - 1);
    if (memcmp("\x87\xaa\x7c\xde\xa5\xef\x61\x9d\x4f\xf0\xb4\x24\x1a\x1d\x6c\xb0\x23\x79\xf4\xe2\xce\x4e\xc2\x78\x7a"
               "\xd0\xb3\x05\x45\xe1\x7c\xde\xda\xa8\x33\xb7\xd6\xb8\xa7\x02\x03\x8b\x27\x4e\xae\xa3\xf4\xe4\xbe\x9d"
               "\x91\x4e\xeb\x61\xf1\x70\x2e\x69\x6c\x20\x3a\x12\x68\x54", mac, 64) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha512 test 1\n", __func__);

    const char k2[] = "Jefe",
    d2[] = "what do ya want for nothing?";

    BRHMAC(mac, BRSHA224, 224/8, k2, sizeof(k2) - 1, d2, sizeof(d2) - 1);
    if (memcmp("\xa3\x0e\x01\x09\x8b\xc6\xdb\xbf\x45\x69\x0f\x3a\x7e\x9e\x6d\x0f\x8b\xbe\xa2\xa3\x9e\x61\x48\x00\x8f"
               "\xd0\x5e\x44", mac, 28) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha224 test 2\n", __func__);
    
    BRHMAC(mac, BRSHA256, 256/8, k2, sizeof(k2) - 1, d2, sizeof(d2) - 1);
    if (memcmp("\x5b\xdc\xc1\x46\xbf\x60\x75\x4e\x6a\x04\x24\x26\x08\x95\x75\xc7\x5a\x00\x3f\x08\x9d\x27\x39\x83\x9d"
               "\xec\x58\xb9\x64\xec\x38\x43", mac, 32) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha256 test 2\n", __func__);
    
    BRHMAC(mac, BRSHA384, 384/8, k2, sizeof(k2) - 1, d2, sizeof(d2) - 1);
    if (memcmp("\xaf\x45\xd2\xe3\x76\x48\x40\x31\x61\x7f\x78\xd2\xb5\x8a\x6b\x1b\x9c\x7e\xf4\x64\xf5\xa0\x1b\x47\xe4"
               "\x2e\xc3\x73\x63\x22\x44\x5e\x8e\x22\x40\xca\x5e\x69\xe2\xc7\x8b\x32\x39\xec\xfa\xb2\x16\x49", mac, 48)
        != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha384 test 2\n", __func__);
    
    BRHMAC(mac, BRSHA512, 512/8, k2, sizeof(k2) - 1, d2, sizeof(d2) - 1);
    if (memcmp("\x16\x4b\x7a\x7b\xfc\xf8\x19\xe2\xe3\x95\xfb\xe7\x3b\x56\xe0\xa3\x87\xbd\x64\x22\x2e\x83\x1f\xd6\x10"
               "\x27\x0c\xd7\xea\x25\x05\x54\x97\x58\xbf\x75\xc0\x5a\x99\x4a\x6d\x03\x4f\x65\xf8\xf0\xe6\xfd\xca\xea"
               "\xb1\xa3\x4d\x4a\x6b\x4b\x63\x6e\x07\x0a\x38\xbc\xe7\x37", mac, 64) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMAC() sha512 test 2\n", __func__);
    
    // test poly1305

    const char key1[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg1[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0";
    
    BRPoly1305(mac, key1, msg1, sizeof(msg1) - 1);
    if (memcmp("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 1\n", __func__);

    const char key2[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x36\xe5\xf6\xb5\xc5\xe0\x60\x70\xf0\xef\xca\x96\x22\x7a\x86"
    "\x3e",
    msg2[] = "Any submission to the IETF intended by the Contributor for publication as all or part of an IETF "
    "Internet-Draft or RFC and any statement made within the context of an IETF activity is considered an \"IETF "
    "Contribution\". Such statements include oral statements in IETF sessions, as well as written and electronic "
    "communications made at any time or place, which are addressed to";

    BRPoly1305(mac, key2, msg2, sizeof(msg2) - 1);
    if (memcmp("\x36\xe5\xf6\xb5\xc5\xe0\x60\x70\xf0\xef\xca\x96\x22\x7a\x86\x3e", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 2\n", __func__);

    const char key3[] = "\x36\xe5\xf6\xb5\xc5\xe0\x60\x70\xf0\xef\xca\x96\x22\x7a\x86\x3e\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0",
    msg3[] = "Any submission to the IETF intended by the Contributor for publication as all or part of an IETF "
    "Internet-Draft or RFC and any statement made within the context of an IETF activity is considered an \"IETF "
    "Contribution\". Such statements include oral statements in IETF sessions, as well as written and electronic "
    "communications made at any time or place, which are addressed to";

    BRPoly1305(mac, key3, msg3, sizeof(msg3) - 1);
    if (memcmp("\xf3\x47\x7e\x7c\xd9\x54\x17\xaf\x89\xa6\xb8\x79\x4c\x31\x0c\xf0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 3\n", __func__);
    
    const char key4[] = "\x1c\x92\x40\xa5\xeb\x55\xd3\x8a\xf3\x33\x88\x86\x04\xf6\xb5\xf0\x47\x39\x17\xc1\x40\x2b\x80"
    "\x09\x9d\xca\x5c\xbc\x20\x70\x75\xc0",
    msg4[] = "'Twas brillig, and the slithy toves\nDid gyre and gimble in the wabe:\nAll mimsy were the borogoves,\n"
    "And the mome raths outgrabe.";

    BRPoly1305(mac, key4, msg4, sizeof(msg4) - 1);
    if (memcmp("\x45\x41\x66\x9a\x7e\xaa\xee\x61\xe7\x08\xdc\x7c\xbc\xc5\xeb\x62", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 4\n", __func__);

    const char key5[] = "\x02\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg5[] = "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";

    BRPoly1305(mac, key5, msg5, sizeof(msg5) - 1);
    if (memcmp("\x03\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 5\n", __func__);

    const char key6[] = "\x02\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
    "\xFF",
    msg6[] = "\x02\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    
    BRPoly1305(mac, key6, msg6, sizeof(msg6) - 1);
    if (memcmp("\x03\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 6\n", __func__);

    const char key7[] = "\x01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg7[] = "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xF0\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
    "\xFF\xFF\xFF\xFF\xFF\xFF\x11\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    
    BRPoly1305(mac, key7, msg7, sizeof(msg7) - 1);
    if (memcmp("\x05\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 7\n", __func__);

    const char key8[] = "\x01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg8[] = "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFB\xFE\xFE\xFE\xFE\xFE\xFE\xFE\xFE\xFE"
    "\xFE\xFE\xFE\xFE\xFE\xFE\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01";
    
    BRPoly1305(mac, key8, msg8, sizeof(msg8) - 1);
    if (memcmp("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 8\n", __func__);

    const char key9[] = "\x02\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg9[] = "\xFD\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";
    
    BRPoly1305(mac, key9, msg9, sizeof(msg9) - 1);
    if (memcmp("\xFA\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 9\n", __func__);

    const char key10[] = "\x01\0\0\0\0\0\0\0\x04\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg10[] = "\xE3\x35\x94\xD7\x50\x5E\x43\xB9\0\0\0\0\0\0\0\0\x33\x94\xD7\x50\x5E\x43\x79\xCD\x01\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0\x01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    
    BRPoly1305(mac, key10, msg10, sizeof(msg10) - 1);
    if (memcmp("\x14\0\0\0\0\0\0\0\x55\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 10\n", __func__);

    const char key11[] = "\x01\0\0\0\0\0\0\0\x04\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    msg11[] = "\xE3\x35\x94\xD7\x50\x5E\x43\xB9\0\0\0\0\0\0\0\0\x33\x94\xD7\x50\x5E\x43\x79\xCD\x01\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0\0";
    
    BRPoly1305(mac, key11, msg11, sizeof(msg11) - 1);
    if (memcmp("\x13\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", mac, 16) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPoly1305() test 11\n", __func__);
    
    return r;
}

int BRDrbgTests()
{
    int r = 1;
    const char seed1[] = "\xa7\x6e\x77\xa9\x69\xab\x92\x64\x51\x81\xf0\x15\x78\x02\x52\x37\x46\xc3\x4b\xf3\x21\x86\x76"
    "\x41", nonce1[] = "\x05\x1e\xd6\xba\x39\x36\x80\x33\xad\xc9\x3d\x4e";
    uint8_t out[2048/8], K[512/8], V[512/8];
    
    BRHMACDRBG(out, 896/8, K, V, BRSHA224, 224/8, seed1, sizeof(seed1) - 1, nonce1, sizeof(nonce1) - 1, NULL, 0);
    BRHMACDRBG(out, 896/8, K, V, BRSHA224, 224/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\x89\x25\x98\x7d\xb5\x56\x6e\x60\x52\x0f\x09\xbd\xdd\xab\x48\x82\x92\xbe\xd9\x2c\xd3\x85\xe5\xb6\xfc"
               "\x22\x3e\x19\x19\x64\x0b\x4e\x34\xe3\x45\x75\x03\x3e\x56\xc0\xa8\xf6\x08\xbe\x21\xd3\xd2\x21\xc6\x7d"
               "\x39\xab\xec\x98\xd8\x13\x12\xf3\xa2\x65\x3d\x55\xff\xbf\x44\xc3\x37\xc8\x2b\xed\x31\x4c\x21\x1b\xe2"
               "\x3e\xc3\x94\x39\x9b\xa3\x51\xc4\x68\x7d\xce\x64\x9e\x7c\x2a\x1b\xa7\xb0\xb5\xda\xb1\x25\x67\x1b\x1b"
               "\xcf\x90\x08\xda\x65\xca\xd6\x12\xd9\x5d\xdc\x92", out, 896/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 1\n", __func__);

    const char seed2[] = "\xf6\xe6\x8b\xb0\x58\x5c\x84\xd7\xb9\xf1\x75\x79\xad\x9b\x9a\x8a\xa2\x66\x6a\xbf\x4e\x8b\x44"
    "\xa3", nonce2[] = "\xa4\x33\x11\xd5\x78\x42\xef\x09\x6b\x66\xfa\x5e",
    ps2[] = "\x2f\x50\x7e\x12\xd6\x8a\x88\x0f\xa7\x0d\x6e\x5e\x54\x39\x15\x38\x17\x32\x97\x81\x4e\x06\xd7\xfd";

    BRHMACDRBG(out, 896/8, K, V, BRSHA224, 224/8, seed2, sizeof(seed2) - 1, nonce2, sizeof(nonce2) - 1,
               ps2, sizeof(ps2) - 1);
    BRHMACDRBG(out, 896/8, K, V, BRSHA224, 224/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\x10\xc2\xf9\x3c\xa9\x9a\x8e\x8e\xcf\x22\x54\x00\xc8\x04\xa7\xb3\x68\xd9\x3c\xee\x3b\xfa\x6f\x44\x59"
               "\x20\xa6\xa9\x12\xd2\x68\xd6\x91\xf1\x78\x8b\xaf\x01\x3f\xb1\x68\x50\x1c\xa1\x56\xb5\x71\xba\x04\x7d"
               "\x8d\x02\x9d\xc1\xc1\xee\x07\xfc\xa5\x0a\xf6\x99\xc5\xbc\x2f\x79\x0a\xcf\x27\x80\x41\x51\x81\x41\xe7"
               "\xdc\x91\x64\xc3\xe5\x71\xb2\x65\xfb\x89\x54\x26\x1d\x92\xdb\xf2\x0a\xe0\x2f\xc2\xb7\x80\xc0\x18\xb6"
               "\xb5\x4b\x43\x20\xf2\xb8\x9d\x34\x33\x07\xfb\xb2", out, 896/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 2\n", __func__);

    const char seed3[] = "\xca\x85\x19\x11\x34\x93\x84\xbf\xfe\x89\xde\x1c\xbd\xc4\x6e\x68\x31\xe4\x4d\x34\xa4\xfb\x93"
    "\x5e\xe2\x85\xdd\x14\xb7\x1a\x74\x88",
    nonce3[] = "\x65\x9b\xa9\x6c\x60\x1d\xc6\x9f\xc9\x02\x94\x08\x05\xec\x0c\xa8";
    
    BRHMACDRBG(out, 1024/8, K, V, BRSHA256, 256/8, seed3, sizeof(seed3) - 1, nonce3, sizeof(nonce3) - 1, NULL, 0);
    BRHMACDRBG(out, 1024/8, K, V, BRSHA256, 256/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\xe5\x28\xe9\xab\xf2\xde\xce\x54\xd4\x7c\x7e\x75\xe5\xfe\x30\x21\x49\xf8\x17\xea\x9f\xb4\xbe\xe6\xf4"
               "\x19\x96\x97\xd0\x4d\x5b\x89\xd5\x4f\xbb\x97\x8a\x15\xb5\xc4\x43\xc9\xec\x21\x03\x6d\x24\x60\xb6\xf7"
               "\x3e\xba\xd0\xdc\x2a\xba\x6e\x62\x4a\xbf\x07\x74\x5b\xc1\x07\x69\x4b\xb7\x54\x7b\xb0\x99\x5f\x70\xde"
               "\x25\xd6\xb2\x9e\x2d\x30\x11\xbb\x19\xd2\x76\x76\xc0\x71\x62\xc8\xb5\xcc\xde\x06\x68\x96\x1d\xf8\x68"
               "\x03\x48\x2c\xb3\x7e\xd6\xd5\xc0\xbb\x8d\x50\xcf\x1f\x50\xd4\x76\xaa\x04\x58\xbd\xab\xa8\x06\xf4\x8b"
               "\xe9\xdc\xb8", out, 1024/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 3\n", __func__);

    const char seed4[] = "\x5c\xac\xc6\x81\x65\xa2\xe2\xee\x20\x81\x2f\x35\xec\x73\xa7\x9d\xbf\x30\xfd\x47\x54\x76\xac"
    "\x0c\x44\xfc\x61\x74\xcd\xac\x2b\x55",
    nonce4[] = "\x6f\x88\x54\x96\xc1\xe6\x3a\xf6\x20\xbe\xcd\x9e\x71\xec\xb8\x24",
    ps4[] = "\xe7\x2d\xd8\x59\x0d\x4e\xd5\x29\x55\x15\xc3\x5e\xd6\x19\x9e\x9d\x21\x1b\x8f\x06\x9b\x30\x58\xca\xa6\x67"
    "\x0b\x96\xef\x12\x08\xd0";
    
    BRHMACDRBG(out, 1024/8, K, V, BRSHA256, 256/8, seed4, sizeof(seed4) - 1, nonce4, sizeof(nonce4) - 1,
               ps4, sizeof(ps4) - 1);
    BRHMACDRBG(out, 1024/8, K, V, BRSHA256, 256/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\xf1\x01\x2c\xf5\x43\xf9\x45\x33\xdf\x27\xfe\xdf\xbf\x58\xe5\xb7\x9a\x3d\xc5\x17\xa9\xc4\x02\xbd\xbf"
               "\xc9\xa0\xc0\xf7\x21\xf9\xd5\x3f\xaf\x4a\xaf\xdc\x4b\x8f\x7a\x1b\x58\x0f\xca\xa5\x23\x38\xd4\xbd\x95"
               "\xf5\x89\x66\xa2\x43\xcd\xcd\x3f\x44\x6e\xd4\xbc\x54\x6d\x9f\x60\x7b\x19\x0d\xd6\x99\x54\x45\x0d\x16"
               "\xcd\x0e\x2d\x64\x37\x06\x7d\x8b\x44\xd1\x9a\x6a\xf7\xa7\xcf\xa8\x79\x4e\x5f\xbd\x72\x8e\x8f\xb2\xf2"
               "\xe8\xdb\x5d\xd4\xff\x1a\xa2\x75\xf3\x58\x86\x09\x8e\x80\xff\x84\x48\x86\x06\x0d\xa8\xb1\xe7\x13\x78"
               "\x46\xb2\x3b", out, 1024/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 4\n", __func__);

    const char seed5[] = "\xa1\xdc\x2d\xfe\xda\x4f\x3a\x11\x24\xe0\xe7\x5e\xbf\xbe\x5f\x98\xca\xc1\x10\x18\x22\x1d\xda"
    "\x3f\xdc\xf8\xf9\x12\x5d\x68\x44\x7a",
    nonce5[] = "\xba\xe5\xea\x27\x16\x65\x40\x51\x52\x68\xa4\x93\xa9\x6b\x51\x87";
    
    BRHMACDRBG(out, 1536/8, K, V, BRSHA384, 384/8, seed5, sizeof(seed5) - 1, nonce5, sizeof(nonce5) - 1, NULL, 0);
    BRHMACDRBG(out, 1536/8, K, V, BRSHA384, 384/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\x22\x82\x93\xe5\x9b\x1e\x45\x45\xa4\xff\x9f\x23\x26\x16\xfc\x51\x08\xa1\x12\x8d\xeb\xd0\xf7\xc2\x0a"
               "\xce\x83\x7c\xa1\x05\xcb\xf2\x4c\x0d\xac\x1f\x98\x47\xda\xfd\x0d\x05\x00\x72\x1f\xfa\xd3\xc6\x84\xa9"
               "\x92\xd1\x10\xa5\x49\xa2\x64\xd1\x4a\x89\x11\xc5\x0b\xe8\xcd\x6a\x7e\x8f\xac\x78\x3a\xd9\x5b\x24\xf6"
               "\x4f\xd8\xcc\x4c\x8b\x64\x9e\xac\x2b\x15\xb3\x63\xe3\x0d\xf7\x95\x41\xa6\xb8\xa1\xca\xac\x23\x89\x49"
               "\xb4\x66\x43\x69\x4c\x85\xe1\xd5\xfc\xbc\xd9\xaa\xae\x62\x60\xac\xee\x66\x0b\x8a\x79\xbe\xa4\x8e\x07"
               "\x9c\xeb\x6a\x5e\xaf\x49\x93\xa8\x2c\x3f\x1b\x75\x8d\x7c\x53\xe3\x09\x4e\xea\xc6\x3d\xc2\x55\xbe\x6d"
               "\xcd\xcc\x2b\x51\xe5\xca\x45\xd2\xb2\x06\x84\xa5\xa8\xfa\x58\x06\xb9\x6f\x84\x61\xeb\xf5\x1b\xc5\x15"
               "\xa7\xdd\x8c\x54\x75\xc0\xe7\x0f\x2f\xd0\xfa\xf7\x86\x9a\x99\xab\x6c", out, 1536/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 5\n", __func__);

    const char seed6[] = "\x2c\xd9\x68\xba\xcd\xa2\xbc\x31\x4d\x2f\xb4\x1f\xe4\x33\x54\xfb\x76\x11\x34\xeb\x19\xee\xc6"
    "\x04\x31\xe2\xf3\x67\x55\xb8\x51\x26",
    nonce6[] = "\xe3\xde\xdf\x2a\xf9\x38\x2a\x1e\x65\x21\x43\xe9\x52\x21\x2d\x39",
    ps6[] = "\x59\xfa\x82\x35\x10\x88\x21\xac\xcb\xd3\xc1\x4e\xaf\x76\x85\x6d\x6a\x07\xf4\x33\x83\xdb\x4c\xc6\x03\x80"
    "\x40\xb1\x88\x10\xd5\x3c";

    BRHMACDRBG(out, 1536/8, K, V, BRSHA384, 384/8, seed6, sizeof(seed6) - 1, nonce6, sizeof(nonce6) - 1,
               ps6, sizeof(ps6) - 1);
    BRHMACDRBG(out, 1536/8, K, V, BRSHA384, 384/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\x06\x05\x1c\xe6\xb2\xf1\xc3\x43\x78\xe0\x8c\xaf\x8f\xe8\x36\x20\x1f\xf7\xec\x2d\xb8\xfc\x5a\x25\x19"
               "\xad\xd2\x52\x4d\x90\x47\x01\x94\xb2\x47\xaf\x3a\x34\xa6\x73\x29\x8e\x57\x07\x0b\x25\x6f\x59\xfd\x09"
               "\x86\x32\x76\x8e\x2d\x55\x13\x7d\x6c\x17\xb1\xa5\x3f\xe4\x5d\x6e\xd0\xe3\x1d\x49\xe6\x48\x20\xdb\x14"
               "\x50\x14\xe2\xf0\x38\xb6\x9b\x72\x20\xe0\x42\xa8\xef\xc9\x89\x85\x70\x6a\xb9\x63\x54\x51\x23\x0a\x12"
               "\x8a\xee\x80\x1d\x4e\x37\x18\xff\x59\x51\x1c\x3f\x3f\xf1\xb2\x0f\x10\x97\x74\xa8\xdd\xc1\xfa\xdf\x41"
               "\xaf\xcc\x13\xd4\x00\x96\xd9\x97\x94\x88\x57\xa8\x94\xd0\xef\x8b\x32\x35\xc3\x21\x3b\xa8\x5c\x50\xc2"
               "\xf3\xd6\x1b\x0d\x10\x4e\xcc\xfc\xf3\x6c\x35\xfe\x5e\x49\xe7\x60\x2c\xb1\x53\x3d\xe1\x2f\x0b\xec\x61"
               "\x3a\x0e\xd9\x63\x38\x21\x95\x7e\x5b\x7c\xb3\x2f\x60\xb7\xc0\x2f\xa4", out, 1536/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 6\n", __func__);

    const char seed7[] = "\x35\x04\x9f\x38\x9a\x33\xc0\xec\xb1\x29\x32\x38\xfd\x95\x1f\x8f\xfd\x51\x7d\xfd\xe0\x60\x41"
    "\xd3\x29\x45\xb3\xe2\x69\x14\xba\x15",
    nonce7[] = "\xf7\x32\x87\x60\xbe\x61\x68\xe6\xaa\x9f\xb5\x47\x84\x98\x9a\x11";
    
    BRHMACDRBG(out, 2048/8, K, V, BRSHA512, 512/8, seed7, sizeof(seed7) - 1, nonce7, sizeof(nonce7) - 1, NULL, 0);
    BRHMACDRBG(out, 2048/8, K, V, BRSHA512, 512/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\xe7\x64\x91\xb0\x26\x0a\xac\xfd\xed\x01\xad\x39\xfb\xf1\xa6\x6a\x88\x28\x4c\xaa\x51\x23\x36\x8a\x2a"
               "\xd9\x33\x0e\xe4\x83\x35\xe3\xc9\xc9\xba\x90\xe6\xcb\xc9\x42\x99\x62\xd6\x0c\x1a\x66\x61\xed\xcf\xaa"
               "\x31\xd9\x72\xb8\x26\x4b\x9d\x45\x62\xcf\x18\x49\x41\x28\xa0\x92\xc1\x7a\x8d\xa6\xf3\x11\x3e\x8a\x7e"
               "\xdf\xcd\x44\x27\x08\x2b\xd3\x90\x67\x5e\x96\x62\x40\x81\x44\x97\x17\x17\x30\x3d\x8d\xc3\x52\xc9\xe8"
               "\xb9\x5e\x7f\x35\xfa\x2a\xc9\xf5\x49\xb2\x92\xbc\x7c\x4b\xc7\xf0\x1e\xe0\xa5\x77\x85\x9e\xf6\xe8\x2d"
               "\x79\xef\x23\x89\x2d\x16\x7c\x14\x0d\x22\xaa\xc3\x2b\x64\xcc\xdf\xee\xe2\x73\x05\x28\xa3\x87\x63\xb2"
               "\x42\x27\xf9\x1a\xc3\xff\xe4\x7f\xb1\x15\x38\xe4\x35\x30\x7e\x77\x48\x18\x02\xb0\xf6\x13\xf3\x70\xff"
               "\xb0\xdb\xea\xb7\x74\xfe\x1e\xfb\xb1\xa8\x0d\x01\x15\x4a\x94\x59\xe7\x3a\xd3\x61\x10\x8b\xbc\x86\xb0"
               "\x91\x4f\x09\x51\x36\xcb\xe6\x34\x55\x5c\xe0\xbb\x26\x36\x18\xdc\x5c\x36\x72\x91\xce\x08\x25\x51\x89"
               "\x87\x15\x4f\xe9\xec\xb0\x52\xb3\xf0\xa2\x56\xfc\xc3\x0c\xc1\x45\x72\x53\x1c\x96\x28\x97\x36\x39\xbe"
               "\xda\x45\x6f\x2b\xdd\xf6", out, 2048/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 7\n", __func__);

    const char seed8[] = "\x73\x52\x9b\xba\x71\xa3\xd4\xb4\xfc\xf9\xa7\xed\xee\xd2\x69\xdb\xdc\x37\x48\xb9\x0d\xf6\x8c"
    "\x0d\x00\xe2\x45\xde\x54\x69\x8c\x77",
    nonce8[] = "\x22\xe2\xd6\xe2\x45\x01\x21\x2b\x6f\x05\x8e\x7c\x54\x13\x80\x07",
    ps8[] = "\xe2\xcc\x19\xe3\x15\x95\xd0\xe4\xde\x9e\x8b\xd3\xb2\x36\xde\xc2\xd4\xb0\x32\xc3\xdd\x5b\xf9\x89\x1c\x28"
    "\x4c\xd1\xba\xc6\x7b\xdb";
    
    BRHMACDRBG(out, 2048/8, K, V, BRSHA512, 512/8, seed8, sizeof(seed8) - 1, nonce8, sizeof(nonce8) - 1,
               ps8, sizeof(ps8) - 1);
    BRHMACDRBG(out, 2048/8, K, V, BRSHA512, 512/8, NULL, 0, NULL, 0, NULL, 0);
    if (memcmp("\x1a\x73\xd5\x8b\x73\x42\xc3\xc9\x33\xe3\xba\x15\xee\xdd\x82\x70\x98\x86\x91\xc3\x79\x4b\x45\xaa\x35"
               "\x85\x70\x39\x15\x71\x88\x1c\x0d\x9c\x42\x89\xe5\xb1\x98\xdb\x55\x34\xc3\xcb\x84\x66\xab\x48\x25\x0f"
               "\xa6\x7f\x24\xcb\x19\xb7\x03\x8e\x46\xaf\x56\x68\x7b\xab\x7e\x5d\xe3\xc8\x2f\xa7\x31\x2f\x54\xdc\x0f"
               "\x1d\xc9\x3f\x5b\x03\xfc\xaa\x60\x03\xca\xe2\x8d\x3d\x47\x07\x36\x8c\x14\x4a\x7a\xa4\x60\x91\x82\x2d"
               "\xa2\x92\xf9\x7f\x32\xca\xf9\x0a\xe3\xdd\x3e\x48\xe8\x08\xae\x12\xe6\x33\xaa\x04\x10\x10\x6e\x1a\xb5"
               "\x6b\xc0\xa0\xd8\x0f\x43\x8e\x9b\x34\x92\xe4\xa3\xbc\x88\xd7\x3a\x39\x04\xf7\xdd\x06\x0c\x48\xae\x8d"
               "\x7b\x12\xbf\x89\xa1\x95\x51\xb5\x3b\x3f\x55\xa5\x11\xd2\x82\x0e\x94\x16\x40\xc8\x45\xa8\xa0\x46\x64"
               "\x32\xc5\x85\x0c\x5b\x61\xbe\xc5\x27\x26\x02\x52\x11\x25\xad\xdf\x67\x7e\x94\x9b\x96\x78\x2b\xc0\x1a"
               "\x90\x44\x91\xdf\x08\x08\x9b\xed\x00\x4a\xd5\x6e\x12\xf8\xea\x1a\x20\x08\x83\xad\x72\xb3\xb9\xfa\xe1"
               "\x2b\x4e\xb6\x5d\x5c\x2b\xac\xb3\xce\x46\xc7\xc4\x84\x64\xc9\xc2\x91\x42\xfb\x35\xe7\xbc\x26\x7c\xe8"
               "\x52\x29\x6a\xc0\x42\xf9", out, 2048/8) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRHMACDRBG() test 8\n", __func__);
    
    return r;
}

int BRChachaTests()
{
    int r = 1;
    const char key[] = "\0\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16"
    "\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f",
    iv[] = "\0\0\0\x4a\0\0\0\0",
    msg[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen "
    "would be it.",
    cipher[] = "\x6e\x2e\x35\x9a\x25\x68\xf9\x80\x41\xba\x07\x28\xdd\x0d\x69\x81\xe9\x7e\x7a\xec\x1d\x43\x60\xc2\x0a"
    "\x27\xaf\xcc\xfd\x9f\xae\x0b\xf9\x1b\x65\xc5\x52\x47\x33\xab\x8f\x59\x3d\xab\xcd\x62\xb3\x57\x16\x39\xd6\x24\xe6"
    "\x51\x52\xab\x8f\x53\x0c\x35\x9f\x08\x61\xd8\x07\xca\x0d\xbf\x50\x0d\x6a\x61\x56\xa3\x8e\x08\x8a\x22\xb6\x5e\x52"
    "\xbc\x51\x4d\x16\xcc\xf8\x06\x81\x8c\xe9\x1a\xb7\x79\x37\x36\x5a\xf9\x0b\xbf\x74\xa3\x5b\xe6\xb4\x0b\x8e\xed\xf2"
    "\x78\x5e\x42\x87\x4d";
    uint8_t out[sizeof(msg) - 1];

    BRChacha20(out, key, iv, msg, sizeof(msg) - 1, 1);
    if (memcmp(cipher, out, sizeof(out)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() cipher test 0\n", __func__);

    BRChacha20(out, key, iv, out, sizeof(out), 1);
    if (memcmp(msg, out, sizeof(out)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() de-cipher test 0\n", __func__);

    const char key1[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    iv1[] = "\0\0\0\0\0\0\0\0",
    msg1[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
    "\0\0\0\0\0\0\0\0\0\0\0\0",
    cipher1[] = "\x76\xb8\xe0\xad\xa0\xf1\x3d\x90\x40\x5d\x6a\xe5\x53\x86\xbd\x28\xbd\xd2\x19\xb8\xa0\x8d\xed\x1a\xa8"
    "\x36\xef\xcc\x8b\x77\x0d\xc7\xda\x41\x59\x7c\x51\x57\x48\x8d\x77\x24\xe0\x3f\xb8\xd8\x4a\x37\x6a\x43\xb8\xf4\x15"
    "\x18\xa1\x1c\xc3\x87\xb6\x69\xb2\xee\x65\x86";
    uint8_t out1[sizeof(msg1) - 1];
    
    BRChacha20(out1, key1, iv1, msg1, sizeof(msg1) - 1, 0);
    if (memcmp(cipher1, out1, sizeof(out1)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() cipher test 1\n", __func__);
    
    BRChacha20(out1, key1, iv1, out1, sizeof(out1), 0);
    if (memcmp(msg1, out1, sizeof(out1)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() de-cipher test 1\n", __func__);

    const char key2[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01",
    iv2[] = "\0\0\0\0\0\0\0\x02",
    msg2[] = "Any submission to the IETF intended by the Contributor for publication as all or part of an IETF "
    "Internet-Draft or RFC and any statement made within the context of an IETF activity is considered an \"IETF "
    "Contribution\". Such statements include oral statements in IETF sessions, as well as written and electronic "
    "communications made at any time or place, which are addressed to",
    cipher2[] = "\xa3\xfb\xf0\x7d\xf3\xfa\x2f\xde\x4f\x37\x6c\xa2\x3e\x82\x73\x70\x41\x60\x5d\x9f\x4f\x4f\x57\xbd\x8c"
    "\xff\x2c\x1d\x4b\x79\x55\xec\x2a\x97\x94\x8b\xd3\x72\x29\x15\xc8\xf3\xd3\x37\xf7\xd3\x70\x05\x0e\x9e\x96\xd6\x47"
    "\xb7\xc3\x9f\x56\xe0\x31\xca\x5e\xb6\x25\x0d\x40\x42\xe0\x27\x85\xec\xec\xfa\x4b\x4b\xb5\xe8\xea\xd0\x44\x0e\x20"
    "\xb6\xe8\xdb\x09\xd8\x81\xa7\xc6\x13\x2f\x42\x0e\x52\x79\x50\x42\xbd\xfa\x77\x73\xd8\xa9\x05\x14\x47\xb3\x29\x1c"
    "\xe1\x41\x1c\x68\x04\x65\x55\x2a\xa6\xc4\x05\xb7\x76\x4d\x5e\x87\xbe\xa8\x5a\xd0\x0f\x84\x49\xed\x8f\x72\xd0\xd6"
    "\x62\xab\x05\x26\x91\xca\x66\x42\x4b\xc8\x6d\x2d\xf8\x0e\xa4\x1f\x43\xab\xf9\x37\xd3\x25\x9d\xc4\xb2\xd0\xdf\xb4"
    "\x8a\x6c\x91\x39\xdd\xd7\xf7\x69\x66\xe9\x28\xe6\x35\x55\x3b\xa7\x6c\x5c\x87\x9d\x7b\x35\xd4\x9e\xb2\xe6\x2b\x08"
    "\x71\xcd\xac\x63\x89\x39\xe2\x5e\x8a\x1e\x0e\xf9\xd5\x28\x0f\xa8\xca\x32\x8b\x35\x1c\x3c\x76\x59\x89\xcb\xcf\x3d"
    "\xaa\x8b\x6c\xcc\x3a\xaf\x9f\x39\x79\xc9\x2b\x37\x20\xfc\x88\xdc\x95\xed\x84\xa1\xbe\x05\x9c\x64\x99\xb9\xfd\xa2"
    "\x36\xe7\xe8\x18\xb0\x4b\x0b\xc3\x9c\x1e\x87\x6b\x19\x3b\xfe\x55\x69\x75\x3f\x88\x12\x8c\xc0\x8a\xaa\x9b\x63\xd1"
    "\xa1\x6f\x80\xef\x25\x54\xd7\x18\x9c\x41\x1f\x58\x69\xca\x52\xc5\xb8\x3f\xa3\x6f\xf2\x16\xb9\xc1\xd3\x00\x62\xbe"
    "\xbc\xfd\x2d\xc5\xbc\xe0\x91\x19\x34\xfd\xa7\x9a\x86\xf6\xe6\x98\xce\xd7\x59\xc3\xff\x9b\x64\x77\x33\x8f\x3d\xa4"
    "\xf9\xcd\x85\x14\xea\x99\x82\xcc\xaf\xb3\x41\xb2\x38\x4d\xd9\x02\xf3\xd1\xab\x7a\xc6\x1d\xd2\x9c\x6f\x21\xba\x5b"
    "\x86\x2f\x37\x30\xe3\x7c\xfd\xc4\xfd\x80\x6c\x22\xf2\x21";
    uint8_t out2[sizeof(msg2) - 1];
    
    BRChacha20(out2, key2, iv2, msg2, sizeof(msg2) - 1, 1);
    if (memcmp(cipher2, out2, sizeof(out2)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() cipher test 2\n", __func__);
    
    BRChacha20(out2, key2, iv2, out2, sizeof(out2), 1);
    if (memcmp(msg2, out2, sizeof(out2)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() de-cipher test 2\n", __func__);
    
    const char key3[] = "\x1c\x92\x40\xa5\xeb\x55\xd3\x8a\xf3\x33\x88\x86\x04\xf6\xb5\xf0\x47\x39\x17\xc1\x40\x2b\x80"
    "\x09\x9d\xca\x5c\xbc\x20\x70\x75\xc0",
    iv3[] = "\0\0\0\0\0\0\0\x02",
    msg3[] = "'Twas brillig, and the slithy toves\nDid gyre and gimble in the wabe:\nAll mimsy were the borogoves,\n"
    "And the mome raths outgrabe.",
    cipher3[] = "\x62\xe6\x34\x7f\x95\xed\x87\xa4\x5f\xfa\xe7\x42\x6f\x27\xa1\xdf\x5f\xb6\x91\x10\x04\x4c\x0d\x73\x11"
    "\x8e\xff\xa9\x5b\x01\xe5\xcf\x16\x6d\x3d\xf2\xd7\x21\xca\xf9\xb2\x1e\x5f\xb1\x4c\x61\x68\x71\xfd\x84\xc5\x4f\x9d"
    "\x65\xb2\x83\x19\x6c\x7f\xe4\xf6\x05\x53\xeb\xf3\x9c\x64\x02\xc4\x22\x34\xe3\x2a\x35\x6b\x3e\x76\x43\x12\xa6\x1a"
    "\x55\x32\x05\x57\x16\xea\xd6\x96\x25\x68\xf8\x7d\x3f\x3f\x77\x04\xc6\xa8\xd1\xbc\xd1\xbf\x4d\x50\xd6\x15\x4b\x6d"
    "\xa7\x31\xb1\x87\xb5\x8d\xfd\x72\x8a\xfa\x36\x75\x7a\x79\x7a\xc1\x88\xd1";
    uint8_t out3[sizeof(msg3) - 1];
    
    BRChacha20(out3, key3, iv3, msg3, sizeof(msg3) - 1, 42);
    if (memcmp(cipher3, out3, sizeof(out3)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() cipher test 3\n", __func__);
    
    BRChacha20(out3, key3, iv3, out3, sizeof(out3), 42);
    if (memcmp(msg3, out3, sizeof(out3)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20() de-cipher test 3\n", __func__);

    return r;
}

int BRAuthEncryptTests()
{
    int r = 1;
    const char msg1[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
    "sunscreen would be it.",
    ad1[] = "\x50\x51\x52\x53\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7",
    key1[] = "\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99"
    "\x9a\x9b\x9c\x9d\x9e\x9f",
    nonce1[] = "\x07\x00\x00\x00\x40\x41\x42\x43\x44\x45\x46\x47",
    cipher1[] = "\xd3\x1a\x8d\x34\x64\x8e\x60\xdb\x7b\x86\xaf\xbc\x53\xef\x7e\xc2\xa4\xad\xed\x51\x29\x6e\x08\xfe\xa9"
    "\xe2\xb5\xa7\x36\xee\x62\xd6\x3d\xbe\xa4\x5e\x8c\xa9\x67\x12\x82\xfa\xfb\x69\xda\x92\x72\x8b\x1a\x71\xde\x0a\x9e"
    "\x06\x0b\x29\x05\xd6\xa5\xb6\x7e\xcd\x3b\x36\x92\xdd\xbd\x7f\x2d\x77\x8b\x8c\x98\x03\xae\xe3\x28\x09\x1b\x58\xfa"
    "\xb3\x24\xe4\xfa\xd6\x75\x94\x55\x85\x80\x8b\x48\x31\xd7\xbc\x3f\xf4\xde\xf0\x8e\x4b\x7a\x9d\xe5\x76\xd2\x65\x86"
    "\xce\xc6\x4b\x61\x16\x1a\xe1\x0b\x59\x4f\x09\xe2\x6a\x7e\x90\x2e\xcb\xd0\x60\x06\x91";
    uint8_t out1[16 + sizeof(msg1) - 1];
    size_t len;

    len = BRChacha20Poly1305AEADEncrypt(out1, sizeof(out1), key1, nonce1, msg1, sizeof(msg1) - 1, ad1, sizeof(ad1) - 1);
    if (len != sizeof(cipher1) - 1 || memcmp(cipher1, out1, len) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20Poly1305AEADEncrypt() cipher test 1\n", __func__);
    
    len = BRChacha20Poly1305AEADDecrypt(out1, sizeof(out1), key1, nonce1, cipher1, sizeof(cipher1) - 1, ad1,
                                        sizeof(ad1) - 1);
    if (len != sizeof(msg1) - 1 || memcmp(msg1, out1, len) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20Poly1305AEADDecrypt() cipher test 1\n", __func__);
    
    const char msg2[] = "Internet-Drafts are draft documents valid for a maximum of six months and may be updated, "
    "replaced, or obsoleted by other documents at any time. It is inappropriate to use Internet-Drafts as reference "
    "material or to cite them other than as /“work in progress./”",
    ad2[] = "\xf3\x33\x88\x86\0\0\0\0\0\0\x4e\x91",
    key2[] = "\x1c\x92\x40\xa5\xeb\x55\xd3\x8a\xf3\x33\x88\x86\x04\xf6\xb5\xf0\x47\x39\x17\xc1\x40\x2b\x80\x09\x9d\xca"
    "\x5c\xbc\x20\x70\x75\xc0",
    nonce2[] = "\0\0\0\0\x01\x02\x03\x04\x05\x06\x07\x08",
    cipher2[] = "\x64\xa0\x86\x15\x75\x86\x1a\xf4\x60\xf0\x62\xc7\x9b\xe6\x43\xbd\x5e\x80\x5c\xfd\x34\x5c\xf3\x89\xf1"
    "\x08\x67\x0a\xc7\x6c\x8c\xb2\x4c\x6c\xfc\x18\x75\x5d\x43\xee\xa0\x9e\xe9\x4e\x38\x2d\x26\xb0\xbd\xb7\xb7\x3c\x32"
    "\x1b\x01\x00\xd4\xf0\x3b\x7f\x35\x58\x94\xcf\x33\x2f\x83\x0e\x71\x0b\x97\xce\x98\xc8\xa8\x4a\xbd\x0b\x94\x81\x14"
    "\xad\x17\x6e\x00\x8d\x33\xbd\x60\xf9\x82\xb1\xff\x37\xc8\x55\x97\x97\xa0\x6e\xf4\xf0\xef\x61\xc1\x86\x32\x4e\x2b"
    "\x35\x06\x38\x36\x06\x90\x7b\x6a\x7c\x02\xb0\xf9\xf6\x15\x7b\x53\xc8\x67\xe4\xb9\x16\x6c\x76\x7b\x80\x4d\x46\xa5"
    "\x9b\x52\x16\xcd\xe7\xa4\xe9\x90\x40\xc5\xa4\x04\x33\x22\x5e\xe2\x82\xa1\xb0\xa0\x6c\x52\x3e\xaf\x45\x34\xd7\xf8"
    "\x3f\xa1\x15\x5b\x00\x47\x71\x8c\xbc\x54\x6a\x0d\x07\x2b\x04\xb3\x56\x4e\xea\x1b\x42\x22\x73\xf5\x48\x27\x1a\x0b"
    "\xb2\x31\x60\x53\xfa\x76\x99\x19\x55\xeb\xd6\x31\x59\x43\x4e\xce\xbb\x4e\x46\x6d\xae\x5a\x10\x73\xa6\x72\x76\x27"
    "\x09\x7a\x10\x49\xe6\x17\xd9\x1d\x36\x10\x94\xfa\x68\xf0\xff\x77\x98\x71\x30\x30\x5b\xea\xba\x2e\xda\x04\xdf\x99"
    "\x7b\x71\x4d\x6c\x6f\x2c\x29\xa6\xad\x5c\xb4\x02\x2b\x02\x70\x9b\xee\xad\x9d\x67\x89\x0c\xbb\x22\x39\x23\x36\xfe"
    "\xa1\x85\x1f\x38";
    uint8_t out2[sizeof(cipher2) - 1];

    len = BRChacha20Poly1305AEADDecrypt(out2, sizeof(out2), key2, nonce2, cipher2, sizeof(cipher2) - 1, ad2,
                                        sizeof(ad2) - 1);
    if (len != sizeof(msg2) - 1 || memcmp(msg2, out2, len) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20Poly1305AEADDecrypt() cipher test 2\n", __func__);

    len = BRChacha20Poly1305AEADEncrypt(out2, sizeof(out2), key2, nonce2, msg2, sizeof(msg2) - 1, ad2, sizeof(ad2) - 1);
    if (len != sizeof(cipher2) - 1 || memcmp(cipher2, out2, len) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRChacha20Poly1305AEADEncrypt() cipher test 2\n", __func__);

    return r;
}

int BRAesTests()
{
    int r = 1;
    
    const char iv[] = "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff";
    const char plain[] = "\x6b\xc1\xbe\xe2\x2e\x40\x9f\x96\xe9\x3d\x7e\x11\x73\x93\x17\x2a\xae\x2d\x8a\x57\x1e\x03\xac"
    "\x9c\x9e\xb7\x6f\xac\x45\xaf\x8e\x51\x30\xc8\x1c\x46\xa3\x5c\xe4\x11\xe5\xfb\xc1\x19\x1a\x0a\x52\xef\xf6\x9f\x24"
    "\x45\xdf\x4f\x9b\x17\xad\x2b\x41\x7b\xe6\x6c\x37\x10";
    char buf[sizeof(plain)];
    UInt256 key1 = toUInt256("2b7e151628aed2a6abf7158809cf4f3c00000000000000000000000000000000");
    const char cipher1[] = "\x3a\xd7\x7b\xb4\x0d\x7a\x36\x60\xa8\x9e\xca\xf3\x24\x66\xef\x97";
    const char in1[] = "\x87\x4d\x61\x91\xb6\x20\xe3\x26\x1b\xef\x68\x64\x99\x0d\xb6\xce\x98\x06\xf6\x6b\x79\x70\xfd"
    "\xff\x86\x17\x18\x7b\xb9\xff\xfd\xff\x5a\xe4\xdf\x3e\xdb\xd5\xd3\x5e\x5b\x4f\x09\x02\x0d\xb0\x3e\xab\x1e\x03\x1d"
    "\xda\x2f\xbe\x03\xd1\x79\x21\x70\xa0\xf3\x00\x9c\xee";
    
    memcpy(buf, plain, 16);
    BRAESECBEncrypt(buf, &key1, 16);
    if (memcmp(buf, cipher1, 16) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESECBEncrypt() test 1", __func__);

    memcpy(buf, cipher1, 16);
    BRAESECBDecrypt(buf, &key1, 16);
    if (memcmp(buf, plain, 16) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESECBDecrypt() test 1", __func__);

    BRAESCTR(buf, &key1, 16, iv, in1, 64);
    if (memcmp(buf, plain, 64) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESCTR() test 1", __func__);
    
    UInt256 key2 = toUInt256("8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b0000000000000000");
    const char cipher2[] = "\xbd\x33\x4f\x1d\x6e\x45\xf2\x5f\xf7\x12\xa2\x14\x57\x1f\xa5\xcc";
    const char in2[] = "\x1a\xbc\x93\x24\x17\x52\x1c\xa2\x4f\x2b\x04\x59\xfe\x7e\x6e\x0b\x09\x03\x39\xec\x0a\xa6\xfa"
    "\xef\xd5\xcc\xc2\xc6\xf4\xce\x8e\x94\x1e\x36\xb2\x6b\xd1\xeb\xc6\x70\xd1\xbd\x1d\x66\x56\x20\xab\xf7\x4f\x78\xa7"
    "\xf6\xd2\x98\x09\x58\x5a\x97\xda\xec\x58\xc6\xb0\x50";
    
    memcpy(buf, plain, 16);
    BRAESECBEncrypt(buf, &key2, 24);
    if (memcmp(buf, cipher2, 16) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESECBEncrypt() test 2", __func__);

    memcpy(buf, cipher2, 16);
    BRAESECBDecrypt(buf, &key2, 24);
    if (memcmp(buf, plain, 16) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESECBDecrypt() test 2", __func__);

    BRAESCTR(buf, &key2, 24, iv, in2, 64);
    if (memcmp(buf, plain, 64) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESCTR() test 2", __func__);

    UInt256 key3 = toUInt256("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    const char cipher3[] = "\xf3\xee\xd1\xbd\xb5\xd2\xa0\x3c\x06\x4b\x5a\x7e\x3d\xb1\x81\xf8";
    const char in3[] = "\x60\x1e\xc3\x13\x77\x57\x89\xa5\xb7\xa7\xf5\x04\xbb\xf3\xd2\x28\xf4\x43\xe3\xca\x4d\x62\xb5"
    "\x9a\xca\x84\xe9\x90\xca\xca\xf5\xc5\x2b\x09\x30\xda\xa2\x3d\xe9\x4c\xe8\x70\x17\xba\x2d\x84\x98\x8d\xdf\xc9\xc5"
    "\x8d\xb6\x7a\xad\xa6\x13\xc2\xdd\x08\x45\x79\x41\xa6";
    
    memcpy(buf, plain, 16);
    BRAESECBEncrypt(buf, &key3, 32);
    if (memcmp(buf, cipher3, 16) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESECBEncrypt() test 3", __func__);

    memcpy(buf, cipher3, 16);
    BRAESECBDecrypt(buf, &key3, 32);
    if (memcmp(buf, plain, 16) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESECBDecrypt() test 3", __func__);

    BRAESCTR(buf, &key3, 32, iv, in3, 64);
    if (memcmp(buf, plain, 64) != 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAESCTR() test 3", __func__);
    
    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

int BRKeyTests()
{
    int r = 1;
    BRKey key, key2;
    BRAddress addr;
    char *msg;
    UInt256 md;
    uint8_t sig[72], pubKey[65];
    size_t sigLen, pkLen;

    if (BRPrivKeyIsValid("S6c56bnXQiBjk9mqSYE7ykVQ7NzrRz"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPrivKeyIsValid() test 0\n", __func__);

    // mini private key format
    if (! BRPrivKeyIsValid("S6c56bnXQiBjk9mqSYE7ykVQ7NzrRy"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPrivKeyIsValid() test 1\n", __func__);

    printf("\n");
    BRKeySetPrivKey(&key, "S6c56bnXQiBjk9mqSYE7ykVQ7NzrRy");
    BRKeyLegacyAddr(&key, addr.s, sizeof(addr));
    printf("privKey:S6c56bnXQiBjk9mqSYE7ykVQ7NzrRy = %s\n", addr.s);
#if BITCOIN_TESTNET
    if (! BRAddressEq(&addr, "ms8fwvXzrCoyatnGFRaLbepSqwGRxVJQF1"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetPrivKey() test 1\n", __func__);
#else
    if (! BRAddressEq(&addr, "1CciesT23BNionJeXrbxmjc7ywfiyM4oLW"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetPrivKey() test 1\n", __func__);
#endif

    // old mini private key format
    if (! BRPrivKeyIsValid("SzavMBLoXU6kDrqtUVmffv"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPrivKeyIsValid() test 2\n", __func__);

    BRKeySetPrivKey(&key, "SzavMBLoXU6kDrqtUVmffv");
    BRKeyLegacyAddr(&key, addr.s, sizeof(addr));
    printf("privKey:SzavMBLoXU6kDrqtUVmffv = %s\n", addr.s);
#if BITCOIN_TESTNET
    if (! BRAddressEq(&addr, "mrhzp5mstA4Midx85EeCjuaUAAGANMFmRP"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetPrivKey() test 2\n", __func__);
#else
    if (! BRAddressEq(&addr, "1CC3X2gu58d6wXUWMffpuzN9JAfTUWu4Kj"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetPrivKey() test 2\n", __func__);
#endif

#if ! BITCOIN_TESTNET
    // uncompressed private key
    if (! BRPrivKeyIsValid("5Kb8kLf9zgWQnogidDA76MzPL6TsZZY36hWXMssSzNydYXYB9KF"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPrivKeyIsValid() test 3\n", __func__);
    
    BRKeySetPrivKey(&key, "5Kb8kLf9zgWQnogidDA76MzPL6TsZZY36hWXMssSzNydYXYB9KF");
    BRKeyLegacyAddr(&key, addr.s, sizeof(addr));
    printf("privKey:5Kb8kLf9zgWQnogidDA76MzPL6TsZZY36hWXMssSzNydYXYB9KF = %s\n", addr.s);
    if (! BRAddressEq(&addr, "1CC3X2gu58d6wXUWMffpuzN9JAfTUWu4Kj"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetPrivKey() test 3\n", __func__);

    // uncompressed private key export
    char privKey1[BRKeyPrivKey(&key, NULL, 0)];
    
    BRKeyPrivKey(&key, privKey1, sizeof(privKey1));
    printf("privKey:%s\n", privKey1);
    if (strcmp(privKey1, "5Kb8kLf9zgWQnogidDA76MzPL6TsZZY36hWXMssSzNydYXYB9KF") != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 1\n", __func__);
    
    // compressed private key
    if (! BRPrivKeyIsValid("KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPrivKeyIsValid() test 4\n", __func__);
    
    BRKeySetPrivKey(&key, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    BRKeyLegacyAddr(&key, addr.s, sizeof(addr));
    printf("privKey:KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL = %s\n", addr.s);
    if (! BRAddressEq(&addr, "1JMsC6fCtYWkTjPPdDrYX3we2aBrewuEM3"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetPrivKey() test 4\n", __func__);
    
    // compressed private key export
    char privKey2[BRKeyPrivKey(&key, NULL, 0)];
    
    BRKeyPrivKey(&key, privKey2, sizeof(privKey2));
    printf("privKey:%s\n", privKey2);
    if (strcmp(privKey2, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL") != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 2\n", __func__);
#endif

    // pubkey match
    BRKey prvKeyX1, prvKeyX2;
    BRKey pubKeyX1, pubKeyX2;

    BRKeySetPrivKey (&prvKeyX1, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.1\n", __func__);

    BRKeyClean(&prvKeyX1); BRKeyClean(&prvKeyX2);
    BRKeySetPrivKey (&prvKeyX1, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    BRKeySetPrivKey (&prvKeyX2, "5Kb8kLf9zgWQnogidDA76MzPL6TsZZY36hWXMssSzNydYXYB9KF");
    if (BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.2\n", __func__);

    BRKeyClean(&prvKeyX1); BRKeyClean(&prvKeyX2);
    BRKeySetPrivKey (&prvKeyX1, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    BRKeySetPrivKey (&prvKeyX2, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    prvKeyX1.compressed = 0;
    prvKeyX2.compressed = 0;
   if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.1\n", __func__);
    BRKeySetPubKey (&pubKeyX1, prvKeyX1.pubKey, 65);
    BRKeySetPubKey (&pubKeyX2, prvKeyX2.pubKey, 65);
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.2\n", __func__);

    BRKeyClean(&prvKeyX1); BRKeyClean(&prvKeyX2);
    BRKeySetPrivKey (&prvKeyX1, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    BRKeySetPrivKey (&prvKeyX2, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    prvKeyX1.compressed = 0;
    prvKeyX2.compressed = 1;
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.1\n", __func__);
    BRKeySetPubKey (&pubKeyX1, prvKeyX1.pubKey, 65);
    BRKeySetPubKey (&pubKeyX2, prvKeyX2.pubKey, 33);
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.2\n", __func__);

    BRKeyClean(&prvKeyX1); BRKeyClean(&prvKeyX2);
    BRKeySetPrivKey (&prvKeyX1, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    BRKeySetPrivKey (&prvKeyX2, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    prvKeyX1.compressed = 1;
    prvKeyX2.compressed = 0;
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.1\n", __func__);
    BRKeySetPubKey (&pubKeyX1, prvKeyX1.pubKey, 33);
    BRKeySetPubKey (&pubKeyX2, prvKeyX2.pubKey, 65);
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.2\n", __func__);

    BRKeyClean(&prvKeyX1); BRKeyClean(&prvKeyX2);
    BRKeySetPrivKey (&prvKeyX1, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    BRKeySetPrivKey (&prvKeyX2, "KyvGbxRUoofdw3TNydWn2Z78dBHSy2odn1d3wXWN2o3SAtccFNJL");
    prvKeyX1.compressed = 1;
    prvKeyX2.compressed = 1;
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.1\n", __func__);
    BRKeySetPubKey (&pubKeyX1, prvKeyX1.pubKey, 33);
    BRKeySetPubKey (&pubKeyX2, prvKeyX2.pubKey, 33);
    if (!BRKeyPubKeyMatch (&prvKeyX1, &prvKeyX2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyPrivKey() test 5.3.2\n", __func__);

    // signing
    BRKeySetSecret(&key, &toUInt256("0000000000000000000000000000000000000000000000000000000000000001"), 1);
    msg = "Everything should be made as simple as possible, but not simpler.";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig1[] = "\x30\x44\x02\x20\x33\xa6\x9c\xd2\x06\x54\x32\xa3\x0f\x3d\x1c\xe4\xeb\x0d\x59\xb8\xab\x58\xc7\x4f\x27"
    "\xc4\x1a\x7f\xdb\x56\x96\xad\x4e\x61\x08\xc9\x02\x20\x6f\x80\x79\x82\x86\x6f\x78\x5d\x3f\x64\x18\xd2\x41\x63\xdd"
    "\xae\x11\x7b\x7d\xb4\xd5\xfd\xf0\x07\x1d\xe0\x69\xfa\x54\x34\x22\x62";

    if (sigLen != sizeof(sig1) - 1 || memcmp(sig, sig1, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 1\n", __func__);

    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 1\n", __func__);

    BRKeySetSecret(&key, &toUInt256("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140"), 1);
    msg = "Equations are more important to me, because politics is for the present, but an equation is something for "
    "eternity.";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig2[] = "\x30\x44\x02\x20\x54\xc4\xa3\x3c\x64\x23\xd6\x89\x37\x8f\x16\x0a\x7f\xf8\xb6\x13\x30\x44\x4a\xbb\x58"
    "\xfb\x47\x0f\x96\xea\x16\xd9\x9d\x4a\x2f\xed\x02\x20\x07\x08\x23\x04\x41\x0e\xfa\x6b\x29\x43\x11\x1b\x6a\x4e\x0a"
    "\xaa\x7b\x7d\xb5\x5a\x07\xe9\x86\x1d\x1f\xb3\xcb\x1f\x42\x10\x44\xa5";

    if (sigLen != sizeof(sig2) - 1 || memcmp(sig, sig2, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 2\n", __func__);
    
    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 2\n", __func__);

    BRKeySetSecret(&key, &toUInt256("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364140"), 1);
    msg = "Not only is the Universe stranger than we think, it is stranger than we can think.";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig3[] = "\x30\x45\x02\x21\x00\xff\x46\x6a\x9f\x1b\x7b\x27\x3e\x2f\x4c\x3f\xfe\x03\x2e\xb2\xe8\x14\x12\x1e\xd1"
    "\x8e\xf8\x46\x65\xd0\xf5\x15\x36\x0d\xab\x3d\xd0\x02\x20\x6f\xc9\x5f\x51\x32\xe5\xec\xfd\xc8\xe5\xe6\xe6\x16\xcc"
    "\x77\x15\x14\x55\xd4\x6e\xd4\x8f\x55\x89\xb7\xdb\x77\x71\xa3\x32\xb2\x83";
    
    if (sigLen != sizeof(sig3) - 1 || memcmp(sig, sig3, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 3\n", __func__);
    
    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 3\n", __func__);

    BRKeySetSecret(&key, &toUInt256("0000000000000000000000000000000000000000000000000000000000000001"), 1);
    msg = "How wonderful that we have met with a paradox. Now we have some hope of making progress.";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig4[] = "\x30\x45\x02\x21\x00\xc0\xda\xfe\xc8\x25\x1f\x1d\x50\x10\x28\x9d\x21\x02\x32\x22\x0b\x03\x20\x2c\xba"
    "\x34\xec\x11\xfe\xc5\x8b\x3e\x93\xa8\x5b\x91\xd3\x02\x20\x75\xaf\xdc\x06\xb7\xd6\x32\x2a\x59\x09\x55\xbf\x26\x4e"
    "\x7a\xaa\x15\x58\x47\xf6\x14\xd8\x00\x78\xa9\x02\x92\xfe\x20\x50\x64\xd3";
    
    if (sigLen != sizeof(sig4) - 1 || memcmp(sig, sig4, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 4\n", __func__);
    
    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 4\n", __func__);

    BRKeySetSecret(&key, &toUInt256("69ec59eaa1f4f2e36b639716b7c30ca86d9a5375c7b38d8918bd9c0ebc80ba64"), 1);
    msg = "Computer science is no more about computers than astronomy is about telescopes.";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig5[] = "\x30\x44\x02\x20\x71\x86\x36\x35\x71\xd6\x5e\x08\x4e\x7f\x02\xb0\xb7\x7c\x3e\xc4\x4f\xb1\xb2\x57\xde"
    "\xe2\x62\x74\xc3\x8c\x92\x89\x86\xfe\xa4\x5d\x02\x20\x0d\xe0\xb3\x8e\x06\x80\x7e\x46\xbd\xa1\xf1\xe2\x93\xf4\xf6"
    "\x32\x3e\x85\x4c\x86\xd5\x8a\xbd\xd0\x0c\x46\xc1\x64\x41\x08\x5d\xf6";
    
    if (sigLen != sizeof(sig5) - 1 || memcmp(sig, sig5, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 5\n", __func__);
    
    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 5\n", __func__);

    BRKeySetSecret(&key, &toUInt256("00000000000000000000000000007246174ab1e92e9149c6e446fe194d072637"), 1);
    msg = "...if you aren't, at any given time, scandalized by code you wrote five or even three years ago, you're not"
    " learning anywhere near enough";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig6[] = "\x30\x45\x02\x21\x00\xfb\xfe\x50\x76\xa1\x58\x60\xba\x8e\xd0\x0e\x75\xe9\xbd\x22\xe0\x5d\x23\x0f\x02"
    "\xa9\x36\xb6\x53\xeb\x55\xb6\x1c\x99\xdd\xa4\x87\x02\x20\x0e\x68\x88\x0e\xbb\x00\x50\xfe\x43\x12\xb1\xb1\xeb\x08"
    "\x99\xe1\xb8\x2d\xa8\x9b\xaa\x5b\x89\x5f\x61\x26\x19\xed\xf3\x4c\xbd\x37";
    
    if (sigLen != sizeof(sig6) - 1 || memcmp(sig, sig6, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 6\n", __func__);
    
    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 6\n", __func__);

    BRKeySetSecret(&key, &toUInt256("000000000000000000000000000000000000000000056916d0f9b31dc9b637f3"), 1);
    msg = "The question of whether computers can think is like the question of whether submarines can swim.";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeySign(&key, sig, sizeof(sig), md);
    
    char sig7[] = "\x30\x45\x02\x21\x00\xcd\xe1\x30\x2d\x83\xf8\xdd\x83\x5d\x89\xae\xf8\x03\xc7\x4a\x11\x9f\x56\x1f\xba"
    "\xef\x3e\xb9\x12\x9e\x45\xf3\x0d\xe8\x6a\xbb\xf9\x02\x20\x06\xce\x64\x3f\x50\x49\xee\x1f\x27\x89\x04\x67\xb7\x7a"
    "\x6a\x8e\x11\xec\x46\x61\xcc\x38\xcd\x8b\xad\xf9\x01\x15\xfb\xd0\x3c\xef";
    
    if (sigLen != sizeof(sig7) - 1 || memcmp(sig, sig7, sigLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySign() test 7\n", __func__);
    
    if (! BRKeyVerify(&key, md, sig, sigLen))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyVerify() test 7\n", __func__);

    // compact signing
    BRKeySetSecret(&key, &toUInt256("0000000000000000000000000000000000000000000000000000000000000001"), 1);
    msg = "foo";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeyCompactSign(&key, sig, sizeof(sig), md);
    BRKeyRecoverPubKey(&key2, md, sig, sigLen);
    pkLen = BRKeyPubKey(&key2, pubKey, sizeof(pubKey));
    
    uint8_t pubKey1[BRKeyPubKey(&key, NULL, 0)];
    size_t pkLen1 = BRKeyPubKey(&key, pubKey1, sizeof(pubKey1));
    
    if (pkLen1 != pkLen || memcmp(pubKey, pubKey1, pkLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyCompactSign() test 1\n", __func__);

    BRKeySetSecret(&key, &toUInt256("0000000000000000000000000000000000000000000000000000000000000001"), 0);
    msg = "foo";
    BRSHA256(&md, msg, strlen(msg));
    sigLen = BRKeyCompactSign(&key, sig, sizeof(sig), md);
    BRKeyRecoverPubKey(&key2, md, sig, sigLen);
    pkLen = BRKeyPubKey(&key2, pubKey, sizeof(pubKey));
    
    uint8_t pubKey2[BRKeyPubKey(&key, NULL, 0)];
    size_t pkLen2 = BRKeyPubKey(&key, pubKey2, sizeof(pubKey2));
    
    if (pkLen2 != pkLen || memcmp(pubKey, pubKey2, pkLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyCompactSign() test 2\n", __func__);

    // compact pubkey recovery
    pkLen = BRBase58Decode(pubKey, sizeof(pubKey), "26wZYDdvpmCrYZeUcxgqd1KquN4o6wXwLomBW5SjnwUqG");
    msg = "i am a test signed string";
    BRSHA256_2(&md, msg, strlen(msg));
    sigLen = BRBase58Decode(sig, sizeof(sig),
                           "3kq9e842BzkMfbPSbhKVwGZgspDSkz4YfqjdBYQPWDzqd77gPgR1zq4XG7KtAL5DZTcfFFs2iph4urNyXeBkXsEYY");
    BRKeyRecoverPubKey(&key2, md, sig, sigLen);
    uint8_t pubKey3[BRKeyPubKey(&key2, NULL, 0)];
    size_t pkLen3 = BRKeyPubKey(&key2, pubKey3, sizeof(pubKey3));

    if (pkLen3 != pkLen || memcmp(pubKey, pubKey3, pkLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPubKeyRecover() test 1\n", __func__);

    pkLen = BRBase58Decode(pubKey, sizeof(pubKey), "26wZYDdvpmCrYZeUcxgqd1KquN4o6wXwLomBW5SjnwUqG");
    msg = "i am a test signed string do de dah";
    BRSHA256_2(&md, msg, strlen(msg));
    sigLen = BRBase58Decode(sig, sizeof(sig),
                           "3qECEYmb6x4X22sH98Aer68SdfrLwtqvb5Ncv7EqKmzbxeYYJ1hU9irP6R5PeCctCPYo5KQiWFgoJ3H5MkuX18gHu");
    
    BRKeyRecoverPubKey(&key2, md, sig, sigLen);
    uint8_t pubKey4[BRKeyPubKey(&key2, NULL, 0)];
    size_t pkLen4 = BRKeyPubKey(&key2, pubKey4, sizeof(pubKey4));
    
    if (pkLen4 != pkLen || memcmp(pubKey, pubKey4, pkLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPubKeyRecover() test 2\n", __func__);

    pkLen = BRBase58Decode(pubKey, sizeof(pubKey), "gpRv1sNA3XURB6QEtGrx6Q18DZ5cSgUSDQKX4yYypxpW");
    msg = "i am a test signed string";
    BRSHA256_2(&md, msg, strlen(msg));
    sigLen = BRBase58Decode(sig, sizeof(sig),
                           "3oHQhxq5eW8dnp7DquTCbA5tECoNx7ubyiubw4kiFm7wXJF916SZVykFzb8rB1K6dEu7mLspBWbBEJyYk79jAosVR");
    
    BRKeyRecoverPubKey(&key2, md, sig, sigLen);
    uint8_t pubKey5[BRKeyPubKey(&key2, NULL, 0)];
    size_t pkLen5 = BRKeyPubKey(&key2, pubKey5, sizeof(pubKey5));
    
    if (pkLen5 != pkLen || memcmp(pubKey, pubKey5, pkLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPubKeyRecover() test 3\n", __func__);

    printf("                                    ");
    return r;
}

int BRBIP38KeyTests()
{
    int r = 1;
    BRKey key;
    char privKey[55], bip38Key[61];
    
    printf("\n");

    // non EC multiplied, uncompressed
    if (! BRKeySetPrivKey(&key, "5KN7MzqK5wt2TP1fQCYyHBtDrXdJuXbUzm4A9rKAteGu3Qi5CVR") ||
        ! BRKeyBIP38Key(&key, bip38Key, sizeof(bip38Key), "TestingOneTwoThree") ||
        strncmp(bip38Key, "6PRVWUbkzzsbcVac2qwfssoUJAN1Xhrg6bNk8J7Nzm5H7kxEbn2Nh2ZoGg", sizeof(bip38Key)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyBIP38Key() test 1\n", __func__);
    
    if (! BRKeySetBIP38Key(&key, "6PRVWUbkzzsbcVac2qwfssoUJAN1Xhrg6bNk8J7Nzm5H7kxEbn2Nh2ZoGg", "TestingOneTwoThree") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "5KN7MzqK5wt2TP1fQCYyHBtDrXdJuXbUzm4A9rKAteGu3Qi5CVR", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 1\n", __func__);

    printf("privKey:%s\n", privKey);

    if (! BRKeySetPrivKey(&key, "5HtasZ6ofTHP6HCwTqTkLDuLQisYPah7aUnSKfC7h4hMUVw2gi5") ||
        ! BRKeyBIP38Key(&key, bip38Key, sizeof(bip38Key), "Satoshi") ||
        strncmp(bip38Key, "6PRNFFkZc2NZ6dJqFfhRoFNMR9Lnyj7dYGrzdgXXVMXcxoKTePPX1dWByq", sizeof(bip38Key)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyBIP38Key() test 2\n", __func__);

    if (! BRKeySetBIP38Key(&key, "6PRNFFkZc2NZ6dJqFfhRoFNMR9Lnyj7dYGrzdgXXVMXcxoKTePPX1dWByq", "Satoshi") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "5HtasZ6ofTHP6HCwTqTkLDuLQisYPah7aUnSKfC7h4hMUVw2gi5", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 2\n", __func__);

    printf("privKey:%s\n", privKey);
    
    // non EC multiplied, compressed
    if (! BRKeySetPrivKey(&key, "L44B5gGEpqEDRS9vVPz7QT35jcBG2r3CZwSwQ4fCewXAhAhqGVpP") ||
        ! BRKeyBIP38Key(&key, bip38Key, sizeof(bip38Key), "TestingOneTwoThree") ||
        strncmp(bip38Key, "6PYNKZ1EAgYgmQfmNVamxyXVWHzK5s6DGhwP4J5o44cvXdoY7sRzhtpUeo", sizeof(bip38Key)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyBIP38Key() test 3\n", __func__);

    if (! BRKeySetBIP38Key(&key, "6PYNKZ1EAgYgmQfmNVamxyXVWHzK5s6DGhwP4J5o44cvXdoY7sRzhtpUeo", "TestingOneTwoThree") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "L44B5gGEpqEDRS9vVPz7QT35jcBG2r3CZwSwQ4fCewXAhAhqGVpP", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 3\n", __func__);

    printf("privKey:%s\n", privKey);

    if (! BRKeySetPrivKey(&key, "KwYgW8gcxj1JWJXhPSu4Fqwzfhp5Yfi42mdYmMa4XqK7NJxXUSK7") ||
        ! BRKeyBIP38Key(&key, bip38Key, sizeof(bip38Key), "Satoshi") ||
        strncmp(bip38Key, "6PYLtMnXvfG3oJde97zRyLYFZCYizPU5T3LwgdYJz1fRhh16bU7u6PPmY7", sizeof(bip38Key)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeyBIP38Key() test 4\n", __func__);

    if (! BRKeySetBIP38Key(&key, "6PYLtMnXvfG3oJde97zRyLYFZCYizPU5T3LwgdYJz1fRhh16bU7u6PPmY7", "Satoshi") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "KwYgW8gcxj1JWJXhPSu4Fqwzfhp5Yfi42mdYmMa4XqK7NJxXUSK7", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 4\n", __func__);

    printf("privKey:%s\n", privKey);

    // EC multiplied, uncompressed, no lot/sequence number
    if (! BRKeySetBIP38Key(&key, "6PfQu77ygVyJLZjfvMLyhLMQbYnu5uguoJJ4kMCLqWwPEdfpwANVS76gTX", "TestingOneTwoThree") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "5K4caxezwjGCGfnoPTZ8tMcJBLB7Jvyjv4xxeacadhq8nLisLR2", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 5\n", __func__);

    printf("privKey:%s\n", privKey);

    if (! BRKeySetBIP38Key(&key, "6PfLGnQs6VZnrNpmVKfjotbnQuaJK4KZoPFrAjx1JMJUa1Ft8gnf5WxfKd", "Satoshi") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "5KJ51SgxWaAYR13zd9ReMhJpwrcX47xTJh2D3fGPG9CM8vkv5sH", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 6\n", __func__);

    printf("privKey:%s\n", privKey);
    
    // EC multiplied, uncompressed, with lot/sequence number
    if (! BRKeySetBIP38Key(&key, "6PgNBNNzDkKdhkT6uJntUXwwzQV8Rr2tZcbkDcuC9DZRsS6AtHts4Ypo1j", "MOLON LABE") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "5JLdxTtcTHcfYcmJsNVy1v2PMDx432JPoYcBTVVRHpPaxUrdtf8", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 7\n", __func__);

    printf("privKey:%s\n", privKey);

    if (! BRKeySetBIP38Key(&key, "6PgGWtx25kUg8QWvwuJAgorN6k9FbE25rv5dMRwu5SKMnfpfVe5mar2ngH",
                           "\u039c\u039f\u039b\u03a9\u039d \u039b\u0391\u0392\u0395") ||
        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
        strncmp(privKey, "5KMKKuUmAkiNbA3DazMQiLfDq47qs8MAEThm4yL8R2PhV1ov33D", sizeof(privKey)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 8\n", __func__);

    printf("privKey:%s\n", privKey);
    
//    // password NFC unicode normalization test
//    if (! BRKeySetBIP38Key(&key, "6PRW5o9FLp4gJDDVqJQKJFTpMvdsSGJxMYHtHaQBF3ooa8mwD69bapcDQn",
//                           "\u03D2\u0301\0\U00010400\U0001F4A9") ||
//        ! BRKeyPrivKey(&key, privKey, sizeof(privKey)) ||
//        strncmp(privKey, "5Jajm8eQ22H3pGWLEVCXyvND8dQZhiQhoLJNKjYXk9roUFTMSZ4", sizeof(privKey)) != 0)
//        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 9\n", __func__);
//
//    printf("privKey:%s\n", privKey);

    // incorrect password test
    if (BRKeySetBIP38Key(&key, "6PRW5o9FLp4gJDDVqJQKJFTpMvdsSGJxMYHtHaQBF3ooa8mwD69bapcDQn", "foobar"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRKeySetBIP38Key() test 10\n", __func__);

    printf("                                    ");
    return r;
}

int BRKeyECIESTests()
{
    int r = 1;
    BRKey key, ephem;
    size_t len;

    char plain[] = "All decent, reasonable men are horrified by the idea that the government might control the press. "
    "None of them seem concerned at all that the press might control the government.";
    
    BRKeySetSecret(&key, &toUInt256("0000000000000000000000000000000000000000000000000000000000000001"), 0);
    BRKeySetSecret(&ephem, &toUInt256("0000000000000000000000000000000000000000000000000000000000000002"), 0);
    char dec[sizeof(plain)], cipher[sizeof(plain) + 65 + 16 + 32];
    
    len = BRKeyECIESAES128SHA256Encrypt(&key, cipher, sizeof(cipher), &ephem, plain, sizeof(plain) - 1);
    if (len == 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRKeyECIESAES128SHA256Encrypt() test 1", __func__);

    len = BRKeyECIESAES128SHA256Decrypt(&key, dec, sizeof(dec), cipher, len);
    if (len != sizeof(plain) - 1 || strncmp(dec, plain, len) != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRKeyECIESAES128SHA256Decrypt() test 1", __func__);
    
    char cipher2[] = "\x04\xff\x2c\x87\x4d\x0a\x47\x91\x7c\x84\xee\xa0\xb2\xa4\x14\x1c\xa9\x52\x33\x72\x0b\x5c\x70\xf8"
    "\x1a\x84\x15\xba\xe1\xdc\x7b\x74\x6b\x61\xdf\x75\x58\x81\x1c\x1d\x60\x54\x33\x39\x07\x33\x3e\xf9\xbb\x0c\xc2\xfb"
    "\xf8\xb3\x4a\xbb\x97\x30\xd1\x4e\x01\x40\xf4\x55\x3f\x4b\x15\xd7\x05\x12\x0a\xf4\x6c\xf6\x53\xa1\xdc\x5b\x95\xb3"
    "\x12\xcf\x84\x44\x71\x4f\x95\xa4\xf7\xa0\x42\x5b\x67\xfc\x06\x4d\x18\xf4\xd0\xa5\x28\x76\x15\x65\xca\x02\xd9\x7f"
    "\xaf\xfd\xac\x23\xde\x10";
    char dec2[2];
    
    BRKeySetSecret(&key, &toUInt256("57baf2c62005ddec64c357d96183ebc90bf9100583280e848aa31d683cad73cb"), 0);
    len = BRKeyECIESAES128SHA256Decrypt(&key, dec2, sizeof(dec2), cipher2, sizeof(cipher2) - 1);
    if (len != 1 || strncmp(dec2, "a", 1) != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRKeyECIESAES128SHA256Decrypt() test2", __func__);
    
    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

int BRAddressTests()
{
    int r = 1;
    UInt256 secret = toUInt256("0000000000000000000000000000000000000000000000000000000000000001");
    BRKey k;
    BRAddress addr, addr2;
    
    BRKeySetSecret(&k, &secret, 1);
    if (! BRKeyAddress(&k, addr.s, sizeof(addr)))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRKeyAddress()", __func__);

    uint8_t script[BRAddressScriptPubKey(NULL, 0, addr.s)];
    size_t scriptLen = BRAddressScriptPubKey(script, sizeof(script), addr.s);
    
    BRAddressFromScriptPubKey(addr2.s, sizeof(addr2), script, scriptLen);
    if (! BRAddressEq(&addr, &addr2))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAddressFromScriptPubKey() test 1", __func__);
    
    // TODO: test BRAddressFromScriptSig()
    
    BRAddress addr3;
    char script2[] = "\0\x14\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    if (! BRAddressFromScriptPubKey(addr3.s, sizeof(addr3), (uint8_t *)script2, sizeof(script2)))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAddressFromScriptPubKey() test 2", __func__);

    uint8_t script3[BRAddressScriptPubKey(NULL, 0, addr3.s)];
    size_t script3Len = BRAddressScriptPubKey(script3, sizeof(script3), addr3.s);

    if (script3Len != sizeof(script2) || memcmp(script2, script3, sizeof(script2)))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRAddressScriptPubKey() test", __func__);

    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

int BRBIP39MnemonicTests()
{
    int r = 1;
    
    const char *s = "bless cloud wheel regular tiny venue bird web grief security dignity zoo";

    // test correct handling of bad checksum
    if (BRBIP39PhraseIsValid(BRBIP39WordsEn, s))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39PhraseIsValid() test\n", __func__);

    UInt512 key = UINT512_ZERO;

//    BRBIP39DeriveKey(key.u8, NULL, NULL); // test invalid key
//    if (! UInt512IsZero(key)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 0\n", __func__);

    UInt128 entropy = UINT128_ZERO;
    char phrase[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy.u8, sizeof(entropy))];
    size_t len = BRBIP39Encode(phrase, sizeof(phrase), BRBIP39WordsEn, entropy.u8, sizeof(entropy));
    
    if (strncmp(phrase, "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
                len)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 1\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase);
    if (! UInt128IsZero(entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 1\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\xc5\x52\x57\xc3\x60\xc0\x7c\x72\x02\x9a\xeb\xc1\xb5\x3c\x05\xed\x03\x62\xad\xa3"
                    "\x8e\xad\x3e\x3e\x9e\xfa\x37\x08\xe5\x34\x95\x53\x1f\x09\xa6\x98\x75\x99\xd1\x82\x64\xc1\xe1\xc9"
                    "\x2f\x2c\xf1\x41\x63\x0c\x7a\x3c\x4a\xb7\xc8\x1b\x2f\x00\x16\x98\xe7\x46\x3b\x04"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 1\n", __func__);

    UInt128 entropy2 = *(UInt128 *)"\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f";
    char phrase2[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy2.u8, sizeof(entropy2))];
    size_t len2 = BRBIP39Encode(phrase2, sizeof(phrase2), BRBIP39WordsEn, entropy2.u8, sizeof(entropy2));
    
    if (strncmp(phrase2, "legal winner thank year wave sausage worth useful legal winner thank yellow", len2))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 2\n", __func__);

    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase2);
    if (! UInt128Eq(entropy2, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 2\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase2, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\x2e\x89\x05\x81\x9b\x87\x23\xfe\x2c\x1d\x16\x18\x60\xe5\xee\x18\x30\x31\x8d\xbf"
                    "\x49\xa8\x3b\xd4\x51\xcf\xb8\x44\x0c\x28\xbd\x6f\xa4\x57\xfe\x12\x96\x10\x65\x59\xa3\xc8\x09\x37"
                    "\xa1\xc1\x06\x9b\xe3\xa3\xa5\xbd\x38\x1e\xe6\x26\x0e\x8d\x97\x39\xfc\xe1\xf6\x07"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 2\n", __func__);

    UInt128 entropy3 = *(UInt128 *)"\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80";
    char phrase3[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy3.u8, sizeof(entropy3))];
    size_t len3 = BRBIP39Encode(phrase3, sizeof(phrase3), BRBIP39WordsEn, entropy3.u8, sizeof(entropy3));
    
    if (strncmp(phrase3, "letter advice cage absurd amount doctor acoustic avoid letter advice cage above",
                len3)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 3\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase3);
    if (! UInt128Eq(entropy3, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 3\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase3, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\xd7\x1d\xe8\x56\xf8\x1a\x8a\xcc\x65\xe6\xfc\x85\x1a\x38\xd4\xd7\xec\x21\x6f\xd0"
                    "\x79\x6d\x0a\x68\x27\xa3\xad\x6e\xd5\x51\x1a\x30\xfa\x28\x0f\x12\xeb\x2e\x47\xed\x2a\xc0\x3b\x5c"
                    "\x46\x2a\x03\x58\xd1\x8d\x69\xfe\x4f\x98\x5e\xc8\x17\x78\xc1\xb3\x70\xb6\x52\xa8"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 3\n", __func__);

    UInt128 entropy4 = *(UInt128 *)"\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
    char phrase4[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy4.u8, sizeof(entropy4))];
    size_t len4 = BRBIP39Encode(phrase4, sizeof(phrase4), BRBIP39WordsEn, entropy4.u8, sizeof(entropy4));
    
    if (strncmp(phrase4, "zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo zoo wrong", len4))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 4\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase4);
    if (! UInt128Eq(entropy4, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 4\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase4, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\xac\x27\x49\x54\x80\x22\x52\x22\x07\x9d\x7b\xe1\x81\x58\x37\x51\xe8\x6f\x57\x10"
                    "\x27\xb0\x49\x7b\x5b\x5d\x11\x21\x8e\x0a\x8a\x13\x33\x25\x72\x91\x7f\x0f\x8e\x5a\x58\x96\x20\xc6"
                    "\xf1\x5b\x11\xc6\x1d\xee\x32\x76\x51\xa1\x4c\x34\xe1\x82\x31\x05\x2e\x48\xc0\x69"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 4\n", __func__);

    UInt128 entropy5 = *(UInt128 *)"\x77\xc2\xb0\x07\x16\xce\xc7\x21\x38\x39\x15\x9e\x40\x4d\xb5\x0d";
    char phrase5[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy5.u8, sizeof(entropy5))];
    size_t len5 = BRBIP39Encode(phrase5, sizeof(phrase5), BRBIP39WordsEn, entropy5.u8, sizeof(entropy5));
    
    if (strncmp(phrase5, "jelly better achieve collect unaware mountain thought cargo oxygen act hood bridge",
                len5)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 5\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase5);
    if (! UInt128Eq(entropy5, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 5\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase5, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\xb5\xb6\xd0\x12\x7d\xb1\xa9\xd2\x22\x6a\xf0\xc3\x34\x60\x31\xd7\x7a\xf3\x1e\x91"
                    "\x8d\xba\x64\x28\x7a\x1b\x44\xb8\xeb\xf6\x3c\xdd\x52\x67\x6f\x67\x2a\x29\x0a\xae\x50\x24\x72\xcf"
                    "\x2d\x60\x2c\x05\x1f\x3e\x6f\x18\x05\x5e\x84\xe4\xc4\x38\x97\xfc\x4e\x51\xa6\xff"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 5\n", __func__);

    UInt128 entropy6 = *(UInt128 *)"\x04\x60\xef\x47\x58\x56\x04\xc5\x66\x06\x18\xdb\x2e\x6a\x7e\x7f";
    char phrase6[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy6.u8, sizeof(entropy6))];
    size_t len6 = BRBIP39Encode(phrase6, sizeof(phrase6), BRBIP39WordsEn, entropy6.u8, sizeof(entropy6));
    
    if (strncmp(phrase6, "afford alter spike radar gate glance object seek swamp infant panel yellow", len6))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 6\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase6);
    if (! UInt128Eq(entropy6, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 6\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase6, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\x65\xf9\x3a\x9f\x36\xb6\xc8\x5c\xbe\x63\x4f\xfc\x1f\x99\xf2\xb8\x2c\xbb\x10\xb3"
                    "\x1e\xdc\x7f\x08\x7b\x4f\x6c\xb9\xe9\x76\xe9\xfa\xf7\x6f\xf4\x1f\x8f\x27\xc9\x9a\xfd\xf3\x8f\x7a"
                    "\x30\x3b\xa1\x13\x6e\xe4\x8a\x4c\x1e\x7f\xcd\x3d\xba\x7a\xa8\x76\x11\x3a\x36\xe4"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 6\n", __func__);

    UInt128 entropy7 = *(UInt128 *)"\xea\xeb\xab\xb2\x38\x33\x51\xfd\x31\xd7\x03\x84\x0b\x32\xe9\xe2";
    char phrase7[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy7.u8, sizeof(entropy7))];
    size_t len7 = BRBIP39Encode(phrase7, sizeof(phrase7), BRBIP39WordsEn, entropy7.u8, sizeof(entropy7));
    
    if (strncmp(phrase7, "turtle front uncle idea crush write shrug there lottery flower risk shell", len7))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 7\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase7);
    if (! UInt128Eq(entropy7, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 7\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase7, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\xbd\xfb\x76\xa0\x75\x9f\x30\x1b\x0b\x89\x9a\x1e\x39\x85\x22\x7e\x53\xb3\xf5\x1e"
                    "\x67\xe3\xf2\xa6\x53\x63\xca\xed\xf3\xe3\x2f\xde\x42\xa6\x6c\x40\x4f\x18\xd7\xb0\x58\x18\xc9\x5e"
                    "\xf3\xca\x1e\x51\x46\x64\x68\x56\xc4\x61\xc0\x73\x16\x94\x67\x51\x16\x80\x87\x6c"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 7\n", __func__);

    UInt128 entropy8 = *(UInt128 *)"\x18\xab\x19\xa9\xf5\x4a\x92\x74\xf0\x3e\x52\x09\xa2\xac\x8a\x91";
    char phrase8[BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy8.u8, sizeof(entropy8))];
    size_t len8 = BRBIP39Encode(phrase8, sizeof(phrase8), BRBIP39WordsEn, entropy8.u8, sizeof(entropy8));
    
    if (strncmp(phrase8, "board flee heavy tunnel powder denial science ski answer betray cargo cat", len8))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Encode() test 8\n", __func__);
    
    BRBIP39Decode(entropy.u8, sizeof(entropy), BRBIP39WordsEn, phrase8);
    if (! UInt128Eq(entropy8, entropy)) r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39Decode() test 8\n", __func__);
    
    BRBIP39DeriveKey(key.u8, phrase8, "TREZOR");
    if (! UInt512Eq(key, *(UInt512 *)"\x6e\xff\x1b\xb2\x15\x62\x91\x85\x09\xc7\x3c\xb9\x90\x26\x0d\xb0\x7c\x0c\xe3\x4f"
                    "\xf0\xe3\xcc\x4a\x8c\xb3\x27\x61\x29\xfb\xcb\x30\x0b\xdd\xfe\x00\x58\x31\x35\x0e\xfd\x63\x39\x09"
                    "\xf4\x76\xc4\x5c\x88\x25\x32\x76\xd9\xfd\x0d\xf6\xef\x48\x60\x9e\x8b\xb7\xdc\xa8"))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP39DeriveKey() test 8\n", __func__);

    return r;
}

int BRBIP32SequenceTests()
{
    int r = 1;

    UInt128 seed = *(UInt128 *)"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F";
    BRKey key;

    printf("\n");

    BRBIP32PrivKey(&key, &seed, sizeof(seed), SEQUENCE_INTERNAL_CHAIN, 2 | 0x80000000);
    printf("000102030405060708090a0b0c0d0e0f/0H/1/2H prv = %s\n", u256hex(key.secret));
    if (! UInt256Eq(key.secret, toUInt256("cbce0d719ecf7431d88e6a89fa1483e02e35092af60c042b1df2ff59fa424dca")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32PrivKey() test 1\n", __func__);
    
    // test for correct zero padding of private keys
    BRBIP32PrivKey(&key, &seed, sizeof(seed), SEQUENCE_EXTERNAL_CHAIN, 97);
    printf("000102030405060708090a0b0c0d0e0f/0H/0/97 prv = %s\n", u256hex(key.secret));
    if (! UInt256Eq(key.secret, toUInt256("00136c1ad038f9a00871895322a487ed14f1cdc4d22ad351cfa1a0d235975dd7")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32PrivKey() test 2\n", __func__);
    
    BRMasterPubKey mpk = BRBIP32MasterPubKey(&seed, sizeof(seed));
    
//    printf("000102030405060708090a0b0c0d0e0f/0H fp:%08x chain:%s pubkey:%02x%s\n", be32(mpk.fingerPrint),
//           u256hex(mpk.chainCode), mpk.pubKey[0], u256hex(*(UInt256 *)&mpk.pubKey[1]));
//    if (be32(mpk.fingerPrint) != 0x3442193e ||
//        ! UInt256Eq(mpk.chainCode, toUInt256("47fdacbd0f1097043b78c63c20c34ef4ed9a111d980047ad16282c7ae6236141")) ||
//        mpk.pubKey[0] != 0x03 ||
//        ! UInt256Eq(*(UInt256 *)&mpk.pubKey[1],
//                    toUInt256("5a784662a4a20a65bf6aab9ae98a6c068a81c52e4b032c0fb5400c706cfccc56")))
//        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32MasterPubKey() test\n", __func__);

    uint8_t pubKey[33];

    BRBIP32PubKey(pubKey, sizeof(pubKey), mpk, SEQUENCE_EXTERNAL_CHAIN, 0);
    printf("000102030405060708090a0b0c0d0e0f/0H/0/0 pub = %02x%s\n", pubKey[0], u256hex(*(UInt256 *)&pubKey[1]));
    if (pubKey[0] != 0x02 ||
        ! UInt256Eq(*(UInt256 *)&pubKey[1],
                    toUInt256("7b6a7dd645507d775215a9035be06700e1ed8c541da9351b4bd14bd50ab61428")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32PubKey() test\n", __func__);

    UInt512 dk;
    BRAddress addr;

    BRBIP39DeriveKey(dk.u8, "inhale praise target steak garlic cricket paper better evil almost sadness crawl city "
                     "banner amused fringe fox insect roast aunt prefer hollow basic ladder", NULL);
    BRBIP32BitIDKey(&key, dk.u8, sizeof(dk), 0, "http://bitid.bitcoin.blue/callback");
    BRKeyLegacyAddr(&key, addr.s, sizeof(addr));
#if BITCOIN_TESTNET
    if (strncmp(addr.s, "mxZ2Dn9vcyNeKh9DNHZw6d6NrxeYCVNjc2", sizeof(addr)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32BitIDKey() test\n", __func__);
#else
    if (strncmp(addr.s, "1J34vj4wowwPYafbeibZGht3zy3qERoUM1", sizeof(addr)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32BitIDKey() test\n", __func__);
#endif
    
    char mpks[] =
    "xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw";
    char s[sizeof(mpks)];
    
    mpk = BRBIP32ParseMasterPubKey(mpks);
    BRBIP32SerializeMasterPubKey(s, sizeof(s), mpk);
    if (strncmp(s, mpks, sizeof(mpks)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32ParseMasterPubKey() test\n", __func__);

    BRBIP32SerializeMasterPrivKey(s, sizeof(s), &seed, sizeof(seed));
    if (strncmp(s, "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk"
                "33yuGBxrMPHi", sizeof(s)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBIP32SerializeMasterPrivKey() test\n", __func__);

    printf("                                    ");
    return r;
}

static int BRTxOutputEqual(BRTxOutput *out1, BRTxOutput *out2) {
    return out1->amount == out2->amount
           && 0 == memcmp (out1->address, out2->address, sizeof (out1->address))
           && out1->scriptLen == out2->scriptLen
           && 0 == memcmp (out1->script, out2->script, out1->scriptLen * sizeof (uint8_t));
}


//
static int BRTxInputEqual(BRTxInput *in1, BRTxInput *in2) {
    return 0 == memcmp(&in1->txHash, &in2->txHash, sizeof(UInt256))
           && in1->index == in2->index
           && 0 == memcmp(in1->address, in2->address, sizeof(in1->address))
           && in1->amount == in2->amount
           && in1->scriptLen == in2->scriptLen
           && 0 == memcmp(in1->script, in2->script, in1->scriptLen * sizeof(uint8_t))
           && in1->sigLen == in2->sigLen
           && 0 == memcmp(in1->signature, in2->signature, in1->sigLen * sizeof(uint8_t))
           && in1->sequence == in2->sequence;
}

// true if tx1 and tx2 have equal data (in their respective structures).
static int BRTransactionEqual (BRTransaction *tx1, BRTransaction *tx2) {
    if (memcmp (&tx1->txHash, &tx2->txHash, sizeof(UInt256))
        || tx1->version != tx2->version
        || tx1->lockTime != tx2->lockTime
        || tx1->blockHeight != tx2->blockHeight
        || tx1->timestamp != tx2->timestamp
        || tx1->inCount != tx2->inCount
        || tx1->outCount != tx2->outCount)
        return 0;

    // Inputs
    if (NULL != tx1->inputs)
        for (int i = 0; i < tx1->inCount; i++)
            if (!BRTxInputEqual(&tx1->inputs[i], &tx2->inputs[i]))
                return 0;
    // Outputs
    if (NULL != tx1->outputs)
        for (int i = 0; i < tx1->outCount; i++)
            if (!BRTxOutputEqual(&tx1->outputs[i], &tx2->outputs[i]))
                return 0;

    return 1;
}

int BRTransactionTests()
{
    int r = 1;
    UInt256 secret = toUInt256("0000000000000000000000000000000000000000000000000000000000000001"),
            inHash = toUInt256("0000000000000000000000000000000000000000000000000000000000000001");
    BRKey k[2];
    BRAddress address, addr;
    
    memset(&k[0], 0, sizeof(k[0])); // test with array of keys where first key is empty/invalid
    BRKeySetSecret(&k[1], &secret, 1);
    BRKeyLegacyAddr(&k[1], address.s, sizeof(address));

    uint8_t script[BRAddressScriptPubKey(NULL, 0, address.s)];
    size_t scriptLen = BRAddressScriptPubKey(script, sizeof(script), address.s);
    BRTransaction *tx = BRTransactionNew();
    
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(tx, 100000000, script, scriptLen);
    BRTransactionAddOutput(tx, 4900000000, script, scriptLen);
    
    uint8_t buf[BRTransactionSerialize(tx, NULL, 0)]; // test serializing/parsing unsigned tx
    size_t len = BRTransactionSerialize(tx, buf, sizeof(buf));
    
    if (len == 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSerialize() test 0", __func__);
    BRTransactionFree(tx);
    tx = BRTransactionParse(buf, len);
    
    if (! tx || tx->inCount != 1 || tx->outCount != 2)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionParse() test 0", __func__);
    if (! tx) return r;
    
    BRTransactionSign(tx, 0, k, 2);
    BRAddressFromScriptSig(addr.s, sizeof(addr), tx->inputs[0].signature, tx->inputs[0].sigLen);
    if (! BRTransactionIsSigned(tx) || ! BRAddressEq(&address, &addr))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSign() test 1", __func__);

    uint8_t buf2[BRTransactionSerialize(tx, NULL, 0)];
    size_t len2 = BRTransactionSerialize(tx, buf2, sizeof(buf2));

    BRTransactionFree(tx);
    tx = BRTransactionParse(buf2, len2);

    if (! tx || ! BRTransactionIsSigned(tx))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionParse() test 1", __func__);
    if (! tx) return r;
    
    uint8_t buf3[BRTransactionSerialize(tx, NULL, 0)];
    size_t len3 = BRTransactionSerialize(tx, buf3, sizeof(buf3));
    
    if (len2 != len3 || memcmp(buf2, buf3, len2) != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSerialize() test 1", __func__);
    BRTransactionFree(tx);
    
    tx = BRTransactionNew();
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionSign(tx, 0, k, 2);
    BRAddressFromScriptSig(addr.s, sizeof(addr), tx->inputs[tx->inCount - 1].signature,
                           tx->inputs[tx->inCount - 1].sigLen);
    if (! BRTransactionIsSigned(tx) || ! BRAddressEq(&address, &addr))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSign() test 2", __func__);

    uint8_t buf4[BRTransactionSerialize(tx, NULL, 0)];
    size_t len4 = BRTransactionSerialize(tx, buf4, sizeof(buf4));
    
    BRTransactionFree(tx);
    tx = BRTransactionParse(buf4, len4);
    if (! tx || ! BRTransactionIsSigned(tx))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionParse() test 2", __func__);
    if (! tx) return r;

    uint8_t buf5[BRTransactionSerialize(tx, NULL, 0)];
    size_t len5 = BRTransactionSerialize(tx, buf5, sizeof(buf5));
    
    if (len4 != len5 || memcmp(buf4, buf5, len4) != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSerialize() test 2", __func__);
    BRTransactionFree(tx);

    BRKeyAddress(&k[1], addr.s, sizeof(addr));
    
    uint8_t wscript[BRAddressScriptPubKey(NULL, 0, addr.s)];
    size_t wscriptLen = BRAddressScriptPubKey(wscript, sizeof(wscript), addr.s);

    tx = BRTransactionNew();
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, wscript, wscriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, wscript, wscriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, wscript, wscriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, wscript, wscriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, wscript, wscriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(tx, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionAddOutput(tx, 1000000, script, scriptLen);
    BRTransactionSign(tx, 0, k, 2);
    BRAddressFromScriptSig(addr.s, sizeof(addr), tx->inputs[tx->inCount - 1].signature,
                           tx->inputs[tx->inCount - 1].sigLen);
    if (! BRTransactionIsSigned(tx) || ! BRAddressEq(&address, &addr) || tx->inputs[1].sigLen > 0 ||
        tx->inputs[1].witLen == 0) r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSign() test 3", __func__);
    
    uint8_t buf6[BRTransactionSerialize(tx, NULL, 0)];
    size_t len6 = BRTransactionSerialize(tx, buf6, sizeof(buf6));
    
    BRTransactionFree(tx);
    tx = BRTransactionParse(buf6, len6);
    if (! tx || ! BRTransactionIsSigned(tx))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionParse() test 3", __func__);
    if (! tx) return r;
    
    uint8_t buf7[BRTransactionSerialize(tx, NULL, 0)];
    size_t len7 = BRTransactionSerialize(tx, buf7, sizeof(buf7));
    
    if (len6 != len7 || memcmp(buf6, buf7, len6) != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSerialize() test 3", __func__);
    BRTransactionFree(tx);
    
    tx = BRTransactionNew();
    BRTransactionAddInput(tx, toUInt256("fff7f7881a8099afa6940d42d1e7f6362bec38171ea3edf433541db4e4ad969f"), 0, 625000000,
                          (uint8_t *)"\x21\x03\xc9\xf4\x83\x6b\x9a\x4f\x77\xfc\x0d\x81\xf7\xbc\xb0\x1b\x7f\x1b\x35\x91"
                          "\x68\x64\xb9\x47\x6c\x24\x1c\xe9\xfc\x19\x8b\xd2\x54\x32\xac", 35,
                          (uint8_t *)"\x48\x30\x45\x02\x21\x00\x8b\x9d\x1d\xc2\x6b\xa6\xa9\xcb\x62\x12\x7b\x02\x74\x2f"
                          "\xa9\xd7\x54\xcd\x3b\xeb\xf3\x37\xf7\xa5\x5d\x11\x4c\x8e\x5c\xdd\x30\xbe\x02\x20\x40\x52\x9b"
                          "\x19\x4b\xa3\xf9\x28\x1a\x99\xf2\xb1\xc0\xa1\x9c\x04\x89\xbc\x22\xed\xe9\x44\xcc\xf4\xec\xba"
                          "\xb4\xcc\x61\x8e\xf3\xed\x01", 73, (uint8_t *)"", 0, 0xffffffee);
    BRTransactionAddInput(tx, toUInt256("ef51e1b804cc89d182d279655c3aa89e815b1b309fe287d9b2b55d57b90ec68a"), 1, 600000000,
                          (uint8_t *)"\x00\x14\x1d\x0f\x17\x2a\x0e\xcb\x48\xae\xe1\xbe\x1f\x26\x87\xd2\x96\x3a\xe3\x3f"
                          "\x71\xa1", 22, NULL, 0, NULL, 0, 0xffffffff);
    BRTransactionAddOutput(tx, 0x06b22c20, (uint8_t *)"\x76\xa9\x14\x82\x80\xb3\x7d\xf3\x78\xdb\x99\xf6\x6f\x85\xc9"
                           "\x5a\x78\x3a\x76\xac\x7a\x6d\x59\x88\xac", 25);
    BRTransactionAddOutput(tx, 0x0d519390, (uint8_t *)"\x76\xa9\x14\x3b\xde\x42\xdb\xee\x7e\x4d\xbe\x6a\x21\xb2\xd5"
                           "\x0c\xe2\xf0\x16\x7f\xaa\x81\x59\x88\xac", 25);
    tx->lockTime = 0x00000011;
    BRKeySetSecret(k, &toUInt256("619c335025c7f4012e556c2a58b2506e30b8511b53ade95ea316fd8c3286feb9"), 1);
    BRTransactionSign(tx, 0, k, 1);
    
    uint8_t buf8[BRTransactionSerialize(tx, NULL, 0)];
    size_t len8 = BRTransactionSerialize(tx, buf8, sizeof(buf8));
    char buf9[] = "\x01\x00\x00\x00\x00\x01\x02\xff\xf7\xf7\x88\x1a\x80\x99\xaf\xa6\x94\x0d\x42\xd1\xe7\xf6\x36\x2b\xec"
    "\x38\x17\x1e\xa3\xed\xf4\x33\x54\x1d\xb4\xe4\xad\x96\x9f\x00\x00\x00\x00\x49\x48\x30\x45\x02\x21\x00\x8b\x9d\x1d"
    "\xc2\x6b\xa6\xa9\xcb\x62\x12\x7b\x02\x74\x2f\xa9\xd7\x54\xcd\x3b\xeb\xf3\x37\xf7\xa5\x5d\x11\x4c\x8e\x5c\xdd\x30"
    "\xbe\x02\x20\x40\x52\x9b\x19\x4b\xa3\xf9\x28\x1a\x99\xf2\xb1\xc0\xa1\x9c\x04\x89\xbc\x22\xed\xe9\x44\xcc\xf4\xec"
    "\xba\xb4\xcc\x61\x8e\xf3\xed\x01\xee\xff\xff\xff\xef\x51\xe1\xb8\x04\xcc\x89\xd1\x82\xd2\x79\x65\x5c\x3a\xa8\x9e"
    "\x81\x5b\x1b\x30\x9f\xe2\x87\xd9\xb2\xb5\x5d\x57\xb9\x0e\xc6\x8a\x01\x00\x00\x00\x00\xff\xff\xff\xff\x02\x20\x2c"
    "\xb2\x06\x00\x00\x00\x00\x19\x76\xa9\x14\x82\x80\xb3\x7d\xf3\x78\xdb\x99\xf6\x6f\x85\xc9\x5a\x78\x3a\x76\xac\x7a"
    "\x6d\x59\x88\xac\x90\x93\x51\x0d\x00\x00\x00\x00\x19\x76\xa9\x14\x3b\xde\x42\xdb\xee\x7e\x4d\xbe\x6a\x21\xb2\xd5"
    "\x0c\xe2\xf0\x16\x7f\xaa\x81\x59\x88\xac\x00\x02\x47\x30\x44\x02\x20\x36\x09\xe1\x7b\x84\xf6\xa7\xd3\x0c\x80\xbf"
    "\xa6\x10\xb5\xb4\x54\x2f\x32\xa8\xa0\xd5\x44\x7a\x12\xfb\x13\x66\xd7\xf0\x1c\xc4\x4a\x02\x20\x57\x3a\x95\x4c\x45"
    "\x18\x33\x15\x61\x40\x6f\x90\x30\x0e\x8f\x33\x58\xf5\x19\x28\xd4\x3c\x21\x2a\x8c\xae\xd0\x2d\xe6\x7e\xeb\xee\x01"
    "\x21\x02\x54\x76\xc2\xe8\x31\x88\x36\x8d\xa1\xff\x3e\x29\x2e\x7a\xca\xfc\xdb\x35\x66\xbb\x0a\xd2\x53\xf6\x2f\xc7"
    "\x0f\x07\xae\xee\x63\x57\x11\x00\x00\x00";
    
    BRTransactionFree(tx);
    
    if (len8 != sizeof(buf9) - 1 || memcmp(buf8, buf9, len8))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSign() test 4", __func__);

    char buf0[] = "\x01\x00\x00\x00\x00\x01\x01\x7b\x03\x2f\x6a\x65\x1c\x7d\xcb\xcf\xb7\x8d\x81\x7b\x30\x3b\xe8\xd2\x0a"
    "\xfa\x22\x90\x16\x18\xb5\x17\xf2\x17\x55\xa7\xcd\x8d\x48\x01\x00\x00\x00\x23\x22\x00\x20\xe0\x62\x7b\x64\x74\x59"
    "\x05\x64\x6f\x27\x6f\x35\x55\x02\xa4\x05\x30\x58\xb6\x4e\xdb\xf2\x77\x11\x92\x49\x61\x1c\x98\xda\x41\x69\xff\xff"
    "\xff\xff\x02\x0c\xf9\x62\x01\x00\x00\x00\x00\x17\xa9\x14\x24\x31\x57\xd5\x78\xbd\x92\x8a\x92\xe0\x39\xe8\xd4\xdb"
    "\xbb\x29\x44\x16\x93\x5c\x87\xf3\xbe\x2a\x00\x00\x00\x00\x00\x19\x76\xa9\x14\x48\x38\x0b\xc7\x60\x5e\x91\xa3\x8f"
    "\x8d\x7b\xa0\x1a\x27\x95\x41\x6b\xf9\x2d\xde\x88\xac\x04\x00\x47\x30\x44\x02\x20\x5f\x5d\xe6\x88\x96\xca\x3e\xdf"
    "\x97\xe3\xea\x1f\xd3\x51\x39\x03\x53\x7f\xd5\xf2\xe0\xb3\x66\x1d\x6c\x61\x7b\x1c\x48\xfc\x69\xe1\x02\x20\x0e\x0f"
    "\x20\x59\x51\x3b\xe9\x31\x83\x92\x9c\x7d\x3e\x2d\xe0\xe9\xc7\x08\x57\x06\xa8\x8e\x8f\x74\x6e\x8f\x5a\xa7\x13\xd2"
    "\x7a\x52\x01\x47\x30\x44\x02\x20\x50\xd8\xec\xb9\xcd\x7f\xda\xcb\x6d\x63\x51\xde\xc2\xbc\x5b\x37\x16\x32\x8e\xf2"
    "\xc4\x46\x6d\xb4\x4b\xdd\x34\xa6\x57\x29\x2b\x8c\x02\x20\x68\x50\x1b\xf8\x18\x12\xad\x8e\x3e\xd9\xdf\x24\x35\x4c"
    "\x37\x19\x23\xa0\x7d\xc9\x66\xa6\xe4\x14\x63\x59\x47\x74\xd0\x09\x16\x9e\x01\x69\x52\x21\x03\xb8\xe1\x38\xed\x70"
    "\x23\x2c\x9c\xbd\x1b\x90\x28\x12\x10\x64\x23\x6a\xf1\x2d\xbe\x98\x64\x1c\x3f\x74\xfa\x13\x16\x6f\x27\x2f\x58\x21"
    "\x03\xf6\x6e\xe7\xc8\x78\x17\xd3\x24\x92\x1e\xdc\x3f\x7d\x77\x26\xde\x5a\x18\xcf\xed\x05\x7e\x5a\x50\xe7\xc7\x4e"
    "\x2a\xe7\xe0\x5a\xd7\x21\x02\xa7\xbf\x21\x58\x2d\x71\xe5\xda\x5c\x3b\xc4\x3e\x84\xc8\x8f\xdf\x32\x80\x3a\xa4\x72"
    "\x0e\x1c\x1a\x9d\x08\xaa\xb5\x41\xa4\xf3\x31\x53\xae\x00\x00\x00\x00";
    
    tx = BRTransactionParse((uint8_t *)buf0, sizeof(buf0) - 1);
    
    uint8_t buf1[BRTransactionSerialize(tx, NULL, 0)];
    size_t len0 = BRTransactionSerialize(tx, buf1, sizeof(buf1));

    BRTransactionFree(tx);
    
    if (len0 != sizeof(buf0) - 1 || memcmp(buf0, buf1, len0) != 0)
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionSerialize() test 4", __func__);
    
    BRTransaction *src = BRTransactionNew();
    BRTransactionAddInput(src, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddInput(src, inHash, 0, 1, script, scriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(src, 1000000, script, scriptLen);
    BRTransactionAddOutput(src, 1000000, script, scriptLen);
    BRTransactionAddOutput(src, 1000000, script, scriptLen);

    BRTransaction *tgt = BRTransactionCopy(src);
    if (! BRTransactionEqual(tgt, src))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionCopy() test 1", __func__);

    tgt->blockHeight++;
    if (BRTransactionEqual(tgt, src)) // fail if equal
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionCopy() test 2", __func__);
    BRTransactionFree(tgt);
    BRTransactionFree(src);

    src = BRTransactionParse(buf4, len4);
    tgt = BRTransactionCopy(src);
    if (! BRTransactionEqual(tgt, src))
        r = 0, fprintf(stderr, "\n***FAILED*** %s: BRTransactionCopy() test 3", __func__);
    BRTransactionFree(tgt);
    BRTransactionFree(src);
    
    if (! r) fprintf(stderr, "\n                                    ");
    return r;
}

static void walletBalanceChanged(void *info, uint64_t balance)
{
    printf("balance changed %"PRIu64"\n", balance);
}

static void walletTxAdded(void *info, BRTransaction *tx)
{
    printf("tx added: %s\n", u256hex(tx->txHash));
}

static void walletTxUpdated(void *info, const UInt256 txHashes[], size_t txCount, uint32_t blockHeight,
                            uint32_t timestamp)
{
    for (size_t i = 0; i < txCount; i++) printf("tx updated: %s\n", u256hex(txHashes[i]));
}

static void walletTxDeleted(void *info, UInt256 txHash, int notifyUser, int recommendRescan)
{
    printf("tx deleted: %s\n", u256hex(txHash));
}

// TODO: test standard free transaction no change
// TODO: test free transaction who's inputs are too new to hit min free priority
// TODO: test transaction with change below min allowable output
// TODO: test gap limit with gaps in address chain less than the limit
// TODO: test removing a transaction that other transansactions depend on
// TODO: test tx ordering for multiple tx with same block height
// TODO: port all applicable tests from bitcoinj and bitcoincore

int BRWalletTests()
{
    int r = 1;
    const char *phrase = "a random seed";
    UInt512 seed;

    BRBIP39DeriveKey(&seed, phrase, NULL);

    BRMasterPubKey mpk = BRBIP32MasterPubKey(&seed, sizeof(seed));
    BRWallet *w = BRWalletNew(NULL, 0, mpk, 0);
    UInt256 secret = toUInt256("0000000000000000000000000000000000000000000000000000000000000001"),
            inHash = toUInt256("0000000000000000000000000000000000000000000000000000000000000001");
    BRKey k;
    BRAddress addr, recvAddr = BRWalletReceiveAddress(w);
    BRTransaction *tx;
    
    printf("\n");
    
    BRWalletSetCallbacks(w, w, walletBalanceChanged, walletTxAdded, walletTxUpdated, walletTxDeleted);
    BRKeySetSecret(&k, &secret, 1);
    BRKeyAddress(&k, addr.s, sizeof(addr));
    
    tx = BRWalletCreateTransaction(w, 1, addr.s);
    if (tx) r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletCreateTransaction() test 0\n", __func__);
    
    tx = BRWalletCreateTransaction(w, SATOSHIS, addr.s);
    if (tx) r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletCreateTransaction() test 1\n", __func__);
    
    uint8_t inScript[BRAddressScriptPubKey(NULL, 0, addr.s)];
    size_t inScriptLen = BRAddressScriptPubKey(inScript, sizeof(inScript), addr.s);
    uint8_t outScript[BRAddressScriptPubKey(NULL, 0, recvAddr.s)];
    size_t outScriptLen = BRAddressScriptPubKey(outScript, sizeof(outScript), recvAddr.s);
    
    tx = BRTransactionNew();
    BRTransactionAddInput(tx, inHash, 0, 1, inScript, inScriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(tx, SATOSHIS, outScript, outScriptLen);
//    BRWalletRegisterTransaction(w, tx); // test adding unsigned tx
//    if (BRWalletBalance(w) != 0)
//        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletRegisterTransaction() test 1\n", __func__);

    if (BRWalletTransactions(w, NULL, 0) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactions() test 1\n", __func__);

    BRTransactionSign(tx, 0, &k, 1);
    BRWalletRegisterTransaction(w, tx);
    if (BRWalletBalance(w) != SATOSHIS)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletRegisterTransaction() test 2\n", __func__);

    if (BRWalletTransactions(w, NULL, 0) != 1)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactions() test 2\n", __func__);

    BRWalletRegisterTransaction(w, tx); // test adding same tx twice
    if (BRWalletBalance(w) != SATOSHIS)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletRegisterTransaction() test 3\n", __func__);

    tx = BRTransactionNew();
    BRTransactionAddInput(tx, inHash, 1, 1, inScript, inScriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE - 1);
    BRTransactionAddOutput(tx, SATOSHIS, outScript, outScriptLen);
    tx->lockTime = 1000;
    BRTransactionSign(tx, 0, &k, 1);

    if (! BRWalletTransactionIsPending(w, tx))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactionIsPending() test\n", __func__);

    BRWalletRegisterTransaction(w, tx); // test adding tx with future lockTime
    if (BRWalletBalance(w) != SATOSHIS)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletRegisterTransaction() test 4\n", __func__);

    BRWalletUpdateTransactions(w, &tx->txHash, 1, 1000, 1);
    if (BRWalletBalance(w) != SATOSHIS*2)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletUpdateTransactions() test\n", __func__);

    BRWalletFree(w);
    tx = BRTransactionNew();
    BRTransactionAddInput(tx, inHash, 0, 1, inScript, inScriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(tx, SATOSHIS, outScript, outScriptLen);
    BRTransactionSign(tx, 0, &k, 1);
    tx->timestamp = 1;
    w = BRWalletNew(&tx, 1, mpk, 0);
    if (BRWalletBalance(w) != SATOSHIS)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletNew() test\n", __func__);

    if (BRWalletAllAddrs(w, NULL, 0) != SEQUENCE_GAP_LIMIT_EXTERNAL + SEQUENCE_GAP_LIMIT_INTERNAL + 1)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletAllAddrs() test\n", __func__);
    
    UInt256 hash = tx->txHash;

    tx = BRWalletCreateTransaction(w, SATOSHIS*2, addr.s);
    if (tx) r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletCreateTransaction() test 3\n", __func__);

    if (BRWalletFeeForTxAmount(w, SATOSHIS/2) < 1000)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletFeeForTxAmount() test 1\n", __func__);
    
    tx = BRWalletCreateTransaction(w, SATOSHIS/2, addr.s);
    if (! tx) r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletCreateTransaction() test 4\n", __func__);

    if (tx) BRWalletSignTransaction(w, tx, &seed, sizeof(seed));
    if (tx && ! BRTransactionIsSigned(tx))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletSignTransaction() test\n", __func__);
    
    if (tx) tx->timestamp = 1, BRWalletRegisterTransaction(w, tx);
    if (tx && BRWalletBalance(w) + BRWalletFeeForTx(w, tx) != SATOSHIS/2)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletRegisterTransaction() test 5\n", __func__);
    
    if (BRWalletTransactions(w, NULL, 0) != 2)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactions() test 3\n", __func__);
    
    if (tx && BRWalletTransactionForHash(w, tx->txHash) != tx)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactionForHash() test\n", __func__);

    if (tx && ! BRWalletTransactionIsValid(w, tx))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactionIsValid() test\n", __func__);

    if (tx && ! BRWalletTransactionIsVerified(w, tx))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactionIsVerified() test\n", __func__);

    if (tx && BRWalletTransactionIsPending(w, tx))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletTransactionIsPending() test 2\n", __func__);
    
    BRWalletRemoveTransaction(w, hash); // removing first tx should recursively remove second, leaving none
    if (BRWalletTransactions(w, NULL, 0) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletRemoveTransaction() test\n", __func__);

    if (! BRAddressEq(BRWalletReceiveAddress(w).s, recvAddr.s)) // verify used addresses are correctly tracked
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletReceiveAddress() test\n", __func__);
    
    if (BRWalletFeeForTxAmount(w, SATOSHIS) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletFeeForTxAmount() test 2\n", __func__);
    
    printf("                                    ");
    BRWalletFree(w);

    int64_t amt;
    
    tx = BRTransactionNew();
    BRTransactionAddInput(tx, inHash, 0, 1, inScript, inScriptLen, NULL, 0, NULL, 0, TXIN_SEQUENCE);
    BRTransactionAddOutput(tx, 740000, outScript, outScriptLen);
    BRTransactionSign(tx, 0, &k, 1);
    w = BRWalletNew(&tx, 1, mpk, 0);
    BRWalletSetCallbacks(w, w, walletBalanceChanged, walletTxAdded, walletTxUpdated, walletTxDeleted);
    BRWalletSetFeePerKb(w, 65000);
    amt = BRWalletMaxOutputAmount(w);
    tx = BRWalletCreateTransaction(w, amt, addr.s);
    
    if (BRWalletAmountSentByTx(w, tx) - BRWalletFeeForTx(w, tx) != amt || BRWalletAmountReceivedFromTx(w, tx) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRWalletMaxOutputAmount() test 1\n", __func__);

    BRTransactionFree(tx);
    BRWalletFree(w);
    
    amt = BRBitcoinAmount(50000, 50000);
    if (amt != SATOSHIS) r = 0, fprintf(stderr, "***FAILED*** %s: BRBitcoinAmount() test 1\n", __func__);

    amt = BRBitcoinAmount(-50000, 50000);
    if (amt != -SATOSHIS) r = 0, fprintf(stderr, "***FAILED*** %s: BRBitcoinAmount() test 2\n", __func__);
    
    amt = BRLocalAmount(SATOSHIS, 50000);
    if (amt != 50000) r = 0, fprintf(stderr, "***FAILED*** %s: BRLocalAmount() test 1\n", __func__);

    amt = BRLocalAmount(-SATOSHIS, 50000);
    if (amt != -50000) r = 0, fprintf(stderr, "***FAILED*** %s: BRLocalAmount() test 2\n", __func__);
    
    return r;
}

int BRBloomFilterTests()
{
    int r = 1;
    BRBloomFilter *f = BRBloomFilterNew(0.01, 3, 0, BLOOM_UPDATE_ALL);
    char data1[] = "\x99\x10\x8a\xd8\xed\x9b\xb6\x27\x4d\x39\x80\xba\xb5\xa8\x5c\x04\x8f\x09\x50\xc8";

    BRBloomFilterInsertData(f, (uint8_t *)data1, sizeof(data1) - 1);
    if (! BRBloomFilterContainsData(f, (uint8_t *)data1, sizeof(data1) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 1\n", __func__);

    // one bit difference
    char data2[] = "\x19\x10\x8a\xd8\xed\x9b\xb6\x27\x4d\x39\x80\xba\xb5\xa8\x5c\x04\x8f\x09\x50\xc8";
    
    if (BRBloomFilterContainsData(f, (uint8_t *)data2, sizeof(data2) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 2\n", __func__);
    
    char data3[] = "\xb5\xa2\xc7\x86\xd9\xef\x46\x58\x28\x7c\xed\x59\x14\xb3\x7a\x1b\x4a\xa3\x2e\xee";

    BRBloomFilterInsertData(f, (uint8_t *)data3, sizeof(data3) - 1);
    if (! BRBloomFilterContainsData(f, (uint8_t *)data3, sizeof(data3) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 3\n", __func__);

    char data4[] = "\xb9\x30\x06\x70\xb4\xc5\x36\x6e\x95\xb2\x69\x9e\x8b\x18\xbc\x75\xe5\xf7\x29\xc5";
    
    BRBloomFilterInsertData(f, (uint8_t *)data4, sizeof(data4) - 1);
    if (! BRBloomFilterContainsData(f, (uint8_t *)data4, sizeof(data4) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 4\n", __func__);

    // check against satoshi client output
    uint8_t buf1[BRBloomFilterSerialize(f, NULL, 0)];
    size_t len1 = BRBloomFilterSerialize(f, buf1, sizeof(buf1));
    char d1[] = "\x03\x61\x4e\x9b\x05\x00\x00\x00\x00\x00\x00\x00\x01";
    
    if (len1 != sizeof(d1) - 1 || memcmp(buf1, d1, len1) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterSerialize() test 1\n", __func__);
    
    BRBloomFilterFree(f);
    f = BRBloomFilterNew(0.01, 3, 2147483649, BLOOM_UPDATE_P2PUBKEY_ONLY);

    char data5[] = "\x99\x10\x8a\xd8\xed\x9b\xb6\x27\x4d\x39\x80\xba\xb5\xa8\x5c\x04\x8f\x09\x50\xc8";
    
    BRBloomFilterInsertData(f, (uint8_t *)data5, sizeof(data5) - 1);
    if (! BRBloomFilterContainsData(f, (uint8_t *)data5, sizeof(data5) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 5\n", __func__);

    // one bit difference
    char data6[] = "\x19\x10\x8a\xd8\xed\x9b\xb6\x27\x4d\x39\x80\xba\xb5\xa8\x5c\x04\x8f\x09\x50\xc8";
    
    if (BRBloomFilterContainsData(f, (uint8_t *)data6, sizeof(data6) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 6\n", __func__);

    char data7[] = "\xb5\xa2\xc7\x86\xd9\xef\x46\x58\x28\x7c\xed\x59\x14\xb3\x7a\x1b\x4a\xa3\x2e\xee";
    
    BRBloomFilterInsertData(f, (uint8_t *)data7, sizeof(data7) - 1);
    if (! BRBloomFilterContainsData(f, (uint8_t *)data7, sizeof(data7) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 7\n", __func__);

    char data8[] = "\xb9\x30\x06\x70\xb4\xc5\x36\x6e\x95\xb2\x69\x9e\x8b\x18\xbc\x75\xe5\xf7\x29\xc5";
    
    BRBloomFilterInsertData(f, (uint8_t *)data8, sizeof(data8) - 1);
    if (! BRBloomFilterContainsData(f, (uint8_t *)data8, sizeof(data8) - 1))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterContainsData() test 8\n", __func__);

    // check against satoshi client output
    uint8_t buf2[BRBloomFilterSerialize(f, NULL, 0)];
    size_t len2 = BRBloomFilterSerialize(f, buf2, sizeof(buf2));
    char d2[] = "\x03\xce\x42\x99\x05\x00\x00\x00\x01\x00\x00\x80\x02";
    
    if (len2 != sizeof(d2) - 1 || memcmp(buf2, d2, len2) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRBloomFilterSerialize() test 2\n", __func__);
    
    BRBloomFilterFree(f);
    return r;
}

// true if block and otherBlock have equal data (in their respective structures).
static int BRMerkleBlockEqual (const BRMerkleBlock *block1, const BRMerkleBlock *block2) {
    return 0 == memcmp(&block1->blockHash, &block2->blockHash, sizeof(UInt256))
           && block1->version == block2->version
           && 0 == memcmp(&block1->prevBlock, &block2->prevBlock, sizeof(UInt256))
           && 0 == memcmp(&block1->merkleRoot, &block2->merkleRoot, sizeof(UInt256))
           && block1->timestamp == block2->timestamp
           && block1->target == block2->target
           && block1->nonce == block2->nonce
           && block1->totalTx == block2->totalTx
           && block1->hashesCount == block2->hashesCount
           && 0 == memcmp(block1->hashes, block2->hashes, block1->hashesCount * sizeof(UInt256))
           && block1->flagsLen == block2->flagsLen
           && 0 == memcmp(block1->flags, block2->flags, block1->flagsLen * sizeof(uint8_t))
           && block1->height == block2->height;
}

int BRMerkleBlockTests()
{
    int r = 1;
    char block[] = // block 10001 filtered to include only transactions 0, 1, 2, and 6
    "\x01\x00\x00\x00\x06\xe5\x33\xfd\x1a\xda\x86\x39\x1f\x3f\x6c\x34\x32\x04\xb0\xd2\x78\xd4\xaa\xec\x1c"
    "\x0b\x20\xaa\x27\xba\x03\x00\x00\x00\x00\x00\x6a\xbb\xb3\xeb\x3d\x73\x3a\x9f\xe1\x89\x67\xfd\x7d\x4c\x11\x7e\x4c"
    "\xcb\xba\xc5\xbe\xc4\xd9\x10\xd9\x00\xb3\xae\x07\x93\xe7\x7f\x54\x24\x1b\x4d\x4c\x86\x04\x1b\x40\x89\xcc\x9b\x0c"
    "\x00\x00\x00\x08\x4c\x30\xb6\x3c\xfc\xdc\x2d\x35\xe3\x32\x94\x21\xb9\x80\x5e\xf0\xc6\x56\x5d\x35\x38\x1c\xa8\x57"
    "\x76\x2e\xa0\xb3\xa5\xa1\x28\xbb\xca\x50\x65\xff\x96\x17\xcb\xcb\xa4\x5e\xb2\x37\x26\xdf\x64\x98\xa9\xb9\xca\xfe"
    "\xd4\xf5\x4c\xba\xb9\xd2\x27\xb0\x03\x5d\xde\xfb\xbb\x15\xac\x1d\x57\xd0\x18\x2a\xae\xe6\x1c\x74\x74\x3a\x9c\x4f"
    "\x78\x58\x95\xe5\x63\x90\x9b\xaf\xec\x45\xc9\xa2\xb0\xff\x31\x81\xd7\x77\x06\xbe\x8b\x1d\xcc\x91\x11\x2e\xad\xa8"
    "\x6d\x42\x4e\x2d\x0a\x89\x07\xc3\x48\x8b\x6e\x44\xfd\xa5\xa7\x4a\x25\xcb\xc7\xd6\xbb\x4f\xa0\x42\x45\xf4\xac\x8a"
    "\x1a\x57\x1d\x55\x37\xea\xc2\x4a\xdc\xa1\x45\x4d\x65\xed\xa4\x46\x05\x54\x79\xaf\x6c\x6d\x4d\xd3\xc9\xab\x65\x84"
    "\x48\xc1\x0b\x69\x21\xb7\xa4\xce\x30\x21\xeb\x22\xed\x6b\xb6\xa7\xfd\xe1\xe5\xbc\xc4\xb1\xdb\x66\x15\xc6\xab\xc5"
    "\xca\x04\x21\x27\xbf\xaf\x9f\x44\xeb\xce\x29\xcb\x29\xc6\xdf\x9d\x05\xb4\x7f\x35\xb2\xed\xff\x4f\x00\x64\xb5\x78"
    "\xab\x74\x1f\xa7\x82\x76\x22\x26\x51\x20\x9f\xe1\xa2\xc4\xc0\xfa\x1c\x58\x51\x0a\xec\x8b\x09\x0d\xd1\xeb\x1f\x82"
    "\xf9\xd2\x61\xb8\x27\x3b\x52\x5b\x02\xff\x1a";
    uint8_t block2[sizeof(block) - 1];
    BRMerkleBlock *b;
    
    b = BRMerkleBlockParse((uint8_t *)block, sizeof(block) - 1);
    
    if (! UInt256Eq(b->blockHash,
                    UInt256Reverse(toUInt256("00000000000080b66c911bd5ba14a74260057311eaeb1982802f7010f1a9f090"))))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockParse() test\n", __func__);

    if (! BRMerkleBlockIsValid(b, (uint32_t)time(NULL)))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockParse() test\n", __func__);
    
    if (BRMerkleBlockSerialize(b, block2, sizeof(block2)) != sizeof(block2) ||
        memcmp(block, block2, sizeof(block2)) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockSerialize() test\n", __func__);
    
    if (! BRMerkleBlockContainsTxHash(b, toUInt256("4c30b63cfcdc2d35e3329421b9805ef0c6565d35381ca857762ea0b3a5a128bb")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockContainsTxHash() test\n", __func__);
    
    if (BRMerkleBlockTxHashes(b, NULL, 0) != 4)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockTxHashes() test 0\n", __func__);
    
    UInt256 txHashes[BRMerkleBlockTxHashes(b, NULL, 0)];
    
    BRMerkleBlockTxHashes(b, txHashes, 4);
    
    if (! UInt256Eq(txHashes[0], toUInt256("4c30b63cfcdc2d35e3329421b9805ef0c6565d35381ca857762ea0b3a5a128bb")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockTxHashes() test 1\n", __func__);
    
    if (! UInt256Eq(txHashes[1], toUInt256("ca5065ff9617cbcba45eb23726df6498a9b9cafed4f54cbab9d227b0035ddefb")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockTxHashes() test 2\n", __func__);
    
    if (! UInt256Eq(txHashes[2], toUInt256("bb15ac1d57d0182aaee61c74743a9c4f785895e563909bafec45c9a2b0ff3181")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockTxHashes() test 3\n", __func__);
    
    if (! UInt256Eq(txHashes[3], toUInt256("c9ab658448c10b6921b7a4ce3021eb22ed6bb6a7fde1e5bcc4b1db6615c6abc5")))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockTxHashes() test 4\n", __func__);
    
    // TODO: test a block with an odd number of tree rows both at the tx level and merkle node level

    // TODO: XXX test BRMerkleBlockVerifyDifficulty()
    
    // TODO: test (CVE-2012-2459) vulnerability

    BRMerkleBlock *c = BRMerkleBlockCopy(b);

    if (!BRMerkleBlockEqual(b, c))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockEqual() test 1\n", __func__);

    c->height++;
    if (BRMerkleBlockEqual(b, c)) // fail if equal
        r = 0, fprintf(stderr, "***FAILED*** %s: BRMerkleBlockEqual() test 2\n", __func__);

    if (c) BRMerkleBlockFree(c);


    if (b) BRMerkleBlockFree(b);
    return r;
}

int BRPaymentProtocolTests()
{
    int r = 1;
    const char buf1[] = "\x08\x01\x12\x0b\x78\x35\x30\x39\x2b\x73\x68\x61\x32\x35\x36\x1a\xb8\x1d\x0a\xc9\x0b\x30\x82"
    "\x05\xc5\x30\x82\x04\xad\xa0\x03\x02\x01\x02\x02\x07\x2b\x85\x8c\x53\xee\xed\x2f\x30\x0d\x06\x09\x2a\x86\x48\x86"
    "\xf7\x0d\x01\x01\x05\x05\x00\x30\x81\xca\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x10\x30\x0e\x06"
    "\x03\x55\x04\x08\x13\x07\x41\x72\x69\x7a\x6f\x6e\x61\x31\x13\x30\x11\x06\x03\x55\x04\x07\x13\x0a\x53\x63\x6f\x74"
    "\x74\x73\x64\x61\x6c\x65\x31\x1a\x30\x18\x06\x03\x55\x04\x0a\x13\x11\x47\x6f\x44\x61\x64\x64\x79\x2e\x63\x6f\x6d"
    "\x2c\x20\x49\x6e\x63\x2e\x31\x33\x30\x31\x06\x03\x55\x04\x0b\x13\x2a\x68\x74\x74\x70\x3a\x2f\x2f\x63\x65\x72\x74"
    "\x69\x66\x69\x63\x61\x74\x65\x73\x2e\x67\x6f\x64\x61\x64\x64\x79\x2e\x63\x6f\x6d\x2f\x72\x65\x70\x6f\x73\x69\x74"
    "\x6f\x72\x79\x31\x30\x30\x2e\x06\x03\x55\x04\x03\x13\x27\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x53\x65\x63\x75\x72"
    "\x65\x20\x43\x65\x72\x74\x69\x66\x69\x63\x61\x74\x69\x6f\x6e\x20\x41\x75\x74\x68\x6f\x72\x69\x74\x79\x31\x11\x30"
    "\x0f\x06\x03\x55\x04\x05\x13\x08\x30\x37\x39\x36\x39\x32\x38\x37\x30\x1e\x17\x0d\x31\x33\x30\x34\x32\x35\x31\x39"
    "\x31\x31\x30\x30\x5a\x17\x0d\x31\x35\x30\x34\x32\x35\x31\x39\x31\x31\x30\x30\x5a\x30\x81\xbe\x31\x13\x30\x11\x06"
    "\x0b\x2b\x06\x01\x04\x01\x82\x37\x3c\x02\x01\x03\x13\x02\x55\x53\x31\x19\x30\x17\x06\x0b\x2b\x06\x01\x04\x01\x82"
    "\x37\x3c\x02\x01\x02\x13\x08\x44\x65\x6c\x61\x77\x61\x72\x65\x31\x1d\x30\x1b\x06\x03\x55\x04\x0f\x13\x14\x50\x72"
    "\x69\x76\x61\x74\x65\x20\x4f\x72\x67\x61\x6e\x69\x7a\x61\x74\x69\x6f\x6e\x31\x10\x30\x0e\x06\x03\x55\x04\x05\x13"
    "\x07\x35\x31\x36\x33\x39\x36\x36\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x10\x30\x0e\x06\x03\x55"
    "\x04\x08\x13\x07\x47\x65\x6f\x72\x67\x69\x61\x31\x10\x30\x0e\x06\x03\x55\x04\x07\x13\x07\x41\x74\x6c\x61\x6e\x74"
    "\x61\x31\x15\x30\x13\x06\x03\x55\x04\x0a\x13\x0c\x42\x69\x74\x50\x61\x79\x2c\x20\x49\x6e\x63\x2e\x31\x13\x30\x11"
    "\x06\x03\x55\x04\x03\x13\x0a\x62\x69\x74\x70\x61\x79\x2e\x63\x6f\x6d\x30\x82\x01\x22\x30\x0d\x06\x09\x2a\x86\x48"
    "\x86\xf7\x0d\x01\x01\x01\x05\x00\x03\x82\x01\x0f\x00\x30\x82\x01\x0a\x02\x82\x01\x01\x00\xc4\x6e\xef\xc2\x8b\x15"
    "\x7d\x03\x71\x7f\x0c\x00\xa1\xd6\x7b\xa7\x61\x2c\x1f\x2b\x56\x21\x82\xce\x99\x60\x2c\x47\x68\xff\x8f\xbd\x10\x66"
    "\x85\xd9\x39\x26\x32\x66\xbb\x9e\x10\x7d\x05\x7d\xb8\x44\x50\x2d\x8e\xc6\x1e\x88\x7e\xa5\x5b\x55\xc2\xc1\x71\x21"
    "\x89\x64\x54\xa3\x19\xf6\x5b\x3d\xb3\x4c\x86\x29\xa7\x5b\x3e\x12\x3f\xe2\x07\x6d\x85\xcf\x4f\x64\x4a\xe3\xf6\xfb"
    "\x84\x29\xc5\xa7\x83\x0d\xf4\x65\x85\x9c\x4d\x6c\x0b\xcd\xbc\x12\x86\x5f\xab\x22\x18\xbd\x65\xf2\xb2\x53\x00\x12"
    "\xce\x49\x96\x98\xcc\xae\x02\x59\xac\x0b\x34\x70\xa8\x56\x6b\x70\x5e\x1a\x66\x1a\xd8\x28\x64\x29\xac\xf0\xb3\x13"
    "\x6e\x4c\xdf\x4d\x91\x19\x08\x4a\x5b\x6e\xcf\x19\x76\x94\xc2\xb5\x57\x82\x70\x12\x11\xca\x28\xda\xfa\x6d\x96\xac"
    "\xec\xc2\x23\x2a\xc5\xe9\xa8\x61\x81\xd4\xf7\x41\x7f\xd8\xd9\x38\x50\x7f\x6d\x0c\x62\x52\x94\x02\x16\x30\x09\x46"
    "\xf7\x62\x70\x13\xd7\x49\x98\xe0\x92\x2d\x4b\x9c\x97\xa7\x77\x9b\x1d\x56\xf3\x0c\x07\xd0\x26\x9b\x15\x89\xbd\x60"
    "\x4d\x38\x4a\x52\x37\x21\x3c\x75\xd0\xc6\xbf\x81\x1b\xce\x8c\xdb\xbb\x06\xc1\xa2\xc6\xe4\x79\xd2\x71\xfd\x02\x03"
    "\x01\x00\x01\xa3\x82\x01\xb8\x30\x82\x01\xb4\x30\x0f\x06\x03\x55\x1d\x13\x01\x01\xff\x04\x05\x30\x03\x01\x01\x00"
    "\x30\x1d\x06\x03\x55\x1d\x25\x04\x16\x30\x14\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x01\x06\x08\x2b\x06\x01\x05\x05"
    "\x07\x03\x02\x30\x0e\x06\x03\x55\x1d\x0f\x01\x01\xff\x04\x04\x03\x02\x05\xa0\x30\x33\x06\x03\x55\x1d\x1f\x04\x2c"
    "\x30\x2a\x30\x28\xa0\x26\xa0\x24\x86\x22\x68\x74\x74\x70\x3a\x2f\x2f\x63\x72\x6c\x2e\x67\x6f\x64\x61\x64\x64\x79"
    "\x2e\x63\x6f\x6d\x2f\x67\x64\x73\x33\x2d\x37\x32\x2e\x63\x72\x6c\x30\x53\x06\x03\x55\x1d\x20\x04\x4c\x30\x4a\x30"
    "\x48\x06\x0b\x60\x86\x48\x01\x86\xfd\x6d\x01\x07\x17\x03\x30\x39\x30\x37\x06\x08\x2b\x06\x01\x05\x05\x07\x02\x01"
    "\x16\x2b\x68\x74\x74\x70\x3a\x2f\x2f\x63\x65\x72\x74\x69\x66\x69\x63\x61\x74\x65\x73\x2e\x67\x6f\x64\x61\x64\x64"
    "\x79\x2e\x63\x6f\x6d\x2f\x72\x65\x70\x6f\x73\x69\x74\x6f\x72\x79\x2f\x30\x81\x80\x06\x08\x2b\x06\x01\x05\x05\x07"
    "\x01\x01\x04\x74\x30\x72\x30\x24\x06\x08\x2b\x06\x01\x05\x05\x07\x30\x01\x86\x18\x68\x74\x74\x70\x3a\x2f\x2f\x6f"
    "\x63\x73\x70\x2e\x67\x6f\x64\x61\x64\x64\x79\x2e\x63\x6f\x6d\x2f\x30\x4a\x06\x08\x2b\x06\x01\x05\x05\x07\x30\x02"
    "\x86\x3e\x68\x74\x74\x70\x3a\x2f\x2f\x63\x65\x72\x74\x69\x66\x69\x63\x61\x74\x65\x73\x2e\x67\x6f\x64\x61\x64\x64"
    "\x79\x2e\x63\x6f\x6d\x2f\x72\x65\x70\x6f\x73\x69\x74\x6f\x72\x79\x2f\x67\x64\x5f\x69\x6e\x74\x65\x72\x6d\x65\x64"
    "\x69\x61\x74\x65\x2e\x63\x72\x74\x30\x1f\x06\x03\x55\x1d\x23\x04\x18\x30\x16\x80\x14\xfd\xac\x61\x32\x93\x6c\x45"
    "\xd6\xe2\xee\x85\x5f\x9a\xba\xe7\x76\x99\x68\xcc\xe7\x30\x25\x06\x03\x55\x1d\x11\x04\x1e\x30\x1c\x82\x0a\x62\x69"
    "\x74\x70\x61\x79\x2e\x63\x6f\x6d\x82\x0e\x77\x77\x77\x2e\x62\x69\x74\x70\x61\x79\x2e\x63\x6f\x6d\x30\x1d\x06\x03"
    "\x55\x1d\x0e\x04\x16\x04\x14\xb9\x41\x17\x56\x7a\xe7\xc3\xef\x50\x72\x82\xac\xc4\xd5\x51\xc6\xbf\x7f\xa4\x4a\x30"
    "\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05\x05\x00\x03\x82\x01\x01\x00\xb8\xd5\xac\xa9\x63\xa6\xf9\xa0\xb5"
    "\xc5\xaf\x03\x4a\xcc\x83\x2a\x13\xf1\xbb\xeb\x93\x2d\x39\x7a\x7d\x4b\xd3\xa4\x5e\x6a\x3d\x6d\xb3\x10\x9a\x23\x54"
    "\xa8\x08\x14\xee\x3e\x6c\x7c\xef\xf5\xd7\xf4\xa9\x83\xdb\xde\x55\xf0\x96\xba\x99\x2d\x0f\xff\x4f\xe1\xa9\x2e\xaa"
    "\xb7\x9b\xd1\x47\xb3\x52\x1e\xe3\x61\x2c\xee\x2c\xf7\x59\x5b\xc6\x35\xa1\xfe\xef\xc6\xdb\x5c\x58\x3a\x59\x23\xc7"
    "\x1c\x86\x4d\xda\xcb\xcf\xf4\x63\xe9\x96\x7f\x4c\x02\xbd\xd7\x72\x71\x63\x55\x75\x96\x7e\xc2\x3e\x8b\x6c\xdb\xda"
    "\xb6\x32\xce\x79\x07\x2f\x47\x70\x4a\x6e\xf1\xf1\x60\x31\x08\x37\xde\x45\x6e\x4a\x01\xa2\x2b\xbf\x89\xd8\xe0\xf5"
    "\x26\x7d\xfb\x71\x99\x8a\xde\x3e\xa2\x60\xdc\x9b\xc6\xcf\xf3\x89\x9a\x88\xca\xf6\xa5\xe0\xea\x74\x97\xff\xbc\x42"
    "\xed\x4f\xa6\x95\x51\xe5\xe0\xb2\x15\x6e\x9e\x2d\x22\x5b\xa7\xa5\xe5\x6d\xe5\xff\x13\x0a\x4c\x6e\x5f\x1a\x99\x68"
    "\x68\x7b\x82\x62\x0f\x86\x17\x02\xd5\x6c\x44\x29\x79\x9f\xff\x9d\xb2\x56\x2b\xc2\xdc\xe9\x7f\xe7\xe3\x4a\x1f\xab"
    "\xb0\x39\xe5\xe7\x8b\xd4\xda\xe6\x0f\x58\x68\xa5\xe8\xa3\xf8\xc3\x30\xe3\x7f\x38\xfb\xfe\x1f\x0a\xe2\x09\x30\x82"
    "\x04\xde\x30\x82\x03\xc6\xa0\x03\x02\x01\x02\x02\x02\x03\x01\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05"
    "\x05\x00\x30\x63\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x21\x30\x1f\x06\x03\x55\x04\x0a\x13\x18"
    "\x54\x68\x65\x20\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x47\x72\x6f\x75\x70\x2c\x20\x49\x6e\x63\x2e\x31\x31\x30\x2f"
    "\x06\x03\x55\x04\x0b\x13\x28\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x43\x6c\x61\x73\x73\x20\x32\x20\x43\x65\x72\x74"
    "\x69\x66\x69\x63\x61\x74\x69\x6f\x6e\x20\x41\x75\x74\x68\x6f\x72\x69\x74\x79\x30\x1e\x17\x0d\x30\x36\x31\x31\x31"
    "\x36\x30\x31\x35\x34\x33\x37\x5a\x17\x0d\x32\x36\x31\x31\x31\x36\x30\x31\x35\x34\x33\x37\x5a\x30\x81\xca\x31\x0b"
    "\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x10\x30\x0e\x06\x03\x55\x04\x08\x13\x07\x41\x72\x69\x7a\x6f\x6e"
    "\x61\x31\x13\x30\x11\x06\x03\x55\x04\x07\x13\x0a\x53\x63\x6f\x74\x74\x73\x64\x61\x6c\x65\x31\x1a\x30\x18\x06\x03"
    "\x55\x04\x0a\x13\x11\x47\x6f\x44\x61\x64\x64\x79\x2e\x63\x6f\x6d\x2c\x20\x49\x6e\x63\x2e\x31\x33\x30\x31\x06\x03"
    "\x55\x04\x0b\x13\x2a\x68\x74\x74\x70\x3a\x2f\x2f\x63\x65\x72\x74\x69\x66\x69\x63\x61\x74\x65\x73\x2e\x67\x6f\x64"
    "\x61\x64\x64\x79\x2e\x63\x6f\x6d\x2f\x72\x65\x70\x6f\x73\x69\x74\x6f\x72\x79\x31\x30\x30\x2e\x06\x03\x55\x04\x03"
    "\x13\x27\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x53\x65\x63\x75\x72\x65\x20\x43\x65\x72\x74\x69\x66\x69\x63\x61\x74"
    "\x69\x6f\x6e\x20\x41\x75\x74\x68\x6f\x72\x69\x74\x79\x31\x11\x30\x0f\x06\x03\x55\x04\x05\x13\x08\x30\x37\x39\x36"
    "\x39\x32\x38\x37\x30\x82\x01\x22\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00\x03\x82\x01\x0f\x00"
    "\x30\x82\x01\x0a\x02\x82\x01\x01\x00\xc4\x2d\xd5\x15\x8c\x9c\x26\x4c\xec\x32\x35\xeb\x5f\xb8\x59\x01\x5a\xa6\x61"
    "\x81\x59\x3b\x70\x63\xab\xe3\xdc\x3d\xc7\x2a\xb8\xc9\x33\xd3\x79\xe4\x3a\xed\x3c\x30\x23\x84\x8e\xb3\x30\x14\xb6"
    "\xb2\x87\xc3\x3d\x95\x54\x04\x9e\xdf\x99\xdd\x0b\x25\x1e\x21\xde\x65\x29\x7e\x35\xa8\xa9\x54\xeb\xf6\xf7\x32\x39"
    "\xd4\x26\x55\x95\xad\xef\xfb\xfe\x58\x86\xd7\x9e\xf4\x00\x8d\x8c\x2a\x0c\xbd\x42\x04\xce\xa7\x3f\x04\xf6\xee\x80"
    "\xf2\xaa\xef\x52\xa1\x69\x66\xda\xbe\x1a\xad\x5d\xda\x2c\x66\xea\x1a\x6b\xbb\xe5\x1a\x51\x4a\x00\x2f\x48\xc7\x98"
    "\x75\xd8\xb9\x29\xc8\xee\xf8\x66\x6d\x0a\x9c\xb3\xf3\xfc\x78\x7c\xa2\xf8\xa3\xf2\xb5\xc3\xf3\xb9\x7a\x91\xc1\xa7"
    "\xe6\x25\x2e\x9c\xa8\xed\x12\x65\x6e\x6a\xf6\x12\x44\x53\x70\x30\x95\xc3\x9c\x2b\x58\x2b\x3d\x08\x74\x4a\xf2\xbe"
    "\x51\xb0\xbf\x87\xd0\x4c\x27\x58\x6b\xb5\x35\xc5\x9d\xaf\x17\x31\xf8\x0b\x8f\xee\xad\x81\x36\x05\x89\x08\x98\xcf"
    "\x3a\xaf\x25\x87\xc0\x49\xea\xa7\xfd\x67\xf7\x45\x8e\x97\xcc\x14\x39\xe2\x36\x85\xb5\x7e\x1a\x37\xfd\x16\xf6\x71"
    "\x11\x9a\x74\x30\x16\xfe\x13\x94\xa3\x3f\x84\x0d\x4f\x02\x03\x01\x00\x01\xa3\x82\x01\x32\x30\x82\x01\x2e\x30\x1d"
    "\x06\x03\x55\x1d\x0e\x04\x16\x04\x14\xfd\xac\x61\x32\x93\x6c\x45\xd6\xe2\xee\x85\x5f\x9a\xba\xe7\x76\x99\x68\xcc"
    "\xe7\x30\x1f\x06\x03\x55\x1d\x23\x04\x18\x30\x16\x80\x14\xd2\xc4\xb0\xd2\x91\xd4\x4c\x11\x71\xb3\x61\xcb\x3d\xa1"
    "\xfe\xdd\xa8\x6a\xd4\xe3\x30\x12\x06\x03\x55\x1d\x13\x01\x01\xff\x04\x08\x30\x06\x01\x01\xff\x02\x01\x00\x30\x33"
    "\x06\x08\x2b\x06\x01\x05\x05\x07\x01\x01\x04\x27\x30\x25\x30\x23\x06\x08\x2b\x06\x01\x05\x05\x07\x30\x01\x86\x17"
    "\x68\x74\x74\x70\x3a\x2f\x2f\x6f\x63\x73\x70\x2e\x67\x6f\x64\x61\x64\x64\x79\x2e\x63\x6f\x6d\x30\x46\x06\x03\x55"
    "\x1d\x1f\x04\x3f\x30\x3d\x30\x3b\xa0\x39\xa0\x37\x86\x35\x68\x74\x74\x70\x3a\x2f\x2f\x63\x65\x72\x74\x69\x66\x69"
    "\x63\x61\x74\x65\x73\x2e\x67\x6f\x64\x61\x64\x64\x79\x2e\x63\x6f\x6d\x2f\x72\x65\x70\x6f\x73\x69\x74\x6f\x72\x79"
    "\x2f\x67\x64\x72\x6f\x6f\x74\x2e\x63\x72\x6c\x30\x4b\x06\x03\x55\x1d\x20\x04\x44\x30\x42\x30\x40\x06\x04\x55\x1d"
    "\x20\x00\x30\x38\x30\x36\x06\x08\x2b\x06\x01\x05\x05\x07\x02\x01\x16\x2a\x68\x74\x74\x70\x3a\x2f\x2f\x63\x65\x72"
    "\x74\x69\x66\x69\x63\x61\x74\x65\x73\x2e\x67\x6f\x64\x61\x64\x64\x79\x2e\x63\x6f\x6d\x2f\x72\x65\x70\x6f\x73\x69"
    "\x74\x6f\x72\x79\x30\x0e\x06\x03\x55\x1d\x0f\x01\x01\xff\x04\x04\x03\x02\x01\x06\x30\x0d\x06\x09\x2a\x86\x48\x86"
    "\xf7\x0d\x01\x01\x05\x05\x00\x03\x82\x01\x01\x00\xd2\x86\xc0\xec\xbd\xf9\xa1\xb6\x67\xee\x66\x0b\xa2\x06\x3a\x04"
    "\x50\x8e\x15\x72\xac\x4a\x74\x95\x53\xcb\x37\xcb\x44\x49\xef\x07\x90\x6b\x33\xd9\x96\xf0\x94\x56\xa5\x13\x30\x05"
    "\x3c\x85\x32\x21\x7b\xc9\xc7\x0a\xa8\x24\xa4\x90\xde\x46\xd3\x25\x23\x14\x03\x67\xc2\x10\xd6\x6f\x0f\x5d\x7b\x7a"
    "\xcc\x9f\xc5\x58\x2a\xc1\xc4\x9e\x21\xa8\x5a\xf3\xac\xa4\x46\xf3\x9e\xe4\x63\xcb\x2f\x90\xa4\x29\x29\x01\xd9\x72"
    "\x2c\x29\xdf\x37\x01\x27\xbc\x4f\xee\x68\xd3\x21\x8f\xc0\xb3\xe4\xf5\x09\xed\xd2\x10\xaa\x53\xb4\xbe\xf0\xcc\x59"
    "\x0b\xd6\x3b\x96\x1c\x95\x24\x49\xdf\xce\xec\xfd\xa7\x48\x91\x14\x45\x0e\x3a\x36\x6f\xda\x45\xb3\x45\xa2\x41\xc9"
    "\xd4\xd7\x44\x4e\x3e\xb9\x74\x76\xd5\xa2\x13\x55\x2c\xc6\x87\xa3\xb5\x99\xac\x06\x84\x87\x7f\x75\x06\xfc\xbf\x14"
    "\x4c\x0e\xcc\x6e\xc4\xdf\x3d\xb7\x12\x71\xf4\xe8\xf1\x51\x40\x22\x28\x49\xe0\x1d\x4b\x87\xa8\x34\xcc\x06\xa2\xdd"
    "\x12\x5a\xd1\x86\x36\x64\x03\x35\x6f\x6f\x77\x6e\xeb\xf2\x85\x50\x98\x5e\xab\x03\x53\xad\x91\x23\x63\x1f\x16\x9c"
    "\xcd\xb9\xb2\x05\x63\x3a\xe1\xf4\x68\x1b\x17\x05\x35\x95\x53\xee\x0a\x84\x08\x30\x82\x04\x00\x30\x82\x02\xe8\xa0"
    "\x03\x02\x01\x02\x02\x01\x00\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05\x05\x00\x30\x63\x31\x0b\x30\x09"
    "\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x21\x30\x1f\x06\x03\x55\x04\x0a\x13\x18\x54\x68\x65\x20\x47\x6f\x20\x44"
    "\x61\x64\x64\x79\x20\x47\x72\x6f\x75\x70\x2c\x20\x49\x6e\x63\x2e\x31\x31\x30\x2f\x06\x03\x55\x04\x0b\x13\x28\x47"
    "\x6f\x20\x44\x61\x64\x64\x79\x20\x43\x6c\x61\x73\x73\x20\x32\x20\x43\x65\x72\x74\x69\x66\x69\x63\x61\x74\x69\x6f"
    "\x6e\x20\x41\x75\x74\x68\x6f\x72\x69\x74\x79\x30\x1e\x17\x0d\x30\x34\x30\x36\x32\x39\x31\x37\x30\x36\x32\x30\x5a"
    "\x17\x0d\x33\x34\x30\x36\x32\x39\x31\x37\x30\x36\x32\x30\x5a\x30\x63\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02"
    "\x55\x53\x31\x21\x30\x1f\x06\x03\x55\x04\x0a\x13\x18\x54\x68\x65\x20\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x47\x72"
    "\x6f\x75\x70\x2c\x20\x49\x6e\x63\x2e\x31\x31\x30\x2f\x06\x03\x55\x04\x0b\x13\x28\x47\x6f\x20\x44\x61\x64\x64\x79"
    "\x20\x43\x6c\x61\x73\x73\x20\x32\x20\x43\x65\x72\x74\x69\x66\x69\x63\x61\x74\x69\x6f\x6e\x20\x41\x75\x74\x68\x6f"
    "\x72\x69\x74\x79\x30\x82\x01\x20\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00\x03\x82\x01\x0d\x00"
    "\x30\x82\x01\x08\x02\x82\x01\x01\x00\xde\x9d\xd7\xea\x57\x18\x49\xa1\x5b\xeb\xd7\x5f\x48\x86\xea\xbe\xdd\xff\xe4"
    "\xef\x67\x1c\xf4\x65\x68\xb3\x57\x71\xa0\x5e\x77\xbb\xed\x9b\x49\xe9\x70\x80\x3d\x56\x18\x63\x08\x6f\xda\xf2\xcc"
    "\xd0\x3f\x7f\x02\x54\x22\x54\x10\xd8\xb2\x81\xd4\xc0\x75\x3d\x4b\x7f\xc7\x77\xc3\x3e\x78\xab\x1a\x03\xb5\x20\x6b"
    "\x2f\x6a\x2b\xb1\xc5\x88\x7e\xc4\xbb\x1e\xb0\xc1\xd8\x45\x27\x6f\xaa\x37\x58\xf7\x87\x26\xd7\xd8\x2d\xf6\xa9\x17"
    "\xb7\x1f\x72\x36\x4e\xa6\x17\x3f\x65\x98\x92\xdb\x2a\x6e\x5d\xa2\xfe\x88\xe0\x0b\xde\x7f\xe5\x8d\x15\xe1\xeb\xcb"
    "\x3a\xd5\xe2\x12\xa2\x13\x2d\xd8\x8e\xaf\x5f\x12\x3d\xa0\x08\x05\x08\xb6\x5c\xa5\x65\x38\x04\x45\x99\x1e\xa3\x60"
    "\x60\x74\xc5\x41\xa5\x72\x62\x1b\x62\xc5\x1f\x6f\x5f\x1a\x42\xbe\x02\x51\x65\xa8\xae\x23\x18\x6a\xfc\x78\x03\xa9"
    "\x4d\x7f\x80\xc3\xfa\xab\x5a\xfc\xa1\x40\xa4\xca\x19\x16\xfe\xb2\xc8\xef\x5e\x73\x0d\xee\x77\xbd\x9a\xf6\x79\x98"
    "\xbc\xb1\x07\x67\xa2\x15\x0d\xdd\xa0\x58\xc6\x44\x7b\x0a\x3e\x62\x28\x5f\xba\x41\x07\x53\x58\xcf\x11\x7e\x38\x74"
    "\xc5\xf8\xff\xb5\x69\x90\x8f\x84\x74\xea\x97\x1b\xaf\x02\x01\x03\xa3\x81\xc0\x30\x81\xbd\x30\x1d\x06\x03\x55\x1d"
    "\x0e\x04\x16\x04\x14\xd2\xc4\xb0\xd2\x91\xd4\x4c\x11\x71\xb3\x61\xcb\x3d\xa1\xfe\xdd\xa8\x6a\xd4\xe3\x30\x81\x8d"
    "\x06\x03\x55\x1d\x23\x04\x81\x85\x30\x81\x82\x80\x14\xd2\xc4\xb0\xd2\x91\xd4\x4c\x11\x71\xb3\x61\xcb\x3d\xa1\xfe"
    "\xdd\xa8\x6a\xd4\xe3\xa1\x67\xa4\x65\x30\x63\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x21\x30\x1f"
    "\x06\x03\x55\x04\x0a\x13\x18\x54\x68\x65\x20\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x47\x72\x6f\x75\x70\x2c\x20\x49"
    "\x6e\x63\x2e\x31\x31\x30\x2f\x06\x03\x55\x04\x0b\x13\x28\x47\x6f\x20\x44\x61\x64\x64\x79\x20\x43\x6c\x61\x73\x73"
    "\x20\x32\x20\x43\x65\x72\x74\x69\x66\x69\x63\x61\x74\x69\x6f\x6e\x20\x41\x75\x74\x68\x6f\x72\x69\x74\x79\x82\x01"
    "\x00\x30\x0c\x06\x03\x55\x1d\x13\x04\x05\x30\x03\x01\x01\xff\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x05"
    "\x05\x00\x03\x82\x01\x01\x00\x32\x4b\xf3\xb2\xca\x3e\x91\xfc\x12\xc6\xa1\x07\x8c\x8e\x77\xa0\x33\x06\x14\x5c\x90"
    "\x1e\x18\xf7\x08\xa6\x3d\x0a\x19\xf9\x87\x80\x11\x6e\x69\xe4\x96\x17\x30\xff\x34\x91\x63\x72\x38\xee\xcc\x1c\x01"
    "\xa3\x1d\x94\x28\xa4\x31\xf6\x7a\xc4\x54\xd7\xf6\xe5\x31\x58\x03\xa2\xcc\xce\x62\xdb\x94\x45\x73\xb5\xbf\x45\xc9"
    "\x24\xb5\xd5\x82\x02\xad\x23\x79\x69\x8d\xb8\xb6\x4d\xce\xcf\x4c\xca\x33\x23\xe8\x1c\x88\xaa\x9d\x8b\x41\x6e\x16"
    "\xc9\x20\xe5\x89\x9e\xcd\x3b\xda\x70\xf7\x7e\x99\x26\x20\x14\x54\x25\xab\x6e\x73\x85\xe6\x9b\x21\x9d\x0a\x6c\x82"
    "\x0e\xa8\xf8\xc2\x0c\xfa\x10\x1e\x6c\x96\xef\x87\x0d\xc4\x0f\x61\x8b\xad\xee\x83\x2b\x95\xf8\x8e\x92\x84\x72\x39"
    "\xeb\x20\xea\x83\xed\x83\xcd\x97\x6e\x08\xbc\xeb\x4e\x26\xb6\x73\x2b\xe4\xd3\xf6\x4c\xfe\x26\x71\xe2\x61\x11\x74"
    "\x4a\xff\x57\x1a\x87\x0f\x75\x48\x2e\xcf\x51\x69\x17\xa0\x02\x12\x61\x95\xd5\xd1\x40\xb2\x10\x4c\xee\xc4\xac\x10"
    "\x43\xa6\xa5\x9e\x0a\xd5\x95\x62\x9a\x0d\xcf\x88\x82\xc5\x32\x0c\xe4\x2b\x9f\x45\xe6\x0d\x9f\x28\x9c\xb1\xb9\x2a"
    "\x5a\x57\xad\x37\x0f\xaf\x1d\x7f\xdb\xbd\x9f\x22\x9b\x01\x0a\x04\x6d\x61\x69\x6e\x12\x1f\x08\xe0\xb6\x0d\x12\x19"
    "\x76\xa9\x14\xa5\x33\xd4\xfa\x07\x66\x34\xaf\xef\x47\x45\x1f\x6a\xec\x8c\xdc\x1e\x49\xda\xf0\x88\xac\x18\xee\xe1"
    "\x80\x9b\x05\x20\xf2\xe8\x80\x9b\x05\x2a\x39\x50\x61\x79\x6d\x65\x6e\x74\x20\x72\x65\x71\x75\x65\x73\x74\x20\x66"
    "\x6f\x72\x20\x42\x69\x74\x50\x61\x79\x20\x69\x6e\x76\x6f\x69\x63\x65\x20\x38\x63\x58\x35\x52\x62\x4e\x38\x61\x6f"
    "\x66\x63\x35\x33\x61\x57\x41\x6d\x35\x58\x46\x44\x32\x2b\x68\x74\x74\x70\x73\x3a\x2f\x2f\x62\x69\x74\x70\x61\x79"
    "\x2e\x63\x6f\x6d\x2f\x69\x2f\x38\x63\x58\x35\x52\x62\x4e\x38\x61\x6f\x66\x63\x35\x33\x61\x57\x41\x6d\x35\x58\x46"
    "\x44\x2a\x80\x02\x5e\xf8\x8b\xec\x4e\x09\xbe\x97\x9b\x07\x06\x64\x76\x4a\xfa\xe4\xfa\x3b\x1e\xca\x95\x47\x44\xa7"
    "\x66\x99\xb1\x85\x30\x18\x3e\x6f\x46\x7e\xc5\x92\x39\x13\x66\x8c\x5a\xbe\x38\x2c\xb7\xef\x6a\x88\x58\xfa\xe6\x18"
    "\x0c\x47\x8e\x81\x17\x9d\x39\x35\xcd\x53\x23\xf0\xc5\xcc\x2e\xea\x0f\x1e\x29\xb5\xa6\xb2\x65\x4b\x4c\xbd\xa3\x89"
    "\xea\xee\x32\x21\x5c\x87\x77\xaf\xbb\xe0\x7d\x60\xa4\xf9\xfa\x07\xab\x6e\x9a\x6d\x3a\xd2\xa9\xef\xb5\x25\x22\x16"
    "\x31\xc8\x04\x4e\xc7\x59\xd9\xc1\xfc\xcc\x39\xbb\x3e\xe4\xf4\x4e\xbc\x7c\x1c\xc8\x24\x83\x41\x44\x27\x22\xac\x88"
    "\x0d\xa0\xc7\xd5\x9d\x69\x67\x06\xc7\xbc\xf0\x91"; // 4095 character string literal limit in C99
    
    const char buf2[] = "\x01\xb4\x92\x5a\x07\x84\x22\x0a\x93\xc5\xb3\x09\xda\xd8\xe3\x26\x61\xf2\xcc\xab\x4e\xc8\x68"
    "\xb2\xde\x00\x0f\x24\x2d\xb7\x3f\xff\xb2\x69\x37\xcf\x83\xed\x6d\x2e\xfa\xa7\x71\xd2\xd2\xc6\x97\x84\x4b\x83\x94"
    "\x8c\x98\x25\x2b\x5f\x35\x2e\xdd\x4f\xe9\x6b\x29\xcb\xe0\xc9\xca\x3d\x10\x7a\x3e\xb7\x90\xda\xb5\xdd\xd7\x3d\xe6"
    "\xc7\x48\xf2\x04\x7d\xb4\x25\xc8\x0c\x39\x13\x54\x73\xca\xca\xd3\x61\x9b\xaa\xf2\x8e\x39\x1d\xa4\xa6\xc7\xb8\x2b"
    "\x74";
    
    uint8_t buf3[(sizeof(buf1) - 1) + (sizeof(buf2) - 1)];
    
    memcpy(buf3, buf1, sizeof(buf1) - 1);
    memcpy(buf3 + (sizeof(buf1) - 1), buf2, sizeof(buf2) - 1);

    BRPaymentProtocolRequest *req = BRPaymentProtocolRequestParse(buf3, sizeof(buf3));
    uint8_t buf4[BRPaymentProtocolRequestSerialize(req, NULL, 0)];
    size_t len = BRPaymentProtocolRequestSerialize(req, buf4, sizeof(buf4));
    int i = 0;

    if (len != sizeof(buf3) || memcmp(buf3, buf4, len) != 0) // check if parse/serialize produces same result
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequestParse/Serialize() test 1\n", __func__);

    do {
        uint8_t buf5[BRPaymentProtocolRequestCert(req, NULL, 0, i)];
    
        len = BRPaymentProtocolRequestCert(req, buf5, sizeof(buf5), i);
        if (len > 0) i++;
    } while (len > 0);

    // check for a chain of 3 certificates
    if (i != 3) r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequestCert() test 1\n", __func__);
    
    if (req->details->expires == 0 || req->details->expires >= time(NULL)) // check that request is expired
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequest->details->expires test 1\n", __func__);
    
    if (req) BRPaymentProtocolRequestFree(req);

    const char buf5[] = "\x0a\x00\x12\x5f\x54\x72\x61\x6e\x73\x61\x63\x74\x69\x6f\x6e\x20\x72\x65\x63\x65\x69\x76\x65"
    "\x64\x20\x62\x79\x20\x42\x69\x74\x50\x61\x79\x2e\x20\x49\x6e\x76\x6f\x69\x63\x65\x20\x77\x69\x6c\x6c\x20\x62\x65"
    "\x20\x6d\x61\x72\x6b\x65\x64\x20\x61\x73\x20\x70\x61\x69\x64\x20\x69\x66\x20\x74\x68\x65\x20\x74\x72\x61\x6e\x73"
    "\x61\x63\x74\x69\x6f\x6e\x20\x69\x73\x20\x63\x6f\x6e\x66\x69\x72\x6d\x65\x64\x2e";
    BRPaymentProtocolACK *ack = BRPaymentProtocolACKParse((const uint8_t *)buf5, sizeof(buf5) - 1);
    uint8_t buf6[BRPaymentProtocolACKSerialize(ack, NULL, 0)];

    len = BRPaymentProtocolACKSerialize(ack, buf6, sizeof(buf6));
    if (len != sizeof(buf5) - 1 || memcmp(buf5, buf6, len) != 0) // check if parse/serialize produces same result
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolACKParse/Serialize() test\n", __func__);
    
    printf("\n");
    if (ack->memo) printf("%s\n", ack->memo);
    // check that memo is not NULL
    if (! ack->memo) r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolACK->memo test\n", __func__);

    const char buf7[] = "\x12\x0b\x78\x35\x30\x39\x2b\x73\x68\x61\x32\x35\x36\x1a\xbe\x15\x0a\xfe\x0b\x30\x82\x05\xfa"
    "\x30\x82\x04\xe2\xa0\x03\x02\x01\x02\x02\x10\x09\x0b\x35\xca\x5c\x5b\xf1\xb9\x8b\x3d\x8f\x9f\x4a\x77\x55\xd6\x30"
    "\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0b\x05\x00\x30\x75\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55"
    "\x53\x31\x15\x30\x13\x06\x03\x55\x04\x0a\x13\x0c\x44\x69\x67\x69\x43\x65\x72\x74\x20\x49\x6e\x63\x31\x19\x30\x17"
    "\x06\x03\x55\x04\x0b\x13\x10\x77\x77\x77\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x31\x34\x30\x32\x06"
    "\x03\x55\x04\x03\x13\x2b\x44\x69\x67\x69\x43\x65\x72\x74\x20\x53\x48\x41\x32\x20\x45\x78\x74\x65\x6e\x64\x65\x64"
    "\x20\x56\x61\x6c\x69\x64\x61\x74\x69\x6f\x6e\x20\x53\x65\x72\x76\x65\x72\x20\x43\x41\x30\x1e\x17\x0d\x31\x34\x30"
    "\x35\x30\x39\x30\x30\x30\x30\x30\x30\x5a\x17\x0d\x31\x36\x30\x35\x31\x33\x31\x32\x30\x30\x30\x30\x5a\x30\x82\x01"
    "\x05\x31\x1d\x30\x1b\x06\x03\x55\x04\x0f\x0c\x14\x50\x72\x69\x76\x61\x74\x65\x20\x4f\x72\x67\x61\x6e\x69\x7a\x61"
    "\x74\x69\x6f\x6e\x31\x13\x30\x11\x06\x0b\x2b\x06\x01\x04\x01\x82\x37\x3c\x02\x01\x03\x13\x02\x55\x53\x31\x19\x30"
    "\x17\x06\x0b\x2b\x06\x01\x04\x01\x82\x37\x3c\x02\x01\x02\x13\x08\x44\x65\x6c\x61\x77\x61\x72\x65\x31\x10\x30\x0e"
    "\x06\x03\x55\x04\x05\x13\x07\x35\x31\x35\x34\x33\x31\x37\x31\x0f\x30\x0d\x06\x03\x55\x04\x09\x0c\x06\x23\x32\x33"
    "\x30\x30\x38\x31\x17\x30\x15\x06\x03\x55\x04\x09\x13\x0e\x35\x34\x38\x20\x4d\x61\x72\x6b\x65\x74\x20\x53\x74\x2e"
    "\x31\x0e\x30\x0c\x06\x03\x55\x04\x11\x13\x05\x39\x34\x31\x30\x34\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55"
    "\x53\x31\x13\x30\x11\x06\x03\x55\x04\x08\x13\x0a\x43\x61\x6c\x69\x66\x6f\x72\x6e\x69\x61\x31\x16\x30\x14\x06\x03"
    "\x55\x04\x07\x13\x0d\x53\x61\x6e\x20\x46\x72\x61\x6e\x63\x69\x73\x63\x6f\x31\x17\x30\x15\x06\x03\x55\x04\x0a\x13"
    "\x0e\x43\x6f\x69\x6e\x62\x61\x73\x65\x2c\x20\x49\x6e\x63\x2e\x31\x15\x30\x13\x06\x03\x55\x04\x03\x13\x0c\x63\x6f"
    "\x69\x6e\x62\x61\x73\x65\x2e\x63\x6f\x6d\x30\x82\x01\x22\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05"
    "\x00\x03\x82\x01\x0f\x00\x30\x82\x01\x0a\x02\x82\x01\x01\x00\xb4\x5e\x3f\xf3\x80\x66\x7a\xa1\x4d\x5a\x12\xfc\x2f"
    "\xc9\x83\xfc\x66\x18\xb5\x54\x99\x93\x3c\x3b\xde\x15\xc0\x1d\x83\x88\x46\xb4\xca\xf9\x84\x8e\x7c\x40\xe5\xfa\x7c"
    "\x67\xef\x9b\x5b\x1e\xfe\x26\xee\x55\x71\xc5\xfa\x2e\xff\x75\x90\x52\x45\x47\x01\xad\x89\x31\x55\x7d\x69\x7b\x13"
    "\x9e\x5d\x19\xab\xb3\xe4\x39\x67\x5f\x31\xdb\x7f\x2e\xf1\xa5\xd9\x7d\xb0\x7c\x1f\x69\x66\x26\x63\x80\xeb\x4f\xcf"
    "\xa8\xe1\x47\x1a\x6e\xcc\x2f\xbe\xbf\x3e\x67\xb3\xea\xa8\x4d\x0f\xbe\x06\x3e\x60\x38\x0d\xcd\xb7\xa2\x02\x03\xd2"
    "\x9a\x94\x05\x9e\xf7\xf2\x0d\x47\x2c\xc2\x57\x83\xab\x2a\x1d\xb6\xa3\x94\xec\xc0\x7b\x40\x24\x97\x41\x00\xbc\xfd"
    "\x47\x0f\x59\xef\x3b\x57\x23\x65\x21\x32\x09\x60\x9f\xad\x22\x99\x94\xb4\x92\x3c\x1d\xf3\xa1\x8c\x41\xe3\xe7\xbc"
    "\x1f\x19\x2b\xa6\xe7\xe5\xc3\x2a\xe1\x55\x10\x7e\x21\x90\x3e\xff\x7b\xce\x9f\xc5\x94\xb4\x9d\x9f\x6a\xe7\x90\x1f"
    "\xa1\x91\xfc\xba\xe8\xa2\xcf\x09\xc3\xbf\xc2\x43\x77\xd7\x17\xb6\x01\x00\x80\xc5\x68\x1a\x7d\xbc\x6e\x1d\x52\x98"
    "\x7b\x7e\xbb\xe9\x5e\x7a\xf4\x20\x2d\xa4\x36\xe6\x7a\x88\x47\x2a\xac\xed\xc9\x02\x03\x01\x00\x01\xa3\x82\x01\xf2"
    "\x30\x82\x01\xee\x30\x1f\x06\x03\x55\x1d\x23\x04\x18\x30\x16\x80\x14\x3d\xd3\x50\xa5\xd6\xa0\xad\xee\xf3\x4a\x60"
    "\x0a\x65\xd3\x21\xd4\xf8\xf8\xd6\x0f\x30\x1d\x06\x03\x55\x1d\x0e\x04\x16\x04\x14\x6d\x33\xb9\x74\x3a\x61\xb7\x49"
    "\x94\x23\xd1\xa8\x9d\x08\x5d\x01\x48\x68\x0b\xba\x30\x29\x06\x03\x55\x1d\x11\x04\x22\x30\x20\x82\x0c\x63\x6f\x69"
    "\x6e\x62\x61\x73\x65\x2e\x63\x6f\x6d\x82\x10\x77\x77\x77\x2e\x63\x6f\x69\x6e\x62\x61\x73\x65\x2e\x63\x6f\x6d\x30"
    "\x0e\x06\x03\x55\x1d\x0f\x01\x01\xff\x04\x04\x03\x02\x05\xa0\x30\x1d\x06\x03\x55\x1d\x25\x04\x16\x30\x14\x06\x08"
    "\x2b\x06\x01\x05\x05\x07\x03\x01\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x02\x30\x75\x06\x03\x55\x1d\x1f\x04\x6e\x30"
    "\x6c\x30\x34\xa0\x32\xa0\x30\x86\x2e\x68\x74\x74\x70\x3a\x2f\x2f\x63\x72\x6c\x33\x2e\x64\x69\x67\x69\x63\x65\x72"
    "\x74\x2e\x63\x6f\x6d\x2f\x73\x68\x61\x32\x2d\x65\x76\x2d\x73\x65\x72\x76\x65\x72\x2d\x67\x31\x2e\x63\x72\x6c\x30"
    "\x34\xa0\x32\xa0\x30\x86\x2e\x68\x74\x74\x70\x3a\x2f\x2f\x63\x72\x6c\x34\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e"
    "\x63\x6f\x6d\x2f\x73\x68\x61\x32\x2d\x65\x76\x2d\x73\x65\x72\x76\x65\x72\x2d\x67\x31\x2e\x63\x72\x6c\x30\x42\x06"
    "\x03\x55\x1d\x20\x04\x3b\x30\x39\x30\x37\x06\x09\x60\x86\x48\x01\x86\xfd\x6c\x02\x01\x30\x2a\x30\x28\x06\x08\x2b"
    "\x06\x01\x05\x05\x07\x02\x01\x16\x1c\x68\x74\x74\x70\x73\x3a\x2f\x2f\x77\x77\x77\x2e\x64\x69\x67\x69\x63\x65\x72"
    "\x74\x2e\x63\x6f\x6d\x2f\x43\x50\x53\x30\x81\x88\x06\x08\x2b\x06\x01\x05\x05\x07\x01\x01\x04\x7c\x30\x7a\x30\x24"
    "\x06\x08\x2b\x06\x01\x05\x05\x07\x30\x01\x86\x18\x68\x74\x74\x70\x3a\x2f\x2f\x6f\x63\x73\x70\x2e\x64\x69\x67\x69"
    "\x63\x65\x72\x74\x2e\x63\x6f\x6d\x30\x52\x06\x08\x2b\x06\x01\x05\x05\x07\x30\x02\x86\x46\x68\x74\x74\x70\x3a\x2f"
    "\x2f\x63\x61\x63\x65\x72\x74\x73\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x2f\x44\x69\x67\x69\x43\x65"
    "\x72\x74\x53\x48\x41\x32\x45\x78\x74\x65\x6e\x64\x65\x64\x56\x61\x6c\x69\x64\x61\x74\x69\x6f\x6e\x53\x65\x72\x76"
    "\x65\x72\x43\x41\x2e\x63\x72\x74\x30\x0c\x06\x03\x55\x1d\x13\x01\x01\xff\x04\x02\x30\x00\x30\x0d\x06\x09\x2a\x86"
    "\x48\x86\xf7\x0d\x01\x01\x0b\x05\x00\x03\x82\x01\x01\x00\xaa\xdf\xcf\x94\x05\x0e\xd9\x38\xe3\x11\x4a\x64\x0a\xf3"
    "\xd9\xb0\x42\x76\xda\x00\xf5\x21\x5d\x71\x48\xf9\xf1\x6d\x4c\xac\x0c\x77\xbd\x53\x49\xec\x2f\x47\x29\x9d\x03\xc9"
    "\x00\xf7\x01\x46\x75\x2d\xa7\x28\x29\x29\x0a\xc5\x0a\x77\x99\x2f\x01\x53\x7a\xb2\x68\x93\x92\xce\x0b\xfe\xb7\xef"
    "\xa4\x9f\x4c\x4f\xe4\xe1\xe4\x3c\xa1\xfc\xfb\x16\x26\xce\x55\x4d\xa4\xf6\xe7\xfa\x34\xa5\x97\xe4\x01\xf2\x15\xc4"
    "\x3a\xfd\x0b\xa7\x77\xad\x58\x7e\xb0\xaf\xac\xd7\x1f\x7a\x6a\xf7\x75\x28\x14\xf7\xab\x4c\x20\x2e\xd7\x6d\x33\xde"
    "\xfd\x12\x89\xd5\x41\x80\x3f\xed\x01\xac\x80\xa3\xca\xcf\xda\xae\x29\x27\x9e\x5d\xe1\x4d\x46\x04\x75\xf4\xba\xf2"
    "\x7e\xab\x69\x33\x79\xd3\x91\x20\xe7\x47\x7b\xf3\xec\x71\x96\x64\xc7\xb6\xcb\x5e\x55\x75\x56\xe5\xbb\xdd\xd9\xc9"
    "\xd1\xeb\xc9\xf8\x35\xe9\xda\x5b\x3d\xbb\x72\xfe\x8d\x94\xac\x05\xea\xb3\xc4\x79\x98\x75\x20\xad\xe3\xa1\xd2\x75"
    "\xe1\xe2\xfe\x72\x56\x98\xd2\xf7\xcb\x13\x90\xa9\xd4\x0e\xa6\xcb\xf2\x1a\x73\xbd\xdc\xcd\x1a\xd6\x1a\xa2\x49\xce"
    "\x8e\x28\x85\xa3\x73\x0b\x7d\x53\xbd\x07\x5f\x55\x09\x9d\x29\x60\xf3\xcc\x0a\xba\x09\x30\x82\x04\xb6\x30\x82\x03"
    "\x9e\xa0\x03\x02\x01\x02\x02\x10\x0c\x79\xa9\x44\xb0\x8c\x11\x95\x20\x92\x61\x5f\xe2\x6b\x1d\x83\x30\x0d\x06\x09"
    "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0b\x05\x00\x30\x6c\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53\x31\x15"
    "\x30\x13\x06\x03\x55\x04\x0a\x13\x0c\x44\x69\x67\x69\x43\x65\x72\x74\x20\x49\x6e\x63\x31\x19\x30\x17\x06\x03\x55"
    "\x04\x0b\x13\x10\x77\x77\x77\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x31\x2b\x30\x29\x06\x03\x55\x04"
    "\x03\x13\x22\x44\x69\x67\x69\x43\x65\x72\x74\x20\x48\x69\x67\x68\x20\x41\x73\x73\x75\x72\x61\x6e\x63\x65\x20\x45"
    "\x56\x20\x52\x6f\x6f\x74\x20\x43\x41\x30\x1e\x17\x0d\x31\x33\x31\x30\x32\x32\x31\x32\x30\x30\x30\x30\x5a\x17\x0d"
    "\x32\x38\x31\x30\x32\x32\x31\x32\x30\x30\x30\x30\x5a\x30\x75\x31\x0b\x30\x09\x06\x03\x55\x04\x06\x13\x02\x55\x53"
    "\x31\x15\x30\x13\x06\x03\x55\x04\x0a\x13\x0c\x44\x69\x67\x69\x43\x65\x72\x74\x20\x49\x6e\x63\x31\x19\x30\x17\x06"
    "\x03\x55\x04\x0b\x13\x10\x77\x77\x77\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x31\x34\x30\x32\x06\x03"
    "\x55\x04\x03\x13\x2b\x44\x69\x67\x69\x43\x65\x72\x74\x20\x53\x48\x41\x32\x20\x45\x78\x74\x65\x6e\x64\x65\x64\x20"
    "\x56\x61\x6c\x69\x64\x61\x74\x69\x6f\x6e\x20\x53\x65\x72\x76\x65\x72\x20\x43\x41\x30\x82\x01\x22\x30\x0d\x06\x09"
    "\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00\x03\x82\x01\x0f\x00\x30\x82\x01\x0a\x02\x82\x01\x01\x00\xd7\x53\xa4"
    "\x04\x51\xf8\x99\xa6\x16\x48\x4b\x67\x27\xaa\x93\x49\xd0\x39\xed\x0c\xb0\xb0\x00\x87\xf1\x67\x28\x86\x85\x8c\x8e"
    "\x63\xda\xbc\xb1\x40\x38\xe2\xd3\xf5\xec\xa5\x05\x18\xb8\x3d\x3e\xc5\x99\x17\x32\xec\x18\x8c\xfa\xf1\x0c\xa6\x64"
    "\x21\x85\xcb\x07\x10\x34\xb0\x52\x88\x2b\x1f\x68\x9b\xd2\xb1\x8f\x12\xb0\xb3\xd2\xe7\x88\x1f\x1f\xef\x38\x77\x54"
    "\x53\x5f\x80\x79\x3f\x2e\x1a\xaa\xa8\x1e\x4b\x2b\x0d\xab\xb7\x63\xb9\x35\xb7\x7d\x14\xbc\x59\x4b\xdf\x51\x4a\xd2"
    "\xa1\xe2\x0c\xe2\x90\x82\x87\x6a\xae\xea\xd7\x64\xd6\x98\x55\xe8\xfd\xaf\x1a\x50\x6c\x54\xbc\x11\xf2\xfd\x4a\xf2"
    "\x9d\xbb\x7f\x0e\xf4\xd5\xbe\x8e\x16\x89\x12\x55\xd8\xc0\x71\x34\xee\xf6\xdc\x2d\xec\xc4\x87\x25\x86\x8d\xd8\x21"
    "\xe4\xb0\x4d\x0c\x89\xdc\x39\x26\x17\xdd\xf6\xd7\x94\x85\xd8\x04\x21\x70\x9d\x6f\x6f\xff\x5c\xba\x19\xe1\x45\xcb"
    "\x56\x57\x28\x7e\x1c\x0d\x41\x57\xaa\xb7\xb8\x27\xbb\xb1\xe4\xfa\x2a\xef\x21\x23\x75\x1a\xad\x2d\x9b\x86\x35\x8c"
    "\x9c\x77\xb5\x73\xad\xd8\x94\x2d\xe4\xf3\x0c\x9d\xee\xc1\x4e\x62\x7e\x17\xc0\x71\x9e\x2c\xde\xf1\xf9\x10\x28\x19"
    "\x33\x02\x03\x01\x00\x01\xa3\x82\x01\x49\x30\x82\x01\x45\x30\x12\x06\x03\x55\x1d\x13\x01\x01\xff\x04\x08\x30\x06"
    "\x01\x01\xff\x02\x01\x00\x30\x0e\x06\x03\x55\x1d\x0f\x01\x01\xff\x04\x04\x03\x02\x01\x86\x30\x1d\x06\x03\x55\x1d"
    "\x25\x04\x16\x30\x14\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x01\x06\x08\x2b\x06\x01\x05\x05\x07\x03\x02\x30\x34\x06"
    "\x08\x2b\x06\x01\x05\x05\x07\x01\x01\x04\x28\x30\x26\x30\x24\x06\x08\x2b\x06\x01\x05\x05\x07\x30\x01\x86\x18\x68"
    "\x74\x74\x70\x3a\x2f\x2f\x6f\x63\x73\x70\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x30\x4b\x06\x03\x55"
    "\x1d\x1f\x04\x44\x30\x42\x30\x40\xa0\x3e\xa0\x3c\x86\x3a\x68\x74\x74\x70\x3a\x2f\x2f\x63\x72\x6c\x34\x2e\x64\x69"
    "\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x2f\x44\x69\x67\x69\x43\x65\x72\x74\x48\x69\x67\x68\x41\x73\x73\x75\x72"
    "\x61\x6e\x63\x65\x45\x56\x52\x6f\x6f\x74\x43\x41\x2e\x63\x72\x6c\x30\x3d\x06\x03\x55\x1d\x20\x04\x36\x30\x34\x30"
    "\x32\x06\x04\x55\x1d\x20\x00\x30\x2a\x30\x28\x06\x08\x2b\x06\x01\x05\x05\x07\x02\x01\x16\x1c\x68\x74\x74\x70\x73"
    "\x3a\x2f\x2f\x77\x77\x77\x2e\x64\x69\x67\x69\x63\x65\x72\x74\x2e\x63\x6f\x6d\x2f\x43\x50\x53\x30\x1d\x06\x03\x55"
    "\x1d\x0e\x04\x16\x04\x14\x3d\xd3\x50\xa5\xd6\xa0\xad\xee\xf3\x4a\x60\x0a\x65\xd3\x21\xd4\xf8\xf8\xd6\x0f\x30\x1f"
    "\x06\x03\x55\x1d\x23\x04\x18\x30\x16\x80\x14\xb1\x3e\xc3\x69\x03\xf8\xbf\x47\x01\xd4\x98\x26\x1a\x08\x02\xef\x63"
    "\x64\x2b\xc3\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x0b\x05\x00\x03\x82\x01\x01\x00\x9d\xb6\xd0\x90\x86"
    "\xe1\x86\x02\xed\xc5\xa0\xf0\x34\x1c\x74\xc1\x8d\x76\xcc\x86\x0a\xa8\xf0\x4a\x8a\x42\xd6\x3f\xc8\xa9\x4d\xad\x7c"
    "\x08\xad\xe6\xb6\x50\xb8\xa2\x1a\x4d\x88\x07\xb1\x29\x21\xdc\xe7\xda\xc6\x3c\x21\xe0\xe3\x11\x49\x70\xac\x7a\x1d"
    "\x01\xa4\xca\x11\x3a\x57\xab\x7d\x57\x2a\x40\x74\xfd\xd3\x1d\x85\x18\x50\xdf\x57\x47\x75\xa1\x7d\x55\x20\x2e\x47"
    "\x37\x50\x72\x8c\x7f\x82\x1b\xd2\x62\x8f\x2d\x03\x5a\xda\xc3\xc8\xa1\xce\x2c\x52\xa2\x00\x63\xeb\x73\xba\x71\xc8"
    "\x49\x27\x23\x97\x64\x85\x9e\x38\x0e\xad\x63\x68\x3c\xba\x52\x81\x58\x79\xa3\x2c\x0c\xdf\xde\x6d\xeb\x31\xf2\xba"
    "\xa0\x7c\x6c\xf1\x2c\xd4\xe1\xbd\x77\x84\x37\x03\xce\x32\xb5\xc8\x9a\x81\x1a\x4a\x92\x4e\x3b\x46\x9a\x85\xfe\x83"
    "\xa2\xf9\x9e\x8c\xa3\xcc\x0d\x5e\xb3\x3d\xcf\x04\x78\x8f\x14\x14\x7b\x32\x9c\xc7\x00\xa6\x5c\xc4\xb5\xa1\x55\x8d"
    "\x5a\x56\x68\xa4\x22\x70\xaa\x3c\x81\x71\xd9\x9d\xa8\x45\x3b\xf4\xe5\xf6\xa2\x51\xdd\xc7\x7b\x62\xe8\x6f\x0c\x74"
    "\xeb\xb8\xda\xf8\xbf\x87\x0d\x79\x50\x91\x90\x9b\x18\x3b\x91\x59\x27\xf1\x35\x28\x13\xab\x26\x7e\xd5\xf7\x7a\x22"
    "\xb4\x01\x12\x1f\x08\x98\xb7\x68\x12\x19\x76\xa9\x14\x7d\x53\x25\xa8\x54\xf0\xc9\xa1\xcb\xb6\xcb\xfb\x89\xb2\xa9"
    "\x6d\x83\x7e\xd7\xbf\x88\xac\x18\xac\xb9\xe0\x9e\x05\x20\xd2\xbc\xe0\x9e\x05\x2a\x31\x50\x61\x79\x6d\x65\x6e\x74"
    "\x20\x72\x65\x71\x75\x65\x73\x74\x20\x66\x6f\x72\x20\x43\x6f\x69\x6e\x62\x61\x73\x65\x20\x6f\x72\x64\x65\x72\x20"
    "\x63\x6f\x64\x65\x3a\x20\x51\x43\x4f\x49\x47\x44\x50\x41\x32\x30\x68\x74\x74\x70\x73\x3a\x2f\x2f\x63\x6f\x69\x6e"
    "\x62\x61\x73\x65\x2e\x63\x6f\x6d\x2f\x72\x70\x2f\x35\x33\x64\x38\x31\x62\x66\x61\x35\x64\x36\x62\x31\x64\x64\x61"
    "\x37\x62\x30\x30\x30\x30\x30\x34\x3a\x20\x33\x36\x32\x64\x32\x39\x31\x39\x32\x31\x37\x36\x32\x31\x33\x39\x32\x35"
    "\x38\x37\x36\x63\x65\x32\x63\x62\x34\x30\x30\x34\x31\x62\x2a\x80\x02\x4d\x81\xca\x72\x21\x38\x13\xb2\x58\x5d\x98"
    "\x00\x5b\x23\x8e\x26\x8a\x00\x9e\xc0\x2d\x04\xdd\x7a\x8a\x98\x48\x32\xb9\x90\xd7\x40\xa9\x69\x09\xd6\x2a\x5d\xf9"
    "\xf8\xf8\x5b\x67\x32\x93\x79\xbb\xa0\xa9\xba\x03\xbc\xa3\xd6\x14\x00\xd4\xe4\x77\x98\x4b\x7e\xdc\xf3\x04\x22\x61"
    "\x71\x84\x23\x73\x6c\x44\x1d\x14\x0e\xe8\x9d\x64\x60\x96\x67\xde\x50\xea\xdb\x4c\xab\xbe\xf4\x78\xd3\xa9\xcb\xd4"
    "\xdf\xda\xb9\xa0\xc2\x81\x83\x90\xd2\x0c\x24\x3a\xd0\x2c\xc2\x7a\xbf\x0b\xbb\x2b\xab\x32\x27\xba\xa8\xe5\xd6\x73"
    "\xf8\x49\x91\x41\x22\x53\xbe\x1e\x69\xdf\xa7\x80\xdc\x06\xb6\xf4\x8e\xdf\xa1\x5d\xe6\xd0\xcc\xec\x22\xd9\xfa\xaf"
    "\x67\xb5\x35\xe8\xb2\x77\x8c\xdf\x61\x84\xda\x2f\x2d\x17\x92\xd3\x4c\x64\x40\x98\x83\x27\x32\x9e\x9c\x5a\xe1\x8c"
    "\x34\xdd\xa1\x6d\xcd\xfb\xf4\x19\xf7\xfd\x27\xbf\x57\x5b\x6f\x9c\x95\xb1\xf0\x90\x02\x16\x40\xaf\x5c\x02\xad\x02"
    "\x7b\x5d\x76\x05\x3a\x58\x40\xbc\x4d\x61\x04\xdd\x87\xef\xc3\x1b\xcc\x3a\x8a\xef\xc3\x10\x02\x35\xbe\x61\xc0\x3a"
    "\x50\x55\x66\x77\x71\x85\xdd\x6f\x93\x2b\xae\xb5\xd5\xe2\xd4\x39\x8d\x01\x14\x0d\x48";

    req = BRPaymentProtocolRequestParse((const uint8_t *)buf7, sizeof(buf7) - 1);

    uint8_t buf8[BRPaymentProtocolRequestSerialize(req, NULL, 0)];

    len = BRPaymentProtocolRequestSerialize(req, buf8, sizeof(buf8));
    if (len != sizeof(buf7) - 1 || memcmp(buf7, buf8, len) != 0) // check if parse/serialize produces same result
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequestParse/Serialize() test 2\n", __func__);
    i = 0;
    
    do {
        uint8_t buf9[BRPaymentProtocolRequestCert(req, NULL, 0, i)];
        
        len = BRPaymentProtocolRequestCert(req, buf9, sizeof(buf9), i);
        if (len > 0) i++;
    } while (len > 0);
    
    // check for a chain of 2 certificates
    if (i != 2) r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequestCert() test 2\n", __func__);
    
    if (req->details->expires == 0 || req->details->expires >= time(NULL)) // check that request is expired
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequest->details->expires test 2\n", __func__);
    
    if (req) BRPaymentProtocolRequestFree(req);
    
    // test garbage input
    const char buf9[] = "jfkdlasjfalk;sjfal;jflsadjfla;s";
    
    req = BRPaymentProtocolRequestParse((const uint8_t *)buf9, sizeof(buf9) - 1);
    
    uint8_t buf0[(req) ? BRPaymentProtocolRequestSerialize(req, NULL, 0) : 0];

    len = (req) ? BRPaymentProtocolRequestSerialize(req, buf0, sizeof(buf0)) : 0;
    if (len > 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolRequestParse/Serialize() test 3\n", __func__);
    
    printf("                                    ");
    return r;
}

int BRPaymentProtocolEncryptionTests()
{
    int r = 1;
    BRKey senderKey, receiverKey;
    uint8_t id[32] = { 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
                       0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00 };
    
    BRKeySetSecret(&senderKey, &toUInt256("0000000000000000000000000000000000000000000000000000000000000001"), 1);
    BRKeySetSecret(&receiverKey, &toUInt256("0000000000000000000000000000000000000000000000000000000000000002"), 1);
    
    BRPaymentProtocolInvoiceRequest *req = BRPaymentProtocolInvoiceRequestNew(&senderKey, 0, NULL, NULL, 0, NULL, NULL,
                                                                              NULL, 0);
    
    if (! req) r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolInvoiceRequestNew() test\n", __func__);
    
    uint8_t buf0[(req) ? BRPaymentProtocolInvoiceRequestSerialize(req, NULL, 0) : 0];
    size_t len0 = (req) ? BRPaymentProtocolInvoiceRequestSerialize(req, buf0, sizeof(buf0)) : 0;
    
    if (req) BRPaymentProtocolInvoiceRequestFree(req);
    req = BRPaymentProtocolInvoiceRequestParse(buf0, len0);
    
    if (! req || memcmp(req->senderPubKey.pubKey, senderKey.pubKey, 33) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolInvoiceRequestSerialize/Parse() test\n", __func__);
    
    if (req) BRPaymentProtocolInvoiceRequestFree(req);
    
    const char buf[] = "\x0a\x00\x12\x5f\x54\x72\x61\x6e\x73\x61\x63\x74\x69\x6f\x6e\x20\x72\x65\x63\x65\x69\x76\x65"
    "\x64\x20\x62\x79\x20\x42\x69\x74\x50\x61\x79\x2e\x20\x49\x6e\x76\x6f\x69\x63\x65\x20\x77\x69\x6c\x6c\x20\x62\x65"
    "\x20\x6d\x61\x72\x6b\x65\x64\x20\x61\x73\x20\x70\x61\x69\x64\x20\x69\x66\x20\x74\x68\x65\x20\x74\x72\x61\x6e\x73"
    "\x61\x63\x74\x69\x6f\x6e\x20\x69\x73\x20\x63\x6f\x6e\x66\x69\x72\x6d\x65\x64\x2e";
    
    BRPaymentProtocolMessage *msg1 = BRPaymentProtocolMessageNew(BRPaymentProtocolMessageTypeACK, (uint8_t *)buf,
                                                                 sizeof(buf) - 1, 1, NULL, id, sizeof(id));
    
    if (! msg1) r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolMessageNew() test\n", __func__);
    
    uint8_t buf1[(msg1) ? BRPaymentProtocolMessageSerialize(msg1, NULL, 0) : 0];
    size_t len1 = (msg1) ? BRPaymentProtocolMessageSerialize(msg1, buf1, sizeof(buf1)) : 0;
    
    if (msg1) BRPaymentProtocolMessageFree(msg1);
    msg1 = BRPaymentProtocolMessageParse(buf1, len1);
    
    if (! msg1 || msg1->msgLen != sizeof(buf) - 1 || memcmp(buf, msg1->message, sizeof(buf) - 1) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolMessageSerialize/Parse() test\n", __func__);
    
    if (msg1) BRPaymentProtocolMessageFree(msg1);
    
    BRPaymentProtocolEncryptedMessage *msg2 =
        BRPaymentProtocolEncryptedMessageNew(BRPaymentProtocolMessageTypeACK, (uint8_t *)buf, sizeof(buf) - 1,
                                             &receiverKey, &senderKey, time(NULL), id, sizeof(id), 1, NULL);

    if (! msg2) r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolEncryptedMessageNew() test\n", __func__);
    
    uint8_t buf2[(msg2) ? BRPaymentProtocolEncryptedMessageSerialize(msg2, NULL, 0) : 0];
    size_t len2 = (msg2) ? BRPaymentProtocolEncryptedMessageSerialize(msg2, buf2, sizeof(buf2)) : 0;
    
    if (msg2) BRPaymentProtocolEncryptedMessageFree(msg2);
    msg2 = BRPaymentProtocolEncryptedMessageParse(buf2, len2);
    
    if (! msg2 || msg2->msgLen != sizeof(buf) - 1 + 16)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolEncryptedMessageSerialize/Parse() test\n", __func__);

    if (msg2 && ! BRPaymentProtocolEncryptedMessageVerify(msg2, &receiverKey))
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolEncryptedMessageVerify() test\n", __func__);

    uint8_t out[(msg2) ? msg2->msgLen : 0];
    size_t outLen = BRPaymentProtocolEncryptedMessageDecrypt(msg2, out, sizeof(out), &receiverKey);
    
    if (outLen != sizeof(buf) - 1 || memcmp(buf, out, outLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolEncryptedMessageDecrypt() test 1\n", __func__);
    
    if (msg2) outLen = BRPaymentProtocolEncryptedMessageDecrypt(msg2, out, sizeof(out), &senderKey);
    
    if (outLen != sizeof(buf) - 1 || memcmp(buf, out, outLen) != 0)
        r = 0, fprintf(stderr, "***FAILED*** %s: BRPaymentProtocolEncryptedMessageDecrypt() test 2\n", __func__);
    
    if (msg2) BRPaymentProtocolEncryptedMessageFree(msg2);
    return r;
}

void BRPeerAcceptMessageTest(BRPeer *peer, const uint8_t *msg, size_t len, const char *type);

int BRPeerTests()
{
    int r = 1;
    BRPeer *p = BRPeerNew(BR_CHAIN_PARAMS->magicNumber);
    const char msg[] = "my message";
    
    BRPeerAcceptMessageTest(p, (const uint8_t *)msg, sizeof(msg) - 1, "inv");
    return r;
}

int BRRunTests()
{
    int fail = 0;
    
    printf("BRIntsTests...                      ");
    printf("%s\n", (BRIntsTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRArrayTests...                     ");
    printf("%s\n", (BRArrayTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRSetTests...                       ");
    printf("%s\n", (BRSetTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBase58Tests...                    ");
    printf("%s\n", (BRBase58Tests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBech32Tests...                    ");
    printf("%s\n", (BRBech32Tests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBCashAddrTests...                 ");
    printf("%s\n", (BRBCashAddrTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRHashTests...                      ");
    printf("%s\n", (BRHashTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRMacTests...                       ");
    printf("%s\n", (BRMacTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRDrbgTests...                      ");
    printf("%s\n", (BRDrbgTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRChachaTests...                    ");
    printf("%s\n", (BRChachaTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRAuthEncryptTests...               ");
    printf("%s\n", (BRAuthEncryptTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRAesTests...                       ");
    printf("%s\n", (BRAesTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRKeyTests...                       ");
    printf("%s\n", (BRKeyTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBIP38KeyTests...                  ");
#if SKIP_BIP38
    printf("SKIPPED\n");
#else
    printf("%s\n", (BRBIP38KeyTests()) ? "success" : (fail++, "***FAIL***"));
#endif
    printf("BRKeyECIESTests...                  ");
    printf("%s\n", (BRKeyECIESTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRAddressTests...                   ");
    printf("%s\n", (BRAddressTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBIP39MnemonicTests...             ");
    printf("%s\n", (BRBIP39MnemonicTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBIP32SequenceTests...             ");
    printf("%s\n", (BRBIP32SequenceTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRTransactionTests...               ");
    printf("%s\n", (BRTransactionTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRWalletTests...                    ");
    printf("%s\n", (BRWalletTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRBloomFilterTests...               ");
    printf("%s\n", (BRBloomFilterTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRMerkleBlockTests...               ");
    printf("%s\n", (BRMerkleBlockTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRPaymentProtocolTests...           ");
    printf("%s\n", (BRPaymentProtocolTests()) ? "success" : (fail++, "***FAIL***"));
    printf("BRPaymentProtocolEncryptionTests... ");
    printf("%s\n", (BRPaymentProtocolEncryptionTests()) ? "success" : (fail++, "***FAIL***"));
    printf("\n");
    
    if (fail > 0) printf("%d TEST FUNCTION(S) ***FAILED***\n", fail);
    else printf("ALL TESTS PASSED\n");
    
    return (fail == 0);
}

//
// Rescan // Sync Test
//
typedef struct {
    int syncDone;
    BRPeerManager *pm;
    pthread_mutex_t lock;
} BRRunTestContext;

static void contextSyncInit (BRRunTestContext *context,
                             BRPeerManager *pm) {
    context->syncDone = 0;
    context->pm = pm;
    pthread_mutex_init(&context->lock, NULL);
}

static int contextSyncDone (BRRunTestContext *context) {
    int done;

    pthread_mutex_lock (&context->lock);
    done = context->syncDone;
    pthread_mutex_unlock (&context->lock);

    return done;
}

static void testSyncStoppedX(void *c, int error) {
    BRRunTestContext *context = (BRRunTestContext*) c;
    if (error) printf ("Sync: Error: %d\n", error);
    pthread_mutex_lock (&context->lock);
    context->syncDone = 1;
    pthread_mutex_unlock (&context->lock);
}

static void testSyncSaveBlocks (void *c, int replace, BRMerkleBlock *blocks[], size_t blocksCount) {
    // BRRunTestContext *context = (BRRunTestContext*) c;
    printf ("Sync: saveBlock: %zu, Replace: %s\n", blocksCount, (replace ? "Yes" : "No"));
    uint32_t unixTime =  (uint32_t) time (NULL);

    for (int i = 0; i < blocksCount; i++) {
        BRMerkleBlock *block = blocks[i];
        assert (block->flagsLen < 10000);
        assert (block->timestamp < unixTime);
        /*
        assert (block->version == 2 || block->version == 3 || block->version == 4 ||
                block->version == 0x60000000 ||
                block->version == 0x3fff0000 ||
                block->version == 0x3fffe000 ||
                block->version == 0x30000000 ||
                block->version == 0x20000000 ||
                block->version == 0x20000002 ||
                block->version == 0x20000007 ||
                block->version == 0x2000e000 ||
                block->version == 0x20FFF000 ||
                block->version == 0x7fffe000 ||
                0);
         */
        assert (BRMerkleBlockIsValid(block, unixTime));
    }
}

extern int BRRunTestsSync (const char *paperKey,
                           int isBTC,
                           int isMainnet) {
    const BRChainParams *params = (isBTC & isMainnet ? BRMainNetParams
                                   : (isBTC & !isMainnet ? BRTestNetParams
                                      : (isMainnet ? BRBCashParams : BRBCashTestNetParams)));

    uint32_t epoch;
    int needPaperKey = NULL == paperKey;

    if (needPaperKey) {
        UInt128 entropy;
        arc4random_buf(entropy.u64, sizeof (entropy));

        size_t phraseLen = BRBIP39Encode(NULL, 0, BRBIP39WordsEn, entropy.u8, sizeof(entropy));
        char phrase[phraseLen];
        assert (phraseLen == BRBIP39Encode(phrase, sizeof(phrase), BRBIP39WordsEn, entropy.u8, sizeof(entropy)));
        paperKey = strdup(phrase);
        epoch = (uint32_t) (time (NULL) - (14 * 24 * 60 * 60));
    }
    else {
//        epoch = 1483228800;  // 1/1/2017 // BIP39_CREATION_TIME
//        epoch = 1527811200;  // 06/01/2018
//        epoch = 1541030400;  // 11/01/2018
        epoch = 1543190400;  // 11/26/2018
    }

//    epoch = 0;
    
    printf ("***\n*** PaperKey (Start): \"%s\"\n***\n", paperKey);
    UInt512 seed = UINT512_ZERO;
    BRBIP39DeriveKey (seed.u8, paperKey, NULL);
    BRMasterPubKey mpk = BRBIP32MasterPubKey(&seed, sizeof (seed));

    BRWallet *wallet = BRWalletNew (NULL, 0, mpk, 0x00);
    // BRWalletSetCallbacks

    BRPeerManager *pm = BRPeerManagerNew (params, wallet, epoch, NULL, 0, NULL, 0);

    BRRunTestContext context;
    contextSyncInit (&context, pm);

    BRPeerManagerSetCallbacks (pm, &context, NULL, testSyncStoppedX, NULL, testSyncSaveBlocks, NULL, NULL, NULL);

    BRPeerManagerConnect (pm);

    int err = 0;
    while (err == 0 &&  !contextSyncDone(&context)) {
        if (0.05 == BRPeerManagerSyncProgress (pm, 0))
            usleep(1);
        err = usleep(100000);
    }

    printf ("***\n***\nPaperKey (Done): \"%s\"\n***\n***\n", paperKey);
    BRPeerManagerDisconnect(pm);
    BRPeerManagerFree(pm);
    BRWalletFree(wallet);
    if (needPaperKey) free ((char *) paperKey);
    return 1;
}

///
///
///

static void
_testTransactionEventCallback (BRWalletManager manager,
                                BRWallet *wallet,
                                BRTransaction *transaction,
                                BRTransactionEvent event) {
    printf ("TST: TransactionEvent: %d\n", event.type);
}

static void
_testWalletEventCallback (BRWalletManager manager,
                          BRWallet *wallet,
                          BRWalletEvent event) {
    printf ("TST: WalletEvent: %d\n", event.type);
}

static int syncDone = 0;

static void
_testWalletManagerEventCallback (BRWalletManager manager,
                                 BRWalletManagerEvent event) {
    printf ("TST: WalletManagerEvent: %d\n", event.type);
    switch (event.type) {

        case BITCOIN_WALLET_MANAGER_CONNECTED:
             break;
        case BITCOIN_WALLET_MANAGER_SYNC_STARTED:
             break;
        case BITCOIN_WALLET_MANAGER_SYNC_STOPPED:
            syncDone = 1;
            break;
        default:
            break;
    }
}


extern int BRRunTestWalletManagerSync (const char *paperKey,
                                       const char *storagePath,
                                       int isBTC,
                                       int isMainnet) {
    const BRChainParams *params = (isBTC & isMainnet ? BRMainNetParams
                                   : (isBTC & !isMainnet ? BRTestNetParams
                                      : (isMainnet ? BRBCashParams : BRBCashTestNetParams)));

    uint32_t epoch = 1483228800; // 1/1/17
    epoch += (365 + 365/2) * 24 * 60 * 60;

    printf ("***\n***\nPaperKey (Start): \"%s\"\n***\n***\n", paperKey);
    UInt512 seed = UINT512_ZERO;
    BRBIP39DeriveKey (seed.u8, paperKey, NULL);
    BRMasterPubKey mpk = BRBIP32MasterPubKey(&seed, sizeof (seed));

    BRWalletManagerClient client = {
        _testTransactionEventCallback,
        _testWalletEventCallback,
        _testWalletManagerEventCallback
    };

    BRWalletManager manager = BRWalletManagerNew (client, mpk, params, epoch, storagePath);

    BRPeerManager *pm = BRWalletManagerGetPeerManager(manager);

    syncDone = 0;
    BRPeerManagerConnect (pm);

    int err = 0;
//    while (err == 0 && !syncDone) {
//        err = sleep(1);
//    }
//    err = 0;

    int seconds = 300;
    while (err == 0 && seconds-- > 0) {
        err = sleep(1);
    }

    printf ("***\n***\nPaperKey (Done): \"%s\"\n***\n***\n", paperKey);
    BRPeerManagerDisconnect(pm);
    sleep (2);
    BRWalletManagerFree (manager);
    return 1;
}

#ifndef BITCOIN_TEST_NO_MAIN
void syncStarted(void *info)
{
    printf("sync started\n");
}

void syncStopped(void *info, int error)
{
    printf("sync stopped: %s\n", strerror(error));
}

void txStatusUpdate(void *info)
{
    printf("transaction status updated\n");
}

void saveBlocks(void *info, int replace, BRMerkleBlock *blocks[], size_t blocksCount)
{
    printf("BLOCKS: %lu\n", blocksCount);
    if (blocksCount)
    {
        FILE * f = fopen("blocks", "a");
        for (size_t i = 0; i < blocksCount; ++i)
        {
//            BRMerkleBlock *block = BRMerkleBlockCopy(blocks[i]);
            size_t len = BRMerkleBlockSerialize(blocks[i], NULL, 0);
            BRMerkleBlock *buf = calloc(1, len);
            BRMerkleBlockSerialize(blocks[i], NULL, 0);
        }

    }

}

int main(int argc, const char *argv[])
{
//    int r = BRRunTests();

    BRMainNetCheckpoints[0] = (BRCheckPoint) {      0, toUInt256("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"), 1231006505, 0x1d00ffff };
    BRMainNetCheckpoints[1] = (BRCheckPoint) { 584640, toUInt256("0000000000000000000e5af6f531133eb548fe3854486ade75523002a1a27687"), 1562663868, 0x171f0d9b };


    BRRunTestWalletManagerSync("axis husband project any sea patch drip tip spirit tide bring belt", "./testspv/", 1, 1);
    printf("Rerun sync for 60 seconds\n");
    sleep(5);
    BRRunTestWalletManagerSync("axis husband project any sea patch drip tip spirit tide bring belt", "./testspv/", 1, 1);

    
////    BRMainNetCheckpoints[0].height = 0;
////    BRMainNetCheckpoints[0].hash = toUInt256("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
////    BRMainNetCheckpoints[0].timestamp = 1231006505;
////    BRMainNetCheckpoints[0].target = 0x1d00ffff;

////    BRTestNetCheckpoints[0].height = 0;
////    BRTestNetCheckpoints[0].hash = toUInt256("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943");
////    BRTestNetCheckpoints[0].timestamp = 1296688602;
////    BRTestNetCheckpoints[0].target = 0x1d00ffff;

//    int err = 0;
//    UInt512 seed = UINT512_ZERO;
//    BRMasterPubKey mpk = BR_MASTER_PUBKEY_NONE;
//    BRWallet *wallet;
//    BRPeerManager *manager;

//    //BRBIP39DeriveKey(seed.u8, "video tiger report bid suspect taxi mail argue naive layer metal surface", NULL);
//    BRBIP39DeriveKey(seed.u8, "axis husband project any sea patch drip tip spirit tide bring belt", NULL);
//    mpk = BRBIP32MasterPubKey(&seed, sizeof(seed));

//    wallet = BRWalletNew(NULL, 0, mpk, 0);
//    BRWalletSetCallbacks(wallet, wallet, walletBalanceChanged, walletTxAdded, walletTxUpdated, walletTxDeleted);
//    printf("wallet created with first receive address: %s\n", BRWalletLegacyAddress(wallet).s);

////    BRPeer* peers;
////    array_new(peers, 10);
////    manager = BRPeerManagerNew(&BRMainNetParams, wallet, BIP39_CREATION_TIME, NULL, 0, NULL, 0);
//    manager = BRPeerManagerNew(BRMainNetParams, wallet, BIP39_CREATION_TIME, NULL, 0, NULL, 0);
//    BRPeerManagerSetCallbacks(manager, manager, syncStarted, syncStopped, txStatusUpdate, saveBlocks, NULL, NULL, NULL);

////    BRPeerManagerRescanFromBlockNumber(manager, 600000);
//    BRPeerManagerConnect(manager);
//    while (err == 0 && BRPeerManagerPeerCount(manager) > 0) err = sleep(1);
//    if (err != 0) printf("sleep got a signal\n");

//    BRPeerManagerDisconnect(manager);
//    BRPeerManagerFree(manager);
//    BRWalletFree(wallet);
//    sleep(5);
    
//    return (r) ? 0 : 1;
    return 0;
}
#endif

//
//  BRChainParams.c
//  BRCore
//
//  Created by Aaron Voisine on 3/11/19.
//  Copyright (c) 2019 breadwallet LLC
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

//

#include "BRChainParams.h"

static const char *BRMainNetDNSSeeds[] = {
    "seed.breadwallet.com.", "seed.bitcoin.sipa.be.", "dnsseed.bluematt.me.", "dnsseed.bitcoin.dashjr.org.",
    "seed.bitcoinstats.com.", "bitseed.xf2.org.", "seed.bitcoin.jonasschnelli.ch.", NULL
};

static const char *BRTestNetDNSSeeds[] = {
    "testnet-seed.breadwallet.com.", "testnet-seed.bitcoin.petertodd.org.", "testnet-seed.bluematt.me.",
    "testnet-seed.bitcoin.schildbach.de.", NULL
};

// blockchain checkpoints - these are also used as starting points for partial chain downloads, so they must be at
// difficulty transition boundaries in order to verify the block difficulty at the immediately following transition
BRCheckPoint BRMainNetCheckpoints[30];
/*
= {
    {      0, toUInt256("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"), 1231006505, 0x1d00ffff },
    {  20160, toUInt256("000000000f1aef56190aee63d33a373e6487132d522ff4cd98ccfc96566d461e"), 1248481816, 0x1d00ffff },
    {  40320, toUInt256("0000000045861e169b5a961b7034f8de9e98022e7a39100dde3ae3ea240d7245"), 1266191579, 0x1c654657 },
    {  60480, toUInt256("000000000632e22ce73ed38f46d5b408ff1cff2cc9e10daaf437dfd655153837"), 1276298786, 0x1c0eba64 },
    {  80640, toUInt256("0000000000307c80b87edf9f6a0697e2f01db67e518c8a4d6065d1d859a3a659"), 1284861847, 0x1b4766ed },
    { 100800, toUInt256("000000000000e383d43cc471c64a9a4a46794026989ef4ff9611d5acb704e47a"), 1294031411, 0x1b0404cb },
    { 120960, toUInt256("0000000000002c920cf7e4406b969ae9c807b5c4f271f490ca3de1b0770836fc"), 1304131980, 0x1b0098fa },
    { 141120, toUInt256("00000000000002d214e1af085eda0a780a8446698ab5c0128b6392e189886114"), 1313451894, 0x1a094a86 },
    { 161280, toUInt256("00000000000005911fe26209de7ff510a8306475b75ceffd434b68dc31943b99"), 1326047176, 0x1a0d69d7 },
    { 181440, toUInt256("00000000000000e527fc19df0992d58c12b98ef5a17544696bbba67812ef0e64"), 1337883029, 0x1a0a8b5f },
    { 201600, toUInt256("00000000000003a5e28bef30ad31f1f9be706e91ae9dda54179a95c9f9cd9ad0"), 1349226660, 0x1a057e08 },
    { 221760, toUInt256("00000000000000fc85dd77ea5ed6020f9e333589392560b40908d3264bd1f401"), 1361148470, 0x1a04985c },
    { 241920, toUInt256("00000000000000b79f259ad14635739aaf0cc48875874b6aeecc7308267b50fa"), 1371418654, 0x1a00de15 },
    { 262080, toUInt256("000000000000000aa77be1c33deac6b8d3b7b0757d02ce72fffddc768235d0e2"), 1381070552, 0x1916b0ca },
    { 282240, toUInt256("0000000000000000ef9ee7529607286669763763e0c46acfdefd8a2306de5ca8"), 1390570126, 0x1901f52c },
    { 302400, toUInt256("0000000000000000472132c4daaf358acaf461ff1c3e96577a74e5ebf91bb170"), 1400928750, 0x18692842 },
    { 322560, toUInt256("000000000000000002df2dd9d4fe0578392e519610e341dd09025469f101cfa1"), 1411680080, 0x181fb893 },
    { 342720, toUInt256("00000000000000000f9cfece8494800d3dcbf9583232825da640c8703bcd27e7"), 1423496415, 0x1818bb87 },
    { 362880, toUInt256("000000000000000014898b8e6538392702ffb9450f904c80ebf9d82b519a77d5"), 1435475246, 0x1816418e },
    { 383040, toUInt256("00000000000000000a974fa1a3f84055ad5ef0b2f96328bc96310ce83da801c9"), 1447236692, 0x1810b289 },
    { 403200, toUInt256("000000000000000000c4272a5c68b4f55e5af734e88ceab09abf73e9ac3b6d01"), 1458292068, 0x1806a4c3 },
    { 423360, toUInt256("000000000000000001630546cde8482cc183708f076a5e4d6f51cd24518e8f85"), 1470163842, 0x18057228 },
    { 443520, toUInt256("00000000000000000345d0c7890b2c81ab5139c6e83400e5bed00d23a1f8d239"), 1481765313, 0x18038b85 },
    { 463680, toUInt256("000000000000000000431a2f4619afe62357cd16589b638bb638f2992058d88e"), 1493259601, 0x18021b3e },
    { 483840, toUInt256("0000000000000000008e5d72027ef42ca050a0776b7184c96d0d4b300fa5da9e"), 1504704195, 0x1801310b },
    { 504000, toUInt256("0000000000000000006cd44d7a940c79f94c7c272d159ba19feb15891aa1ea54"), 1515827554, 0x177e578c },
    { 524160, toUInt256("00000000000000000009d1e9bee76d334347060c6a2985d6cbc5c22e48f14ed2"), 1527168053, 0x17415a49 },
    { 544320, toUInt256("0000000000000000000a5e9b5e4fbee51f3d53f31f40cd26b8e59ef86acb2ebd"), 1538639362, 0x1725c191 },
    { 564480, toUInt256("0000000000000000002567dc317da20ddb0d7ef922fe1f9c2375671654f9006c"), 1551026038, 0x172e5b50 },
    { 584640, toUInt256("0000000000000000000e5af6f531133eb548fe3854486ade75523002a1a27687"), 1562663868, 0x171f0d9b }
//{ 604800,
};
*/
BRCheckPoint BRTestNetCheckpoints[17];
/*
= {
    {       0, toUInt256("000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943"), 1296688602, 0x1d00ffff },
    {  100800, toUInt256("0000000000a33112f86f3f7b0aa590cb4949b84c2d9c673e9e303257b3be9000"), 1376543922, 0x1c00d907 },
    {  201600, toUInt256("0000000000376bb71314321c45de3015fe958543afcbada242a3b1b072498e38"), 1393813869, 0x1b602ac0 },
    {  302400, toUInt256("0000000000001c93ebe0a7c33426e8edb9755505537ef9303a023f80be29d32d"), 1413766239, 0x1a33605e },
    {  403200, toUInt256("0000000000ef8b05da54711e2106907737741ac0278d59f358303c71d500f3c4"), 1431821666, 0x1c02346c },
    {  504000, toUInt256("0000000000005d105473c916cd9d16334f017368afea6bcee71629e0fcf2f4f5"), 1436951946, 0x1b00ab86 },
    {  604800, toUInt256("00000000000008653c7e5c00c703c5a9d53b318837bb1b3586a3d060ce6fff2e"), 1447484641, 0x1a092a20 },
    {  705600, toUInt256("00000000004ee3bc2e2dd06c31f2d7a9c3e471ec0251924f59f222e5e9c37e12"), 1455728685, 0x1c0ffff0 },
    {  806400, toUInt256("0000000000000faf114ff29df6dbac969c6b4a3b407cd790d3a12742b50c2398"), 1462006183, 0x1a34e280 },
    {  907200, toUInt256("0000000000166938e6f172a21fe69fe335e33565539e74bf74eeb00d2022c226"), 1469705562, 0x1c00ffff },
    { 1008000, toUInt256("000000000000390aca616746a9456a0d64c1bd73661fd60a51b5bf1c92bae5a0"), 1476926743, 0x1a52ccc0 },
    { 1108800, toUInt256("00000000000288d9a219419d0607fb67cc324d4b6d2945ca81eaa5e739fab81e"), 1490751239, 0x1b09ecf0 },
    { 1209600, toUInt256("0000000000000026b4692a26f1651bec8e9d4905640bd8e56056c9a9c53badf8"), 1507328506, 0x1973e180 },
    { 1310400, toUInt256("0000000000013b434bbe5668293c92ef26df6d6d4843228e8958f6a3d8101709"), 1527038604, 0x1b0ffff0 },
    { 1411200, toUInt256("00000000000000008b3baea0c3de24b9333c169e1543874f4202397f5b8502cb"), 1535535770, 0x194ac105 }
    //{ 1512000,
};
*/
static int BRMainNetVerifyDifficulty(const BRMerkleBlock *block, const BRSet *blockSet)
{
    const BRMerkleBlock *previous, *b = NULL;
    uint32_t i;

    assert(block != NULL);
    assert(blockSet != NULL);

    // check if we hit a difficulty transition, and find previous transition block
    if ((block->height % BLOCK_DIFFICULTY_INTERVAL) == 0) {
        for (i = 0, b = block; b && i < BLOCK_DIFFICULTY_INTERVAL; i++) {
            b = BRSetGet(blockSet, &b->prevBlock);
        }
    }

    previous = BRSetGet(blockSet, &block->prevBlock);
    return BRMerkleBlockVerifyDifficulty(block, previous, (b) ? b->timestamp : 0);
}

static int BRTestNetVerifyDifficulty(const BRMerkleBlock *block, const BRSet *blockSet)
{
    return 1; // XXX skip testnet difficulty check for now
}

static const char BRMainBip32xprv[] = "\x04\x88\xAD\xE4";
static const char BRMainBip32xpub[] = "\x04\x88\xB2\x1E";
static const char BRMainBech32[] = "bc";

static const char BRTestBip32xprv[] = "\x04\x35\x83\x94";
static const char BRTestBip32xpub[] = "\x04\x35\x87\xCF";
static const char BRTestBech32[] = "tb";

static const BRChainParams BRMainNetParamsRecord = {
    BRMainNetDNSSeeds,
    8333,                  // standardPort
    0xd9b4bef9,            // magicNumber
    SERVICES_NODE_WITNESS, // services
    BRMainNetVerifyDifficulty,
    BRMainNetCheckpoints,
    sizeof(BRMainNetCheckpoints)/sizeof(*BRMainNetCheckpoints),
    128,
    0,
    5,
    BRMainBip32xprv,
    BRMainBip32xpub,
    BRMainBech32
};
const BRChainParams *BRMainNetParams = &BRMainNetParamsRecord;

static const BRChainParams BRTestNetParamsRecord = {
    BRTestNetDNSSeeds,
    18333,                 // standardPort
    0x0709110b,            // magicNumber
    SERVICES_NODE_WITNESS, // services
    BRTestNetVerifyDifficulty,
    BRTestNetCheckpoints,
    sizeof(BRTestNetCheckpoints)/sizeof(*BRTestNetCheckpoints),
    239,
    111,
    196,
    BRTestBip32xprv,
    BRTestBip32xpub,
    BRTestBech32
};
const BRChainParams *BRTestNetParams = &BRTestNetParamsRecord;

int spv_mainnet = 1;


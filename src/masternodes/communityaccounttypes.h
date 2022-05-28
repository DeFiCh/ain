// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MASTERNODES_COMMUNITYACCOUNTTYPES_H
#define BITCOIN_MASTERNODES_COMMUNITYACCOUNTTYPES_H

enum class CommunityAccountType : unsigned char
{
    None = 0,
    IncentiveFunding    = 'I', // or 'yield farming' - source of community rewards for LP (Liquidity Pools)
    AnchorReward        = 'A',
    Loan                = 'L',
    Options             = 'O',
    Unallocated         = 'U',
};

#endif //BITCOIN_MASTERNODES_COMMUNITYACCOUNTTYPES_H

// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_COMMUNITYACCOUNTTYPES_H
#define DEFI_MASTERNODES_COMMUNITYACCOUNTTYPES_H

enum class CommunityAccountType : unsigned char
{
    None = 0,
    IncentiveFunding    = 'I', // or 'yield farming' - source of community rewards for LP (Liquidity Pools)
    AnchorReward        = 'A',
    CommunityDevFunds   = 'C',
    Loan                = 'L',
    Options             = 'O',
    Unallocated         = 'U',
};

#endif //DEFI_MASTERNODES_COMMUNITYACCOUNTTYPES_H

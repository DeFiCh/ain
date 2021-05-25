// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_INCENTIVEFUNDING_H
#define DEFI_MASTERNODES_INCENTIVEFUNDING_H

#include <flushablestorage.h>
#include <masternodes/communityaccounttypes.h>
#include <masternodes/res.h>
#include <amount.h>

inline CommunityAccountType CommunityAccountCodeToType (unsigned char ch) {
    char const types[] = "IASFOU";
    if (memchr(types, ch, strlen(types)))
        return static_cast<CommunityAccountType>(ch);
    else
        return CommunityAccountType::None;
}

inline char const * GetCommunityAccountName(CommunityAccountType t)
{
    switch (t)
    {
        case CommunityAccountType::IncentiveFunding: return "IncentiveFunding";
        case CommunityAccountType::AnchorReward:     return "AnchorReward";
        case CommunityAccountType::Swap:             return "Swap";
        case CommunityAccountType::Futures:          return "Futures";
        case CommunityAccountType::Options:          return "Options";
        case CommunityAccountType::Unallocated:      return "Burnt";
        default:
            return "Unknown";
    }
}

class CCommunityBalancesView : public virtual CStorageView
{
public:
    CAmount GetCommunityBalance(CommunityAccountType account) const;
    Res SetCommunityBalance(CommunityAccountType account, CAmount amount);

    void ForEachCommunityBalance(std::function<bool(CommunityAccountType, CLazySerialize<CAmount>)> callback);

    Res AddCommunityBalance(CommunityAccountType account, CAmount amount);
    Res SubCommunityBalance(CommunityAccountType account, CAmount amount);

    // tags
    struct ById { static const unsigned char prefix; };
};

#endif //DEFI_MASTERNODES_INCENTIVEFUNDING_H

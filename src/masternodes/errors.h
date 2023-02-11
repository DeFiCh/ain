// Copyright (c) 2023 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_ERRORS_H
#define DEFI_ERRORS_H

#include <amount.h>
#include <masternodes/res.h>

class DeFiErrors {
public:
    static Res MNInvalid(const std::string &nodeRefString) { 
        return Res::Err("node %s does not exists", nodeRefString);
    }

    static Res MNInvalidAltMsg(const std::string &nodeRefString) { 
        return Res::Err("masternode %s does not exist", nodeRefString);
    }

    static Res MNStateNotEnabled(const std::string &nodeRefString) { 
        return Res::Err("Masternode %s is not in 'ENABLED' state", nodeRefString);
    }

    static Res ICXBTCBelowMinSwap(const CAmount amount, const CAmount minSwap) {
        // TODO: Change error in later version to include amount. Retaining old msg for compatibility
        return Res::Err("Below minimum swapable amount, must be at least %s BTC", GetDecimaleString(minSwap));
    }
};

#endif  // DEFI_ERRORS_H


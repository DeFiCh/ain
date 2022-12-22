// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_PARAMS_H
#define DEFI_MASTERNODES_PARAMS_H

#include <script/script.h>

#include <map>
#include <string>

const auto SMART_CONTRACT_DFIP_2201 = "DFIP2201";
const auto SMART_CONTRACT_DFIP_2203 = "DFIP2203";
const auto SMART_CONTRACT_DFIP2206F = "DFIP2206F";

namespace DeFiConsensus {
    struct Params {
        std::map<std::string, CScript> smartContracts;
    };
}

class CDeFiParams
{
public:
    const DeFiConsensus::Params& GetConsensus() const { return consensus; }

protected:
    DeFiConsensus::Params consensus;
};

/**
 * Return the currently selected parameters.
 */
const CDeFiParams &DeFiParams();

/**
 * Sets the params returned by DeFiParams().
 */
void SelectDeFiParams(const std::string &chain);

#endif  // DEFI_MASTERNODES_PARAMS_H

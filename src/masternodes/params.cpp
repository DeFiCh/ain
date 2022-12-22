// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/params.h>

#include <chainparamsbase.h>
#include <script/standard.h>

#include <vector>

class CMainnetParams : public CDeFiParams {
public:
    CMainnetParams() {
        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));
    }
};

class CTestnetParams : public CDeFiParams {
public:
    CTestnetParams() {
        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));

    }
};

class CDevnetParams : public CDeFiParams {
public:
    CDevnetParams() {
        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));
    }
};

class CRegtestParams : public CDeFiParams {
public:
    explicit CRegtestParams() {
        consensus.smartContracts.clear();
        consensus.smartContracts[SMART_CONTRACT_DFIP_2201] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0})));
        consensus.smartContracts[SMART_CONTRACT_DFIP_2203] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1})));
        consensus.smartContracts[SMART_CONTRACT_DFIP2206F] = GetScriptForDestination(CTxDestination(WitnessV0KeyHash(std::vector<unsigned char>{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2})));
    }
};

static std::unique_ptr<const CDeFiParams> defiChainParams;

std::unique_ptr<const CDeFiParams> CreateDeFiChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CDeFiParams>(new CMainnetParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CDeFiParams>(new CTestnetParams());
    else if (chain == CBaseChainParams::DEVNET)
        return std::unique_ptr<CDeFiParams>(new CDevnetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CDeFiParams>(new CRegtestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

const CDeFiParams &DeFiParams() {
    assert(defiChainParams);
    return *defiChainParams;
}

void SelectDeFiParams(const std::string& network) {
    defiChainParams = CreateDeFiChainParams(network);
}

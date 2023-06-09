// Copyright (c) 2014-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_CHAINPARAMSBASE_H
#define DEFI_CHAINPARAMSBASE_H

#include <memory>
#include <string>

/**
 * CBaseChainParams defines the base parameters (shared between defi-cli and defid)
 * of a given instance of the DeFi Blockchain system.
 */
class CBaseChainParams
{
public:
    /** BIP70 chain name strings (main, test or regtest) */
    static const std::string MAIN;
    static const std::string TESTNET;
    static const std::string CHANGI;
    static const std::string DEVNET;
    static const std::string REGTEST;

    const std::string& DataDir() const { return strDataDir; }
    int RPCPort() const { return nRPCPort; }
    int GRPCPort() const { return nGRPCPort; }
    int ETHRPCPort() const { return nETHRPCPort; }

    CBaseChainParams() = delete;
    CBaseChainParams(const std::string& data_dir, int rpc_port, int grpc_port, int ethrpc_port) : nRPCPort(rpc_port), nGRPCPort(grpc_port), nETHRPCPort(ethrpc_port), strDataDir(data_dir) {}

private:
    int nRPCPort;
    int nGRPCPort;
    int nETHRPCPort;
    std::string strDataDir;
};

/**
 * Creates and returns a std::unique_ptr<CBaseChainParams> of the chosen chain.
 * @returns a CBaseChainParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const std::string& chain);

/**
 *Set the arguments for chainparams
 */
void SetupChainParamsBaseOptions();

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CBaseChainParams& BaseParams();

/** Sets the params returned by Params() to those for the given network. */
void SelectBaseParams(const std::string& chain);

#endif // DEFI_CHAINPARAMSBASE_H

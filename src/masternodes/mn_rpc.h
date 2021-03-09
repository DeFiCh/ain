#ifndef DEFI_MASTERNODES_MN_RPC_H
#define DEFI_MASTERNODES_MN_RPC_H

#include <arith_uint256.h>
#include <univalue.h>

#include <chainparams.h>
#include <core_io.h>
#include <validation.h>

#include <masternodes/criminals.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>

#include <rpc/rawtransaction_util.h>
#include <rpc/server.h>
#include <rpc/util.h>

//#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/rpcwallet.h>
//#endif

typedef enum {
    // selecting accounts without sorting
    SelectionForward,
    // selecting accounts by ascending of sum token amounts
    // it means that we select first accounts with min sum of
    // neccessary token amounts
    SelectionCrumbs,
    // selecting accounts by descending of sum token amounts
    // it means that we select first accounts with max sum of
    // neccessary token amounts
    SelectionPie,
} AccountSelectionMode;

// Special guarding object. Should be created before the first use of funding (straight or by GetAuthInputsSmart())
struct LockedCoinsScopedGuard {
    CWallet* const pwallet;
    std::set<COutPoint> lockedCoinsBackup;

    LockedCoinsScopedGuard(CWallet* const pwl) : pwallet(pwl)
    {
        LOCK(pwallet->cs_wallet);
        lockedCoinsBackup = pwallet->setLockedCoins;
    }

    ~LockedCoinsScopedGuard()
    {
        LOCK(pwallet->cs_wallet);
        if (lockedCoinsBackup.empty()) {
            pwallet->UnlockAllCoins();
        } else {
            std::vector<COutPoint> diff;
            std::set_difference(pwallet->setLockedCoins.begin(), pwallet->setLockedCoins.end(), lockedCoinsBackup.begin(), lockedCoinsBackup.end(), std::back_inserter(diff));
            for (auto const& coin : diff) {
                pwallet->UnlockCoin(coin);
            }
        }
    }
};

// common functions
int chainHeight(interfaces::Chain::Lock& locked_chain);
std::vector<CTxIn> GetInputs(UniValue const& inputs);
CMutableTransaction fund(CMutableTransaction& mtx, CWallet* const pwallet, CTransactionRef optAuthTx, CCoinControl* coin_control = nullptr, bool lockUnspents = false);
CTransactionRef sign(CMutableTransaction& mtx, CWallet* const pwallet, CTransactionRef optAuthTx);
CTransactionRef send(CTransactionRef tx, CTransactionRef optAuthTx);
CTransactionRef signsend(CMutableTransaction& mtx, CWallet* const pwallet, CTransactionRef optAuthTx /* = {}*/);
CWallet* GetWallet(const JSONRPCRequest& request);
bool GetCustomTXInfo(const int nHeight, const CTransactionRef tx, CustomTxType& guess, Res& res, UniValue& txResults);
std::vector<CTxIn> GetAuthInputsSmart(CWallet* const pwallet, int32_t txVersion, std::set<CScript>& auths, bool needFounderAuth, CTransactionRef& optAuthTx, UniValue const& explicitInputs);
std::string ScriptToString(CScript const& script);
CAccounts GetAllMineAccounts(CWallet* const pwallet);
CAccounts SelectAccountsByTargetBalances(const CAccounts& accounts, const CBalances& targetBalances, AccountSelectionMode selectionMode);

#endif // DEFI_MASTERNODES_MN_RPC_H

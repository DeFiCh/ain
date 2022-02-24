#ifndef DEFI_MASTERNODES_MN_RPC_H
#define DEFI_MASTERNODES_MN_RPC_H

#include <arith_uint256.h>
#include <univalue.h>

#include <chainparams.h>
#include <core_io.h>
#include <validation.h>

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

class CWalletCoinsUnlocker {
    std::shared_ptr<CWallet> pwallet;
    std::vector<COutPoint> coins;
public:
    explicit CWalletCoinsUnlocker(std::shared_ptr<CWallet> pwallet);
    CWalletCoinsUnlocker(const CWalletCoinsUnlocker&) = delete;
    CWalletCoinsUnlocker(CWalletCoinsUnlocker&&) = default;
    ~CWalletCoinsUnlocker();
    CWallet* operator->();
    CWallet& operator*();
    operator CWallet*();
    void AddLockedCoin(const COutPoint& coin);
};

// common functions
bool IsSkippedTx(const uint256& hash);
CMutableTransaction fund(CMutableTransaction& mtx, CWalletCoinsUnlocker& pwallet, CTransactionRef optAuthTx, CCoinControl* coin_control = nullptr);
CTransactionRef signsend(CMutableTransaction& mtx, CWalletCoinsUnlocker& pwallet, CTransactionRef optAuthTx);
CWalletCoinsUnlocker GetWallet(const JSONRPCRequest& request);
std::vector<CTxIn> GetAuthInputsSmart(CWalletCoinsUnlocker& pwallet, int32_t txVersion, std::set<CScript>& auths, bool needFounderAuth, CTransactionRef& optAuthTx, UniValue const& explicitInputs);
std::string ScriptToString(CScript const& script);
CAccounts GetAllMineAccounts(CWallet* const pwallet);
CAccounts SelectAccountsByTargetBalances(const CAccounts& accounts, const CBalances& targetBalances, AccountSelectionMode selectionMode);
void execTestTx(const CTransaction& tx, uint32_t height, CTransactionRef optAuthTx = {});
CScript CreateScriptForHTLC(const JSONRPCRequest& request, uint32_t &blocks, std::vector<unsigned char>& image);
CPubKey PublickeyFromString(const std::string &pubkey);

#endif // DEFI_MASTERNODES_MN_RPC_H

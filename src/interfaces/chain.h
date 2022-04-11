// Copyright (c) 2018-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_INTERFACES_CHAIN_H
#define DEFI_INTERFACES_CHAIN_H

#include <primitives/transaction.h> // For CTransactionRef
#include <sync.h>

#include <functional>
#include <memory>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

class CBlock;
class CFeeRate;
class CMasternode;
class CRPCCommand;
class CScheduler;
class CTokenImplementation;
class CValidationState;
class Coin;
class uint256;
enum class RBFTransactionState;
struct CBlockLocator;
struct FeeCalculation;

namespace interfaces {

class Handler;

//! Interface giving clients (wallet processes, maybe other analysis tools in
//! the future) ability to access to the chain state, receive notifications,
//! estimate fees, and submit transactions.
//!
//! TODO: Current chain methods are too low level, exposing too much of the
//! internal workings of the DeFi Blockchain node, and not being very convenient to use.
//! Chain methods should be cleaned up and simplified over time. Examples:
//!
//! * The Chain::lock() method, which lets clients delay chain tip updates
//!   should be removed when clients are able to respond to updates
//!   asynchronously
//!   (https://github.com/bitcoin/bitcoin/pull/10973#issuecomment-380101269).
//!
//! * The initMessages() and loadWallet() methods which the wallet uses to send
//!   notifications to the GUI should go away when GUI and wallet can directly
//!   communicate with each other without going through the node
//!   (https://github.com/bitcoin/bitcoin/pull/15288#discussion_r253321096).
//!
//! * The handleRpc, registerRpcs, rpcEnableDeprecated methods and other RPC
//!   methods can go away if wallets listen for HTTP requests on their own
//!   ports instead of registering to handle requests on the node HTTP port.
class Chain
{
public:
    virtual ~Chain() {}

    //! Interface for querying locked chain state, used by legacy code that
    //! assumes state won't change between calls. New code should avoid using
    //! the Lock interface and instead call higher-level Chain methods
    //! that return more information so the chain doesn't need to stay locked
    //! between calls.
    class Lock
    {
    public:
        virtual ~Lock() {}

        //! Get current chain height, not including genesis block (returns 0 if
        //! chain only contains genesis block, nullopt if chain does not contain
        //! any blocks).
        virtual std::optional<int> getHeight() = 0;

        //! Get block height above genesis block. Returns 0 for genesis block,
        //! 1 for following block, and so on. Returns nullopt for a block not
        //! included in the current chain.
        virtual std::optional<int> getBlockHeight(const uint256& hash) = 0;

        //! Get block depth. Returns 1 for chain tip, 2 for preceding block, and
        //! so on. Returns 0 for a block not included in the current chain.
        virtual int getBlockDepth(const uint256& hash) = 0;

        //! Get block hash. Height must be valid or this function will abort.
        virtual uint256 getBlockHash(int height) = 0;

        //! Get block time. Height must be valid or this function will abort.
        virtual int64_t getBlockTime(int height) = 0;

        //! Get block median time past. Height must be valid or this function
        //! will abort.
        virtual int64_t getBlockMedianTimePast(int height) = 0;

        //! Check that the block is available on disk (i.e. has not been
        //! pruned), and contains transactions.
        virtual bool haveBlockOnDisk(int height) = 0;

        //! Return height of the first block in the chain with timestamp equal
        //! or greater than the given time and height equal or greater than the
        //! given height, or nullopt if there is no block with a high enough
        //! timestamp and height. Also return the block hash as an optional output parameter
        //! (to avoid the cost of a second lookup in case this information is needed.)
        virtual std::optional<int> findFirstBlockWithTimeAndHeight(int64_t time, int height, uint256* hash) = 0;

        //! Return height of last block in the specified range which is pruned, or
        //! nullopt if no block in the range is pruned. Range is inclusive.
        virtual std::optional<int> findPruned(int start_height = 0, std::optional<int> stop_height = std::nullopt) = 0;

        //! Return height of the specified block if it is on the chain, otherwise
        //! return the height of the highest block on chain that's an ancestor
        //! of the specified block, or nullopt if there is no common ancestor.
        //! Also return the height of the specified block as an optional output
        //! parameter (to avoid the cost of a second hash lookup in case this
        //! information is desired).
        virtual std::optional<int> findFork(const uint256& hash, std::optional<int>* height) = 0;

        //! Get locator for the current chain tip.
        virtual CBlockLocator getTipLocator() = 0;

        //! Return height of the highest block on chain in common with the locator,
        //! which will either be the original block used to create the locator,
        //! or one of its ancestors.
        virtual std::optional<int> findLocatorFork(const CBlockLocator& locator) = 0;

        //! Check if transaction will be final given chain height current time.
        virtual bool checkFinalTx(const CTransaction& tx) = 0;

        //! Assosiated mutex accessor
        virtual CCriticalSection& mutex() = 0;
    };

    //! Return Lock interface. Chain is NOT locked when this is called,
    //! it's defered to caller
    virtual std::unique_ptr<Lock> lock() = 0;

    //! Return whether node has the block and optionally return block metadata
    //! or contents.
    //!
    //! If a block pointer is provided to retrieve the block contents, and the
    //! block exists but doesn't have data (for example due to pruning), the
    //! block will be empty and all fields set to null.
    virtual bool findBlock(const uint256& hash,
        CBlock* block = nullptr,
        int64_t* time = nullptr,
        int64_t* max_time = nullptr) = 0;

    //! Look up unspent output information. Returns coins in the mempool and in
    //! the current chain UTXO set. Iterates through all the keys in the map and
    //! populates the values.
    virtual void findCoins(std::map<COutPoint, Coin>& coins) = 0;

    virtual bool mnCanSpend(const uint256 & nodeId, int height) const = 0;
    virtual std::optional<CMasternode> mnExists(const uint256 & nodeId) const = 0;
    virtual std::optional<CTokenImplementation> existTokenGuessId(const std::string & str, DCT_ID & id) const = 0;

    //! Estimate fraction of total transactions verified if blocks up to
    //! the specified block hash are verified.
    virtual double guessVerificationProgress(const uint256& block_hash) = 0;

    //! Check if transaction is RBF opt in.
    virtual RBFTransactionState isRBFOptIn(const CTransaction& tx) = 0;

    //! Check if transaction has descendants in mempool.
    virtual bool hasDescendantsInMempool(const uint256& txid) = 0;

    //! Transaction is added to memory pool, if the transaction fee is below the
    //! amount specified by max_tx_fee, and broadcast to all peers if relay is set to true.
    //! Return false if the transaction could not be added due to the fee or for another reason.
    virtual bool broadcastTransaction(const CTransactionRef& tx, std::string& err_string, const CAmount& max_tx_fee, bool relay) = 0;

    //! Calculate mempool ancestor and descendant counts for the given transaction.
    virtual void getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) = 0;

    //! Check if transaction will pass the mempool's chain limits.
    virtual bool checkChainLimits(const CTransactionRef& tx) = 0;

    //! Estimate smart fee.
    virtual CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc = nullptr) = 0;

    //! Fee estimator max target.
    virtual unsigned int estimateMaxBlocks() = 0;

    //! Mempool minimum fee.
    virtual CFeeRate mempoolMinFee() = 0;

    //! Relay current minimum fee (from -minrelaytxfee and -incrementalrelayfee settings).
    virtual CFeeRate relayMinFee() = 0;

    //! Relay incremental fee setting (-incrementalrelayfee), reflecting cost of relay.
    virtual CFeeRate relayIncrementalFee() = 0;

    //! Relay dust fee setting (-dustrelayfee), reflecting lowest rate it's economical to spend.
    virtual CFeeRate relayDustFee() = 0;

    //! Check if any block has been pruned.
    virtual bool havePruned() = 0;

    //! Check if the node is ready to broadcast transactions.
    virtual bool isReadyToBroadcast() = 0;

    //! Check if in IBD.
    virtual bool isInitialBlockDownload() = 0;

    //! Check if shutdown requested.
    virtual bool shutdownRequested() = 0;

    //! Get adjusted time.
    virtual int64_t getAdjustedTime() = 0;

    //! Send init message.
    virtual void initMessage(const std::string& message) = 0;

    //! Send init warning.
    virtual void initWarning(const std::string& message) = 0;

    //! Send init error.
    virtual void initError(const std::string& message) = 0;

    //! Send progress indicator.
    virtual void showProgress(const std::string& title, int progress, bool resume_possible) = 0;

    //! Chain notifications.
    class Notifications
    {
    public:
        virtual ~Notifications() {}
        virtual void TransactionAddedToMempool(const CTransactionRef& tx) {}
        virtual void TransactionRemovedFromMempool(const CTransactionRef& ptx) {}
        virtual void BlockConnected(const CBlock& block, const std::vector<CTransactionRef>& tx_conflicted) {}
        virtual void BlockDisconnected(const CBlock& block) {}
        virtual void UpdatedBlockTip() {}
        virtual void ChainStateFlushed(const CBlockLocator& locator) {}
    };

    //! Register handler for notifications.
    virtual std::unique_ptr<Handler> handleNotifications(Notifications& notifications) = 0;

    //! Wait for pending notifications to be processed unless block hash points to the current
    //! chain tip, or to a possible descendant of the current chain tip that isn't currently
    //! connected.
    virtual void waitForNotificationsIfNewBlocksConnected(const uint256& old_tip) = 0;

    //! Register handler for RPC. Command is not copied, so reference
    //! needs to remain valid until Handler is disconnected.
    virtual std::unique_ptr<Handler> handleRpc(const CRPCCommand& command) = 0;

    //! Check if deprecated RPC is enabled.
    virtual bool rpcEnableDeprecated(const std::string& method) = 0;

    //! Run function after given number of seconds. Cancel any previous calls with same name.
    virtual void rpcRunLater(const std::string& name, std::function<void()> fn, int64_t seconds) = 0;

    //! Current RPC serialization flags.
    virtual int rpcSerializationFlags() = 0;

    //! Synchronously send TransactionAddedToMempool notifications about all
    //! current mempool transactions to the specified handler and return after
    //! the last one is sent. These notifications aren't coordinated with async
    //! notifications sent by handleNotifications, so out of date async
    //! notifications from handleNotifications can arrive during and after
    //! synchronous notifications from requestMempoolTransactions. Clients need
    //! to be prepared to handle this by ignoring notifications about unknown
    //! removed transactions and already added new transactions.
    virtual void requestMempoolTransactions(Notifications& notifications) = 0;
};

//! Interface to let node manage chain clients (wallets, or maybe tools for
//! monitoring and analysis in the future).
class ChainClient
{
public:
    virtual ~ChainClient() {}

    //! Register rpcs.
    virtual void registerRpcs() = 0;

    //! Check for errors before loading.
    virtual bool verify() = 0;

    //! Load saved state.
    virtual bool load() = 0;

    //! Start client execution and provide a scheduler.
    virtual void start(CScheduler& scheduler) = 0;

    //! Save state to disk.
    virtual void flush() = 0;

    //! Shut down client.
    virtual void stop() = 0;
};

//! Return implementation of Chain interface.
std::unique_ptr<Chain> MakeChain();

//! Return implementation of ChainClient interface for a wallet client. This
//! function will be undefined in builds where ENABLE_WALLET is false.
//!
//! Currently, wallets are the only chain clients. But in the future, other
//! types of chain clients could be added, such as tools for monitoring,
//! analysis, or fee estimation. These clients need to expose their own
//! MakeXXXClient functions returning their implementations of the ChainClient
//! interface.
std::unique_ptr<ChainClient> MakeWalletClient(Chain& chain, std::vector<std::string> wallet_filenames);

} // namespace interfaces

#endif // DEFI_INTERFACES_CHAIN_H

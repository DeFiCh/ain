// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_SCRIPT_STANDARD_H
#define DEFI_SCRIPT_STANDARD_H

#include <pubkey.h>
#include <script/interpreter.h>
#include <uint256.h>

#include <stdint.h>
#include <variant>

static const bool DEFAULT_ACCEPT_DATACARRIER = true;

class CKeyID;
class CScript;

/** A reference to a CScript: the Hash160 of its serialization (see script.h) */
class CScriptID : public uint160
{
public:
    CScriptID() : uint160() {}
    explicit CScriptID(const CScript& in);
    CScriptID(const uint160& in) : uint160(in) {}
};

// While the relay options are free for interpretation to
// each node, the accept values are enforced on validation

static const uint64_t MAX_OP_RETURN_CORE_ACCEPT = 1024;
static const uint64_t MAX_OP_RETURN_DVM_ACCEPT = 4096;
static const uint64_t MAX_OP_RETURN_EVM_ACCEPT = 65536;

/**
 * This is the check used for IsStandardChecks to allow all of the 3 above
 * However each domain is restricted to their allowed sizes
 * Also used as default for nMaxDatacarrierBytes. 
 * Actual data size = N - 3 // 1 for OP_RETURN, 2 for pushdata opcodes.
 */
static constexpr uint64_t MAX_OP_RETURN_RELAY = std::max({MAX_OP_RETURN_CORE_ACCEPT, MAX_OP_RETURN_DVM_ACCEPT, MAX_OP_RETURN_EVM_ACCEPT});

/**
 * A data carrying output is an unspendable output containing data. The script
 * type is designated as TX_NULL_DATA.
 */
extern bool fAcceptDatacarrier;

/** Maximum size of TX_NULL_DATA scripts that this node considers standard. */
extern unsigned nMaxDatacarrierBytes;

/**
 * Mandatory script verification flags that all new blocks must comply with for
 * them to be valid. (but old blocks may not comply with) Currently just P2SH,
 * but in the future other flags may be added, such as a soft-fork to enforce
 * strict DER encoding.
 *
 * Failing one of these tests may trigger a DoS ban - see CheckInputs() for
 * details.
 */
static const unsigned int MANDATORY_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_P2SH;

enum txnouttype {
    TX_NONSTANDARD,
    // 'standard' transaction types:
    TX_PUBKEY,
    TX_PUBKEYHASH,
    TX_SCRIPTHASH,
    TX_MULTISIG,
    TX_NULL_DATA, //!< unspendable OP_RETURN script that carries data
    TX_WITNESS_V0_SCRIPTHASH,
    TX_WITNESS_V0_KEYHASH,
    TX_WITNESS_V1_TAPROOT,
    TX_WITNESS_V16_ETHHASH,
    TX_WITNESS_UNKNOWN, //!< Only for Witness versions not already defined above
};

class CNoDestination
{
public:
    friend bool operator==(const CNoDestination& a, const CNoDestination& b) { return true; }
    friend bool operator<(const CNoDestination& a, const CNoDestination& b) { return true; }
};

struct PKHash : public uint160 {
    PKHash() : uint160() {}
    explicit PKHash(const uint160& hash) : uint160(hash) {}
    explicit PKHash(const CPubKey& pubkey);
    using uint160::uint160;
};

struct ScriptHash : public uint160 {
    ScriptHash() : uint160() {}
    explicit ScriptHash(const uint160& hash) : uint160(hash) {}
    explicit ScriptHash(const CScript& script);
    using uint160::uint160;
};

struct WitnessV0ScriptHash : public uint256 {
    WitnessV0ScriptHash() : uint256() {}
    explicit WitnessV0ScriptHash(const uint256& hash) : uint256(hash) {}
    explicit WitnessV0ScriptHash(const CScript& script);
    using uint256::uint256;
};

struct WitnessV0KeyHash : public uint160 {
    WitnessV0KeyHash() : uint160() {}
    explicit WitnessV0KeyHash(const uint160& hash) : uint160(hash) {}
    explicit WitnessV0KeyHash(const CPubKey& pubkey);
    using uint160::uint160;
};

struct WitnessV16EthHash : public uint160 {
    WitnessV16EthHash() : uint160() {}
    explicit WitnessV16EthHash(const uint160& hash) : uint160(hash) {}
    explicit WitnessV16EthHash(const CPubKey& pubkey);
    using uint160::uint160;
};

struct WitnessV1Taproot : public XOnlyPubKey
{
    WitnessV1Taproot() : XOnlyPubKey() {}
    explicit WitnessV1Taproot(const XOnlyPubKey& xpk) : XOnlyPubKey(xpk) {}
};

//! CTxDestination subtype to encode any future Witness version
struct WitnessUnknown {
    unsigned int version;
    unsigned int length;
    unsigned char program[40];

    friend bool operator==(const WitnessUnknown& w1, const WitnessUnknown& w2)
    {
        if (w1.version != w2.version) return false;
        if (w1.length != w2.length) return false;
        return std::equal(w1.program, w1.program + w1.length, w2.program);
    }

    friend bool operator<(const WitnessUnknown& w1, const WitnessUnknown& w2)
    {
        if (w1.version < w2.version) return true;
        if (w1.version > w2.version) return false;
        if (w1.length < w2.length) return true;
        if (w1.length > w2.length) return false;
        return std::lexicographical_compare(w1.program, w1.program + w1.length, w2.program, w2.program + w2.length);
    }
};

/**
 * A txout script template with a specific destination. It is either:
 *  * CNoDestination: no destination set
 *  * PKHash: TX_PUBKEYHASH destination (P2PKH)
 *  * ScriptHash: TX_SCRIPTHASH destination (P2SH)
 *  * WitnessV0ScriptHash: TX_WITNESS_V0_SCRIPTHASH destination (P2WSH)
 *  * WitnessV0KeyHash: TX_WITNESS_V0_KEYHASH destination (P2WPKH)
 *  * WitnessV1Taproot: TxoutType::WITNESS_V1_TAPROOT destination (P2TR)
 *  * WitnessUnknown: TxoutType::WITNESS_UNKNOWN destination (P2W???)
 *  * WitnessV16EthHash: ERC55 address type. Not a valid destination, here for address support anly.
 *  A CTxDestination is the internal data type encoded in a DFI address
 */
using CTxDestination = std::variant<CNoDestination, PKHash, ScriptHash, WitnessV0ScriptHash, WitnessV0KeyHash, WitnessV1Taproot, WitnessV16EthHash, WitnessUnknown>;

enum TxDestType {
    NoDestType,
    PKHashType,
    ScriptHashType,
    WitV0ScriptHashType,
    WitV0KeyHashType,
    WitUnknownType,
    WitV16KeyEthHashType,
};

enum KeyType {
    UnknownKeyType = 0,
    PKHashKeyType = 1 << 0,
    ScriptHashKeyType = 1 << 1,
    WPKHashKeyType = 1 << 2,
    EthHashKeyType = 1 << 3,
    MNOperatorKeyType = PKHashKeyType | WPKHashKeyType,
    MNOwnerKeyType = PKHashKeyType | WPKHashKeyType,
    MNRewardKeyType = PKHashKeyType | ScriptHashKeyType | WPKHashKeyType,
    SigningProviderType = PKHashKeyType | ScriptHashKeyType | WPKHashKeyType | EthHashKeyType,
    AllKeyType = ~0,
};

inline KeyType TxDestTypeToKeyType(const size_t index)
{
    switch (index) {
    case PKHashType:
        return KeyType::PKHashKeyType;
    case ScriptHashType:
        return KeyType::ScriptHashKeyType;
    case WitV0KeyHashType:
        return KeyType::WPKHashKeyType;
    case WitV16KeyEthHashType:
        return KeyType::EthHashKeyType;
    default:
        return KeyType::UnknownKeyType;
    }
}

/** Check whether a CTxDestination is a CNoDestination. */
bool IsValidDestination(const CTxDestination& dest);

/** Get the name of a txnouttype as a C string, or nullptr if unknown. */
const char* GetTxnOutputType(txnouttype t);

/**
 * Parse a scriptPubKey and identify script type for standard scripts. If
 * successful, returns script type and parsed pubkeys or hashes, depending on
 * the type. For example, for a P2SH script, vSolutionsRet will contain the
 * script hash, for P2PKH it will contain the key hash, etc.
 *
 * @param[in]   scriptPubKey   Script to parse
 * @param[out]  vSolutionsRet  Vector of parsed pubkeys and hashes
 * @return                     The script type. TX_NONSTANDARD represents a failed solve.
 */
txnouttype Solver(const CScript& scriptPubKey, std::vector<std::vector<unsigned char>>& vSolutionsRet);

/** Try to get the destination address from the keyID type. */
std::optional<CTxDestination> TryFromKeyIDToDestination(const CKeyID& keyId, KeyType keyIdType, KeyType filter = KeyType::AllKeyType);

/** Get the destination address (or default) from the keyID type. */
CTxDestination FromOrDefaultKeyIDToDestination(const CKeyID& keyId, KeyType keyIdType, KeyType filter = KeyType::AllKeyType);

/**
 * Parse a standard scriptPubKey for the destination address. Assigns result to
 * the addressRet parameter and returns true if successful. For multisig
 * scripts, instead use ExtractDestinations. Currently only works for P2PK,
 * P2PKH, P2SH, P2WPKH, and P2WSH scripts.
 */
bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet);

/**
 * Parse a standard scriptPubKey with one or more destination addresses. For
 * multisig scripts, this populates the addressRet vector with the pubkey IDs
 * and nRequiredRet with the n required to spend. For other destinations,
 * addressRet is populated with a single value and nRequiredRet is set to 1.
 * Returns true if successful.
 *
 * Note: this function confuses destinations (a subset of CScripts that are
 * encodable as an address) with key identifiers (of keys involved in a
 * CScript), and its use should be phased out.
 */
bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet);

/**
 * Generate a scriptPubKey for the given CTxDestination. Returns a P2PKH
 * script for a CKeyID destination, a P2SH script for a CScriptID, and an empty
 * script for CNoDestination.
 */
CScript GetScriptForDestination(const CTxDestination& dest);

/** Generate a P2PK script for the given pubkey. */
CScript GetScriptForRawPubKey(const CPubKey& pubkey);

/** Generate a multisig script. */
CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys);

/** Generate a Hash-Timelock Script script. */
CScript GetScriptForHTLC(const CPubKey& seller, const CPubKey& refund, const std::vector<unsigned char> image, uint32_t timeout);

/**
 * Generate a pay-to-witness script for the given redeem script. If the redeem
 * script is P2PK or P2PKH, this returns a P2WPKH script, otherwise it returns a
 * P2WSH script.
 *
 * TODO: replace calls to GetScriptForWitness with GetScriptForDestination using
 * the various witness-specific CTxDestination subtypes.
 */
CScript GetScriptForWitness(const CScript& redeemscript);

inline std::optional<CKeyID> TryFromDestination(const CTxDestination &dest, KeyType filter=KeyType::AllKeyType) {
    auto destType = TxDestTypeToKeyType(dest.index()) & filter;
    switch (destType) {
        case KeyType::PKHashKeyType:
            return CKeyID(std::get<PKHash>(dest));
        case KeyType::WPKHashKeyType:
            return CKeyID(std::get<WitnessV0KeyHash>(dest));
        case KeyType::ScriptHashKeyType:
            return CKeyID(std::get<ScriptHash>(dest));
        case KeyType::EthHashKeyType:
            return CKeyID(std::get<WitnessV16EthHash>(dest));
        default:
            return {};
    }
}

inline CKeyID FromOrDefaultDestination(const CTxDestination &dest, KeyType filter=KeyType::AllKeyType) {
    if (auto key = TryFromDestination(dest, filter); key) {
        return *key;
    }
    return {};
}

/** Utility class to construct Taproot outputs from internal key and script tree. */
class TaprootBuilder
{
private:
    /** Information associated with a node in the Merkle tree. */
    struct NodeInfo
    {
        /** Merkle hash of this node. */
        uint256 hash;
    };
    /** Whether the builder is in a valid state so far. */
    bool m_valid = true;

    /** The current state of the builder.
     *
     * For each level in the tree, one NodeInfo object may be present. m_branch[0]
     * is information about the root; further values are for deeper subtrees being
     * explored.
     *
     * For every right branch taken to reach the position we're currently
     * working in, there will be a (non-nullopt) entry in m_branch corresponding
     * to the left branch at that level.
     *
     * For example, imagine this tree:     - N0 -
     *                                    /      \
     *                                   N1      N2
     *                                  /  \    /  \
     *                                 A    B  C   N3
     *                                            /  \
     *                                           D    E
     *
     * Initially, m_branch is empty. After processing leaf A, it would become
     * {nullopt, nullopt, A}. When processing leaf B, an entry at level 2 already
     * exists, and it would thus be combined with it to produce a level 1 one,
     * resulting in {nullopt, N1}. Adding C and D takes us to {nullopt, N1, C}
     * and {nullopt, N1, C, D} respectively. When E is processed, it is combined
     * with D, and then C, and then N1, to produce the root, resulting in {N0}.
     *
     * This structure allows processing with just O(log n) overhead if the leaves
     * are computed on the fly.
     *
     * As an invariant, there can never be nullopt entries at the end. There can
     * also not be more than 128 entries (as that would mean more than 128 levels
     * in the tree). The depth of newly added entries will always be at least
     * equal to the current size of m_branch (otherwise it does not correspond
     * to a depth-first traversal of a tree). m_branch is only empty if no entries
     * have ever be processed. m_branch having length 1 corresponds to being done.
     */
    std::vector<std::optional<NodeInfo>> m_branch;

    XOnlyPubKey m_internal_key;  //!< The internal key, set when finalizing.
    XOnlyPubKey m_output_key; //!< The output key, computed when finalizing. */

    /** Combine information about a parent Merkle tree node from its child nodes. */
    static NodeInfo Combine(NodeInfo&& a, NodeInfo&& b);
    /** Insert information about a node at a certain depth, and propagate information up. */
    void Insert(NodeInfo&& node, int depth);

public:
    /** Add a new script at a certain depth in the tree. Add() operations must be called
     *  in depth-first traversal order of binary tree. */
    TaprootBuilder& Add(int depth, const CScript& script, int leaf_version);
    /** Like Add(), but for a Merkle node with a given hash to the tree. */
    TaprootBuilder& AddOmitted(int depth, const uint256& hash);
    /** Finalize the construction. Can only be called when IsComplete() is true.
        internal_key.IsFullyValid() must be true. */
    TaprootBuilder& Finalize(const XOnlyPubKey& internal_key);

    /** Return true if so far all input was valid. */
    bool IsValid() const { return m_valid; }
    /** Return whether there were either no leaves, or the leaves form a Huffman tree. */
    bool IsComplete() const { return m_valid && (m_branch.size() == 0 || (m_branch.size() == 1 && m_branch[0].has_value())); }
    /** Compute scriptPubKey (after Finalize()). */
    WitnessV1Taproot GetOutput();
    /** Check if a list of depths is legal (will lead to IsComplete()). */
    static bool ValidDepths(const std::vector<int>& depths);
};

#endif // DEFI_SCRIPT_STANDARD_H

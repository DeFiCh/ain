// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <script/standard.h>

#include <crypto/sha256.h>
#include <hash.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <util/strencodings.h>

typedef std::vector<unsigned char> valtype;

bool fAcceptDatacarrier = DEFAULT_ACCEPT_DATACARRIER;
unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

ScriptHash::ScriptHash(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

PKHash::PKHash(const CPubKey& pubkey) : uint160(pubkey.GetID()) {}

WitnessV0ScriptHash::WitnessV0ScriptHash(const CScript& in)
{
    CSHA256().Write(in.data(), in.size()).Finalize(begin());
}

WitnessV0KeyHash::WitnessV0KeyHash(const CPubKey& pubkey) : uint160(pubkey.GetID()) {}

WitnessV16EthHash::WitnessV16EthHash(const CPubKey& pubkey) : uint160(pubkey.GetEthID()) {}

const char* GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
    case TX_MULTISIG: return "multisig";
    case TX_NULL_DATA: return "nulldata";
    case TX_WITNESS_V0_KEYHASH: return "witness_v0_keyhash";
    case TX_WITNESS_V0_SCRIPTHASH: return "witness_v0_scripthash";
    case TX_WITNESS_V1_TAPROOT: return "witness_v1_taproot";
    case TX_WITNESS_V16_ETHHASH: return "witness_v16_ethhash";
    case TX_WITNESS_UNKNOWN: return "witness_unknown";
    }
    return nullptr;
}

static bool MatchPayToPubkey(const CScript& script, valtype& pubkey)
{
    if (script.size() == CPubKey::PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::PUBLIC_KEY_SIZE && script.back() == OP_CHECKSIG) {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::PUBLIC_KEY_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    if (script.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && script.back() == OP_CHECKSIG) {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    return false;
}

static bool MatchPayToPubkeyHash(const CScript& script, valtype& pubkeyhash)
{
    auto envelopeCheck = [](const CScript& script) {
        return (script.size() == 25 && script[0] == OP_DUP && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG);
    };
    auto hashOpCheck = [](const CScript& script) {
        return (script[1] == OP_HASH160 || script[1] == OP_KECCAK);
    };

    if (envelopeCheck(script) && hashOpCheck(script)) {
        pubkeyhash = valtype(script.begin () + 3, script.begin() + 23);
        return true;
    }
    return false;
}

/** Test for "small positive integer" script opcodes - OP_1 through OP_16. */
static constexpr bool IsSmallInteger(opcodetype opcode)
{
    return opcode >= OP_1 && opcode <= OP_16;
}

static bool MatchMultisig(const CScript& script, unsigned int& required, std::vector<valtype>& pubkeys)
{
    opcodetype opcode;
    valtype data;
    CScript::const_iterator it = script.begin();
    if (script.size() < 1 || script.back() != OP_CHECKMULTISIG) return false;

    if (!script.GetOp(it, opcode, data) || !IsSmallInteger(opcode)) return false;
    required = CScript::DecodeOP_N(opcode);
    while (script.GetOp(it, opcode, data) && CPubKey::ValidSize(data)) {
        pubkeys.emplace_back(std::move(data));
    }
    if (!IsSmallInteger(opcode)) return false;
    unsigned int keys = CScript::DecodeOP_N(opcode);
    if (pubkeys.size() != keys || keys < required) return false;
    return (it + 1 == script.end());
}

txnouttype Solver(const CScript& scriptPubKey, std::vector<std::vector<unsigned char>>& vSolutionsRet)
{
    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return TX_SCRIPTHASH;
    }

    int witnessversion;
    std::vector<unsigned char> witnessprogram;
    if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_KEYHASH_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TX_WITNESS_V0_KEYHASH;
        }
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_SCRIPTHASH_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TX_WITNESS_V0_SCRIPTHASH;
        }
        if (witnessversion == 1 && witnessprogram.size() == WITNESS_V1_TAPROOT_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TX_WITNESS_V1_TAPROOT;
        }
        if (witnessversion == 16 && witnessprogram.size() == WITNESS_V16_ETHHASH_SIZE) {
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TX_WITNESS_V16_ETHHASH;
        }
        if (witnessversion != 0) {
            vSolutionsRet.push_back(std::vector<unsigned char>{(unsigned char)witnessversion});
            vSolutionsRet.push_back(std::move(witnessprogram));
            return TX_WITNESS_UNKNOWN;
        }
        return TX_NONSTANDARD;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        return TX_NULL_DATA;
    }

    std::vector<unsigned char> data;
    if (MatchPayToPubkey(scriptPubKey, data)) {
        vSolutionsRet.push_back(std::move(data));
        return TX_PUBKEY;
    }

    if (MatchPayToPubkeyHash(scriptPubKey, data)) {
        vSolutionsRet.push_back(std::move(data));
        return TX_PUBKEYHASH;
    }

    unsigned int required;
    std::vector<std::vector<unsigned char>> keys;
    if (MatchMultisig(scriptPubKey, required, keys)) {
        vSolutionsRet.push_back({static_cast<unsigned char>(required)}); // safe as required is in range 1..16
        vSolutionsRet.insert(vSolutionsRet.end(), keys.begin(), keys.end());
        vSolutionsRet.push_back({static_cast<unsigned char>(keys.size())}); // safe as size is in range 1..16
        return TX_MULTISIG;
    }

    vSolutionsRet.clear();
    return TX_NONSTANDARD;
}

bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType = Solver(scriptPubKey, vSolutions);

    if (whichType == TX_PUBKEY) {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = PKHash(pubKey);
        return true;
    }
    else if (whichType == TX_PUBKEYHASH)
    {
        addressRet = PKHash(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_SCRIPTHASH)
    {
        addressRet = ScriptHash(uint160(vSolutions[0]));
        return true;
    } else if (whichType == TX_WITNESS_V0_KEYHASH) {
        WitnessV0KeyHash hash;
        std::copy(vSolutions[0].begin(), vSolutions[0].end(), hash.begin());
        addressRet = hash;
        return true;
    } else if (whichType == TX_WITNESS_V0_SCRIPTHASH) {
        WitnessV0ScriptHash hash;
        std::copy(vSolutions[0].begin(), vSolutions[0].end(), hash.begin());
        addressRet = hash;
        return true;
    } else if (whichType == TX_WITNESS_V16_ETHHASH) {
        WitnessV16EthHash hash;
        std::copy(vSolutions[0].begin(), vSolutions[0].end(), hash.begin());
        addressRet = hash;
        return true;
    } else if (whichType == TX_WITNESS_V1_TAPROOT) {
        WitnessV1Taproot tap;
        std::copy(vSolutions[0].begin(), vSolutions[0].end(), tap.begin());
        addressRet = tap;
        return true;
    } else if (whichType == TX_WITNESS_UNKNOWN) {
        WitnessUnknown unk;
        unk.version = vSolutions[0][0];
        std::copy(vSolutions[1].begin(), vSolutions[1].end(), unk.program);
        unk.length = vSolutions[1].size();
        addressRet = unk;
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet)
{
    addressRet.clear();
    std::vector<valtype> vSolutions;
    typeRet = Solver(scriptPubKey, vSolutions);
    if (typeRet == TX_NONSTANDARD) {
        return false;
    } else if (typeRet == TX_NULL_DATA) {
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG)
    {
        nRequiredRet = vSolutions.front()[0];
        for (unsigned int i = 1; i < vSolutions.size()-1; i++)
        {
            CPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid())
                continue;

            CTxDestination address = PKHash(pubKey);
            addressRet.push_back(address);
        }

        if (addressRet.empty())
            return false;
    }
    else
    {
        nRequiredRet = 1;
        CTxDestination address;
        if (!ExtractDestination(scriptPubKey, address))
           return false;
        addressRet.push_back(address);
    }

    return true;
}

std::optional<CTxDestination> TryFromKeyIDToDestination(const CKeyID &keyId, KeyType keyIdType, KeyType filter) {
    auto type = keyIdType & filter;
    switch (type) {
        case KeyType::PKHashKeyType:
            return CTxDestination(PKHash(keyId));
        case KeyType::WPKHashKeyType:
            return CTxDestination(WitnessV0KeyHash(keyId));
        case KeyType::ScriptHashKeyType:
            return CTxDestination(ScriptHash(keyId));
        case KeyType::EthHashKeyType:
            return CTxDestination(WitnessV16EthHash(keyId));
        default:
            return {};
    }
}

CTxDestination FromOrDefaultKeyIDToDestination(const CKeyID &keyId, KeyType keyIdType, KeyType filter) {
    auto dest = TryFromKeyIDToDestination(keyId, keyIdType, filter);
    if (dest) {
        return *dest;
    }
    return CTxDestination(CNoDestination());
}

bool IsValidDestination(const CTxDestination& dest) {
    return dest.index() != NoDestType;
}

namespace {
class CScriptVisitor
{
public:
    CScript operator()(const CNoDestination& dest) const
    {
        return CScript();
    }

    CScript operator()(const PKHash& keyID) const
    {
        return CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
    }

    CScript operator()(const ScriptHash& scriptID) const
    {
        return CScript() << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    }

    CScript operator()(const WitnessV0KeyHash& id) const
    {
        return CScript() << OP_0 << ToByteVector(id);
    }

    CScript operator()(const WitnessV0ScriptHash& id) const
    {
        return CScript() << OP_0 << ToByteVector(id);
    }

    CScript operator()(const WitnessV1Taproot& tap) const
    {
        return CScript() << OP_1 << ToByteVector(tap);
    }

    CScript operator()(const WitnessV16EthHash& id) const
    {
        return CScript() << OP_16 << ToByteVector(id);
    }

    CScript operator()(const WitnessUnknown& id) const
    {
        return CScript() << CScript::EncodeOP_N(id.version) << std::vector<unsigned char>(id.program, id.program + id.length);
    }
};
} // namespace

CScript GetScriptForDestination(const CTxDestination& dest)
{
    CScript script;

    return std::visit(CScriptVisitor(), dest);
}

CScript GetScriptForRawPubKey(const CPubKey& pubKey)
{
    return CScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
    CScript script;

    script << CScript::EncodeOP_N(nRequired);
    for (const CPubKey& key : keys)
        script << ToByteVector(key);
    script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}

CScript GetScriptForWitness(const CScript& redeemscript)
{
    std::vector<std::vector<unsigned char> > vSolutions;
    txnouttype typ = Solver(redeemscript, vSolutions);
    if (typ == TX_PUBKEY) {
        return GetScriptForDestination(WitnessV0KeyHash(Hash160(vSolutions[0].begin(), vSolutions[0].end())));
    } else if (typ == TX_PUBKEYHASH) {
        return GetScriptForDestination(WitnessV0KeyHash(vSolutions[0]));
    }
    return GetScriptForDestination(WitnessV0ScriptHash(redeemscript));
}

CScript GetScriptForHTLC(const CPubKey& seller, const CPubKey& refund, const std::vector<unsigned char> image, uint32_t timeout)
{
    CScript script;

    script << OP_IF;
    script << OP_SHA256 << image << OP_EQUALVERIFY << ToByteVector(seller);
    script << OP_ELSE;

    if (timeout <= 16)
    {
        script << CScript::EncodeOP_N(timeout);
    }
    else
    {
        script << CScriptNum(timeout);
    }

    script << OP_CHECKSEQUENCEVERIFY << OP_DROP << ToByteVector(refund);
    script << OP_ENDIF;
    script << OP_CHECKSIG;

    return script;
}

/*static*/ TaprootBuilder::NodeInfo TaprootBuilder::Combine(NodeInfo&& a, NodeInfo&& b)
{
    NodeInfo ret;
    /* Iterate over all tracked leaves in a, add b's hash to their Merkle branch, and move them to ret. */
    for (auto& leaf : a.leaves) {
        leaf.merkle_branch.push_back(b.hash);
        ret.leaves.emplace_back(std::move(leaf));
    }
    /* Iterate over all tracked leaves in b, add a's hash to their Merkle branch, and move them to ret. */
    for (auto& leaf : b.leaves) {
        leaf.merkle_branch.push_back(a.hash);
        ret.leaves.emplace_back(std::move(leaf));
    }
    /* Lexicographically sort a and b's hash, and compute parent hash. */
    if (a.hash < b.hash) {
        ret.hash = (CHashWriter(HASHER_TAPBRANCH) << a.hash << b.hash).GetSHA256();
    } else {
        ret.hash = (CHashWriter(HASHER_TAPBRANCH) << b.hash << a.hash).GetSHA256();
    }
    return ret;
}

void TaprootSpendData::Merge(TaprootSpendData other)
{
    // TODO: figure out how to better deal with conflicting information
    // being merged.
    if (internal_key.IsNull() && !other.internal_key.IsNull()) {
        internal_key = other.internal_key;
    }
    if (merkle_root.IsNull() && !other.merkle_root.IsNull()) {
        merkle_root = other.merkle_root;
    }
    for (auto& [key, control_blocks] : other.scripts) {
        scripts[key].merge(std::move(control_blocks));
    }
}

void TaprootBuilder::Insert(TaprootBuilder::NodeInfo&& node, int depth)
{
    assert(depth >= 0 && (size_t)depth <= TAPROOT_CONTROL_MAX_NODE_COUNT);
    /* We cannot insert a leaf at a lower depth while a deeper branch is unfinished. Doing
     * so would mean the Add() invocations do not correspond to a DFS traversal of a
     * binary tree. */
    if ((size_t)depth + 1 < m_branch.size()) {
        m_valid = false;
        return;
    }
    /* As long as an entry in the branch exists at the specified depth, combine it and propagate up.
     * The 'node' variable is overwritten here with the newly combined node. */
    while (m_valid && m_branch.size() > (size_t)depth && m_branch[depth].has_value()) {
        node = Combine(std::move(node), std::move(*m_branch[depth]));
        m_branch.pop_back();
        if (depth == 0) m_valid = false; /* Can't propagate further up than the root */
        --depth;
    }
    if (m_valid) {
        /* Make sure the branch is big enough to place the new node. */
        if (m_branch.size() <= (size_t)depth) m_branch.resize((size_t)depth + 1);
        assert(!m_branch[depth].has_value());
        m_branch[depth] = std::move(node);
    }
}

/*static*/ bool TaprootBuilder::ValidDepths(const std::vector<int>& depths)
{
    std::vector<bool> branch;
    for (int depth : depths) {
        // This inner loop corresponds to effectively the same logic on branch
        // as what Insert() performs on the m_branch variable. Instead of
        // storing a NodeInfo object, just remember whether or not there is one
        // at that depth.
        if (depth < 0 || (size_t)depth > TAPROOT_CONTROL_MAX_NODE_COUNT) return false;
        if ((size_t)depth + 1 < branch.size()) return false;
        while (branch.size() > (size_t)depth && branch[depth]) {
            branch.pop_back();
            if (depth == 0) return false;
            --depth;
        }
        if (branch.size() <= (size_t)depth) branch.resize((size_t)depth + 1);
        assert(!branch[depth]);
        branch[depth] = true;
    }
    // And this check corresponds to the IsComplete() check on m_branch.
    return branch.size() == 0 || (branch.size() == 1 && branch[0]);
}

TaprootBuilder& TaprootBuilder::Add(int depth, const CScript& script, int leaf_version, bool track)
{
    assert((leaf_version & ~TAPROOT_LEAF_MASK) == 0);
    if (!IsValid()) return *this;
    /* Construct NodeInfo object with leaf hash and (if track is true) also leaf information. */
    NodeInfo node;
    node.hash = (CHashWriter{HASHER_TAPLEAF} << uint8_t(leaf_version) << script).GetSHA256();
    if (track) node.leaves.emplace_back(LeafInfo{script, leaf_version, {}});
    /* Insert into the branch. */
    Insert(std::move(node), depth);
    return *this;
}

TaprootBuilder& TaprootBuilder::AddOmitted(int depth, const uint256& hash)
{
    if (!IsValid()) return *this;
    /* Construct NodeInfo object with the hash directly, and insert it into the branch. */
    NodeInfo node;
    node.hash = hash;
    Insert(std::move(node), depth);
    return *this;
}

TaprootBuilder& TaprootBuilder::Finalize(const XOnlyPubKey& internal_key)
{
    /* Can only call this function when IsComplete() is true. */
    assert(IsComplete());
    m_internal_key = internal_key;
    auto ret = m_internal_key.CreateTapTweak(m_branch.size() == 0 ? nullptr : &m_branch[0]->hash);
    assert(ret.has_value());
    std::tie(m_output_key, m_parity) = *ret;
    return *this;
}

WitnessV1Taproot TaprootBuilder::GetOutput() { return WitnessV1Taproot{m_output_key}; }

TaprootSpendData TaprootBuilder::GetSpendData() const
{
    TaprootSpendData spd;
    spd.merkle_root = m_branch.size() == 0 ? uint256() : m_branch[0]->hash;
    spd.internal_key = m_internal_key;
    if (m_branch.size()) {
        // If any script paths exist, they have been combined into the root m_branch[0]
        // by now. Compute the control block for each of its tracked leaves, and put them in
        // spd.scripts.
        for (const auto& leaf : m_branch[0]->leaves) {
            std::vector<unsigned char> control_block;
            control_block.resize(TAPROOT_CONTROL_BASE_SIZE + TAPROOT_CONTROL_NODE_SIZE * leaf.merkle_branch.size());
            control_block[0] = leaf.leaf_version | (m_parity ? 1 : 0);
            std::copy(m_internal_key.begin(), m_internal_key.end(), control_block.begin() + 1);
            if (leaf.merkle_branch.size()) {
                std::copy(leaf.merkle_branch[0].begin(),
                          leaf.merkle_branch[0].begin() + TAPROOT_CONTROL_NODE_SIZE * leaf.merkle_branch.size(),
                          control_block.begin() + TAPROOT_CONTROL_BASE_SIZE);
            }
            spd.scripts[{leaf.script, leaf.leaf_version}].insert(std::move(control_block));
        }
    }
    return spd;
}

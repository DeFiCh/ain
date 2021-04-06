// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>
#include <masternodes/balances.h>
#include <masternodes/mn_checks.h>
#include <masternodes/res.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/tx_check.h>
#include <core_io.h>
#include <index/txindex.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <txmempool.h>
#include <streams.h>
#include <validation.h>

#include <algorithm>
#include <sstream>
#include <cstring>

using namespace std;

std::string ToString(CustomTxType type) {
    switch (type)
    {
        case CustomTxType::CreateMasternode:    return "CreateMasternode";
        case CustomTxType::ResignMasternode:    return "ResignMasternode";
        case CustomTxType::CreateToken:         return "CreateToken";
        case CustomTxType::UpdateToken:         return "UpdateToken";
        case CustomTxType::UpdateTokenAny:      return "UpdateTokenAny";
        case CustomTxType::MintToken:           return "MintToken";
        case CustomTxType::CreatePoolPair:      return "CreatePoolPair";
        case CustomTxType::UpdatePoolPair:      return "UpdatePoolPair";
        case CustomTxType::PoolSwap:            return "PoolSwap";
        case CustomTxType::AddPoolLiquidity:    return "AddPoolLiquidity";
        case CustomTxType::RemovePoolLiquidity: return "RemovePoolLiquidity";
        case CustomTxType::UtxosToAccount:      return "UtxosToAccount";
        case CustomTxType::AccountToUtxos:      return "AccountToUtxos";
        case CustomTxType::AccountToAccount:    return "AccountToAccount";
        case CustomTxType::AnyAccountsToAccounts:   return "AnyAccountsToAccounts";
        case CustomTxType::SetGovVariable:      return "SetGovVariable";
        case CustomTxType::AutoAuthPrep:        return "AutoAuth";
        default:                                return "None";
    }
}

static ResVal<CBalances> BurntTokens(CTransaction const & tx) {
    CBalances balances;
    for (const auto& out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            auto res = balances.Add(out.TokenAmount());
            if (!res.ok) {
                return res;
            }
        }
    }
    return {balances, Res::Ok()};
}

static ResVal<CBalances> MintedTokens(CTransaction const & tx, uint32_t mintingOutputsStart) {
    CBalances balances;
    for (uint32_t i = mintingOutputsStart; i < (uint32_t) tx.vout.size(); i++) {
        auto res = balances.Add(tx.vout[i].TokenAmount());
        if (!res.ok) {
            return res;
        }
    }
    return {balances, Res::Ok()};
}

CPubKey GetPubkeyFromScriptSig(CScript const & scriptSig)
{
    opcodetype opcode;
    std::vector<unsigned char> data;
    CScript::const_iterator pc = scriptSig.begin();
    // Signature first, then pubkey. I think, that in all cases it will be OP_PUSHDATA1, but...
    if (!scriptSig.GetOp(pc, opcode, data)
    || (opcode > OP_PUSHDATA1 && opcode != OP_PUSHDATA2 && opcode != OP_PUSHDATA4)
    || !scriptSig.GetOp(pc, opcode, data)
    || (opcode > OP_PUSHDATA1 && opcode != OP_PUSHDATA2 && opcode != OP_PUSHDATA4)) {
        return CPubKey();
    }
    return CPubKey(data);
}

CCustomTxMessage customTypeToMessage(CustomTxType txType) {
    switch (txType)
    {
    case CustomTxType::CreateMasternode:        return CCreateMasterNodeMessage{};
    case CustomTxType::ResignMasternode:        return CResignMasterNodeMessage{};
    case CustomTxType::CreateToken:             return CCreateTokenMessage{};
    case CustomTxType::UpdateToken:             return CUpdateTokenPreAMKMessage{};
    case CustomTxType::UpdateTokenAny:          return CUpdateTokenMessage{};
    case CustomTxType::MintToken:               return CMintTokensMessage{};
    case CustomTxType::CreatePoolPair:          return CCreatePoolPairMessage{};
    case CustomTxType::UpdatePoolPair:          return CUpdatePoolPairMessage{};
    case CustomTxType::PoolSwap:                return CPoolSwapMessage{};
    case CustomTxType::AddPoolLiquidity:        return CLiquidityMessage{};
    case CustomTxType::RemovePoolLiquidity:     return CRemoveLiquidityMessage{};
    case CustomTxType::UtxosToAccount:          return CUtxosToAccountMessage{};
    case CustomTxType::AccountToUtxos:          return CAccountToUtxosMessage{};
    case CustomTxType::AccountToAccount:        return CAccountToAccountMessage{};
    case CustomTxType::AnyAccountsToAccounts:   return CAnyAccountsToAccountsMessage{};
    case CustomTxType::SetGovVariable:          return CGovernanceMessage{};
    default:                                    return CCustomTxMessageNone{};
    }
}

extern std::string ScriptToString(CScript const& script);

class CCustomMetadataParseVisitor : public boost::static_visitor<Res>
{
    uint32_t height;
    const Consensus::Params& consensus;
    const std::vector<unsigned char>& metadata;

    Res isPostAMKFork() const {
        if(static_cast<int>(height) < consensus.AMKHeight) {
            return Res::Err("called before AMK height");
        }
        return Res::Ok();
    }

    Res isPostBayfrontFork() const {
        if(static_cast<int>(height) < consensus.BayfrontHeight) {
            return Res::Err("called before Bayfront height");
        }
        return Res::Ok();
    }

    Res isPostBayfrontGardensFork() const {
        if(static_cast<int>(height) < consensus.BayfrontGardensHeight) {
            return Res::Err("called before Bayfront Gardens height");
        }
        return Res::Ok();
    }

    template<typename T>
    Res serialize(T& obj) const {
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj;
        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }
        return Res::Ok();
    }

public:
    CCustomMetadataParseVisitor(uint32_t height,
                                const Consensus::Params& consensus,
                                const std::vector<unsigned char>& metadata)
        : height(height), consensus(consensus), metadata(metadata) {}

    Res operator()(CCreateMasterNodeMessage& obj) const {
        return serialize(obj);
    }

    Res operator()(CResignMasterNodeMessage& obj) const {
        if (metadata.size() != sizeof(obj)) {
            return Res::Err("metadata must contain 32 bytes");
        }
        return serialize(obj);
    }

    Res operator()(CCreateTokenMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUpdateTokenPreAMKMessage& obj) const {
        auto res = isPostAMKFork();
        if (!res) {
            return res;
        }
        if(isPostBayfrontFork()) {
            return Res::Err("called post Bayfront height");
        }
        return serialize(obj);
    }

    Res operator()(CUpdateTokenMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CMintTokensMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CPoolSwapMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CLiquidityMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CRemoveLiquidityMessage& obj) const {
        auto res = isPostBayfrontFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CUtxosToAccountMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAccountToUtxosMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAccountToAccountMessage& obj) const {
        auto res = isPostAMKFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CAnyAccountsToAccountsMessage& obj) const {
        auto res = isPostBayfrontGardensFork();
        return !res ? res : serialize(obj);
    }

    Res operator()(CCreatePoolPairMessage& obj) const {
        auto res = isPostBayfrontFork();
        if (!res) {
            return res;
        }

        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> obj.poolPair;
        ss >> obj.pairSymbol;

        // Read custom pool rewards
        if (static_cast<int>(height) >= consensus.ClarkeQuayHeight && !ss.empty()) {
            ss >> obj.rewards;
        }
        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }
        return Res::Ok();
    }

    Res operator()(CUpdatePoolPairMessage& obj) const {
        auto res = isPostBayfrontFork();
        if (!res) {
            return res;
        }

        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        // serialize poolId as raw integer
        ss >> obj.poolId.v;
        ss >> obj.status;
        ss >> obj.commission;
        ss >> obj.ownerAddress;

        // Read custom pool rewards
        if (static_cast<int>(height) >= consensus.ClarkeQuayHeight && !ss.empty()) {
            ss >> obj.rewards;
        }

        if (!ss.empty()) {
            return Res::Err("deserialization failed: excess %d bytes", ss.size());
        }
        return Res::Ok();
    }

    Res operator()(CGovernanceMessage& obj) const {
        auto res = isPostBayfrontFork();
        if (!res) {
            return res;
        }
        std::string name;
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        while(!ss.empty()) {
            ss >> name;
            auto var = GovVariable::Create(name);
            if (!var) {
                return Res::Err("'%s': variable does not registered", name);
            }
            ss >> *var;
            obj.govs.insert(std::move(var));
        }
        return Res::Ok();
    }

    Res operator()(CCustomTxMessageNone&) const {
        return Res::Ok();
    }
};

class CCustomTxVisitor : public boost::static_visitor<Res>
{
protected:
    uint32_t height;
    CCustomCSView& mnview;
    const CTransaction& tx;
    const CCoinsViewCache& coins;
    const Consensus::Params& consensus;

public:
    CCustomTxVisitor(const CTransaction& tx,
                     uint32_t height,
                     const CCoinsViewCache& coins,
                     CCustomCSView& mnview,
                     const Consensus::Params& consensus)

        : height(height), mnview(mnview), tx(tx), coins(coins), consensus(consensus) {}

    bool HasAuth(const CScript& auth) const {
        for (const auto& input : tx.vin) {
            const Coin& coin = coins.AccessCoin(input.prevout);
            if (!coin.IsSpent() && coin.out.scriptPubKey == auth) {
                return true;
            }
        }
        return false;
    }

    Res HasCollateralAuth(const uint256& collateralTx) const {
        const Coin& auth = coins.AccessCoin(COutPoint(collateralTx, 1)); // always n=1 output
        if (!HasAuth(auth.out.scriptPubKey)) {
            return Res::Err("tx must have at least one input from the owner");
        }
        return Res::Ok();
    }

    Res HasFoundationAuth() const {
        for (const auto& input : tx.vin) {
            const Coin& coin = coins.AccessCoin(input.prevout);
            if (!coin.IsSpent() && consensus.foundationMembers.count(coin.out.scriptPubKey) > 0) {
                return Res::Ok();
            }
        }
        return Res::Err("tx not from foundation member");
    }

    Res CheckMasternodeCreationTx() const {
        if (tx.vout.size() < 2
        || tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0}
        || tx.vout[1].nValue != GetMnCollateralAmount(height) || tx.vout[1].nTokenId != DCT_ID{0}) {
            return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");
        }
        return Res::Ok();
    }

    Res CheckTokenCreationTx() const {
        if (tx.vout.size() < 2
        || tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0}
        || tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}) {
            return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");
        }
        return Res::Ok();
    }

    ResVal<CScript> MintableToken(DCT_ID id, const CTokenImplementation& token) const {
        if (token.destructionTx != uint256{}) {
            return Res::Err("token %s already destroyed at height %i by tx %s", token.symbol,
                            token.destructionHeight, token.destructionTx.GetHex());
        }
        const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output

        // pre-bayfront logic:
        if (static_cast<int>(height) < consensus.BayfrontHeight) {
            if (id < CTokensView::DCT_ID_START) {
                return Res::Err("token %s is a 'stable coin', can't mint stable coin!", id.ToString());
            }

            if (!HasAuth(auth.out.scriptPubKey)) {
                return Res::Err("tx must have at least one input from token owner");
            }
            return {auth.out.scriptPubKey, Res::Ok()};
        }

        if (id == DCT_ID{0}) {
            return Res::Err("can't mint default DFI coin!");
        }

        if (token.IsPoolShare()) {
            return Res::Err("can't mint LPS token %s!", id.ToString());
        }
        // may be different logic with LPS, so, dedicated check:
        if (!token.IsMintable()) {
            return Res::Err("token %s is not mintable!", id.ToString());
        }

        if (!HasAuth(auth.out.scriptPubKey)) { // in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation member himself
            if (!token.IsDAT()) {
                return Res::Err("tx must have at least one input from token owner");
            } else if (!HasFoundationAuth()) { // Is a DAT, check founders auth
                return Res::Err("token is DAT and tx not from foundation member");
            }
        }

        return {auth.out.scriptPubKey, Res::Ok()};
    }

    Res eraseEmptyBalances(TAmounts& balances) const {
        for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
            ++next_it;

            auto token = mnview.GetToken(it->first);
            if (!token) {
                return Res::Err("reward token %d does not exist!", it->first.v);
            }

            if (it->second == 0) {
                balances.erase(it);
            }
        }
        return Res::Ok();
    }

    Res setShares(const CScript& owner, const TAmounts& balances) const {
        for (const auto& balance : balances) {
            auto token = mnview.GetToken(balance.first);
            if (token && token->IsPoolShare()) {
                const auto bal = mnview.GetBalance(owner, balance.first);
                if (bal.nValue == balance.second) {
                    auto res = mnview.SetShare(balance.first, owner, height);
                    if (!res) {
                        return res;
                    }
                }
            }
        }
        return Res::Ok();
    }

    Res delShares(const CScript& owner, const TAmounts& balances) const {
        for (const auto& kv : balances) {
            auto token = mnview.GetToken(kv.first);
            if (token && token->IsPoolShare()) {
                const auto balance = mnview.GetBalance(owner, kv.first);
                if (balance.nValue == 0) {
                    auto res = mnview.DelShare(kv.first, owner);
                    if (!res) {
                        return res;
                    }
                }
            }
        }
        return Res::Ok();
    }

    Res subBalanceDelShares(const CScript& owner, const CBalances& balance) const {
        mnview.CalculateOwnerRewards(owner, height);
        auto res = mnview.SubBalances(owner, balance);
        if (!res) {
            return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, res.msg);
        }
        return delShares(owner, balance.balances);
    }

    Res addBalanceSetShares(const CScript& owner, const CBalances& balance) const {
        mnview.CalculateOwnerRewards(owner, height);
        auto res = mnview.AddBalances(owner, balance);
        return !res ? res : setShares(owner, balance.balances);
    }

    Res addBalancesSetShares(const CAccounts& accounts) const {
        for (const auto& account : accounts) {
            auto res = addBalanceSetShares(account.first, account.second);
            if (!res) {
                return res;
            }
        }
        return Res::Ok();
    }

    Res subBalancesDelShares(const CAccounts& accounts) const {
        for (const auto& account : accounts) {
            auto res = subBalanceDelShares(account.first, account.second);
            if (!res) {
                return res;
            }
        }
        return Res::Ok();
    }
};

class CCustomTxApplyVisitor : public CCustomTxVisitor
{
    uint64_t time;
public:
    CCustomTxApplyVisitor(const CTransaction& tx,
                          uint32_t height,
                          const CCoinsViewCache& coins,
                          CCustomCSView& mnview,
                          const Consensus::Params& consensus,
                          uint64_t time)

        : CCustomTxVisitor(tx, height, coins, mnview, consensus), time(time) {}

    Res operator()(const CCreateMasterNodeMessage& obj) const {
        auto res = CheckMasternodeCreationTx();
        if (!res) {
            return res;
        }

        CMasternode node;
        CTxDestination dest;
        if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
            if (dest.which() == PKHashType) {
                node.ownerType = 1;
                node.ownerAuthAddress = CKeyID(*boost::get<PKHash>(&dest));
            } else if (dest.which() == WitV0KeyHashType) {
                node.ownerType = 4;
                node.ownerAuthAddress = CKeyID(*boost::get<WitnessV0KeyHash>(&dest));
            }
        }
        node.creationHeight = height;
        node.operatorType = obj.operatorType;
        node.operatorAuthAddress = obj.operatorAuthAddress;
        res = mnview.CreateMasternode(tx.GetHash(), node);
        // Build coinage from the point of masternode creation
        if (res && height >= static_cast<uint32_t>(Params().GetConsensus().DakotaCrescentHeight)) {
            mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);
        }
        return res;
    }

    Res operator()(const CResignMasterNodeMessage& obj) const {
        auto res = HasCollateralAuth(obj);
        return !res ? res : mnview.ResignMasternode(obj, tx.GetHash(), height);
    }

    Res operator()(const CCreateTokenMessage& obj) const {
        auto res = CheckTokenCreationTx();
        if (!res) {
            return res;
        }

        CTokenImplementation token;
        static_cast<CToken&>(token) = obj;

        token.symbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        token.name = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.creationTx = tx.GetHash();
        token.creationHeight = height;

        //check foundation auth
        if (token.IsDAT() && !HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }

        if (static_cast<int>(height) >= consensus.BayfrontHeight) { // formal compatibility if someone cheat and create LPS token on the pre-bayfront node
            if (token.IsPoolShare()) {
                return Res::Err("Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");
            }
        }

        return mnview.CreateToken(token, static_cast<int>(height) < consensus.BayfrontHeight);
    }

    Res operator()(const CUpdateTokenPreAMKMessage& obj) const {
        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair) {
            return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
        }
        const auto& token = pair->second;

        //check foundation auth
        auto res = HasFoundationAuth();

        if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
            CToken newToken = static_cast<CToken>(token); // keeps old and triggers only DAT!
            newToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;

            return !res ? res : mnview.UpdateToken(token.creationTx, newToken, true);
        }
        return res;
    }

    Res operator()(const CUpdateTokenMessage& obj) const {
        auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
        if (!pair) {
            return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
        }
        if (pair->first == DCT_ID{0}) {
            return Res::Err("Can't alter DFI token!"); // may be redundant cause DFI is 'finalized'
        }

        const auto& token = pair->second;

        // need to check it exectly here cause lps has no collateral auth (that checked next)
        if (token.IsPoolShare()) {
            return Res::Err("token %s is the LPS token! Can't alter pool share's tokens!", obj.tokenTx.ToString());
        }

        // check auth, depends from token's "origins"
        const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
        bool isFoundersToken = consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

        auto res = Res::Ok();
        if (isFoundersToken && !(res = HasFoundationAuth())) {
            return res;
        } else if (!(res = HasCollateralAuth(token.creationTx))) {
            return res;
        }

        // Check for isDAT change in non-foundation token after set height
        if (static_cast<int>(height) >= consensus.BayfrontMarinaHeight) {
            //check foundation auth
            if (obj.token.IsDAT() != token.IsDAT() && !HasFoundationAuth()) { //no need to check Authority if we don't create isDAT
                return Res::Err("can't set isDAT to true, tx not from foundation member");
            }
        }

        return mnview.UpdateToken(token.creationTx, obj.token, false);
    }

    Res operator()(const CMintTokensMessage& obj) const {
        // check auth and increase balance of token's owner
        for (const auto& kv : obj.balances) {
            DCT_ID tokenId = kv.first;

            auto token = mnview.GetToken(kv.first);
            if (!token) {
                return Res::Err("token %s does not exist!", tokenId.ToString());
            }
            auto tokenImpl = static_cast<const CTokenImplementation&>(*token);

            auto mintable = MintableToken(tokenId, tokenImpl);
            if (!mintable) {
                return std::move(mintable);
            }
            auto minted = mnview.AddMintedTokens(tokenImpl.creationTx, kv.second);
            if (!minted) {
                return minted;
            }
            mnview.CalculateOwnerRewards(*mintable.val, height);
            auto res = mnview.AddBalance(*mintable.val, CTokenAmount{kv.first, kv.second});
            if (!res) {
                return res;
            }
        }
        return Res::Ok();
    }

    Res operator()(const CCreatePoolPairMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        if (obj.poolPair.commission < 0 || obj.poolPair.commission > COIN) {
            return Res::Err("wrong commission");
        }

        /// @todo ownerAddress validity checked only in rpc. is it enough?
        CPoolPair poolPair(obj.poolPair);
        auto pairSymbol = obj.pairSymbol;
        poolPair.creationTx = tx.GetHash();
        poolPair.creationHeight = height;
        auto& rewards = poolPair.rewards;

        auto tokenA = mnview.GetToken(poolPair.idTokenA);
        if (!tokenA) {
            return Res::Err("token %s does not exist!", poolPair.idTokenA.ToString());
        }

        auto tokenB = mnview.GetToken(poolPair.idTokenB);
        if (!tokenB) {
            return Res::Err("token %s does not exist!", poolPair.idTokenB.ToString());
        }

        if (pairSymbol.empty()) {
            pairSymbol = trim_ws(tokenA->symbol + "-" + tokenB->symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        } else {
            pairSymbol = trim_ws(pairSymbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        }

        CTokenImplementation token;
        token.flags = (uint8_t)CToken::TokenFlags::DAT |
                      (uint8_t)CToken::TokenFlags::LPS |
                      (uint8_t)CToken::TokenFlags::Tradeable |
                      (uint8_t)CToken::TokenFlags::Finalized;

        token.name = trim_ws(tokenA->name + "-" + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
        token.symbol = pairSymbol;
        token.creationTx = tx.GetHash();
        token.creationHeight = height;

        auto tokenId = mnview.CreateToken(token, false);
        if (!tokenId) {
            return std::move(tokenId);
        }

        rewards = obj.rewards;
        if (!rewards.balances.empty()) {
            // Check tokens exist and remove empty reward amounts
            auto res = eraseEmptyBalances(rewards.balances);
            if (!res) {
                return res;
            }
        }

        return mnview.SetPoolPair(tokenId, height, poolPair);
    }

    Res operator()(const CUpdatePoolPairMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }

        auto rewards = obj.rewards;
        if (!rewards.balances.empty()) {
            // Check for special case to wipe rewards
            if (!(rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
            && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max())) {
                // Check if tokens exist and remove empty reward amounts
                auto res = eraseEmptyBalances(rewards.balances);
                if (!res) {
                    return res;
                }
            }
        }
        return mnview.UpdatePoolPair(obj.poolId, height, obj.status, obj.commission, obj.ownerAddress, rewards);
    }

    Res operator()(const CPoolSwapMessage& obj) const {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        auto poolPair = mnview.GetPoolPair(obj.idTokenFrom, obj.idTokenTo);
        if (!poolPair) {
            return Res::Err("can't find the poolpair!");
        }

        CPoolPair& pp = poolPair->second;
        return pp.Swap({obj.idTokenFrom, obj.amountFrom}, obj.maxPrice, [&] (const CTokenAmount &tokenAmount) {
            auto res = mnview.SetPoolPair(poolPair->first, height, pp);
            if (!res) {
                return res;
            }
            mnview.CalculateOwnerRewards(obj.from, height);
            mnview.CalculateOwnerRewards(obj.to, height);
            res = mnview.SubBalance(obj.from, {obj.idTokenFrom, obj.amountFrom});
            return !res ? res : mnview.AddBalance(obj.to, tokenAmount);
        }, static_cast<int>(height));
    }

    Res operator()(const CLiquidityMessage& obj) const {
        CBalances sumTx = SumAllTransfers(obj.from);
        if (sumTx.balances.size() != 2) {
            return Res::Err("the pool pair requires two tokens");
        }

        std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
        std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

        // checked internally too. remove here?
        if (amountA.second <= 0 || amountB.second <= 0) {
            return Res::Err("amount cannot be less than or equal to zero");
        }

        auto pair = mnview.GetPoolPair(amountA.first, amountB.first);
        if (!pair) {
            return Res::Err("there is no such pool pair");
        }

        for (const auto& kv : obj.from) {
            if (!HasAuth(kv.first)) {
                return Res::Err("tx must have at least one input from account owner");
            }
        }

        for (const auto& kv : obj.from) {
            mnview.CalculateOwnerRewards(kv.first, height);
            auto res = mnview.SubBalances(kv.first, kv.second);
            if (!res) {
                return res;
            }
        }

        const auto& lpTokenID = pair->first;
        auto& pool = pair->second;

        // normalize A & B to correspond poolpair's tokens
        if (amountA.first != pool.idTokenA) {
            std::swap(amountA, amountB);
        }

        bool slippageProtection = static_cast<int>(height) >= consensus.BayfrontMarinaHeight;
        auto res = pool.AddLiquidity(amountA.second, amountB.second, [&] /*onMint*/(CAmount liqAmount) {

            CBalances balance{TAmounts{{lpTokenID, liqAmount}}};
            return addBalanceSetShares(obj.shareAddress, balance);
        }, slippageProtection);

        return !res ? res : mnview.SetPoolPair(lpTokenID, height, pool);
    }

    Res operator()(const CRemoveLiquidityMessage& obj) const {
        const auto& from = obj.from;
        auto amount = obj.amount;

        // checked internally too. remove here?
        if (amount.nValue <= 0) {
            return Res::Err("amount cannot be less than or equal to zero");
        }

        auto pair = mnview.GetPoolPair(amount.nTokenId);
        if (!pair) {
            return Res::Err("there is no such pool pair");
        }

        if (!HasAuth(from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        CPoolPair& pool = pair.get();

        // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
        {
            CBalances balance{TAmounts{{amount.nTokenId, amount.nValue}}};
            auto res = subBalanceDelShares(from, balance);
            if (!res) {
                return res;
            }
        }

        auto res = pool.RemoveLiquidity(amount.nValue, [&] (CAmount amountA, CAmount amountB) {

            mnview.CalculateOwnerRewards(from, height);
            CBalances balances{TAmounts{{pool.idTokenA, amountA}, {pool.idTokenB, amountB}}};
            return mnview.AddBalances(from, balances);
        });

        return !res ? res : mnview.SetPoolPair(amount.nTokenId, height, pool);
    }

    Res operator()(const CUtxosToAccountMessage& obj) const {
        // check enough tokens are "burnt"
        const auto burnt = BurntTokens(tx);
        if (!burnt) {
            return burnt;
        }

        const auto mustBeBurnt = SumAllTransfers(obj.to);
        if (*burnt.val != mustBeBurnt) {
            return Res::Err("transfer tokens mismatch burnt tokens: (%s) != (%s)", mustBeBurnt.ToString(), burnt.val->ToString());
        }

        // transfer
        return addBalancesSetShares(obj.to);
    }

    Res operator()(const CAccountToUtxosMessage& obj) const {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        // check that all tokens are minted, and no excess tokens are minted
        auto minted = MintedTokens(tx, obj.mintingOutputsStart);
        if (!minted) {
            return std::move(minted);
        }

        if (obj.balances != *minted.val) {
            return Res::Err("amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)", minted.val->ToString(), obj.balances.ToString());
        }

        // block for non-DFI transactions
        for (const auto& kv : obj.balances.balances) {
            const DCT_ID& tokenId = kv.first;
            if (tokenId != DCT_ID{0}) {
                return Res::Err("only available for DFI transactions");
            }
        }

        // transfer
        return subBalanceDelShares(obj.from, obj.balances);
    }

    Res operator()(const CAccountToAccountMessage& obj) const {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        // transfer
        auto res = subBalanceDelShares(obj.from, SumAllTransfers(obj.to));
        return !res ? res : addBalancesSetShares(obj.to);
    }

    Res operator()(const CAnyAccountsToAccountsMessage& obj) const {
        // check auth
        for (const auto& kv : obj.from) {
            if (!HasAuth(kv.first)) {
                return Res::Err("tx must have at least one input from account owner");
            }
        }

        // compare
        const auto sumFrom = SumAllTransfers(obj.from);
        const auto sumTo = SumAllTransfers(obj.to);

        if (sumFrom != sumTo) {
            return Res::Err("sum of inputs (from) != sum of outputs (to)");
        }

        // transfer
        // substraction
        auto res = subBalancesDelShares(obj.from);
        // addition
        return !res ? res : addBalancesSetShares(obj.to);
    }

    Res operator()(const CGovernanceMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        for(const auto& var : obj.govs) {
            auto result = var->Validate(mnview);
            if (!result) {
                return Res::Err("%s: %s", var->GetName(), result.msg);
            }
            auto res = var->Apply(mnview, height);
            if (!res) {
                return Res::Err("%s: %s", var->GetName(), res.msg);
            }
            auto add = mnview.SetVariable(*var);
            if (!add) {
                return Res::Err("%s: %s", var->GetName(), add.msg);
            }
        }
        return Res::Ok();
    }

    Res operator()(const CCustomTxMessageNone&) const {
        return Res::Ok();
    }
};

class CCustomTxRevertVisitor : public CCustomTxVisitor
{
public:
    using CCustomTxVisitor::CCustomTxVisitor;

    template<typename T>
    Res operator()(const T&) const {
        return Res::Ok();
    }

    Res operator()(const CCreateMasterNodeMessage& obj) const {
        auto res = CheckMasternodeCreationTx();
        return !res ? res : mnview.UnCreateMasternode(tx.GetHash());
    }

    Res operator()(const CResignMasterNodeMessage& obj) const {
        auto res = HasCollateralAuth(obj);
        return !res ? res : mnview.UnResignMasternode(obj, tx.GetHash());
    }

    Res operator()(const CCreateTokenMessage& obj) const {
        auto res = CheckTokenCreationTx();
        return !res ? res : mnview.RevertCreateToken(tx.GetHash());
    }

    Res operator()(const CCreatePoolPairMessage& obj) const {
        //check foundation auth
        if (!HasFoundationAuth()) {
            return Res::Err("tx not from foundation member");
        }
        auto pool = mnview.GetPoolPair(obj.poolPair.idTokenA, obj.poolPair.idTokenB);
        if (!pool) {
            return Res::Err("no such poolPair tokenA %s, tokenB %s",
                            obj.poolPair.idTokenA.ToString(),
                            obj.poolPair.idTokenB.ToString());
        }

        return mnview.RevertCreateToken(tx.GetHash());
    }
};

Res CustomMetadataParse(uint32_t height, const Consensus::Params& consensus, const std::vector<unsigned char>& metadata, CCustomTxMessage& txMessage) {
    try {
        return boost::apply_visitor(CCustomMetadataParseVisitor(height, consensus, metadata), txMessage);
    } catch (const std::exception& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

Res CustomTxVisit(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage, uint64_t time) {
    try {
        return boost::apply_visitor(CCustomTxApplyVisitor(tx, height, coins, mnview, consensus, time), txMessage);
    } catch (const std::exception& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

Res CustomTxRevert(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, uint32_t height, const Consensus::Params& consensus, const CCustomTxMessage& txMessage) {
    try {
        return boost::apply_visitor(CCustomTxRevertVisitor(tx, height, coins, mnview, consensus), txMessage);
    } catch (const std::exception& e) {
        return Res::Err(e.what());
    } catch (...) {
        return Res::Err("unexpected error");
    }
}

bool ShouldReturnNonFatalError(const CTransaction& tx, uint32_t height) {
    static const std::map<uint32_t, uint256> skippedTx = {
        { 471222, uint256S("0ab0b76352e2d865761f4c53037041f33e1200183d55cdf6b09500d6f16b7329") },
    };
    auto it = skippedTx.find(height);
    return it != skippedTx.end() && it->second == tx.GetHash();
}

Res RevertCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height) {
    if (tx.IsCoinBase() && height > 0) { // genesis contains custom coinbase txs
        return Res::Ok();
    }
    std::vector<unsigned char> metadata;
    auto txType = GuessCustomTxType(tx, metadata);
    if (txType == CustomTxType::None) {
        return Res::Ok();
    }
    auto txMessage = customTypeToMessage(txType);
    auto res = CustomMetadataParse(height, consensus, metadata, txMessage);
    return !res ? res : CustomTxRevert(mnview, coins, tx, height, consensus, txMessage);
}

Res ApplyCustomTx(CCustomCSView& mnview, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint64_t time, uint32_t txn) {
    auto res = Res::Ok();
    if (tx.IsCoinBase() && height > 0) { // genesis contains custom coinbase txs
        return res;
    }
    std::vector<unsigned char> metadata;
    auto txType = GuessCustomTxType(tx, metadata);
    if (txType == CustomTxType::None) {
        return res;
    }
    auto txMessage = customTypeToMessage(txType);
    CAccountsHistoryStorage view(mnview, height, txn, tx.GetHash(), uint8_t(txType));
    if ((res = CustomMetadataParse(height, consensus, metadata, txMessage))) {
        res = CustomTxVisit(view, coins, tx, height, consensus, txMessage, time);
    }
    // list of transactions which aren't allowed to fail:
    if (!res) {
        res.msg = strprintf("%sTx: %s", ToString(txType), res.msg);

        if (NotAllowedToFail(txType, height)) {
            if (ShouldReturnNonFatalError(tx, height)) {
                return res;
            }
            res.code |= CustomTxErrCodes::Fatal;
        }
        if (height >= consensus.DakotaHeight) {
            res.code |= CustomTxErrCodes::Fatal;
        }
        return res;
    }

    // construct undo
    auto& flushable = dynamic_cast<CFlushableStorageKV&>(view.GetRaw());
    auto undo = CUndo::Construct(mnview.GetRaw(), flushable.GetRaw());
    // flush changes
    view.Flush();
    // write undo
    if (!undo.before.empty()) {
        mnview.SetUndo(UndoKey{height, tx.GetHash()}, undo);
    }
    return res;
}

ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView & mnview, CTransaction const & tx, int height, uint256 const & prevStakeModifier, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if (height >= consensusParams.DakotaHeight) {
        return Res::Err("Old anchor TX type after Dakota fork. Height %d", height);
    }

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    CAnchorFinalizationMessage finMsg;
    ss >> finMsg;

    auto rewardTx = mnview.GetRewardForAnchor(finMsg.btcTxHash);
    if (rewardTx) {
        return Res::ErrDbg("bad-ar-exists", "reward for anchor %s already exists (tx: %s)",
                           finMsg.btcTxHash.ToString(), (*rewardTx).ToString());
    }

    if (!finMsg.CheckConfirmSigs()) {
        return Res::ErrDbg("bad-ar-sigs", "anchor signatures are incorrect");
    }

    if (finMsg.sigs.size() < GetMinAnchorQuorum(finMsg.currentTeam)) {
        return Res::ErrDbg("bad-ar-sigs-quorum", "anchor sigs (%d) < min quorum (%) ",
                           finMsg.sigs.size(), GetMinAnchorQuorum(finMsg.currentTeam));
    }

    // check reward sum
    if (height >= consensusParams.AMKHeight) {
        auto const cbValues = tx.GetValuesOut();
        if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0})
            return Res::ErrDbg("bad-ar-wrong-tokens", "anchor reward should be payed only in Defi coins");

        auto const anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
        if (cbValues.begin()->second != anchorReward) {
            return Res::ErrDbg("bad-ar-amount", "anchor pays wrong amount (actual=%d vs expected=%d)",
                               cbValues.begin()->second, anchorReward);
        }
    }
    else { // pre-AMK logic
        auto anchorReward = GetAnchorSubsidy(finMsg.anchorHeight, finMsg.prevAnchorHeight, consensusParams);
        if (tx.GetValueOut() > anchorReward) {
            return Res::ErrDbg("bad-ar-amount", "anchor pays too much (actual=%d vs limit=%d)",
                               tx.GetValueOut(), anchorReward);
        }
    }

    CTxDestination destination = finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(finMsg.rewardKeyID));
    if (tx.vout[1].scriptPubKey != GetScriptForDestination(destination)) {
        return Res::ErrDbg("bad-ar-dest", "anchor pay destination is incorrect");
    }

    if (finMsg.currentTeam != mnview.GetCurrentTeam()) {
        return Res::ErrDbg("bad-ar-curteam", "anchor wrong current team");
    }

    if (finMsg.nextTeam != mnview.CalcNextTeam(prevStakeModifier)) {
        return Res::ErrDbg("bad-ar-nextteam", "anchor wrong next team");
    }
    mnview.SetTeam(finMsg.nextTeam);
    if (height >= consensusParams.AMKHeight) {
        mnview.SetCommunityBalance(CommunityAccountType::AnchorReward, 0); // just reset
    }
    else {
        mnview.SetFoundationsDebt(mnview.GetFoundationsDebt() + tx.GetValueOut());
    }

    return { finMsg.btcTxHash, Res::Ok() };
}


ResVal<uint256> ApplyAnchorRewardTxPlus(CCustomCSView & mnview, CTransaction const & tx, int height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if (height < consensusParams.DakotaHeight) {
        return Res::Err("New anchor TX type before Dakota fork. Height %d", height);
    }

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    CAnchorFinalizationMessagePlus finMsg;
    ss >> finMsg;

    auto rewardTx = mnview.GetRewardForAnchor(finMsg.btcTxHash);
    if (rewardTx) {
        return Res::ErrDbg("bad-ar-exists", "reward for anchor %s already exists (tx: %s)",
                           finMsg.btcTxHash.ToString(), (*rewardTx).ToString());
    }

    // Miner used confirm team at chain height when creating this TX, this is height - 1.
    if (!finMsg.CheckConfirmSigs(height - 1)) {
        return Res::ErrDbg("bad-ar-sigs", "anchor signatures are incorrect");
    }

    auto team = pcustomcsview->GetConfirmTeam(height - 1);
    if (!team) {
        return Res::ErrDbg("bad-ar-team", "could not get confirm team for height: %d", height - 1);
    }

    if (finMsg.sigs.size() < GetMinAnchorQuorum(*team)) {
        return Res::ErrDbg("bad-ar-sigs-quorum", "anchor sigs (%d) < min quorum (%) ",
                           finMsg.sigs.size(), GetMinAnchorQuorum(*team));
    }

    // Make sure anchor block height and hash exist in chain.
    CBlockIndex* anchorIndex = ::ChainActive()[finMsg.anchorHeight];
    if (!anchorIndex) {
        return Res::ErrDbg("bad-ar-height", "Active chain does not contain block height %d. Chain height %d",
                           finMsg.anchorHeight, ::ChainActive().Height());
    }

    if (anchorIndex->GetBlockHash() != finMsg.dfiBlockHash) {
        return Res::ErrDbg("bad-ar-hash", "Anchor and blockchain mismatch at height %d. Expected %s found %s",
                           finMsg.anchorHeight, anchorIndex->GetBlockHash().ToString(), finMsg.dfiBlockHash.ToString());
    }

    // check reward sum
    auto const cbValues = tx.GetValuesOut();
    if (cbValues.size() != 1 || cbValues.begin()->first != DCT_ID{0})
        return Res::ErrDbg("bad-ar-wrong-tokens", "anchor reward should be paid in DFI only");

    auto const anchorReward = mnview.GetCommunityBalance(CommunityAccountType::AnchorReward);
    if (cbValues.begin()->second != anchorReward) {
        return Res::ErrDbg("bad-ar-amount", "anchor pays wrong amount (actual=%d vs expected=%d)",
                           cbValues.begin()->second, anchorReward);
    }

    CTxDestination destination = finMsg.rewardKeyType == 1 ? CTxDestination(PKHash(finMsg.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(finMsg.rewardKeyID));
    if (tx.vout[1].scriptPubKey != GetScriptForDestination(destination)) {
        return Res::ErrDbg("bad-ar-dest", "anchor pay destination is incorrect");
    }

    mnview.SetCommunityBalance(CommunityAccountType::AnchorReward, 0); // just reset
    mnview.AddRewardForAnchor(finMsg.btcTxHash, tx.GetHash());

    // Store reward data for RPC info
    mnview.AddAnchorConfirmData(CAnchorConfirmDataPlus{finMsg});

    return { finMsg.btcTxHash, Res::Ok() };
}


bool IsMempooledCustomTxCreate(const CTxMemPool & pool, const uint256 & txid)
{
    CTransactionRef ptx = pool.get(txid);
    if (ptx) {
        std::vector<unsigned char> dummy;
        CustomTxType txType = GuessCustomTxType(*ptx, dummy);
        return txType == CustomTxType::CreateMasternode || txType == CustomTxType::CreateToken;
    }
    return false;
}

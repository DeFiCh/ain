// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>
#include <masternodes/balances.h>
#include <masternodes/mn_checks.h>
#include <masternodes/res.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <txmempool.h>
#include <streams.h>
#include <univalue/include/univalue.h>

#include <algorithm>
#include <sstream>
#include <cstring>

using namespace std;

static ResVal<CBalances> BurntTokens(CTransaction const & tx) {
    CBalances balances{};
    for (const auto& out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            auto res = balances.Add(out.TokenAmount());
            if (!res.ok) {
                return {res};
            }
        }
    }
    return {balances, Res::Ok()};
}

static ResVal<CBalances> MintedTokens(CTransaction const & tx, uint32_t mintingOutputsStart) {
    CBalances balances{};
    for (uint32_t i = mintingOutputsStart; i < (uint32_t) tx.vout.size(); i++) {
        auto res = balances.Add(tx.vout[i].TokenAmount());
        if (!res.ok) {
            return {res};
        }
    }
    return {balances, Res::Ok()};
}

CPubKey GetPubkeyFromScriptSig(CScript const & scriptSig)
{
    CScript::const_iterator pc = scriptSig.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    // Signature first, then pubkey. I think, that in all cases it will be OP_PUSHDATA1, but...
    if (!scriptSig.GetOp(pc, opcode, data) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4))
    {
        return CPubKey();
    }
    if (!scriptSig.GetOp(pc, opcode, data) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4))
    {
        return CPubKey();
    }
    return CPubKey(data);
}

// not used for now (cause works only with signed inputs)
bool HasAuth(CTransaction const & tx, CKeyID const & auth)
{
    for (auto input : tx.vin)
    {
        if (input.scriptWitness.IsNull()) {
            if (GetPubkeyFromScriptSig(input.scriptSig).GetID() == auth)
               return true;
        }
        else {
            auto test = CPubKey(input.scriptWitness.stack.back());
            if (test.GetID() == auth)
               return true;
        }
    }
    return false;
}

bool HasAuth(CTransaction const & tx, CCoinsViewCache const & coins, CScript const & auth)
{
    for (auto input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        assert(!coin.IsSpent());
        if (coin.out.scriptPubKey == auth)
            return true;
    }
    return false;
}

bool HasCollateralAuth(CTransaction const & tx, CCoinsViewCache const & coins, uint256 const & collateralTx)
{
    const Coin& auth = coins.AccessCoin(COutPoint(collateralTx, 1)); // always n=1 output
    return HasAuth(tx, coins, auth.out.scriptPubKey);
}

bool HasFoundationAuth(CTransaction const & tx, CCoinsViewCache const & coins, Consensus::Params const & consensusParams)
{
    for (auto input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        assert(!coin.IsSpent());
        if (consensusParams.foundationMembers.find(coin.out.scriptPubKey) != consensusParams.foundationMembers.end())
            return true;
    }
    return false;
}

Res ApplyCustomTx(CCustomCSView & base_mnview, CCoinsViewCache const & coins, CTransaction const & tx, Consensus::Params const & consensusParams, uint32_t height, bool isCheck)
{
    Res res = Res::Ok();

    if ((tx.IsCoinBase() && height > 0) || tx.vout.empty()) { // genesis contains custom coinbase txs
        return Res::Ok(); // not "custom" tx
    }

    CCustomCSView mnview(base_mnview);

    try {
        // Check if it is custom tx with metadata
        std::vector<unsigned char> metadata;
        CustomTxType guess = GuessCustomTxType(tx, metadata);
        switch (guess)
        {
            case CustomTxType::CreateMasternode:
                res = ApplyCreateMasternodeTx(mnview, tx, height, metadata);
                break;
            case CustomTxType::ResignMasternode:
                res = ApplyResignMasternodeTx(mnview, coins, tx, height, metadata);
                break;
            case CustomTxType::CreateToken:
                res = ApplyCreateTokenTx(mnview, coins, tx, height, metadata, consensusParams);
                break;
            case CustomTxType::UpdateToken:
                res = ApplyUpdateTokenTx(mnview, coins, tx, height, metadata, consensusParams);
                break;
            case CustomTxType::MintToken:
                res = ApplyMintTokenTx(mnview, coins, tx, height, metadata, consensusParams);
                break;
            case CustomTxType::UtxosToAccount:
                res = ApplyUtxosToAccountTx(mnview, tx, height, metadata, consensusParams);
                break;
            case CustomTxType::AccountToUtxos:
                res = ApplyAccountToUtxosTx(mnview, coins, tx, height, metadata, consensusParams);
                break;
            case CustomTxType::AccountToAccount:
                res = ApplyAccountToAccountTx(mnview, coins, tx, height, metadata, consensusParams);
                break;
            default:
                return Res::Ok(); // not "custom" tx
        }
        // list of transactions which aren't allowed to fail:
        if (!res.ok && NotAllowedToFail(guess)) {
            res.code |= CustomTxErrCodes::Fatal;
        }
    } catch (std::exception& e) {
        res = Res::Err(e.what());
    } catch (...) {
        res = Res::Err("unexpected error");
    }

    if (!res.ok || isCheck) { // 'isCheck' - don't create undo nor flush to the upper view
        return res;
    }

    // construct undo
    auto& flushable = dynamic_cast<CFlushableStorageKV&>(mnview.GetRaw());
    auto undo = CUndo::Construct(base_mnview.GetRaw(), flushable.GetRaw());
    // flush changes
    mnview.Flush();
    // write undo
    if (!undo.before.empty()) {
        base_mnview.SetUndo(UndoKey{height, tx.GetHash()}, undo);
    }

    return res;
}

/*
 * Checks if given tx is 'txCreateMasternode'. Creates new MN if all checks are passed
 * Issued by: any
 */
Res ApplyCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata)
{
    const std::string base{"Creation of masternode"};
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetMnCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}
        ) {
        return Res::Err("%s: %s", base, "malformed tx vouts (wrong creation fee or collateral amount)");
    }

    CMasternode node;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> node.operatorType;
    ss >> node.operatorAuthAddress;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", base,  ss.size());
    }

    CTxDestination dest;
    if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
        if (dest.which() == 1) {
            node.ownerType = 1;
            node.ownerAuthAddress = CKeyID(*boost::get<PKHash>(&dest));
        }
        else if (dest.which() == 4) {
            node.ownerType = 4;
            node.ownerAuthAddress = CKeyID(*boost::get<WitnessV0KeyHash>(&dest));
        }
    }
    node.creationHeight = height;

    auto res = mnview.CreateMasternode(tx.GetHash(), node);
    if (!res.ok) {
        return Res::Err("%s: %s", base, res.msg);
    }
    return Res::Ok(base);
}

Res ApplyResignMasternodeTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, const std::vector<unsigned char> & metadata)
{
    const std::string base{"Resigning of masternode"};

    if (metadata.size() != sizeof(uint256)) {
        return Res::Err("%s: metadata must contain 32 bytes", base);
    }
    uint256 nodeId(metadata);
    auto const node = mnview.GetMasternode(nodeId);
    if (!node) {
        return Res::Err("%s: node %s does not exist", base, nodeId.ToString());
    }
    if (!HasCollateralAuth(tx, coins, nodeId)) {
        return Res::Err("%s %s: %s", base, nodeId.ToString(), "tx must have at least one input from masternode owner");
    }

    auto res = mnview.ResignMasternode(nodeId, tx.GetHash(), height);
    if (!res.ok) {
        return Res::Err("%s %s: %s", base, nodeId.ToString(), res.msg);
    }
    return Res::Ok(base);
}

Res ApplyCreateTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    const std::string base{"Token creation"};
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}
        ) {
        return Res::Err("%s: %s", base, "malformed tx vouts (wrong creation fee or collateral amount)");
    }

    CTokenImplementation token;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CToken &>(token);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", base,  ss.size());
    }
    token.symbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    if (token.symbol.size() == 0 || IsDigit(token.symbol[0])) {
        return Res::Err("token symbol '%s' should be non-empty and starts with a letter", token.symbol);
    }
    if (token.symbol.find('#') != std::string::npos) {
        return Res::Err("%s: token symbol must not contain '#'", base);
    }

    token.name = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    //check foundation auth
    if((token.flags & (uint8_t)CToken::TokenFlags::isDAT) && !HasFoundationAuth(tx, coins, Params().GetConsensus()))
    {//no need to check Authority if we don't create isDAT
        return Res::Err("%s: %s", base, "Is not a foundation owner");
    }

    auto res = mnview.CreateToken(token);
    if (!res.ok) {
        return Res::Err("%s %s: %s", base, token.symbol, res.msg);
    }

    return Res::Ok(base);
}

Res ApplyUpdateTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    const std::string base{"Token update"};

    uint256 tokenTx;
    bool isDAT;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> tokenTx;
    ss >> isDAT;
    if (!ss.empty()) {
        return Res::Err("Token Update: deserialization failed: excess %d bytes", ss.size());
    }

    auto pair = mnview.GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("%s: token with creationTx %s does not exist", base, tokenTx.ToString());
    }
    CTokenImplementation const & token = pair->second;

    //check foundation auth
    if (!HasFoundationAuth(tx, coins, Params().GetConsensus())) {
        return Res::Err("%s: %s", base, "Is not a foundation owner");
    }

    if((token.flags & (uint8_t)CToken::TokenFlags::isDAT) != isDAT && pair->first.v >= 128)
    {
        auto res = mnview.UpdateToken(token.creationTx);
        if (!res.ok) {
            return Res::Err("%s %s: %s", base, token.symbol, res.msg);
        }
    }
    return Res::Ok(base);
}

Res ApplyMintTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    const std::string base{"Token minting"};

    CBalances minted;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> minted;
    if (!ss.empty()) {
        return Res::Err("MintToken tx deserialization failed: excess %d bytes", ss.size());
    }

    // check auth and increase balance of token's owner
    for (auto const & kv : minted.balances) {
        DCT_ID tokenId = kv.first;
        if (tokenId < CTokensView::DCT_ID_START)
            return Res::Err("%s: token %s is a 'stable coin', can't mint stable coin!", base, tokenId.ToString());

        auto token = mnview.GetToken(kv.first);
        if (!token) {
            throw Res::Err("%s: token %s does not exist!", tokenId.ToString());
        }

        auto tokenImpl = static_cast<CTokenImplementation const& >(*token);
        if (tokenImpl.destructionTx != uint256{}) {
            throw Res::Err("%s: token %s already destroyed at height %i by tx %s", base, tokenImpl.symbol,
                                         tokenImpl.destructionHeight, tokenImpl.destructionTx.GetHex());
        }
        const Coin& auth = coins.AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
        if (!HasAuth(tx, coins, auth.out.scriptPubKey)) {
            return Res::Err("%s: %s", base, "tx must have at least one input from token owner");
        }
        auto mint = mnview.AddMintedTokens(tokenImpl.creationTx, kv.second);
        if (!mint.ok) {
            return Res::Err("%s %s: %s", base, tokenImpl.symbol, mint.msg);
        }
        const auto res = mnview.AddBalance(auth.out.scriptPubKey, CTokenAmount{kv.first,kv.second});
        if (!res.ok) {
            return Res::Err("%s: %s", base, res.msg);
        }
    }

    return Res::Ok(base);
}


Res ApplyUtxosToAccountTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // deserialize
    CUtxosToAccountMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("UtxosToAccount tx deserialization failed: excess %d bytes", ss.size());
    }
    const auto base = strprintf("Transfer UtxosToAccount: %s", msg.ToString());

    // check enough tokens are "burnt"
    const auto burnt = BurntTokens(tx);
    CBalances mustBeBurnt = SumAllTransfers(msg.to);
    if (!burnt.ok) {
        return Res::Err("%s: %s", base, burnt.msg);
    }
    if (burnt.val->balances != mustBeBurnt.balances) {
        return Res::Err("%s: transfer tokens mismatch burnt tokens: (%s) != (%s)", base, mustBeBurnt.ToString(), burnt.val->ToString());
    }
    // transfer
    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", base, res.msg);
        }
    }
    return Res::Ok(base);
}

Res ApplyAccountToUtxosTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // deserialize
    CAccountToUtxosMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("AccountToUtxos tx deserialization failed: excess %d bytes", ss.size());
    }
    const auto base = strprintf("Transfer AccountToUtxos: %s", msg.ToString());

    // check auth
    if (!HasAuth(tx, coins, msg.from)) {
        return Res::Err("%s: %s", base, "tx must have at least one input from account owner");
    }
    // check that all tokens are minted, and no excess tokens are minted
    const auto minted = MintedTokens(tx, msg.mintingOutputsStart);
    if (!minted.ok) {
        return Res::Err("%s: %s", base, minted.msg);
    }
    if (msg.balances != *minted.val) {
        return Res::Err("%s: amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)", base, minted.val->ToString(), msg.balances.ToString());
    }

    // block for non-DFI transactions
    for (auto const & kv : msg.balances.balances) {
        const DCT_ID& tokenId = kv.first;
        if (tokenId != DCT_ID{0}) {
            return Res::Err("AccountToUtxos only available for DFI transactions");
        }
    }

    // transfer
    const auto res = mnview.SubBalances(msg.from, msg.balances);
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", base, res.msg);
    }
    return Res::Ok(base);
}

Res ApplyAccountToAccountTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // deserialize
    CAccountToAccountMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("AccountToAccount tx deserialization failed: excess %d bytes", ss.size());
    }
    const auto base = strprintf("Transfer AccountToAccount: %s", msg.ToString());

    // check auth
    if (!HasAuth(tx, coins, msg.from)) {
        return Res::Err("%s: %s", base, "tx must have at least one input from account owner");
    }
    // transfer
    auto res = mnview.SubBalances(msg.from, SumAllTransfers(msg.to));
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", base, res.msg);
    }
    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", base, res.msg);
        }
    }
    return Res::Ok(base);
}

ResVal<uint256> ApplyAnchorRewardTx(CCustomCSView & mnview, CTransaction const & tx, int height, uint256 const & prevStakeModifier, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams)
{
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
    mnview.AddRewardForAnchor(finMsg.btcTxHash, tx.GetHash());

    return { finMsg.btcTxHash, Res::Ok() };
}


bool IsMempooledCustomTxCreate(const CTxMemPool & pool, const uint256 & txid)
{
    CTransactionRef ptx = pool.get(txid);
    std::vector<unsigned char> dummy;
    if (ptx) {
        CustomTxType txType = GuessCustomTxType(*ptx, dummy);
        return txType == CustomTxType::CreateMasternode || txType == CustomTxType::CreateToken;
    }
    return false;
}

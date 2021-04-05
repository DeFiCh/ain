// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/anchors.h>
#include <masternodes/balances.h>
#include <masternodes/mn_checks.h>
#include <masternodes/res.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <core_io.h>
#include <index/txindex.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <txmempool.h>
#include <streams.h>
#include <validation.h>

#include <univalue.h>

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
        if (!coin.IsSpent() && coin.out.scriptPubKey == auth)
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
        if (!coin.IsSpent() && consensusParams.foundationMembers.find(coin.out.scriptPubKey) != consensusParams.foundationMembers.end())
            return true;
    }
    return false;
}

Res ApplyCustomTx(CCustomCSView & base_mnview, CCoinsViewCache const & coins, CTransaction const & tx, Consensus::Params const & consensusParams, uint32_t height, const uint64_t &time, uint32_t txn, bool isCheck, bool skipAuth)
{
    Res res = Res::Ok();

    if ((tx.IsCoinBase() && height > 0) || tx.vout.empty()) { // genesis contains custom coinbase txs
        return Res::Ok(); // not "custom" tx
    }

    CustomTxType guess;
    std::vector<unsigned char> metadata;

    try {
        guess = GuessCustomTxType(tx, metadata);
    } catch (std::exception& e) {
        return Res::Err("GuessCustomTxType: %s", e.what());
    } catch (...) {
        return Res::Err("GuessCustomTxType: unexpected error");
    }

    CAccountsHistoryStorage mnview(base_mnview, height, txn, tx.GetHash(), (uint8_t)guess);
    try {
        // Check if it is custom tx with metadata
        switch (guess)
        {
            case CustomTxType::CreateMasternode:
                res = ApplyCreateMasternodeTx(mnview, tx, height, time, metadata);
                break;
            case CustomTxType::ResignMasternode:
                res = ApplyResignMasternodeTx(mnview, coins, tx, height, metadata, skipAuth);
                break;
            case CustomTxType::CreateToken:
                res = ApplyCreateTokenTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::UpdateToken:
                res = ApplyUpdateTokenTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::UpdateTokenAny:
                res = ApplyUpdateTokenAnyTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::MintToken:
                res = ApplyMintTokenTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::CreatePoolPair:
                res = ApplyCreatePoolPairTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::UpdatePoolPair:
                res = ApplyUpdatePoolPairTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::PoolSwap:
                res = ApplyPoolSwapTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::AddPoolLiquidity:
                res = ApplyAddPoolLiquidityTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::RemovePoolLiquidity:
                res = ApplyRemovePoolLiquidityTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::UtxosToAccount:
                res = ApplyUtxosToAccountTx(mnview, tx, height, metadata, consensusParams);
                break;
            case CustomTxType::AccountToUtxos:
                res = ApplyAccountToUtxosTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::AccountToAccount:
                res = ApplyAccountToAccountTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::SetGovVariable:
                res = ApplySetGovernanceTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::AnyAccountsToAccounts:
                res = ApplyAnyAccountsToAccountsTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::ICXCreateOrder:
                res = ApplyICXCreateOrderTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::ICXMakeOffer:
                res = ApplyICXMakeOfferTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::ICXSubmitDFCHTLC:
                res = ApplyICXSubmitDFCHTLCTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::ICXSubmitEXTHTLC:
                res = ApplyICXSubmitEXTHTLCTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::ICXClaimDFCHTLC:
                res = ApplyICXClaimDFCHTLCTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            case CustomTxType::ICXCloseOrder:
                res = ApplyICXCloseOrderTx(mnview, coins, tx, height, metadata, consensusParams, skipAuth);
                break;
            default:
                return Res::Ok(); // not "custom" tx
        }
        // list of transactions which aren't allowed to fail,
        // post Dakota height all failed txs are marked as fatal
        if (!res.ok && (NotAllowedToFail(guess, height) || height >= consensusParams.DakotaHeight)) {
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
Res ApplyCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, const uint64_t &time, std::vector<unsigned char> const & metadata, UniValue *rpcInfo)
{
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetMnCollateralAmount(height) || tx.vout[1].nTokenId != DCT_ID{0}
        ) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong creation fee or collateral amount)");
    }

    CMasternode node;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> node.operatorType;
    ss >> node.operatorAuthAddress;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__,  ss.size());
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

    // Return here to avoid addresses exit error
    if (rpcInfo) {
        rpcInfo->pushKV("collateralamount", ValueFromAmount(GetMnCollateralAmount(height)));
        rpcInfo->pushKV("masternodeoperator", EncodeDestination(node.operatorType == 1 ? CTxDestination(PKHash(node.operatorAuthAddress)) :
                                                CTxDestination(WitnessV0KeyHash(node.operatorAuthAddress))));
        return Res::Ok();
    }

    auto res = mnview.CreateMasternode(tx.GetHash(), node);
    if (!res.ok) {
        return Res::Err("%s: %s", __func__, res.msg);
    }

    // Build coinage from the point of masternode creation
    if (height >= static_cast<uint32_t>(Params().GetConsensus().DakotaCrescentHeight))
    {
        mnview.SetMasternodeLastBlockTime(node.operatorAuthAddress, static_cast<uint32_t>(height), time);
    }

    return Res::Ok();
}

Res ApplyResignMasternodeTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, const std::vector<unsigned char> & metadata, bool skipAuth, UniValue *rpcInfo)
{
    if (metadata.size() != sizeof(uint256)) {
        return Res::Err("%s: metadata must contain 32 bytes", __func__);
    }
    uint256 nodeId(metadata);
    auto const node = mnview.GetMasternode(nodeId);
    if (!node) {
        return Res::Err("%s: node %s does not exist", __func__, nodeId.ToString());
    }
    if (!skipAuth && !HasCollateralAuth(tx, coins, nodeId)) {
        return Res::Err("%s %s: %s", __func__, nodeId.ToString(), "tx must have at least one input from masternode owner");
    }

    // Return here to avoid state is not ENABLED error
    if (rpcInfo) {
        rpcInfo->pushKV("id", nodeId.GetHex());
        return Res::Ok();
    }

    auto res = mnview.ResignMasternode(nodeId, tx.GetHash(), height);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, nodeId.ToString(), res.msg);
    }
    return Res::Ok();
}

Res ApplyCreateTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}
        ) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong creation fee or collateral amount)");
    }

    CTokenImplementation token;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CToken &>(token);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    token.symbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    //check foundation auth
    if((token.IsDAT()) && !skipAuth && !HasFoundationAuth(tx, coins, consensusParams))
    {//no need to check Authority if we don't create isDAT
        return Res::Err("%s: %s", __func__, "tx not from foundation member");
    }

    if ((int)height >= consensusParams.BayfrontHeight) { // formal compatibility if someone cheat and create LPS token on the pre-bayfront node
        if(token.IsPoolShare()) {
            return Res::Err("%s: %s", __func__, "Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");
        }
    }

    // Return here to avoid already exist error
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", token.creationTx.GetHex());
        rpcInfo->pushKV("name", token.name);
        rpcInfo->pushKV("symbol", token.symbol);
        rpcInfo->pushKV("isDAT", token.IsDAT());
        rpcInfo->pushKV("mintable", token.IsMintable());
        rpcInfo->pushKV("tradeable", token.IsTradeable());
        rpcInfo->pushKV("finalized", token.IsFinalized());

        return Res::Ok();
    }

    auto res = mnview.CreateToken(token, (int)height < consensusParams.BayfrontHeight);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, token.symbol, res.msg);
    }

    return Res::Ok();
}

/// @deprecated version of updatetoken tx, prefer using UpdateTokenAny after "bayfront" fork
Res ApplyUpdateTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    if ((int)height >= consensusParams.BayfrontHeight) {
        return Res::Err("Old-style updatetoken tx forbidden after Bayfront height");
    }

    uint256 tokenTx;
    bool isDAT;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> tokenTx;
    ss >> isDAT;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    auto pair = mnview.GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("%s: token with creationTx %s does not exist", __func__, tokenTx.ToString());
    }
    CTokenImplementation const & token = pair->second;

    //check foundation auth
    if (!skipAuth && !HasFoundationAuth(tx, coins, consensusParams)) {
        return Res::Err("%s: %s", __func__, "Is not a foundation owner");
    }

    if(token.IsDAT() != isDAT && pair->first >= CTokensView::DCT_ID_START)
    {
        CToken newToken = static_cast<CToken>(token); // keeps old and triggers only DAT!
        newToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;

        auto res = mnview.UpdateToken(token.creationTx, newToken, true);
        if (!res.ok) {
            return Res::Err("%s %s: %s", __func__, token.symbol, res.msg);
        }
    }

    // Only isDAT changed in deprecated ApplyUpdateTokenTx
    if (rpcInfo) {
        rpcInfo->pushKV("isDAT", token.IsDAT());
    }

    return Res::Ok();
}


Res ApplyUpdateTokenAnyTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if ((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("Improved updatetoken tx before Bayfront height");
    }

    uint256 tokenTx;
    CToken newToken;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> tokenTx;
    ss >> newToken;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    auto pair = mnview.GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("%s: token with creationTx %s does not exist", __func__, tokenTx.ToString());
    }
    if (pair->first == DCT_ID{0}) {
        return Res::Err("Can't alter DFI token!"); // may be redundant cause DFI is 'finalized'
    }

    CTokenImplementation const & token = pair->second;

    // need to check it exectly here cause lps has no collateral auth (that checked next)
    if (token.IsPoolShare())
        return Res::Err("%s: token %s is the LPS token! Can't alter pool share's tokens!", __func__, tokenTx.ToString());

    // check auth, depends from token's "origins"
    const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
    bool isFoundersToken = consensusParams.foundationMembers.find(auth.out.scriptPubKey) != consensusParams.foundationMembers.end();

    if (!skipAuth) {
        if (isFoundersToken && !HasFoundationAuth(tx, coins, consensusParams)) {
            return Res::Err("%s: %s", __func__, "tx not from foundation member");
        }
        else if (!HasCollateralAuth(tx, coins, token.creationTx)) {
            return Res::Err("%s: %s", __func__, "tx must have at least one input from token owner");
        }

        // Check for isDAT change in non-foundation token after set height
        if (static_cast<int>(height) >= consensusParams.BayfrontMarinaHeight)
        {
            //check foundation auth
            if((newToken.IsDAT() != token.IsDAT()) && !HasFoundationAuth(tx, coins, consensusParams))
            {//no need to check Authority if we don't create isDAT
                return Res::Err("%s: %s", __func__, "can't set isDAT to true, tx not from foundation member");
            }
        }
    }

    auto res = mnview.UpdateToken(token.creationTx, newToken, false);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, token.symbol, res.msg);
    }

    if (rpcInfo) {
        rpcInfo->pushKV("name", newToken.name);
        rpcInfo->pushKV("symbol", newToken.symbol);
        rpcInfo->pushKV("isDAT", newToken.IsDAT());
        rpcInfo->pushKV("mintable", newToken.IsDAT());
        rpcInfo->pushKV("tradeable", newToken.IsDAT());
        rpcInfo->pushKV("finalized", newToken.IsDAT());
    }

    return Res::Ok();
}

Res ApplyMintTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    CBalances minted;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> minted;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    // check auth and increase balance of token's owner
    for (auto const & kv : minted.balances) {
        DCT_ID tokenId = kv.first;

        auto token = mnview.GetToken(kv.first);
        if (!token) {
            return Res::Err("%s: token %s does not exist!", tokenId.ToString()); //  pre-bayfront throws but it affects only the message
        }
        auto tokenImpl = static_cast<CTokenImplementation const& >(*token);

        if (tokenImpl.destructionTx != uint256{}) {
            return Res::Err("%s: token %s already destroyed at height %i by tx %s", __func__, tokenImpl.symbol, //  pre-bayfront throws but it affects only the message
                                         tokenImpl.destructionHeight, tokenImpl.destructionTx.GetHex());
        }
        const Coin& auth = coins.AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output

        // pre-bayfront logic:
        if ((int)height < consensusParams.BayfrontHeight) {
            if (tokenId < CTokensView::DCT_ID_START)
                return Res::Err("%s: token %s is a 'stable coin', can't mint stable coin!", __func__, tokenId.ToString());

            if (!skipAuth && !HasAuth(tx, coins, auth.out.scriptPubKey)) {
                return Res::Err("%s: %s", __func__, "tx must have at least one input from token owner");
            }
        }
        else { // post-bayfront logic (changed for minting DAT tokens to be able)
            if (tokenId == DCT_ID{0})
                return Res::Err("can't mint default DFI coin!", __func__, tokenId.ToString());

            if (tokenImpl.IsPoolShare()) {
                return Res::Err("can't mint LPS tokens!", __func__, tokenId.ToString());
            }
            // may be different logic with LPS, so, dedicated check:
            if (!rpcInfo && !tokenImpl.IsMintable()) { // Skip on rpcInfo as we cannot reliably check whether mintable has been toggled historically
                return Res::Err("%s: token not mintable!", tokenId.ToString());
            }

            if (!skipAuth && !HasAuth(tx, coins, auth.out.scriptPubKey)) { // in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation member himself
                if (!tokenImpl.IsDAT()) {
                    return Res::Err("%s: %s", __func__, "tx must have at least one input from token owner");
                } else if (!HasFoundationAuth(tx, coins, consensusParams)) { // Is a DAT, check founders auth
                    return Res::Err("%s: %s", __func__, "token is DAT and tx not from foundation member");
                }
            }
        }
        auto mint = mnview.AddMintedTokens(tokenImpl.creationTx, kv.second, rpcInfo);
        if (!mint.ok) {
            return Res::Err("%s %s: %s", __func__, tokenImpl.symbol, mint.msg);
        }
        const auto res = mnview.AddBalance(auth.out.scriptPubKey, CTokenAmount{kv.first,kv.second});
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
    }
    return Res::Ok();
}

Res ApplyAddPoolLiquidityTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if ((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("LP tx before Bayfront height (block %d)", consensusParams.BayfrontHeight);
    }

    // deserialize
    CLiquidityMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    CBalances sumTx = SumAllTransfers(msg.from);
    if (sumTx.balances.size() != 2) {
        return Res::Err("%s: the pool pair requires two tokens", __func__);
    }

    std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
    std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

    // guaranteed by sumTx.balances.size() == 2
//    if (amountA.first == amountB.first) {
    //        return Res::Err("%s: tokens IDs are the same", __func__);
//    }

    // checked internally too. remove here?
    if (amountA.second <= 0 || amountB.second <= 0) {
        return Res::Err("%s: amount cannot be less than or equal to zero", __func__);
    }

    auto pair = mnview.GetPoolPair(amountA.first, amountB.first);

    if (!pair) {
        return Res::Err("%s: there is no such pool pair", __func__);
    }

    if (!skipAuth) {
        for (const auto& kv : msg.from) {
            if (!HasAuth(tx, coins, kv.first)) {
                return Res::Err("%s: %s", __func__, "tx must have at least one input from account owner");
            }
        }
    }

    // Return here to avoid balance errors below.
    if (rpcInfo) {
        rpcInfo->pushKV(std::to_string(amountA.first.v), ValueFromAmount(amountA.second));
        rpcInfo->pushKV(std::to_string(amountB.first.v), ValueFromAmount(amountB.second));
        rpcInfo->pushKV("shareaddress", msg.shareAddress.GetHex());

        return Res::Ok();
    }

    for (const auto& kv : msg.from) {
        const auto res = mnview.SubBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
    }

    DCT_ID const & lpTokenID = pair->first;
    CPoolPair & pool = pair->second;

    // normalize A & B to correspond poolpair's tokens
    if (amountA.first != pool.idTokenA)
        std::swap(amountA, amountB);

    bool slippageProtection = static_cast<int>(height) >= consensusParams.BayfrontMarinaHeight;
    const auto res = pool.AddLiquidity(amountA.second, amountB.second, msg.shareAddress, [&] /*onMint*/(CScript to, CAmount liqAmount) {

        auto add = mnview.AddBalance(to, { lpTokenID, liqAmount });
        if (!add.ok) {
            return Res::Err("%s: %s", __func__, add.msg);
        }

        //insert update ByShare index
        const auto setShare = mnview.SetShare(lpTokenID, to);
        if (!setShare.ok) {
            return Res::Err("%s: %s", __func__, setShare.msg);
        }

        return Res::Ok();
    }, slippageProtection);

    if (!res.ok) {
        return Res::Err("%s: %s", __func__, res.msg);
    }
    return mnview.SetPoolPair(lpTokenID, pool);
}

Res ApplyRemovePoolLiquidityTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if ((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("LP tx before Bayfront height (block %d)", consensusParams.BayfrontHeight);
    }

    // deserialize
    CRemoveLiquidityMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    CScript from = msg.from;
    CTokenAmount amount = msg.amount;

    // checked internally too. remove here?
    if (amount.nValue <= 0) {
        return Res::Err("%s: amount cannot be less than or equal to zero", __func__);
    }

    auto pair = mnview.GetPoolPair(amount.nTokenId);

    if (!pair) {
        return Res::Err("%s: there is no such pool pair", __func__);
    }

    if (!skipAuth && !HasAuth(tx, coins, from)) {
        return Res::Err("%s: %s", __func__, "tx must have at least one input from account owner");
    }

    // Return here to avoid balance errors below.
    if (rpcInfo) {
        rpcInfo->pushKV("from", msg.from.GetHex());
        rpcInfo->pushKV("amount", msg.amount.ToString());

        return Res::Ok();
    }

    CPoolPair & pool = pair.get();

    // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
    {
        auto sub = mnview.SubBalance(from, amount);
        if (!sub.ok) {
            return Res::Err("%s: %s", __func__, sub.msg);
        }
        if (mnview.GetBalance(from, amount.nTokenId).nValue == 0) {
            //delete ByShare index
            const auto delShare = mnview.DelShare(amount.nTokenId, from);
            if (!delShare.ok) {
                return Res::Err("%s: %s", __func__, delShare.msg);
            }
        }
    }

    const auto res = pool.RemoveLiquidity(from, amount.nValue, [&] (CScript to, CAmount amountA, CAmount amountB) {

        auto addA = mnview.AddBalance(to, { pool.idTokenA, amountA });
        if (!addA.ok) {
            return Res::Err("%s: %s", __func__, addA.msg);
        }

        auto addB = mnview.AddBalance(to, { pool.idTokenB, amountB });
        if (!addB.ok) {
            return Res::Err("%s: %s", __func__, addB.msg);
        }

        return Res::Ok();
    });

    if (!res.ok) {
        return Res::Err("%s: %s", __func__, res.msg);
    }

    return mnview.SetPoolPair(amount.nTokenId, pool);
}


Res ApplyUtxosToAccountTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, UniValue *rpcInfo)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // deserialize
    CUtxosToAccountMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    // check enough tokens are "burnt"
    const auto burnt = BurntTokens(tx);
    CBalances mustBeBurnt = SumAllTransfers(msg.to);
    if (!burnt.ok) {
        return Res::Err("%s: %s", __func__, burnt.msg);
    }
    if (burnt.val->balances != mustBeBurnt.balances) {
        return Res::Err("%s: transfer tokens mismatch burnt tokens: (%s) != (%s)", __func__, mustBeBurnt.ToString(), burnt.val->ToString());
    }

    // Create balances here and return to avoid balance errors below
    if (rpcInfo) {
        for (const auto& kv : msg.to) {
            rpcInfo->pushKV(kv.first.GetHex(), kv.second.ToString());
        }

        return Res::Ok();
    }

    // transfer
    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
        for (const auto& balance : kv.second.balances) {
            auto token = mnview.GetToken(balance.first);
            if (token->IsPoolShare()) {
                const auto bal = mnview.GetBalance(kv.first, balance.first);
                if (bal.nValue == balance.second) {
                    const auto setShare = mnview.SetShare(balance.first, kv.first);
                    if (!setShare.ok) {
                        return Res::Err("%s: %s", __func__, setShare.msg);
                    }
                }
            }
        }
    }
    return Res::Ok();
}

Res ApplyAccountToUtxosTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // deserialize
    CAccountToUtxosMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    // check auth
    if (!skipAuth && !HasAuth(tx, coins, msg.from)) {
        return Res::Err("%s: %s", __func__, "tx must have at least one input from account owner");
    }

    // Return here to avoid balance errors below
    if (rpcInfo) {
        rpcInfo->pushKV("from", msg.from.GetHex());

        UniValue dest(UniValue::VOBJ);
        for (uint32_t i = msg.mintingOutputsStart; i <  static_cast<uint32_t>(tx.vout.size()); i++) {
            dest.pushKV(tx.vout[i].scriptPubKey.GetHex(), tx.vout[i].TokenAmount().ToString());
        }
        rpcInfo->pushKV("to", dest);

        return Res::Ok();
    }

    // check that all tokens are minted, and no excess tokens are minted
    const auto minted = MintedTokens(tx, msg.mintingOutputsStart);
    if (!minted.ok) {
        return Res::Err("%s: %s", __func__, minted.msg);
    }
    if (msg.balances != *minted.val) {
        return Res::Err("%s: amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)", __func__, minted.val->ToString(), msg.balances.ToString());
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
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", __func__, res.msg);
    }

    for (const auto& kv : msg.balances.balances) {
        auto token = mnview.GetToken(kv.first);
        if (token->IsPoolShare()) {
            const auto balance = mnview.GetBalance(msg.from, kv.first);
            if (balance.nValue == 0) {
                const auto delShare = mnview.DelShare(kv.first, msg.from);
                if (!delShare.ok) {
                    return Res::Err("%s: %s", __func__, delShare.msg);
                }
            }
        }
    }
    return Res::Ok();
}

Res ApplyAccountToAccountTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.AMKHeight) { return Res::Err("Token tx before AMK height (block %d)", consensusParams.AMKHeight); }

    // deserialize
    CAccountToAccountMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    // check auth
    if (!skipAuth && !HasAuth(tx, coins, msg.from)) {
        return Res::Err("%s: %s", __func__, "tx must have at least one input from account owner");
    }

    // Return here to avoid balance errors below.
    if (rpcInfo) {
        rpcInfo->pushKV("from", msg.from.GetHex());

        UniValue dest(UniValue::VOBJ);
        for (const auto& to : msg.to) {
            dest.pushKV(to.first.GetHex(), to.second.ToString());
        }
        rpcInfo->pushKV("to", dest);

        return Res::Ok();
    }

    // transfer
    auto res = mnview.SubBalances(msg.from, SumAllTransfers(msg.to));
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", __func__, res.msg);
    }

    for (const auto& kv : SumAllTransfers(msg.to).balances) {
        const auto token = mnview.GetToken(kv.first);
        if (token->IsPoolShare()) {
            const auto balance = mnview.GetBalance(msg.from, kv.first);
            if (balance.nValue == 0) {
                const auto delShare = mnview.DelShare(kv.first, msg.from);
                if (!delShare.ok) {
                    return Res::Err("%s: %s", __func__, delShare.msg);
                }
            }
        }
    }

    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
        for (const auto& balance : kv.second.balances) {
            auto token = mnview.GetToken(balance.first);
            if (token->IsPoolShare()) {
                const auto bal = mnview.GetBalance(kv.first, balance.first);
                if (bal.nValue == balance.second) {
                    const auto setShare = mnview.SetShare(balance.first, kv.first);
                    if (!setShare.ok) {
                        return Res::Err("%s: %s", __func__, setShare.msg);
                    }
                }
            }
        }
    }
    return Res::Ok();
}

Res ApplyAnyAccountsToAccountsTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.BayfrontGardensHeight) { return Res::Err("Token tx before BayfrontGardensHeight (block %d)", consensusParams.BayfrontGardensHeight ); }

    // deserialize
    CAnyAccountsToAccountsMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    // check auth
    if (!skipAuth) {
        for (auto const & kv : msg.from) {
            if (!HasAuth(tx, coins, kv.first)) {
                return Res::Err("%s: %s", __func__, "tx must have at least one input from account owner");
            }
        }
    }

    // Return here to avoid balance errors below.
    if (rpcInfo) {
        UniValue source(UniValue::VOBJ);
        for (const auto& from : msg.from) {
            source.pushKV(from.first.GetHex(), from.second.ToString());
        }
        rpcInfo->pushKV("from", source);

        UniValue dest(UniValue::VOBJ);
        for (const auto& to : msg.to) {
            dest.pushKV(to.first.GetHex(), to.second.ToString());
        }
        rpcInfo->pushKV("to", dest);

        return Res::Ok();
    }

    // compare
    auto const sumFrom = SumAllTransfers(msg.from);
    auto const sumTo = SumAllTransfers(msg.to);

    if (sumFrom != sumTo) {
        return Res::Err("%s: %s", __func__, "sum of inputs (from) != sum of outputs (to)");
    }

    // transfer
    // substraction
    for (const auto& kv : msg.from) {
        const auto res = mnview.SubBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", __func__, res.msg);
        }
        // track pool shares
        for (const auto& balance : kv.second.balances) {
            auto token = mnview.GetToken(balance.first);
            if (token->IsPoolShare()) {
                const auto bal = mnview.GetBalance(kv.first, balance.first);
                if (bal.nValue == 0) {
                    const auto delShare = mnview.DelShare(balance.first, kv.first);
                    if (!delShare.ok) {
                        return Res::Err("%s: %s", __func__, delShare.msg);
                    }
                }
            }
        }
    }

    // addition
    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
        // track pool shares
        for (const auto& balance : kv.second.balances) {
            auto token = mnview.GetToken(balance.first);
            if (token->IsPoolShare()) {
                const auto bal = mnview.GetBalance(kv.first, balance.first);
                if (bal.nValue == balance.second) {
                    const auto setShare = mnview.SetShare(balance.first, kv.first);
                    if (!setShare.ok) {
                        return Res::Err("%s: %s", __func__, setShare.msg);
                    }
                }
            }
        }
    }
    return Res::Ok();
}

extern std::string ScriptToString(CScript const& script);

Res ApplyCreatePoolPairTx(CCustomCSView &mnview, const CCoinsViewCache &coins, const CTransaction &tx, uint32_t height, const std::vector<unsigned char> &metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if ((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("LP tx before Bayfront height (block %d)", consensusParams.BayfrontHeight);
    }

    CPoolPairMessage poolPairMsg;
    std::string pairSymbol;
    CBalances rewards;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> poolPairMsg;
    ss >> pairSymbol;

    // Read custom pool rewards
    if (static_cast<int>(height) >= consensusParams.ClarkeQuayHeight && !ss.empty()) {
        ss >> rewards;
    }

    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    //check foundation auth
    if(!skipAuth && !HasFoundationAuth(tx, coins, consensusParams)) {
        return Res::Err("%s: %s", __func__, "tx not from foundation member");
    }
    if(poolPairMsg.commission < 0 || poolPairMsg.commission > COIN) {
        return Res::Err("%s: %s", __func__, "wrong commission");
    }

    /// @todo ownerAddress validity checked only in rpc. is it enough?
    CPoolPair poolPair(poolPairMsg);
    poolPair.creationTx = tx.GetHash();
    poolPair.creationHeight = height;

    CTokenImplementation token{};

    auto tokenA = mnview.GetToken(poolPairMsg.idTokenA);
    if (!tokenA) {
        return Res::Err("%s: token %s does not exist!", __func__, poolPairMsg.idTokenA.ToString());
    }

    auto tokenB = mnview.GetToken(poolPairMsg.idTokenB);
    if (!tokenB) {
        return Res::Err("%s: token %s does not exist!", __func__, poolPairMsg.idTokenB.ToString());
    }

    if(pairSymbol.empty())
        pairSymbol = trim_ws(tokenA->symbol + "-" + tokenB->symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    else
        pairSymbol = trim_ws(pairSymbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    token.flags = (uint8_t)CToken::TokenFlags::DAT |
                  (uint8_t)CToken::TokenFlags::LPS |
                  (uint8_t)CToken::TokenFlags::Tradeable |
                  (uint8_t)CToken::TokenFlags::Finalized;
    token.name = trim_ws(tokenA->name + "-" + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.symbol = pairSymbol;
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    // Return here to avoid token and poolpair exists errors below
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", tx.GetHash().GetHex());
        rpcInfo->pushKV("name", token.name);
        rpcInfo->pushKV("symbol", pairSymbol);
        rpcInfo->pushKV("tokenA", tokenA->name);
        rpcInfo->pushKV("tokenB", tokenB->name);
        rpcInfo->pushKV("commission", ValueFromAmount(poolPairMsg.commission));
        rpcInfo->pushKV("status", poolPairMsg.status);
        rpcInfo->pushKV("ownerAddress", ScriptToString(poolPairMsg.ownerAddress));
        rpcInfo->pushKV("isDAT", token.IsDAT());
        rpcInfo->pushKV("mineable", token.IsMintable());
        rpcInfo->pushKV("tradeable", token.IsTradeable());
        rpcInfo->pushKV("finalized", token.IsFinalized());

        if (!rewards.balances.empty()) {
            UniValue rewardArr(UniValue::VARR);

            for (const auto& reward : rewards.balances) {
                if (reward.second > 0) {
                    rewardArr.push_back(CTokenAmount{reward.first, reward.second}.ToString());
                }
            }

            if (!rewardArr.empty()) {
                rpcInfo->pushKV("customRewards", rewardArr);
            }
        }

        return Res::Ok();
    }

    auto res = mnview.CreateToken(token, false);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, token.symbol, res.msg);
    }

    //auto pairToken = mnview.GetToken(token.symbol);
    auto pairToken = mnview.GetTokenByCreationTx(token.creationTx);
    if (!pairToken) {
        return Res::Err("%s: token %s does not exist!", __func__, token.symbol);
    }

    auto resPP = mnview.SetPoolPair(pairToken->first, poolPair);
    if (!resPP.ok) {
        return Res::Err("%s %s: %s", __func__, pairSymbol, resPP.msg);
    }

    if (!rewards.balances.empty()) {
        // Check tokens exist and remove empty reward amounts
        for (auto it = rewards.balances.cbegin(), next_it = it; it != rewards.balances.cend(); it = next_it) {
            ++next_it;

            auto token = pcustomcsview->GetToken(it->first);
            if (!token) {
                return Res::Err("%s: reward token %d does not exist!", __func__, it->first.v);
            }

            if (it->second == 0) {
                rewards.balances.erase(it);
            }
        }

        auto resCR = mnview.SetPoolCustomReward(pairToken->first, rewards);

        // Will only fail if pool was not actually created in SetPoolPair
        if (!resCR.ok) {
            return Res::Err("%s %s: %s", __func__, pairSymbol, resCR.msg);
        }
    }

    return Res::Ok();
}

Res ApplyUpdatePoolPairTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("LP tx before Bayfront height (block %d)", consensusParams.BayfrontHeight);
    }

    DCT_ID poolId;
    bool status;
    CAmount commission;
    CScript ownerAddress;
    CBalances rewards;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> poolId;
    ss >> status;
    ss >> commission;
    ss >> ownerAddress;

    // Read custom pool rewards
    if (static_cast<int>(height) >= consensusParams.ClarkeQuayHeight && !ss.empty()) {
        ss >> rewards;
    }

    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    auto pool = mnview.GetPoolPair(poolId);
    if (!pool) {
        return Res::Err("%s: pool with poolId %s does not exist", __func__, poolId.ToString());
    }

    //check foundation auth
    if (!skipAuth && !HasFoundationAuth(tx, coins, consensusParams)) {
        return Res::Err("%s: %s", __func__, "tx not from foundation member");
    }

    auto res = mnview.UpdatePoolPair(poolId, status, commission, ownerAddress);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, poolId.ToString(), res.msg);
    }

    if (rpcInfo) {
        rpcInfo->pushKV("commission", ValueFromAmount(commission));
        rpcInfo->pushKV("status", status);
        rpcInfo->pushKV("ownerAddress", ScriptToString(ownerAddress));

        // Add rewards here before processing them below to avoid adding current rewards
        if (!rewards.balances.empty()) {
            UniValue rewardArr(UniValue::VARR);

            // Check for special case to wipe rewards
            if (rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
                    && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max()) {
                rpcInfo->pushKV("customRewards", rewardArr);
            } else {
                for (const auto& reward : rewards.balances) {
                    if (reward.second > 0) {
                        rewardArr.push_back(CTokenAmount{reward.first, reward.second}.ToString());
                    }
                }

                if (!rewardArr.empty()) {
                    rpcInfo->pushKV("customRewards", rewardArr);
                }
            }
        }
    }

    if (!rewards.balances.empty()) {
        // Check for special case to wipe rewards
        if (rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
                && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max()) {
            rewards.balances.clear();
        }

        // Check if tokens exist and remove empty reward amounts
        for (auto it = rewards.balances.cbegin(), next_it = it; it != rewards.balances.cend(); it = next_it) {
            ++next_it;

            auto token = pcustomcsview->GetToken(it->first);
            if (!token) {
                return Res::Err("%s: reward token %d does not exist!", __func__, it->first.v);
            }

            if (it->second == 0) {
                rewards.balances.erase(it);
            }
        }

        auto resCR = mnview.SetPoolCustomReward(poolId, rewards);

        // Will only fail if pool was not actually created in SetPoolPair
        if (!resCR.ok) {
            return Res::Err("%s %s: %s", __func__, poolId.ToString(), resCR.msg);
        }
    }

    return Res::Ok();
}

Res ApplyPoolSwapTx(CCustomCSView &mnview, const CCoinsViewCache &coins, const CTransaction &tx, uint32_t height, const std::vector<unsigned char> &metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    if ((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("LP tx before Bayfront height (block %d)", consensusParams.BayfrontHeight);
    }

    CPoolSwapMessage poolSwapMsg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> poolSwapMsg;
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }

    // check auth
    if (!skipAuth && !HasAuth(tx, coins, poolSwapMsg.from)) {
        return Res::Err("%s: %s", __func__, "tx must have at least one input from account owner");
    }

//    auto tokenFrom = mnview.GetToken(poolSwapMsg.idTokenFrom);
//    if (!tokenFrom) {
//        return Res::Err("%s: token %s does not exist!", __func__, poolSwapMsg.idTokenFrom.ToString());
//    }

//    auto tokenTo = mnview.GetToken(poolSwapMsg.idTokenTo);
//    if (!tokenTo) {
//        return Res::Err("%s: token %s does not exist!", __func__, poolSwapMsg.idTokenTo.ToString());
//    }

    auto poolPair = mnview.GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo);
    if (!poolPair) {
        return Res::Err("%s: can't find the poolpair!", __func__);
    }

    // Return here to avoid balance errors below
    if (rpcInfo) {
        rpcInfo->pushKV("fromAddress", poolSwapMsg.from.GetHex());
        rpcInfo->pushKV("fromToken", std::to_string(poolSwapMsg.idTokenFrom.v));
        rpcInfo->pushKV("fromAmount", ValueFromAmount(poolSwapMsg.amountFrom));
        rpcInfo->pushKV("toAddress", poolSwapMsg.to.GetHex());
        rpcInfo->pushKV("toToken", std::to_string(poolSwapMsg.idTokenTo.v));
        rpcInfo->pushKV("maxPrice", ValueFromAmount((poolSwapMsg.maxPrice.integer * COIN) + poolSwapMsg.maxPrice.fraction));

        return Res::Ok();
    }

    CPoolPair pp = poolPair->second;
    const auto res = pp.Swap({poolSwapMsg.idTokenFrom, poolSwapMsg.amountFrom}, poolSwapMsg.maxPrice, [&] (const CTokenAmount &tokenAmount) {
        auto resPP = mnview.SetPoolPair(poolPair->first, pp);
        if (!resPP.ok) {
            return Res::Err("%s: %s", __func__, resPP.msg);
        }

        auto sub = mnview.SubBalance(poolSwapMsg.from, {poolSwapMsg.idTokenFrom, poolSwapMsg.amountFrom});
        if (!sub.ok) {
            return Res::Err("%s: %s", __func__, sub.msg);
        }

        auto add = mnview.AddBalance(poolSwapMsg.to, tokenAmount);
        if (!add.ok) {
            return Res::Err("%s: %s", __func__, add.msg);
        }

        return Res::Ok();
    }, static_cast<int>(height));

    if (!res.ok) {
        return Res::Err("%s: %s", __func__, res.msg);
    }

    return Res::Ok();
}

Res ApplySetGovernanceTx(CCustomCSView &mnview, const CCoinsViewCache &coins, const CTransaction &tx, uint32_t height, const std::vector<unsigned char> &metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue* rpcInfo)
{
    if ((int)height < consensusParams.BayfrontHeight) {
        return Res::Err("Governance tx before Bayfront height (block %d)", consensusParams.BayfrontHeight);
    }

    //check foundation auth
    if(!skipAuth && !HasFoundationAuth(tx, coins, consensusParams))
    {
        return Res::Err("%s: %s", __func__, "tx not from foundation member");
    }

    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    std::set<std::string> names;
    while(!ss.empty())
    {
        std::string name;
        ss >> name;
        names.insert(name);
        auto var = GovVariable::Create(name);
        if (!var)
            return Res::Err("%s '%s': variable does not registered", __func__, name);
        ss >> *var;

        Res result = var->Validate(mnview);
        if(!result.ok)
            return Res::Err("%s '%s': %s", __func__, name, result.msg);

        Res res = var->Apply(mnview);
        if(!res.ok)
            return Res::Err("%s '%s': %s", __func__, name, res.msg);

        auto add = mnview.SetVariable(*var);
        if (!add.ok)
            return Res::Err("%s '%s': %s", __func__, name, add.msg);
    }

    if (rpcInfo) {
        for (const auto& name : names) {
            auto var = mnview.GetVariable(name);
            rpcInfo->pushKV(var->GetName(), var->Export());
        }
    }
    //in this case it will throw (and cathced by outer method)
//    if (!ss.empty()) {
//        return Res::Err("%s: deserialization failed: excess %d bytes", __func__,  ss.size());
//    }

    return Res::Ok();
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

Res ApplyICXCreateOrderTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    // if ((int)height < consensusParams.EhardforkHeight) { return Res::Err("Order tx before Ehardfork height (block %d)", consensusParams.EhardforkHeight); }

    // Check quick conditions first
    if (tx.vout.size() != 2) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong number of vouts)");
    }

    CICXOrderImplemetation order;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CICXOrder &>(order);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    
    order.creationTx = tx.GetHash();
    order.creationHeight = height;

    if (order.idTokenFrom.v!=std::numeric_limits<uint32_t>::max() && order.idTokenTo.v!=std::numeric_limits<uint32_t>::max())
        return Res::Err("%s: %s", __func__, "one of the assets \"from\" or \"to\" must be specified");
    
    if (order.orderType && order.idTokenFrom.v!=std::numeric_limits<uint32_t>::max() && order.chainTo.empty())
        return Res::Err("%s: %s", __func__, "when asset \"from\" is DFC token then \"chainTo\" must be specified");
    else if (!order.orderType && order.idTokenTo.v!=std::numeric_limits<uint32_t>::max() && order.chainFrom.empty())
        return Res::Err("%s: %s", __func__, "when asset \"to\" is DFC token then \"chainFrom\" must be specified");
    else
        return Res::Err("%s: %s", __func__, "invalid order type or supplied assets \"from\" and \"to\"");

    if (order.orderType)
    {
        CTxDestination ownerDest = DecodeDestination(order.ownerAddress);
        if (ownerDest.which() == 0) {
            return Res::Err("%s: %s", __func__, "ownerAdress (" + order.ownerAddress + ") does not refer to any valid address");
        }

        auto tokenFrom = mnview.GetToken(order.idTokenFrom);
        if (!tokenFrom) {
            return Res::Err("%s: token %s does not exist!", tokenFrom->symbol);
        }
    }
    else
    {
        auto tokenTo = mnview.GetToken(order.idTokenTo);
        if (!tokenTo) {
            return Res::Err("%s: token %s does not exist!", tokenTo->symbol);
        }
    }

    if (order.amountToFill != order.amountFrom*COIN/order.orderPrice) {
        return Res::Err("order amountToFill does not equal to amountFrom * orderPrice!");
    }

    // Return here to avoid balance problems
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", order.creationTx.GetHex());
        rpcInfo->pushKV("amountFrom", order.amountFrom);
        rpcInfo->pushKV("amountToFill", order.amountToFill);
        rpcInfo->pushKV("orderPrice", order.orderPrice);
        rpcInfo->pushKV("expiry", static_cast<int>(order.expiry));

        return Res::Ok();
    }

    // subtract the balance from tokenFrom to dedicate them for the order
    if (order.orderType)
    {
        CTokenAmount amount={order.idTokenFrom,order.amountFrom};
        auto res = mnview.SubBalance(GetScriptForDestination(DecodeDestination(order.ownerAddress)),amount);
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
    }
    
    auto res = mnview.ICXCreateOrder(order);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, order.creationTx.GetHex(), res.msg);
    }

    return Res::Ok();
}

Res ApplyICXMakeOfferTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    // if ((int)height < consensusParams.EhardforkHeight) { return Res::Err("Order tx before Ehardfork height (block %d)", consensusParams.EhardforkHeight); }

    // Check quick conditions first
    if (tx.vout.size() != 2) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong number of vouts)");
    }

    CICXMakeOfferImplemetation makeoffer;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CICXMakeOffer &>(makeoffer);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    
    makeoffer.creationTx = tx.GetHash();
    makeoffer.creationHeight = height;

    auto order=mnview.GetICXOrderByCreationTx(makeoffer.orderTx);
    if (!order) {
        return Res::Err("order with creation tx %s does not exists!", makeoffer.orderTx.GetHex());
    }

    if (order->orderType == CICXOrder::ICXORDER_TYPE_INTERNAL)
    {
        if (CPubKey(makeoffer.receivePubKey.begin(),makeoffer.receivePubKey.end()).IsFullyValid())
            return Res::Err("Invalid receivePubKey, (" + makeoffer.receivePubKey + ") receivePubKey is not a valid pubkey!");
    }
    else
    {
        auto ownerDest = DecodeDestination(makeoffer.receiveAddress);
        if (ownerDest.which() == 0) {
            return Res::Err("%s: %s", __func__, "Invalid receiverAddress, (" + makeoffer.receiveAddress + ") does not refer to any valid address");
        }
    }

    if (order->amountToFill < makeoffer.amount) {
        return Res::Err("cannot fill order with that amount, order (" + order->creationTx.GetHex() + ") has less amount to fill!");
    }

    order->amountToFill -= makeoffer.amount;
    if (order->amountToFill==0)
    {
        order->closeTx=makeoffer.creationTx;
        order->closeHeight = height;
    }

    // Return here to avoid balance problems
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", makeoffer.creationTx.GetHex());
        rpcInfo->pushKV("orderTx", makeoffer.orderTx.GetHex());
        rpcInfo->pushKV("amount", makeoffer.amount);
        return Res::Ok();
    }
    
    auto res = mnview.ICXMakeOffer(makeoffer,*order);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, makeoffer.creationTx.GetHex(), res.msg);
    }

    return Res::Ok();
}

Res ApplyICXSubmitDFCHTLCTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    // if ((int)height < consensusParams.EhardforkHeight) { return Res::Err("Order tx before Ehardfork height (block %d)", consensusParams.EhardforkHeight); }

    // Check quick conditions first
    if (tx.vout.size() != 2) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong number of vouts)");
    }

    CICXSubmitDFCHTLCImplemetation submitdfchtlc;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CICXSubmitDFCHTLC &>(submitdfchtlc);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    
    submitdfchtlc.creationTx = tx.GetHash();
    submitdfchtlc.creationHeight = height;

    auto offer=mnview.GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
    if (!offer) {
        return Res::Err("offer with creation tx %s does not exists!", submitdfchtlc.offerTx.GetHex());
    }

    auto order=mnview.GetICXOrderByCreationTx(offer->orderTx);
    if (!order) {
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());
    }

    if (submitdfchtlc.hash.IsNull())
        return Res::Err("Invalid hash, htlc hash is empty and it must be set!");
    
    if (submitdfchtlc.timeout == 0 || submitdfchtlc.timeout > CICXSubmitDFCHTLC::ICXSUBMITDFCHTLC_MAX_TIMEOUT)
    {
        uint32_t max=CICXSubmitDFCHTLC::ICXSUBMITDFCHTLC_MAX_TIMEOUT;
        return Res::Err("Invalid timout, 0 < timeout < %d!", max);
    }

    if (order->orderType == CICXOrder::ICXORDER_TYPE_INTERNAL)
    {
        if (submitdfchtlc.amount != offer->amount*COIN/order->orderPrice)
            return Res::Err("amount in dfc htlc must match the amount necessary for offer amount - %f != %f!", submitdfchtlc.amount, offer->amount*COIN/order->orderPrice);

        bool found=false;
        mnview.ForEachICXSubmitEXTHTLC([&found,&offer](CICXOrderView::TxidPairKey const & key, CICXSubmitEXTHTLCImplemetation submitexthtlc) {
            if (key.first == offer->creationTx)
            {
                found = true;
                return (false);
            }
            return true;
        },order->creationTx);
        if (found)
            return Res::Err("offer %s needs to have dfc htlc submitted first, but external htlc already submitted!", offer->creationTx.GetHex());

        if (CPubKey(submitdfchtlc.receivePubKey.begin(),submitdfchtlc.receivePubKey.end()).IsFullyValid())
            return Res::Err("Invalid receivePubKey, (" + submitdfchtlc.receivePubKey + ") is not a valid pubkey!");
    }
    else
    {
        if (submitdfchtlc.amount != offer->amount)
            return Res::Err("amount in dfc htlc must match the amount in offer - %s != %s!", ValueFromAmount(submitdfchtlc.amount).getValStr(), ValueFromAmount(offer->amount).getValStr());
        
        std::unique_ptr<CICXSubmitEXTHTLCImplemetation> exthtlc;
        mnview.ForEachICXSubmitEXTHTLC([&exthtlc,&offer](CICXOrderView::TxidPairKey const & key, CICXSubmitEXTHTLCImplemetation submitexthtlc) {
            if (key.first == offer->creationTx)
            {
                exthtlc = MakeUnique<CICXSubmitEXTHTLCImplemetation>(submitexthtlc);
                return (false);
            }
            return true;
        },order->creationTx);
        if (!exthtlc)
            return Res::Err("offer %s needs to have ext htlc submitted first, but no external htlc found!", offer->creationTx.GetHex());
        
        auto ownerDest = DecodeDestination(submitdfchtlc.receiveAddress);
        if (ownerDest.which() == 0) {
            return Res::Err("%s: %s", __func__, "Invalid receiverAddress, (" + submitdfchtlc.receiveAddress + ") does not refer to any valid address");
        }
        if (submitdfchtlc.hash != exthtlc->hash)
            return Res::Err("Invalid hash, dfc htlc hash is different than extarnal htlc hash!");
    }

    // Return here to avoid balance problems
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", submitdfchtlc.creationTx.GetHex());
        rpcInfo->pushKV("offerTx", submitdfchtlc.offerTx.GetHex());
        rpcInfo->pushKV("amount", submitdfchtlc.amount);
        rpcInfo->pushKV("receiverAddress", submitdfchtlc.receiveAddress);
        rpcInfo->pushKV("hash", submitdfchtlc.hash.GetHex());
        rpcInfo->pushKV("timeout", static_cast<int>(submitdfchtlc.timeout));
        return Res::Ok();
    }
    
    auto res = mnview.ICXSubmitDFCHTLC(submitdfchtlc);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, submitdfchtlc.creationTx.GetHex(), res.msg);
    }

    return Res::Ok();
}

Res ApplyICXSubmitEXTHTLCTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    // if ((int)height < consensusParams.EhardforkHeight) { return Res::Err("Order tx before Ehardfork height (block %d)", consensusParams.EhardforkHeight); }

    // Check quick conditions first
    if (tx.vout.size() != 2) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong number of vouts)");
    }

    CICXSubmitEXTHTLCImplemetation submitexthtlc;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CICXSubmitEXTHTLC &>(submitexthtlc);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    
    submitexthtlc.creationTx = tx.GetHash();
    submitexthtlc.creationHeight = height;

    auto offer=mnview.GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
    if (!offer) {
        return Res::Err("order with creation tx %s does not exists!", submitexthtlc.offerTx.GetHex());
    }

    auto order=mnview.GetICXOrderByCreationTx(offer->orderTx);
    if (!order) {
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());
    }

    if (submitexthtlc.hash.IsNull())
        return Res::Err("Invalid hash, htlc hash is empty and it must be set!");

    if (order->orderType==CICXOrder::ICXORDER_TYPE_INTERNAL)
    {
        if (submitexthtlc.amount != offer->amount)
            return Res::Err("amount in ext htlc must match the amount in the offer - %s != %s!", ValueFromAmount(submitexthtlc.amount).getValStr(), ValueFromAmount(offer->amount*COIN/order->orderPrice).getValStr());
        
        std::unique_ptr<CICXSubmitDFCHTLCImplemetation> dfchtlc;
        mnview.ForEachICXSubmitDFCHTLC([&dfchtlc,&offer](CICXOrderView::TxidPairKey const & key, CICXSubmitDFCHTLCImplemetation submitdfchtlc) {
            if (key.first == offer->creationTx)
            {
                dfchtlc = MakeUnique<CICXSubmitDFCHTLCImplemetation>(submitdfchtlc);
                return (false);
            }
            return true;
        },order->creationTx);
        if (!dfchtlc)
            return Res::Err("offer %s needs to have dfc htlc submitted first, but no dfc htlc found!", offer->creationTx.GetHex());

        if (submitexthtlc.hash != dfchtlc->hash)
            return Res::Err("Invalid hash, external htlc hash is different than dfc htlc hash!");
    }
    else
    {
        if (submitexthtlc.amount != offer->amount*COIN/order->orderPrice)
            return Res::Err("amount in dfc htlc must match the amount necessary for offer amount - %s != %s!", ValueFromAmount(submitexthtlc.amount).getValStr(), ValueFromAmount(offer->amount*COIN/order->orderPrice).getValStr());

        bool found=false;
        mnview.ForEachICXSubmitDFCHTLC([&](CICXOrderView::TxidPairKey const & key, CICXSubmitDFCHTLCImplemetation submitdfchtlc) {
            if (key.first == offer->creationTx)
            {
                found = true;
                return (false);
            }
            return true;
        },order->creationTx);
        if (found)
            return Res::Err("offer %s needs to have dfc htlc submitted first, but external htlc already submitted!", offer->creationTx.GetHex());
    }

    if (submitexthtlc.htlcscriptAddress.empty())
        return Res::Err("Invalid htlcscriptAddress, htlcscriptAddress is empty and it must be set!");
    if (submitexthtlc.ownerPubkey.empty())
        return Res::Err("Invalid ownerPubkey, ownerPubkey is empty and it must be set!");
    if (CPubKey(submitexthtlc.ownerPubkey.begin(),submitexthtlc.ownerPubkey.end()).IsFullyValid())
        return Res::Err("Invalid ownerPubkey, ownerPubkey is not a valid pubkey!");
    if (submitexthtlc.timeout == 0 || submitexthtlc.timeout > CICXSubmitEXTHTLC::ICXSUBMITEXTHTLC_MAX_TIMEOUT)
    {
        uint32_t max=CICXSubmitEXTHTLC::ICXSUBMITEXTHTLC_MAX_TIMEOUT;
        return Res::Err("Invalid timout, 0 < timeout < %d!", max);
    }

    // Return here to avoid balance problems
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", submitexthtlc.creationTx.GetHex());
        rpcInfo->pushKV("offerTx", submitexthtlc.offerTx.GetHex());
        rpcInfo->pushKV("hash", submitexthtlc.hash.GetHex());
        rpcInfo->pushKV("htlcscriptAddress", submitexthtlc.htlcscriptAddress);
        rpcInfo->pushKV("ownerPubkey", submitexthtlc.ownerPubkey);
        rpcInfo->pushKV("timeout", static_cast<int>(submitexthtlc.timeout));
        return Res::Ok();
    }
    
    auto res = mnview.ICXSubmitEXTHTLC(submitexthtlc);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, submitexthtlc.creationTx.GetHex(), res.msg);
    }

    return Res::Ok();
}

Res ApplyICXClaimDFCHTLCTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    // if ((int)height < consensusParams.EhardforkHeight) { return Res::Err("Order tx before Ehardfork height (block %d)", consensusParams.EhardforkHeight); }

    // Check quick conditions first
    if (tx.vout.size() != 2) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong number of vouts)");
    }

    CICXClaimDFCHTLCImplemetation claimdfchtlc;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CICXClaimDFCHTLC &>(claimdfchtlc);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    
    claimdfchtlc.creationTx = tx.GetHash();
    claimdfchtlc.creationHeight = height;

    auto dfchtlc=mnview.GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
    if (!dfchtlc) {
        return Res::Err("dfc htlc with creation tx %s does not exists!", claimdfchtlc.dfchtlcTx.GetHex());
    }

    if (claimdfchtlc.amount!=dfchtlc->amount) {
        return Res::Err("amount in claim different than in dfc htlc: %s - %s!", ValueFromAmount(claimdfchtlc.amount).getValStr(),ValueFromAmount(dfchtlc->amount).getValStr());
    }

    uint256 calcHash;
    CSHA256()
        .Write(claimdfchtlc.seed.begin(),claimdfchtlc.seed.size())
        .Finalize(calcHash.begin());
    if (dfchtlc->hash != calcHash)
        return Res::Err("hash generated from given seed is different than in dfc htlc: %s - %s!", calcHash.GetHex(), dfchtlc->hash.GetHex());

    // Return here to avoid balance problems
    if (rpcInfo) {
        rpcInfo->pushKV("creationTx", claimdfchtlc.creationTx.GetHex());
        rpcInfo->pushKV("dfchtlcTx", claimdfchtlc.dfchtlcTx.GetHex());
        rpcInfo->pushKV("amount", claimdfchtlc.amount);
        rpcInfo->pushKV("seed", claimdfchtlc.seed.GetHex());
        return Res::Ok();
    }

    auto offer=mnview.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
    if (!offer) {
        return Res::Err("offer with creation tx %s does not exists!", dfchtlc->offerTx.GetHex());
    }
    auto order=mnview.GetICXOrderByCreationTx(offer->orderTx);
    if (!order) {
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());
    }

    CTokenAmount amount;
    std::string ownerAddress;
    if (order->orderType == CICXOrder::ICXORDER_TYPE_INTERNAL)
    {
        amount = {order->idTokenFrom,claimdfchtlc.amount};
        ownerAddress = offer->receiveAddress;
    }
    else
    {
        amount = {order->idTokenTo,claimdfchtlc.amount};
        ownerAddress = dfchtlc->receiveAddress;
    }
    
    auto res = mnview.ICXClaimDFCHTLC(claimdfchtlc);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, claimdfchtlc.creationTx.GetHex(), res.msg);
    }
    res = mnview.AddBalance(GetScriptForDestination(DecodeDestination(ownerAddress)),amount);
    if (!res.ok) {
        return Res::Err("%s: %s", __func__, res.msg);
    }
    return Res::Ok();
}

Res ApplyICXCloseOrderTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, Consensus::Params const & consensusParams, bool skipAuth, UniValue *rpcInfo)
{
    // if ((int)height < consensusParams.EhardforkHeight) { return Res::Err("Order tx before Ehardfork height (block %d)", consensusParams.EhardforkHeight); }

    // Check quick conditions first
    if (tx.vout.size() != 2) {
        return Res::Err("%s: %s", __func__, "malformed tx vouts (wrong number of vouts)");
    }

    CICXCloseOrderImplemetation closeorder;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CICXCloseOrder &>(closeorder);
    if (!ss.empty()) {
        return Res::Err("%s: deserialization failed: excess %d bytes", __func__, ss.size());
    }
    
    closeorder.creationTx = tx.GetHash();
    closeorder.creationHeight = height;

    std::unique_ptr<CICXOrderImplemetation> order;
    if (!(order = mnview.GetICXOrderByCreationTx(closeorder.orderTx))) {
        return Res::Err("order with creation tx %s does not exists!", closeorder.orderTx.GetHex());
    }
    if (!order->closeTx.IsNull()) {
        return Res::Err("order with creation tx %s is already closed!", closeorder.orderTx.GetHex());
    }

    const Coin& auth = coins.AccessCoin(COutPoint(order->creationTx, 1)); // always n=1 output
    // check auth
    if (!skipAuth && !HasAuth(tx, coins, auth.out.scriptPubKey)) {
        return Res::Err("%s: %s", __func__, "tx must have at least one input from order owner");
    }

    order->closeTx = closeorder.creationTx;
    order->closeHeight = closeorder.creationHeight;

    // Return here to avoid already exist error
    if (rpcInfo) {
        rpcInfo->pushKV("orderTx", closeorder.orderTx.GetHex());
        return Res::Ok();
    }

    // add the balance to tokenFrom to return amount that is left from reserve
    if (order->amountToFill)
    {
        CTokenAmount amount={order->idTokenFrom,order->amountToFill};;
        auto res = mnview.AddBalance(GetScriptForDestination(DecodeDestination(order->ownerAddress)),amount);
        if (!res.ok) {
            return Res::Err("%s: %s", __func__, res.msg);
        }
    }

    auto res = mnview.ICXCloseOrder(closeorder,*order);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, closeorder.creationTx.GetHex(), res.msg);
    }
    res = mnview.ICXCloseOrderTx(*order);
    if (!res.ok) {
        return Res::Err("%s %s: %s", __func__, closeorder.creationTx.GetHex(), res.msg);
    }

    return Res::Ok();
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

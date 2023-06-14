// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_TOKENS_H
#define DEFI_MASTERNODES_CONSENSUS_TOKENS_H

#include <masternodes/consensus/txvisitor.h>

struct CCreateTokenMessage;
struct CUpdateTokenPreAMKMessage;
struct CUpdateTokenMessage;
struct CMintTokensMessage;
struct CBurnTokensMessage;

class CTokensConsensus : public CCustomTxVisitor {
    [[nodiscard]] Res CheckTokenCreationTx() const;
    [[nodiscard]] ResVal<CScript> MintableToken(DCT_ID id, const CTokenImplementation &token, bool anybodyCanMint) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CCreateTokenMessage& obj) const;
    Res operator()(const CUpdateTokenPreAMKMessage& obj) const;
    Res operator()(const CUpdateTokenMessage& obj) const;
    Res operator()(const CMintTokensMessage& obj) const;
    Res operator()(const CBurnTokensMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_TOKENS_H

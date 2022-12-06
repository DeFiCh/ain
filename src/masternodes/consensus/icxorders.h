// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_ICXORDERS_H
#define DEFI_MASTERNODES_CONSENSUS_ICXORDERS_H

#include <masternodes/consensus/txvisitor.h>

struct CICXCreateOrderMessage;
struct CICXMakeOfferMessage;
struct CICXSubmitDFCHTLCMessage;
struct CICXSubmitEXTHTLCMessage;
struct CICXClaimDFCHTLCMessage;
struct CICXCloseOrderMessage;
struct CICXCloseOfferMessage;

class CICXOrdersConsensus : public CCustomTxVisitor {
    CAmount CalculateTakerFee(CAmount amount) const;
    DCT_ID FindTokenByPartialSymbolName(const std::string &symbol) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CICXCreateOrderMessage& obj) const;
    Res operator()(const CICXMakeOfferMessage& obj) const;
    Res operator()(const CICXSubmitDFCHTLCMessage& obj) const;
    Res operator()(const CICXSubmitEXTHTLCMessage& obj) const;
    Res operator()(const CICXClaimDFCHTLCMessage& obj) const;
    Res operator()(const CICXCloseOrderMessage& obj) const;
    Res operator()(const CICXCloseOfferMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_ICXORDERS_H

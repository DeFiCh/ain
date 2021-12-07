#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test loan - estimateloan."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
import time

class EstimateLoanTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
            ]

    def run_test(self):
        self.nodes[0].generate(125)

        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)


        symbolDFI = "DFI"
        symbolBTC = "BTC"
        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]

        self.nodes[0].minttokens("10@" + symbolBTC)
        self.nodes[0].generate(1)

        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        self.nodes[0].utxostoaccount({account: "100@" + symbolDFI})
        self.nodes[0].generate(1)

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
            {"currency": "USD", "token": "TSLA"},
            {"currency": "USD", "token": "TWTR"},
        ]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "100@BTC"},
            {"currency": "USD", "tokenAmount": "5@TSLA"},
            {"currency": "USD", "tokenAmount": "10@TWTR"},
        ]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)

        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(7)

        loanSchemeRatio = 200
        self.nodes[0].createloanscheme(loanSchemeRatio, 1, 'LOAN0001')
        self.nodes[0].generate(1)

        ownerAddress1 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId1 = self.nodes[0].createvault(ownerAddress1) # default loan scheme
        self.nodes[0].generate(1)

        # Vault not found
        try:
            self.nodes[0].estimateloan("af03dbd05492caf362d0daf623be182469bcbae7095d3bab682e40ea3d7c2cbb", {"TSLA": 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault <af03dbd05492caf362d0daf623be182469bcbae7095d3bab682e40ea3d7c2cbb> not found." in errorString)
        # Without collaterals
        try:
            self.nodes[0].estimateloan(vaultId1, {"TSLA": 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot estimate loan without collaterals." in errorString)

        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': True,
                            'interest': 0.01})
        self.nodes[0].setloantoken({
                            'symbol': "TWTR",
                            'name': "Twitter Token",
                            'fixedIntervalPriceId': "TWTR/USD",
                            'mintable': True,
                            'interest': 0.01})
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId1, account, '1@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, account, '0.01@BTC')
        self.nodes[0].generate(1)

        # Negative split value
        try:
            self.nodes[0].estimateloan(vaultId1, {"TSLA": -1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)
        # Token that does not exists
        try:
            self.nodes[0].estimateloan(vaultId1, {"TSLAAA": 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token TSLAAA does not exist!" in errorString)
        # Token not set as loan token
        try:
            self.nodes[0].estimateloan(vaultId1, {"DFI": 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("(DFI) is not a loan token!" in errorString)
        # Token without live price
        try:
            self.nodes[0].estimateloan(vaultId1, {"TSLA": 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No live fixed price for TSLA" in errorString)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "100@BTC"},
            {"currency": "USD", "tokenAmount": "5@TSLA"},
            {"currency": "USD", "tokenAmount": "10@TWTR"},
        ]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)

        self.nodes[0].generate(8) # activate prices

        # Total split should be equal to 1
        try:
            self.nodes[0].estimateloan(vaultId1, {"TSLA": 0.8})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("total split between loan tokens = 0.80000000 vs expected 1.00000000" in errorString)

        estimateloan = self.nodes[0].estimateloan(vaultId1, {"TSLA":1})
        # Cannot take more loan than estimated
        try:
            [amount, token] = estimateloan[0].split("@")
            newAmount = "@".join([str(float(amount) * 1.01), token])
            self.nodes[0].takeloan({ "vaultId": vaultId1, "amounts": newAmount })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault does not have enough collateralization ratio" in errorString)

        self.nodes[0].takeloan({ "vaultId": vaultId1, "amounts": estimateloan }) # should be able to take loan amount from estimateloan
        self.nodes[0].generate(1)

        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1["collateralRatio"], loanSchemeRatio) # vault collateral ratio should be equal to its loan scheme ratio.

        vaultId2 = self.nodes[0].createvault(ownerAddress1)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId2, account, '1@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId2, account, '0.01@BTC')
        self.nodes[0].generate(1)

        estimateloan = self.nodes[0].estimateloan(vaultId2, {"TSLA":0.8, "TWTR": 0.2})
        self.nodes[0].takeloan({ "vaultId": vaultId2, "amounts": estimateloan }) # Take multiple loan amount from estimateloan
        self.nodes[0].generate(1)

        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2["collateralRatio"], loanSchemeRatio)

        vaultId3 = self.nodes[0].createvault(ownerAddress1)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId3, account, '1@DFI')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId3, account, '0.01@BTC')
        self.nodes[0].generate(1)

        targetRatio = 400
        estimateloan = self.nodes[0].estimateloan(vaultId3, {"TSLA":0.8, "TWTR": 0.2}, targetRatio)
        self.nodes[0].takeloan({ "vaultId": vaultId3, "amounts": estimateloan })
        self.nodes[0].generate(1)

        vault3 = self.nodes[0].getvault(vaultId3)
        assert_equal(vault3["collateralRatio"], targetRatio)

        # make vault enter under liquidation state
        oracle1_prices = [{"currency": "USD", "tokenAmount": "20@TSLA"}]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)
        self.nodes[0].generate(12) # let fixed price update

        try:
            self.nodes[0].estimateloan(vaultId1, {"TSLA":1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault <" + vaultId1 + "> is in liquidation" in errorString)

if __name__ == '__main__':
    EstimateLoanTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test loan - estimatecollateral."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
import time

class EstimateCollateralTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
            ]

    def run_test(self):
        self.nodes[0].generate(150)

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

        self.nodes[0].minttokens("100@" + symbolBTC)
        self.nodes[0].generate(1)

        account = self.nodes[0].get_genesis_keys().ownerAuthAddress

        self.nodes[0].utxostoaccount({account: "500@" + symbolDFI})
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
                                    'factor': 0.8,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(7)

        loanSchemeRatio = 200
        self.nodes[0].createloanscheme(loanSchemeRatio, 1, 'LOAN0001')
        self.nodes[0].generate(1)

        ownerAddress1 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId1 = self.nodes[0].createvault(ownerAddress1) # default loan scheme
        self.nodes[0].generate(1)

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

        # Token that does not exists
        try:
            self.nodes[0].estimatecollateral("10@TSLAA", 200)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid Defi token: TSLAA" in errorString)
        # Token not set as loan token
        try:
            self.nodes[0].estimatecollateral("10@DFI", 200)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not a loan token!" in errorString)
        # Token without live price
        try:
            self.nodes[0].estimatecollateral("10@TSLA", 200)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No live fixed prices for TSLA/USD" in errorString)

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

        # Negative split value
        try:
            self.nodes[0].estimatecollateral("10@TSLA", 200, {"DFI": -1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)
        # Token not set as collateral
        try:
            self.nodes[0].estimatecollateral("10@TSLA", 200, {"TSLA": 1})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("(TSLA) is not a valid collateral!" in errorString)
        # Total split should be equal to 1
        try:
            self.nodes[0].estimatecollateral("10@TSLA", 200, {"DFI": 0.8})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("total split between collateral tokens = 0.80000000 vs expected 1.00000000" in errorString)

        estimatecollateral = self.nodes[0].estimatecollateral("10@TSLA", 200)

        self.nodes[0].deposittovault(vaultId1, account, estimatecollateral[0])
        self.nodes[0].generate(1)
        # Cannot take more loan than estimated
        try:
            self.nodes[0].takeloan({ "vaultId": vaultId1, "amounts": "10.1@TSLA" })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault does not have enough collateralization ratio" in errorString)

        self.nodes[0].takeloan({ "vaultId": vaultId1, "amounts": "10@TSLA" }) # should be able to take loan amount from estimatecollateral
        self.nodes[0].generate(1)

        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1["collateralRatio"], 200) # vault collateral ratio should be equal to estimatecollateral targetRatio

        vaultId2 = self.nodes[0].createvault(ownerAddress1)
        estimatecollateral = self.nodes[0].estimatecollateral("10@TSLA", 200, {"BTC":0.5, "DFI": 0.5})

        amountDFI = next(x for x in estimatecollateral if "DFI" in x)
        amountBTC = next(x for x in estimatecollateral if "BTC" in x)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId2, account, amountDFI)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId2, account, amountBTC)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": vaultId2, "amounts": "10@TSLA" })
        self.nodes[0].generate(1)

        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2["collateralRatio"], 200)

        vaultId3 = self.nodes[0].createvault(ownerAddress1)
        estimatecollateral = self.nodes[0].estimatecollateral(["10@TSLA", "10@TWTR"], 200, {"BTC":0.5, "DFI": 0.5})

        amountDFI = next(x for x in estimatecollateral if "DFI" in x)
        amountBTC = next(x for x in estimatecollateral if "BTC" in x)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId3, account, amountDFI)
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId3, account, amountBTC)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({ "vaultId": vaultId3, "amounts": ["10@TSLA", "10@TWTR"] })
        self.nodes[0].generate(1)

        vault3 = self.nodes[0].getvault(vaultId3)
        assert_equal(vault3["collateralRatio"], 200)

if __name__ == '__main__':
    EstimateCollateralTest().main()

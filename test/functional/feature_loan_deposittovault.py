#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - deposittovault."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than
import time

class DepositToVaultTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    @DefiTestFramework.rollback
    def run_test(self):
        # Prepare tokens for deposittoloan
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI
        self.nodes[0].generate(25)
        self.sync_blocks()
        self.nodes[1].generate(101)
        self.sync_blocks()

        self.nodes[1].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[1].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[1].generate(1)
        self.sync_blocks()

        symbolDFI = "DFI"
        symbolBTC = "BTC"

        self.nodes[1].minttokens("10@" + symbolBTC)

        self.nodes[1].generate(1)
        self.sync_blocks()

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        accountDFI = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountBTC = self.nodes[1].get_genesis_keys().ownerAuthAddress

        self.nodes[0].utxostoaccount({accountDFI: "100@" + symbolDFI})
        self.nodes[0].generate(1)
        self.sync_blocks()

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "BTC"},
            {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "1@BTC"},
            {"currency": "USD", "tokenAmount": "1@TSLA"}]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(7)
        self.sync_blocks()

        self.nodes[0].createloanscheme(200, 1, 'LOAN0001')
        self.nodes[0].generate(1)

        ownerAddress1 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId1 = self.nodes[0].createvault(ownerAddress1) # default loan scheme
        self.nodes[0].generate(1)

        # Insufficient funds
        try:
            self.nodes[0].deposittovault(vaultId1, accountDFI, '101@DFI')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        # Check from auth
        try:
            self.nodes[0].deposittovault(vaultId1, accountBTC, '1@DFI')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect authorization for {}".format(accountBTC) in errorString)

        # Check vault exists
        try:
            self.nodes[0].deposittovault("76a9148080dad765cbfd1c38f95e88592e24e43fb642828a948b2a457a8ba8ac", accountDFI, '1@DFI')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault <76a9148080dad765cbfd1c38f95e88592e24e43fb642828a948b2a457a8ba8ac> not found" in errorString)

        self.nodes[0].deposittovault(vaultId1, accountDFI, '0.7@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@DFI'])
        acDFI = self.nodes[0].getaccount(accountDFI)
        assert_equal(acDFI, ['99.30000000@DFI'])

        # Correct deposittovault
        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.6@BTC')
        self.nodes[1].generate(1)

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@DFI', '0.60000000@BTC'])
        acBTC = self.nodes[1].getaccount(accountBTC)
        assert_equal(acBTC, ['9.40000000@BTC'])

        # Deposit without breacking 50% DFI condition
        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.1@BTC')
        self.nodes[1].generate(1)

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'],['0.70000000@DFI', '0.70000000@BTC'])
        acBTC = self.nodes[1].getaccount(accountBTC)
        assert_equal(acBTC, ['9.30000000@BTC'])

        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': True,
                            'interest': 0.01})

        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, accountDFI, '0.3@DFI')
        self.nodes[0].generate(1)
        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.3@BTC')

        self.nodes[0].generate(1)
        self.nodes[1].generate(1)
        self.sync_blocks()

        oracle1_prices = [
            {"currency": "USD", "tokenAmount": "1@DFI"},
            {"currency": "USD", "tokenAmount": "1@TSLA"},
            {"currency": "USD", "tokenAmount": "1@BTC"}]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)

        self.nodes[0].generate(8)
        self.sync_blocks()

        self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "0.5@TSLA"})

        self.nodes[0].generate(1)
        self.sync_blocks()

        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1['state'], "active")
        assert_equal(vault1['collateralAmounts'], ['1.00000000@DFI', '1.00000000@BTC'])
        assert_equal(vault1['loanAmounts'], ['0.50000009@TSLA'])
        assert_equal(vault1['collateralValue'], Decimal(2.00000000))
        assert_greater_than(Decimal(0.50000009), vault1['loanValue'])
        assert_equal(vault1['informativeRatio'], Decimal('399.99992800'))


        # make vault enter under liquidation state
        oracle1_prices = [{"currency": "USD", "tokenAmount": "4@TSLA"}]
        mock_time = int(time.time())
        self.nodes[0].setmocktime(mock_time)
        self.nodes[0].setoracledata(oracle_id1, mock_time, oracle1_prices)
        self.nodes[0].generate(6) # let fixed price update
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['state'], "frozen")

        self.nodes[0].generate(6) # let fixed price be stable and check vault is now underLiquidation state
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['state'], "inLiquidation")

        # try to deposit mor BTC breaking 50% DFI condition
        try:
            self.nodes[1].deposittovault(vaultId1, accountBTC, '0.2@BTC')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot deposit to vault under liquidation" in errorString)
        self.nodes[1].generate(1)


if __name__ == '__main__':
    DepositToVaultTest().main()

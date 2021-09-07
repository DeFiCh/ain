#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan Scheme."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than
import calendar
import time

class VaultTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1']
            ]

    def run_test(self):
        self.nodes[0].generate(120)
        self.nodes[0].createloanscheme(175, 3, 'LOAN0001')
        self.nodes[0].createloanscheme(200, 2, 'LOAN0002')
        self.nodes[0].createloanscheme(350, 1.5, 'LOAN0003')
        self.nodes[0].createloanscheme(550, 1.5, 'LOAN0004')
        self.nodes[0].generate(1)

        self.nodes[0].setdefaultloanscheme('LOAN0001')

        self.nodes[0].generate(1)
        # VAULT TESTS
        # Create vault with invalid address
        try:
            self.nodes[0].createvault('ffffffffff')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Error: Invalid owner address' in errorString)

        # Create vault with invalid loanschemeid and default owner address
        try:
            self.nodes[0].createvault('', 'FAKELOAN')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Cannot find existing loan scheme with id FAKELOAN' in errorString)

        # create 2 vaults
        vaultId1 = self.nodes[0].createvault('') # default loan scheme

        owneraddress2 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId2 = self.nodes[0].createvault(owneraddress2, 'LOAN0003')
        self.nodes[0].generate(1)

        # check listvaults
        listVaults = self.nodes[0].listvaults()
        assert(listVaults[vaultId1])
        assert(listVaults[vaultId2])
        owneraddress1 = listVaults[vaultId1]['ownerAddress']

        # assert default loanscheme was assigned correctly
        assert_equal(listVaults[vaultId1]['loanSchemeId'], 'LOAN0001')
        assert_equal(listVaults[vaultId1]['ownerAddress'], owneraddress1)

        # assert non-default loanscheme was assigned correctly
        assert_equal(listVaults[vaultId2]['loanSchemeId'], 'LOAN0003')
        assert_equal(listVaults[vaultId2]['ownerAddress'], owneraddress2)

        # check getvault

        # fail
        try:
            self.nodes[0].getvault('5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('vault <5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf> not found' in errorString)

        # success
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1["loanSchemeId"], 'LOAN0001')
        assert_equal(vault1["ownerAddress"], owneraddress1)

        # updateVault

        # fail
        try:
            params = {}
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least ownerAddress OR loanSchemeId must be set" in errorString)

        # bad loan scheme id
        try:
            params = {'loanSchemeId': 'FAKELOAN'}
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot find existing loan scheme with id FAKELOAN" in errorString)

        # bad owner address
        try:
            params = {'ownerAddress': 'ffffffffff'}
            self.nodes[0].updatevault(vaultId1, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Error: Invalid owner address" in errorString)

        # Create or update vault with loan scheme planned to be destroyed
        destruction_height = self.nodes[0].getblockcount() + 3
        self.nodes[0].destroyloanscheme('LOAN0002', destruction_height)
        self.nodes[0].generate(1)

        # create
        try:
            self.nodes[0].createvault('', 'LOAN0002') # default loan scheme
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set LOAN0002 as loan scheme, set to be destroyed on block 126" in errorString)

        # update
        try:
            params = {'loanSchemeId':'LOAN0002'}
            self.nodes[0].updatevault(vaultId2, params) # default loan scheme
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set LOAN0002 as loan scheme, set to be destroyed on block 126" in errorString)

        # check owner address auth
        othersAddress = self.nodes[1].getnewaddress('', 'legacy')
        self.nodes[1].generate(1)
        try:
            self.nodes[0].createvault(othersAddress)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Incorrect authorization for '+othersAddress in errorString)

        # update vault scheme
        newAddress = self.nodes[0].getnewaddress('', 'legacy')
        params = {'loanSchemeId': 'LOAN0001', 'ownerAddress': newAddress}
        self.nodes[0].updatevault(vaultId2, params)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['ownerAddress'], newAddress)
        assert_equal(vault2['loanSchemeId'], 'LOAN0001')

        # update with non-default loan scheme and delete loan to check automatic update
        params = {'loanSchemeId': 'LOAN0003'}
        self.nodes[0].updatevault(vaultId2, params)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0003')

        self.nodes[0].destroyloanscheme('LOAN0003')
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0001')

        # back to non-default loan scheme and delete scheme with delay
        params = {'loanSchemeId': 'LOAN0004'}
        self.nodes[0].updatevault(vaultId2, params)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0004')

        destruction_height = self.nodes[0].getblockcount() + 2
        self.nodes[0].destroyloanscheme('LOAN0004', destruction_height)
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0004')

        # now LOAN0002 is deleted in next block
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['loanSchemeId'], 'LOAN0001')

        # Prepare tokens for deposittoloan/takeloan
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
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
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}, {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "1@DFI"}, {"currency": "USD", "tokenAmount": "1@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].setcollateraltoken({
                                    'token': idDFI,
                                    'factor': 0.5,
                                    'priceFeedId': oracle_id1})

        self.nodes[0].setcollateraltoken({
                                    'token': idBTC,
                                    'factor': 0.9,
                                    'priceFeedId': oracle_id1})

        self.nodes[0].generate(1)
        self.sync_blocks()
        # Try make first deposit other than DFI breaking 50% DFI ondition
        try:
            self.nodes[1].deposittovault(vaultId1, accountBTC, '1@BTC')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("First deposit must be in DFI" in errorString)

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
        assert("vault <76a9148080dad765cbfd1c38f95e88592e24e43fb642828a948b2a457a8ba8ac> not found" in errorString)

        self.nodes[0].deposittovault(vaultId1, accountDFI, '0.7@DFI')

        self.nodes[0].generate(1)
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@DFI'])
        acDFI = self.nodes[0].getaccount(accountDFI)
        assert_equal(acDFI, ['99.30000000@DFI'])

        # Check vault contains at least 50% DFI
        try:
            self.nodes[1].deposittovault(vaultId1, accountBTC, '0.701@BTC')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the vault must be in DFI" in errorString)
        self.nodes[1].generate(1)

        # Collateral amounts are the same so deposit was not done
        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@DFI'])
        acBTC = self.nodes[1].getaccount(accountBTC)
        assert_equal(acBTC, ['10.00000000@BTC'])

        # Correct deposittovault
        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.6@BTC')
        self.nodes[1].generate(1)

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@DFI', '0.60000000@BTC'])
        acBTC = self.nodes[1].getaccount(accountBTC)
        assert_equal(acBTC, ['9.40000000@BTC'])

        # try to deposit mor BTC breaking 50% DFI condition
        try:
            self.nodes[1].deposittovault(vaultId1, accountBTC, '0.2@BTC')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the vault must be in DFI" in errorString)
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
                        'priceFeedId': oracle_id1,
                        'mintable': True,
                        'interest': 0.01})

        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId1, accountDFI, '0.3@DFI')
        self.nodes[0].generate(1)
        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.3@BTC')

        self.nodes[0].generate(1)
        self.nodes[1].generate(1)
        self.sync_blocks()

        oracle1_prices = [{"currency": "USD", "tokenAmount": "1@DFI"}, {"currency": "USD", "tokenAmount": "1@TSLA"}, {"currency": "USD", "tokenAmount": "1@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "0.5@TSLA"})

        self.nodes[0].generate(1)
        self.sync_blocks()
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1['loanAmount'], ['0.50000000@TSLA'])
        assert_equal(vault1['collateralValue'], Decimal(2.00000000))
        assert_greater_than(vault1['loanValue'],Decimal(0.5))

if __name__ == '__main__':
    VaultTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test vault."""

from decimal import Decimal
from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error
import calendar
import time

class VaultTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1', '-fortcanninghillheight=300', '-jellyfish_regtest=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-eunosheight=1', '-txindex=1', '-fortcanningheight=1', '-fortcanninghillheight=300', '-jellyfish_regtest=1']
            ]

    def run_test(self):
        self.nodes[0].generate(120)
        self.nodes[0].createloanscheme(175, 3, 'LOAN0001')
        self.nodes[0].createloanscheme(150, 2.5, 'LOAN000A')
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
        assert('recipient script (ffffffffff) does not solvable/non-standard' in errorString)

        ownerAddress1 = self.nodes[0].getnewaddress('', 'legacy')
        # Create vault with invalid loanschemeid and default owner address
        try:
            self.nodes[0].createvault(ownerAddress1, 'FAKELOAN')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Cannot find existing loan scheme with id FAKELOAN' in errorString)

        # create 4 vaults
        vaultId1 = self.nodes[0].createvault(ownerAddress1) # default loan scheme

        ownerAddress2 = self.nodes[0].getnewaddress('', 'legacy')
        vaultId2 = self.nodes[0].createvault(ownerAddress2, 'LOAN0001')
        self.nodes[0].createvault(ownerAddress2, 'LOAN0003')
        self.nodes[0].createvault(ownerAddress2, 'LOAN0003')
        self.nodes[0].generate(1)
        self.sync_blocks()

        # 4 * 0.5, fee is 1DFI in regtest
        assert_equal(self.nodes[0].getburninfo()['feeburn'], Decimal('2'))
        vaultId3 = self.nodes[0].createvault(ownerAddress2, 'LOAN0001')

        # check listvaults
        listVaults = self.nodes[0].listvaults()
        assert(len(listVaults) == 4)

        # check listVaults filter by ownerAddres
        listVaults = self.nodes[0].listvaults({ "ownerAddress": ownerAddress2 })
        assert(len(listVaults) == 3)
        for vault in listVaults:
            assert(vault["ownerAddress"] == ownerAddress2)

        # check listVaults filter by loanSchemeId
        listVaults = self.nodes[0].listvaults({ "loanSchemeId": "LOAN0003" })
        assert(len(listVaults) == 2)
        for vault in listVaults:
            assert(vault["loanSchemeId"] == "LOAN0003")

        # check listVaults pagination
        listVaults = self.nodes[0].listvaults({}, {"limit": 1})
        assert(len(listVaults) == 1)

        # check getvault

        # fail
        try:
            self.nodes[0].getvault('5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Vault <5474b2e9bfa96446e5ef3c9594634e1aa22d3a0722cb79084d61253acbdf87bf> not found' in errorString)

        # success
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1["loanSchemeId"], 'LOAN0001')
        assert_equal(vault1["ownerAddress"], ownerAddress1)
        assert_equal(vault1["state"], "active")
        assert_equal(vault1["collateralAmounts"], [])
        assert_equal(vault1["loanAmounts"], [])
        assert_equal(vault1["collateralValue"], Decimal(0))
        assert_equal(vault1["loanValue"], Decimal(0))
        assert_equal(vault1["informativeRatio"], Decimal('-1.00000000'))

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
            self.nodes[0].createvault(ownerAddress1, 'LOAN0002') # default loan scheme
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
        self.nodes[1].generate(102)
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

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"}, {"currency": "USD", "token": "BTC"}, {"currency": "USD", "token": "TSLA"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "1@DFI"}, {"currency": "USD", "tokenAmount": "1@BTC"}, {"currency": "USD", "tokenAmount": "1@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)

        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
                            'symbol': "TSLA",
                            'name': "Tesla Token",
                            'fixedIntervalPriceId': "TSLA/USD",
                            'mintable': True,
                            'interest': 2})
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
        self.sync_blocks()

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

        # Deposittovault
        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.7@BTC')
        self.nodes[1].generate(1)
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@BTC'])
        acBTC = self.nodes[1].getaccount(accountBTC)
        assert_equal(acBTC, ['9.30000000@BTC'])

        # Try and take loan with only BTC in vault
        try:
            self.nodes[0].takeloan({
                    'vaultId': vaultId1,
                    'amounts': "0.1@TSLA"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the collateral must be in DFI when taking a loan" in errorString)

        self.nodes[0].deposittovault(vaultId1, accountDFI, '0.7@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'], ['0.70000000@DFI', '0.70000000@BTC'])
        acDFI = self.nodes[0].getaccount(accountDFI)
        assert_equal(acDFI, ['99.30000000@DFI'])

        self.nodes[0].deposittovault(vaultId1, accountDFI, '0.3@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[1].deposittovault(vaultId1, accountBTC, '0.3@BTC')
        self.nodes[1].generate(1)
        self.sync_blocks()

        vault1 = self.nodes[1].getvault(vaultId1)
        assert_equal(vault1['collateralAmounts'],['1.00000000@DFI', '1.00000000@BTC'])
        acBTC = self.nodes[1].getaccount(accountBTC)
        assert_equal(acBTC, ['9.00000000@BTC'])
        acDFI = self.nodes[0].getaccount(accountDFI)
        assert_equal(acDFI, ['99.00000000@DFI'])

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

        interest = self.nodes[0].getinterest('LOAN0001')[0]
        assert_equal(interest['interestPerBlock'], Decimal('4.7E-7'))

        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1['loanAmounts'], ['0.50000047@TSLA'])
        assert_equal(vault1['collateralValue'], Decimal('1.800000000'))
        assert_equal(vault1['loanValue'],Decimal('0.50000047'))
        assert_equal(vault1['interestValue'],Decimal('0.00000047'))
        assert_equal(vault1['interestAmounts'],['0.00000047@TSLA'])

        # Try and withdraw all DFI after loan has been taken
        try:
            self.nodes[0].withdrawfromvault(vaultId1, accountDFI, "1@DFI")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the collateral must be in DFI" in errorString)

        params = {'loanSchemeId':'LOAN000A'}
        self.nodes[0].updatevault(vaultId1, params)
        self.nodes[0].generate(1)
        self.sync_blocks()

        # interest is moved out from old scheme
        interest = self.nodes[0].getinterest('LOAN0001')
        assert_equal(len(interest), 0)

        # make vault enter under liquidation state
        oracle1_prices = [{"currency": "USD", "tokenAmount": "4@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)

        self.nodes[0].generate(11)
        self.sync_blocks()
        vault1 = self.nodes[0].getvault(vaultId1)
        assert_equal(vault1['state'], "inLiquidation")
        assert_equal(vault1['liquidationHeight'], 324)
        assert_equal(vault1['liquidationPenalty'], Decimal('5.00000000'))
        assert_equal(vault1['batchCount'], 1)

        assert_raises_rpc_error(-26, 'Vault is under liquidation', self.nodes[0].closevault, vaultId1, ownerAddress1)

        self.nodes[0].deposittovault(vaultId2, accountDFI, '2.5@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks()

        vault2 = self.nodes[0].getvault(vaultId2)
        assert_equal(vault2['collateralAmounts'], ['2.50000000@DFI'])
        assert_equal(self.nodes[0].getaccount(ownerAddress2), [])

        self.nodes[0].takeloan({
                    'vaultId': vaultId2,
                    'amounts': "0.355@TSLA"})
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)
        self.nodes[0].createloanscheme(200, 2.5, 'LOAN0005')
        self.nodes[0].generate(1)
        vault2 = self.nodes[0].getvault(vaultId2)

        params = {'loanSchemeId': 'LOAN0005'}

        try:
            self.nodes[0].updatevault(vaultId2, params)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault does not have enough collateralization ratio defined by loan scheme - 176 < 200" in errorString)
        self.nodes[0].generate(1)


        try:
            self.nodes[0].closevault(vaultId2, ownerAddress2)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Vault <"+vaultId2+"> has loans" in errorString)
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].deposittovault(vaultId3, accountDFI, '2.5@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].closevault(vaultId3, ownerAddress2)
        self.nodes[0].generate(1)
        self.sync_blocks()

        # collaterals 2.5 + 0.5 fee
        assert_equal(self.nodes[0].getaccount(ownerAddress2)[0], '3.00000000@DFI')

        # Invalid loan token
        try:
            estimatevault = self.nodes[0].estimatevault('3.00000000@DFI', '3.00000000@TSLAA')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid Defi token: TSLAA" in errorString)
        # Invalid collateral token
        try:
            estimatevault = self.nodes[0].estimatevault('3.00000000@DFII', '3.00000000@TSLA')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid Defi token: DFII" in errorString)
        # Token not set as a collateral
        try:
            estimatevault = self.nodes[0].estimatevault('3.00000000@TSLA', '3.00000000@TSLA')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token with id (2) is not a valid collateral!" in errorString)
        # Token not set as loan token
        try:
            estimatevault = self.nodes[0].estimatevault('3.00000000@DFI', '3.00000000@DFI')
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token with id (0) is not a loan token!" in errorString)

        vault = self.nodes[0].getvault(vaultId2)
        estimatevault = self.nodes[0].estimatevault(vault["collateralAmounts"], vault["loanAmounts"])
        assert_equal(estimatevault["collateralValue"], vault["collateralValue"])
        assert_equal(estimatevault["loanValue"], vault["loanValue"])
        assert_equal(estimatevault["informativeRatio"], vault["informativeRatio"])
        assert_equal(estimatevault["collateralRatio"], vault["collateralRatio"])


        # Test BTC price increase and remove some BTC from collateral

        # Reset price
        oracle1_prices = [{"currency": "USD", "tokenAmount": "1@DFI"}, {"currency": "USD", "tokenAmount": "1@TSLA"}, {"currency": "USD", "tokenAmount": "1@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(11)

        # Deposit collaterals. 50% of BTC
        address = self.nodes[0].getnewaddress()
        self.nodes[1].sendtokenstoaddress({}, { address: '1.50@BTC'})
        self.nodes[1].generate(1)
        self.sync_blocks()
        vaultId4 = self.nodes[0].createvault(address, 'LOAN000A')
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId4, address, '1.25@BTC') # 1.25@BTC as collateral factor 0.8
        self.nodes[0].deposittovault(vaultId4, accountDFI, '1@DFI')
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
                        'vaultId': vaultId4,
                        'amounts': "1@TSLA"
                    })
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId4, address, '0.2@BTC')
        self.nodes[0].generate(1)

        # Should be able to withdraw extra BTC
        self.nodes[0].withdrawfromvault(vaultId4, address, "0.1@BTC")
        self.nodes[0].generate(1)

        # BTC doubles in price
        oracle1_prices = [{"currency": "USD", "tokenAmount": "1@DFI"}, {"currency": "USD", "tokenAmount": "1@TSLA"}, {"currency": "USD", "tokenAmount": "2@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(20)

        # Should be able to withdraw part of BTC after BTC appreciation in price
        self.nodes[0].withdrawfromvault(vaultId4, address, "0.5@BTC")
        self.nodes[0].generate(1)

        # Should not be able to withdraw if DFI lower than 50% of collateralized loan value
        try:
            self.nodes[0].withdrawfromvault(vaultId4, accountDFI, "0.26@DFI")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("At least 50% of the minimum required collateral must be in DFI" in errorString)

        # Should be able to take 0.33@TSLA and respect 50% DFI ratio
        self.nodes[0].takeloan({
            'vaultId': vaultId4,
            'amounts': "0.33@TSLA"
        })
        self.nodes[0].generate(1)

        # Collateral value overflow

        # Add token and poolpairs needed for paybackloan
        self.nodes[0].setloantoken({
                                    'symbol': "DUSD",
                                    'name': "DUSD stable token",
                                    'fixedIntervalPriceId': "DUSD/USD",
                                    'mintable': True,
                                    'interest': 1})
        self.nodes[0].generate(1)

        poolOwner = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createpoolpair({
            "tokenA": "DUSD",
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])
        self.nodes[0].generate(1)

        accountDFI = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.nodes[0].minttokens("300@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].utxostoaccount({accountDFI: "100@" + symbolDFI})
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity({
            accountDFI: ["300@DUSD", "100@" + symbolDFI]
        }, accountDFI, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": "DUSD",
            "tokenB": "TSLA",
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-TSLA",
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("100@TSLA")
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("100@DUSD")
        self.nodes[0].generate(1)
        self.nodes[0].addpoolliquidity({
            accountDFI: ["100@TSLA", "100@DUSD"]
        }, accountDFI, [])
        self.nodes[0].generate(1)

        vaultId5 = self.nodes[0].createvault(address, 'LOAN000A')
        self.nodes[0].generate(1)

        self.nodes[0].utxostoaccount({accountDFI: "100000000@" + symbolDFI})
        self.nodes[0].generate(1)
        self.nodes[0].deposittovault(vaultId5, accountDFI, "100000000@" + symbolDFI)
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId5,
            'amounts': "0.5@TSLA"
        })
        self.nodes[0].generate(1)

        oracle1_prices = [{"currency": "USD", "tokenAmount": "9999999999@DFI"}, {"currency": "USD", "tokenAmount": "1@TSLA"}, {"currency": "USD", "tokenAmount": "1@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(20)

        vault5 = self.nodes[0].getvault(vaultId5)
        assert_equal(vault5['collateralValue'], 0) # collateral value overflowed

        # Actions on vault should be blocked
        assert_raises_rpc_error(-32600, 'Value/price too high', self.nodes[0].takeloan, {'vaultId': vaultId5,'amounts': "0.5@TSLA"})
        assert_raises_rpc_error(-32600, 'Value/price too high', self.nodes[0].deposittovault, vaultId5, accountDFI, "1@" + symbolDFI)
        assert_raises_rpc_error(-32600, 'Value/price too high', self.nodes[0].withdrawfromvault, vaultId5, address, "1@DFI")

        # Should be able to close vault
        self.nodes[0].paybackloan({'vaultId': vaultId5, 'from': address, 'amounts': ["1@TSLA"]})
        self.nodes[0].generate(1)
        self.nodes[0].closevault(vaultId5, address)
        self.nodes[0].generate(1)


if __name__ == '__main__':
    VaultTest().main()

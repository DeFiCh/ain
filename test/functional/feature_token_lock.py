#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Token Lock."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time


class TokenLockTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1',
             '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=200', '-subsidytest=1']]

    def run_test(self):
        self.nodes[0].generate(150)

        # Set up oracles and tokens
        self.setup_test()

        # Test pool lock
        self.pool_lock()

        # Test oracle lock
        self.oracle_lock()

        # Test vault lock
        self.vault_lock()

        # Test token lock
        self.token_lock()

    def setup_test(self):
        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Set token symbols
        self.symbolDFI = 'DFI'
        self.symbolDUSD = 'DUSD'
        self.symbolTSLA = 'TSLA'
        self.symbolGOOGL = 'GOOGL'
        self.symbolTD = 'TSLA-DUSD'

        # Setup oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolTSLA},
            {"currency": "USD", "token": self.symbolGOOGL},
        ]
        self.oracle_id = self.nodes[0].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[0].generate(1)

        # Create Oracle prices
        self.price_dfi = 5
        self.price_tsla = 870
        self.price_googl = 2850

        # Set Oracle data
        self.oracle_prices = [
            {"currency": "USD", "tokenAmount": f'{self.price_dfi}@{self.symbolDFI}'},
            {"currency": "USD", "tokenAmount": f'{self.price_tsla}@{self.symbolTSLA}'},
            {"currency": "USD", "tokenAmount": f'{self.price_googl}@{self.symbolGOOGL}'},
        ]
        self.nodes[0].setoracledata(self.oracle_id, int(time.time()), self.oracle_prices)
        self.nodes[0].generate(10)

        # Setup loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f'{self.symbolDUSD}/USD',
            'mintable': True,
            'interest': 0})

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f'{self.symbolTSLA}/USD',
            'mintable': True,
            'interest': 1})

        self.nodes[0].setloantoken({
            'symbol': self.symbolGOOGL,
            'name': self.symbolGOOGL,
            'fixedIntervalPriceId': f'{self.symbolGOOGL}/USD',
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(1)

        # Set collateral token
        self.nodes[0].setcollateraltoken({
            'token': self.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f'{self.symbolDUSD}/USD'
        })

        # Set token ids
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]

        # Mint tokens
        self.nodes[0].minttokens([f'1000000@{self.idDUSD}'])
        self.nodes[0].minttokens([f'1000000@{self.idTSLA}'])
        self.nodes[0].minttokens([f'1000000@{self.idGOOGL}'])
        self.nodes[0].generate(1)

        # Tokenise DFI
        self.nodes[0].utxostoaccount({self.address: f'2000@{self.symbolDFI}'})
        self.nodes[0].generate(1)

        # Create pool
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolTSLA,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.address,
            "pairSymbol": self.symbolTD,
        })

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.address,
        })
        self.nodes[0].generate(1)

        # Add liquidity
        self.nodes[0].addpoolliquidity({
            self.address: [f'870000@{self.symbolTSLA}', f'1000@{self.symbolDUSD}']
        }, self.address)

        self.nodes[0].addpoolliquidity({
            self.address: [f'1000@{self.symbolDFI}', f'5000@{self.symbolDUSD}']
        }, self.address)
        self.nodes[0].generate(1)

        # Create loan schemes
        self.nodes[0].createloanscheme(100, 0.01, 'LOAN100')
        self.nodes[0].generate(1)

        # Create vault
        self.vault = self.nodes[0].createvault(self.address)
        self.nodes[0].generate(1)

        # Fund vault
        self.nodes[0].deposittovault(self.vault, self.address, f'100000@{self.symbolDUSD}')
        self.nodes[0].generate(1)

    def pool_lock(self):
        # Move to fork block
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Enable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Try test pool swap in both directions
        assert_raises_rpc_error(-32600, "Pool currently disabled due to locked token", self.nodes[0].poolswap, {
            "from": self.address,
            "tokenFrom": self.symbolTSLA,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolDUSD
        })
        assert_raises_rpc_error(-32600, "Pool currently disabled due to locked token", self.nodes[0].poolswap, {
            "from": self.address,
            "tokenFrom": self.symbolDUSD,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolTSLA
        })

        # Try pool swap in both directions
        assert_raises_rpc_error(-32600, "Pool currently disabled due to locked token", self.nodes[0].poolswap, {
            "from": self.address,
            "tokenFrom": self.symbolTSLA,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolDUSD
        })
        assert_raises_rpc_error(-32600, "Pool currently disabled due to locked token", self.nodes[0].poolswap, {
            "from": self.address,
            "tokenFrom": self.symbolDUSD,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolTSLA
        })

        # Disable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'false'}})
        self.nodes[0].generate(1)

        # Test pool swap should now work
        self.nodes[0].testpoolswap({
            "from": self.address,
            "tokenFrom": self.symbolDUSD,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolTSLA
        })

        # Pool swap should now work
        self.nodes[0].poolswap({
            "from": self.address,
            "tokenFrom": self.symbolDUSD,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolTSLA
        })
        self.nodes[0].clearmempool()

    def oracle_lock(self):
        # Set Oracle data
        self.nodes[0].setoracledata(self.oracle_id, int(time.time()), self.oracle_prices)
        self.nodes[0].generate(10)

        # Check output before lock
        result = self.nodes[0].getfixedintervalprice(f'{self.symbolTSLA}/USD')
        assert_equal(result['fixedIntervalPriceId'], f'{self.symbolTSLA}/USD')
        assert_equal(result['isLive'], True)

        # Enable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Check price feed disabled
        assert_raises_rpc_error(-5, "Fixed interval price currently disabled due to locked token",
                                self.nodes[0].getfixedintervalprice, f'{self.symbolTSLA}/USD')

        # Disable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'false'}})
        self.nodes[0].generate(1)

        # Set Oracle data
        self.nodes[0].setoracledata(self.oracle_id, int(time.time()), self.oracle_prices)
        self.nodes[0].generate(10)

        # Check output before lock
        result = self.nodes[0].getfixedintervalprice(f'{self.symbolTSLA}/USD')
        assert_equal(result['fixedIntervalPriceId'], f'{self.symbolTSLA}/USD')
        assert_equal(result['isLive'], True)

    def vault_lock(self):
        # Take loan
        self.nodes[0].takeloan({'vaultId': self.vault, 'amounts': f'1@{self.symbolTSLA}'})
        self.nodes[0].generate(1)

        # Enable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Try and take loan while token in vault locked
        assert_raises_rpc_error(-32600, "Cannot take loan while any of the asset's price in the vault is not live",
                                self.nodes[0].takeloan, {'vaultId': self.vault, 'amounts': f'1@{self.symbolTSLA}'})
        assert_raises_rpc_error(-32600, "Cannot take loan while any of the asset's price in the vault is not live",
                                self.nodes[0].takeloan, {'vaultId': self.vault, 'amounts': f'1@{self.symbolGOOGL}'})

        # Vault amounts should be -1 while token locked
        result = self.nodes[0].getvault(self.vault)
        assert_equal(result['collateralValue'], -1)
        assert_equal(result['loanValue'], -1)
        assert_equal(result['interestValue'], -1)
        assert_equal(result['informativeRatio'], -1)
        assert_equal(result['collateralRatio'], -1)
        result = self.nodes[0].getvault(self.vault, True)
        assert_equal(result['collateralValue'], -1)
        assert_equal(result['loanValue'], -1)
        assert_equal(result['interestValue'], -1)
        assert_equal(result['informativeRatio'], -1)
        assert_equal(result['collateralRatio'], -1)
        assert_equal(result['interestPerBlockValue'], -1)
        assert_equal(result['interestsPerBlock'], [])

        # Deposit to vault should fail
        assert_raises_rpc_error(-32600, "Fixed interval price currently disabled due to locked token",
                                self.nodes[0].deposittovault, self.vault, self.address, f'100000@{self.symbolDUSD}')

        # Payback loan with native token failed due to no pool to swap interest to DFI
        assert_raises_rpc_error(-32600, "Pool currently disabled due to locked token", self.nodes[0].paybackloan,
                                {'vaultId': self.vault, 'from': self.address, 'amounts': f'1@{self.symbolTSLA}'})

        # Disable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'false'}})
        self.nodes[0].generate(1)

        # Set Oracle data
        self.nodes[0].setoracledata(self.oracle_id, int(time.time()), self.oracle_prices)
        self.nodes[0].generate(10)

        # Vault amounts should now be restored
        result = self.nodes[0].getvault(self.vault)
        assert_equal(result['collateralValue'], Decimal('100000.00000000'))
        assert_equal(result['loanValue'], Decimal('870.00217500'))
        assert_equal(result['interestValue'], Decimal('0.00217500'))
        assert_equal(result['informativeRatio'], Decimal('11494.22413800'))
        assert_equal(result['collateralRatio'], 11494)

    def token_lock(self):
        # Enable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Try and update token
        assert_raises_rpc_error(-32600, "Cannot update token during lock", self.nodes[0].updatetoken, self.idTSLA,
                                {'name': 'Tesla'})

        # Disable token lock
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'false'}})
        self.nodes[0].generate(1)

        # Try same update
        self.nodes[0].updatetoken(self.idTSLA, {'name': 'Tesla'})
        self.nodes[0].generate(1)

        # Verify results
        result = self.nodes[0].gettoken(self.idTSLA)[self.idTSLA]
        assert_equal(result['name'], 'Tesla')


if __name__ == '__main__':
    TokenLockTest().main()

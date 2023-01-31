#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test migrating v1 in futures attributes"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

from decimal import Decimal
import time

class MigrateV1Test(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1']]

    def run_test(self):

        # Set up tokens
        self.setup_test_tokens()

        # Test v1 remnants are no longer present after split
        self.test_migration_on_fork()

    def setup_test_tokens(self):
        self.nodes[0].generate(101)

        # Symbols
        self.symbolDFI = 'DFI'
        self.symbolDUSD = 'DUSD'
        self.symbolTSLA = 'TSLA'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolDUSD},
            {"currency": "USD", "token": self.symbolTSLA},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDFI}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Set loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f"{self.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.isDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Mint some loan tokens
        self.nodes[0].minttokens([
            f'1000000@{self.symbolDUSD}',
            f'1000000@{self.symbolTSLA}',
        ])
        self.nodes[0].generate(1)

    def test_migration_on_fork(self):

        # Set all futures attributes
        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/dfip2203/reward_pct':'0.05',
            'v0/params/dfip2203/block_period':'10'
        }})
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{
            'v0/params/dfip2203/active':'true'
        }})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(self.address, f'1@{self.symbolDUSD}', self.symbolTSLA)
        self.nodes[0].futureswap(self.address, f'1@{self.symbolTSLA}')
        self.nodes[0].generate(10)

        # Get pre-split values
        [current_amount, current_symbol] = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/live/economy/dfip2203_current'][1].split('@')
        [burnt_amount, burnt_symbol] = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/live/economy/dfip2203_burned'][1].split('@')
        [minted_amount, minted_symbol] = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/live/economy/dfip2203_minted'][1].split('@')

        # Define multiplier
        multiplier = 2

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{self.nodes[0].getblockcount() + 2}':f'{self.idTSLA}/{multiplier}'}})
        self.nodes[0].generate(2)

        # Get split amounts
        [current_split_amount, current_split_symbol] = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/live/economy/dfip2203_current'][1].split('@')
        [burnt_split_amount, burnt_split_symbol] = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/live/economy/dfip2203_burned'][1].split('@')
        [minted_split_amount, minted_split_symbol] = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/live/economy/dfip2203_minted'][1].split('@')

        # Check amounts updated as expected
        assert_equal(Decimal(current_amount) * multiplier, Decimal(current_split_amount))
        assert_equal(Decimal(burnt_amount) * multiplier, Decimal(burnt_split_amount))
        assert_equal(Decimal(minted_amount) * multiplier, Decimal(minted_split_amount))

        # Check symbols are the same
        assert_equal(current_symbol, current_split_symbol)
        assert_equal(burnt_symbol, burnt_split_symbol)
        assert_equal(minted_symbol, minted_split_symbol)

if __name__ == '__main__':
    MigrateV1Test().main()

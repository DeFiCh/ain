#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token split migrate lock"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal
import time

class TokenSplitMigrateLockTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-vaultindex=1', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', f'-fortcanningcrunchheight=200', f'-greatworldheight=200', '-subsidytest=1']]

    def run_test(self):
        self.setup_test_tokens()
        self.test_unlock_migration()

    def setup_test_tokens(self):
        self.nodes[0].generate(101)

        # Symbols
        self.symbolDFI = 'DFI'
        self.symbolGOOGL = 'GOOGL'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolGOOGL},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolGOOGL}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Set loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolGOOGL,
            'name': self.symbolGOOGL,
            'fixedIntervalPriceId': f"{self.symbolGOOGL}/USD",
            "isDAT": True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]

        # Mint some loan tokens
        self.nodes[0].minttokens([
            f'1000000@{self.symbolGOOGL}',
        ])
        self.nodes[0].generate(1)

    def test_unlock_migration(self):

        # Move to FCC / GW
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Define split height
        split_height = self.nodes[0].getblockcount() + 11

        # Lock token
        self.nodes[0].setgovheight({"ATTRIBUTES":{f'v0/locks/token/{self.idGOOGL}':'true'}}, split_height - 2)
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgovheight({"ATTRIBUTES":{f'v0/oracles/splits/{split_height}':f'{self.idGOOGL}/2'}}, split_height - 1)
        self.nodes[0].generate(1)

        # Token unlock
        self.nodes[0].setgovheight({"ATTRIBUTES":{f'v0/locks/token/{self.idGOOGL}':'false'}}, split_height + 10)
        self.nodes[0].generate(1)

        # Move to split height
        self.nodes[0].generate(split_height - self.nodes[0].getblockcount())

        # Udpate token ID
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]

        # Check split successful
        result = self.nodes[0].gettoken(self.symbolGOOGL)[f'{self.idGOOGL}']
        assert_equal(result['minted'], Decimal('2000000.00000000'))

        # Check stored token unlock has migrated
        result = self.nodes[0].listgovs()[8][1]
        assert_equal(result[f'{split_height + 10}'], {f'v0/locks/token/{self.idGOOGL}': 'false'})

if __name__ == '__main__':
    TokenSplitMigrateLockTest().main()

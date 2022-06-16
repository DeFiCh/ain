#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test commission fix"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal

class CommissionFixTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.great_world = 200
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1', f'-greatworldheight={self.great_world}', '-subsidytest=1']]

    def run_test(self):
        self.setup_test_tokens()
        self.setup_test_pools()
        self.pool_commission()

    def setup_test_tokens(self):
        self.nodes[0].generate(101)

        # Symbols
        self.symbolDFI = 'DFI'
        self.symbolDUSD = 'DUSD'
        self.symbolGOOGL = 'GOOGL'
        self.symbolGD = 'GOOGL-DUSD'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Set loan tokens
        self.nodes[0].createtoken({
            'symbol': self.symbolGOOGL,
            'name': self.symbolGOOGL,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]

        # Mint some loan tokens
        self.nodes[0].minttokens([
            f'1000000@{self.symbolDUSD}',
            f'1000000@{self.symbolGOOGL}',
        ])
        self.nodes[0].generate(1)

    def setup_test_pools(self):

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolGOOGL,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.address,
            "symbol": self.symbolGD
        })
        self.nodes[0].generate(1)

    def pool_commission(self):

        # Set up commission address
        commission_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].sendtoaddress(commission_address, 1)
        self.nodes[0].accounttoaccount(self.address, {
            commission_address: [f'1000@{self.symbolGOOGL}', f'1000@{self.symbolDUSD}']
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            commission_address: [f'100@{self.symbolGOOGL}', f'100@{self.symbolDUSD}']
        }, commission_address)
        self.nodes[0].generate(1)

        # Add liquidity twice for valid GetShare, possible bug?
        self.nodes[0].addpoolliquidity({
            commission_address: [f'100@{self.symbolGOOGL}', f'100@{self.symbolDUSD}']
        }, commission_address)
        self.nodes[0].generate(1)

        # Execute pool swap
        self.nodes[0].poolswap({
            "from": self.address,
            "tokenFrom": self.symbolGOOGL,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolDUSD
        })
        self.nodes[0].generate(2)

        # Commission missing
        for result in self.nodes[0].listaccounthistory(commission_address):
            assert(result['type'] != 'Commission')

        # Show Commission with low depth
        result = self.nodes[0].listaccounthistory(commission_address, {'depth': 1})
        assert_equal(result[0]['type'], 'Commission')

if __name__ == '__main__':
    CommissionFixTest().main()

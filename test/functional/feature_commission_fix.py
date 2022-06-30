#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test commission fix"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal
import time

class CommissionFixTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.fortcanningspring = 200
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1', f'-fortcanningspringheight={self.fortcanningspring}', '-subsidytest=1']]

    def run_test(self):
        # Set up test tokens
        self.setup_test_tokens()

        # Set up pool
        self.setup_test_pool()

        # Test pool commission
        self.pool_commission()

        # Set up pool after fork
        self.setup_test_pool_fork()

        # Test pool commission after fork
        self.pool_commission_fork()

    def setup_test_tokens(self):
        # Generate chain
        self.nodes[0].generate(101)

        # Symbols
        self.symbolDUSD = 'DUSD'
        self.symbolGOOGL = 'GOOGL'
        self.symbolTSLA = 'TSLA'
        self.symbolGD = 'GOOGL-DUSD'
        self.symbolTD = 'TSLA-DUSD'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolDUSD},
            {"currency": "USD", "token": self.symbolTSLA},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolDUSD}"},
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Create tokens
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

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD",
            "isDAT": True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Mint some loan tokens
        self.nodes[0].minttokens([
            f'1000000@{self.symbolDUSD}',
            f'1000000@{self.symbolGOOGL}',
            f'1000000@{self.symbolTSLA}',
        ])
        self.nodes[0].generate(1)

    def setup_test_pool(self):

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

        # Store pool ID
        self.idGD = list(self.nodes[0].gettoken(self.symbolGD).keys())[0]

    def setup_test_pool_fork(self):

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolTSLA,
            "tokenB": self.symbolDUSD,
            "commission": 0.01,
            "status": True,
            "ownerAddress": self.address,
            "symbol": self.symbolTD
        })
        self.nodes[0].generate(1)

        # Store pool ID
        self.idTD = list(self.nodes[0].gettoken(self.symbolTD).keys())[0]

    def pool_commission(self):

        # Set up commission address
        commission_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].sendtoaddress(commission_address, 1)
        self.nodes[0].accounttoaccount(self.address, {
            commission_address: [f'1000@{self.symbolGOOGL}', f'1000@{self.symbolDUSD}']
        })
        self.nodes[0].generate(1)

        # Save block for revert
        revert_block = self.nodes[0].getblockcount() + 1

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

        # Test accounttoaccount temp fix
        self.nodes[0].accounttoaccount(commission_address, {
            commission_address: self.nodes[0].getaccount(commission_address)
        })
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

        # Show Commission
        result = self.nodes[0].listaccounthistory(commission_address)
        assert_equal(result[0]['type'], 'Commission')

        # Revert to before add liqudity
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(revert_block))
        self.nodes[0].clearmempool()

        # Check pool empty
        result = self.nodes[0].getpoolpair(self.symbolGD)
        assert_equal(result[f'{self.idGD}']['reserveA'], Decimal('0'))

        # Move to fork
        self.nodes[0].generate(self.fortcanningspring - self.nodes[0].getblockcount())

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

        # Show Commission
        result = self.nodes[0].listaccounthistory(commission_address)
        assert_equal(result[0]['type'], 'Commission')

    def pool_commission_fork(self):

        # Set up commission address
        commission_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].sendtoaddress(commission_address, 1)
        self.nodes[0].accounttoaccount(self.address, {
            commission_address: [f'1000@{self.symbolTSLA}', f'1000@{self.symbolDUSD}']
        })
        self.nodes[0].generate(1)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            commission_address: [f'100@{self.symbolTSLA}', f'100@{self.symbolDUSD}']
        }, commission_address)
        self.nodes[0].generate(1)

        # Add liquidity twice for valid GetShare, possible bug?
        self.nodes[0].addpoolliquidity({
            commission_address: [f'100@{self.symbolTSLA}', f'100@{self.symbolDUSD}']
        }, commission_address)
        self.nodes[0].generate(1)

        # Execute pool swap
        self.nodes[0].poolswap({
            "from": self.address,
            "tokenFrom": self.symbolTSLA,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolDUSD
        })
        self.nodes[0].generate(2)

        # Show Commission
        result = self.nodes[0].listaccounthistory(commission_address)
        assert_equal(result[0]['type'], 'Commission')

        # Token split
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}':f'{self.idTSLA}/2'}})
        self.nodes[0].generate(2)

        # Swap old for new values
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Unlock token
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/locks/token/{self.idTSLA}':'false'}})
        self.nodes[0].generate(1)

        # Execute pool swap
        self.nodes[0].poolswap({
            "from": self.address,
            "tokenFrom": self.symbolTSLA,
            "amountFrom": 1,
            "to": self.address,
            "tokenTo": self.symbolDUSD
        })
        self.nodes[0].generate(2)

        # Show Commission
        result = self.nodes[0].listaccounthistory(commission_address)
        assert_equal(result[0]['type'], 'Commission')

if __name__ == '__main__':
    CommissionFixTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test poolpair - testpoolswap."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
import calendar
import time

class PoolPairTestPoolSwapTest (DefiTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            '-txnotokens=0',
            '-amkheight=1',
            '-bayfrontheight=1',
            '-eunosheight=1',
            '-fortcanningheight=1',
            '-fortcanninghillheight=1',
            '-fortcanningspringheight=1',
            '-jellyfish_regtest=1',
            '-simulatemainnet=1'
            ]]

    def createOracles(self):
        self.oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "DUSD"},
                        {"currency": "USD", "token": "TSLA"},
                        {"currency": "USD", "token": "BTC"}]
        self.oracle_id1 = self.nodes[0].appointoracle(self.oracle_address1, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [{"currency": "USD", "tokenAmount": "1@TSLA"},
                        {"currency": "USD", "tokenAmount": "1@DUSD"},
                        {"currency": "USD", "tokenAmount": "1@BTC"},
                        {"currency": "USD", "tokenAmount": "10@DFI"}]

        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id1, timestamp, oracle_prices)

        self.oracle_address2 = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id2 = self.nodes[0].appointoracle(self.oracle_address2, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(self.oracle_id2, timestamp, oracle_prices)
        self.nodes[0].generate(120)

    def setup(self):
        self.nodes[0].generate(120)

        self.createOracles()

        self.mn_address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.account0 = self.nodes[0].getnewaddress()

        self.swapAmount = 1000

        self.symbolDFI = "DFI"
        self.symbolTSLA = "TSLA"
        self.symbolDUSD = "DUSD"
        self.symbolBTC = "BTC"

        self.nodes[0].setloantoken({
            'symbol': "DUSD",
            'name': "DUSD",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0
        })

        self.nodes[0].setloantoken({
            'symbol': "TSLA",
            'name': "TSLA",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 0
        })

        self.nodes[0].setloantoken({
            'symbol': "BTC",
            'name': "BTC",
            'fixedIntervalPriceId': "BTC/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(120)

        # Mint DUSD
        self.nodes[0].minttokens("100000@DUSD")
        self.nodes[0].minttokens("100000@TSLA")
        self.nodes[0].minttokens("100000@BTC")
        self.nodes[0].generate(1)

        # Create DFI tokens
        self.nodes[0].utxostoaccount({self.mn_address: "100000@" + self.symbolDFI})
        self.nodes[0].generate(1)

        # Create pool pair
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSD,
            "tokenB": self.symbolDFI,
            "commission": 0,
            "status": True,
            "ownerAddress": self.mn_address
        })

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDFI,
            "tokenB": self.symbolTSLA,
            "commission": 0,
            "status": True,
            "ownerAddress": self.mn_address
        })

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolBTC,
            "tokenB": self.symbolTSLA,
            "commission": 0,
            "status": True,
            "ownerAddress": self.mn_address
        })
        self.nodes[0].generate(1)

        self.DUSD_DFIPoolID = list(self.nodes[0].getpoolpair("DUSD-DFI"))[0]
        self.DFI_TSLAPoolID = list(self.nodes[0].getpoolpair("DFI-TSLA"))[0]
        self.BTC_TSLAPoolID = list(self.nodes[0].getpoolpair("BTC-TSLA"))[0]

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            self.mn_address: [
                '10000@' + self.symbolDFI,
                '10000@' + self.symbolDUSD]
            }, self.mn_address)
        self.nodes[0].addpoolliquidity({
            self.mn_address: [
                '10000@' + self.symbolTSLA,
                '10000@' + self.symbolDFI]
            }, self.mn_address)
        self.nodes[0].addpoolliquidity({
            self.mn_address: [
                '10000@' + self.symbolTSLA,
                '10000@' + self.symbolBTC]
            }, self.mn_address)
        self.nodes[0].generate(1)

        self.nodes[0].accounttoaccount(self.mn_address, {self.account0: str(self.swapAmount * 2) + "@" + self.symbolDFI})
        self.nodes[0].accounttoaccount(self.mn_address, {self.account0: str(self.swapAmount * 2) + "@" + self.symbolTSLA})
        self.nodes[0].accounttoaccount(self.mn_address, {self.account0: str(self.swapAmount * 2) + "@" + self.symbolBTC})
        self.nodes[0].generate(1)

    def assert_testpoolswap_amount(self, swap_fn, tokenFrom, path):
        params = {
            "from": self.account0,
            "tokenFrom": tokenFrom,
            "amountFrom": self.swapAmount,
            "to": self.account0,
            "tokenTo": self.symbolDUSD,
        }
        [amountSwapped, _] = self.nodes[0].testpoolswap(params, path).split("@")

        swap_fn(params)
        self.nodes[0].generate(1)

        account = self.nodes[0].getaccount(self.account0)
        assert_equal(account[2], f'{amountSwapped}@{self.symbolDUSD}')

    def test_testpoolswap_no_fee(self, swap_fn, tokenFrom, path):
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_a_fee_in(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'in'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_b_fee_in(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_direction': 'in'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_a_fee_out(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'out'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_b_fee_out(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_direction': 'out'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_a_fee_in_token_b_fee_in(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'in',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_direction': 'in'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_a_fee_in_token_b_fee_out(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'in',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_direction': 'out'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_a_fee_out_token_b_fee_in(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'out',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_direction': 'in'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_token_a_fee_out_token_b_fee_out(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'out',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_b_fee_direction': 'out'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def test_testpoolswap_with_multi_pool_fee(self, swap_fn, tokenFrom, path):
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DUSD_DFIPoolID}/token_a_fee_direction': 'in',
            f'v0/poolpairs/{self.DFI_TSLAPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.DFI_TSLAPoolID}/token_a_fee_direction': 'out',
            f'v0/poolpairs/{self.BTC_TSLAPoolID}/token_a_fee_pct': '0.10',
            f'v0/poolpairs/{self.BTC_TSLAPoolID}/token_a_fee_direction': 'out'
        }})
        self.nodes[0].generate(1)
        self.assert_testpoolswap_amount(swap_fn, tokenFrom, path)

    def run_test(self):
        self.setup()

        # List of (swap, tokenFrom, path) tuples
        # swap should be one of poolswap or compositeswap RPC call
        # path should be one of ["direct", "auto", "composite"] or a list of pool ids
        combinations = [
            # poolswap DFI -> DUSD through DFI-DUSD pool
            (self.nodes[0].poolswap, self.symbolDFI, "direct"),
            (self.nodes[0].poolswap, self.symbolDFI, "auto"),
            (self.nodes[0].poolswap, self.symbolDFI, "composite"),
            (self.nodes[0].poolswap, self.symbolDFI, [str(self.DUSD_DFIPoolID)]),

            # compositeswap DFI -> DUSD through DFI-DUSD pool
            (self.nodes[0].compositeswap, self.symbolDFI, "direct"),
            (self.nodes[0].compositeswap, self.symbolDFI, "auto"),
            (self.nodes[0].compositeswap, self.symbolDFI, "composite"),
            (self.nodes[0].compositeswap, self.symbolDFI, [str(self.DUSD_DFIPoolID)]),

            # comspositeswap TSLA -> DUSD swap through TSLA-DFI -> DFI-DUSD pools
            (self.nodes[0].compositeswap, self.symbolTSLA, "auto"),
            (self.nodes[0].compositeswap, self.symbolTSLA, "composite"),
            (self.nodes[0].compositeswap, self.symbolTSLA, [str(self.DFI_TSLAPoolID), str(self.DUSD_DFIPoolID)]),

            # comspositeswap BTC -> DUSD swap through BTC-TSLA -> TSLA-DFI -> DFI-DUSD pools
            (self.nodes[0].compositeswap, self.symbolBTC, "auto"),
            (self.nodes[0].compositeswap, self.symbolBTC, "composite"),
            (self.nodes[0].compositeswap, self.symbolBTC, [str(self.BTC_TSLAPoolID), str(self.DFI_TSLAPoolID), str(self.DUSD_DFIPoolID)]),
        ]

        testCases = [
            self.test_testpoolswap_no_fee,
            self.test_testpoolswap_with_token_a_fee_in,
            self.test_testpoolswap_with_token_b_fee_in,
            self.test_testpoolswap_with_token_a_fee_out,
            self.test_testpoolswap_with_token_b_fee_out,
            self.test_testpoolswap_with_token_a_fee_in_token_b_fee_in,
            self.test_testpoolswap_with_token_a_fee_out_token_b_fee_in,
            self.test_testpoolswap_with_token_a_fee_in_token_b_fee_out,
            self.test_testpoolswap_with_token_a_fee_out_token_b_fee_out,
            self.test_testpoolswap_with_multi_pool_fee
        ]

        height = self.nodes[0].getblockcount()

        for (swap_fn, tokenFrom, path) in combinations:
            for test in testCases:
                test(swap_fn, tokenFrom, path)
                self.rollback_to(height)

if __name__ == '__main__':
    PoolPairTestPoolSwapTest().main()

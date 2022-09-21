#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test asymmetric pool fees"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

from decimal import Decimal
from math import trunc

class PoolPairAsymmetricTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-dakotaheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningspringheight=150', '-jellyfish_regtest=1']]

    def run_test(self):

        # Set up test tokens
        self.setup_test_tokens()

        # Set up test pools
        self.setup_test_pools()

        # Test pool swaps
        self.pool_swaps()

    def pool_swaps(self):

        # Move to fork
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Set DUSD fee on in only
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_pct': '0.05',
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'in'
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert_equal(result['dexFeePctTokenA'], Decimal('0.05000000'))
        assert_equal(result['dexFeeInPctTokenA'], Decimal('0.05000000'))
        assert('dexFeeOutPctTokenA' not in result)
        assert('dexFeePctTokenB' not in result)
        assert('dexFeeInPctTokenB' not in result)
        assert('dexFeeOutPctTokenB' not in result)

        # Test DFI to DUSD, no fees incurred
        self.test_swap(self.symbolDFI, self.symbolDUSD, 0, 0)

        # Test DUSD to DFI, 5% fee on DUSD
        self.test_swap(self.symbolDUSD, self.symbolDFI, Decimal('0.05'), 0)

        # Set DUSD fee on out only
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'out'
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert_equal(result['dexFeePctTokenA'], Decimal('0.05000000'))
        assert('dexFeeInPctTokenA' not in result)
        assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.05000000'))
        assert('dexFeePctTokenB' not in result)
        assert('dexFeeInPctTokenB' not in result)
        assert('dexFeeOutPctTokenB' not in result)

        # Test DFI to DUSD, 5% fee on DUSD
        self.test_swap(self.symbolDFI, self.symbolDUSD, 0, Decimal('0.05'))

        # Test DUSD toDFI, no fees incurred
        self.test_swap(self.symbolDUSD, self.symbolDFI, 0, 0)

        # Set DFI fee on in only
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_pct': '0',
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'both',
            f'v0/poolpairs/{self.idDD}/token_b_fee_pct': '0.05',
            f'v0/poolpairs/{self.idDD}/token_b_fee_direction': 'in',
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert('dexFeePctTokenA' not in result)
        assert('dexFeeInPctTokenA' not in result)
        assert('dexFeeOutPctTokenA' not in result)
        assert_equal(result['dexFeePctTokenB'], Decimal('0.05000000'))
        assert_equal(result['dexFeeInPctTokenB'], Decimal('0.05000000'))
        assert('dexFeeOutPctTokenB' not in result)

        # Test DFI to DUSD, 5% fee on DFI
        self.test_swap(self.symbolDFI, self.symbolDUSD, Decimal('0.05'), 0)

        # Test DUSD to DFI, no fees incurred
        self.test_swap(self.symbolDUSD, self.symbolDFI, 0, 0)

        # Set DFI fee on out only
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_b_fee_direction': 'out',
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert('dexFeePctTokenA' not in result)
        assert('dexFeeInPctTokenA' not in result)
        assert('dexFeeOutPctTokenA' not in result)
        assert_equal(result['dexFeePctTokenB'], Decimal('0.05000000'))
        assert('dexFeeInPctTokenB' not in result)
        assert_equal(result['dexFeeOutPctTokenB'], Decimal('0.05000000'))

        # Test DFI to DUSD, no fees incurred
        self.test_swap(self.symbolDFI, self.symbolDUSD, 0, 0)

        # Test DUSD to DFI, 5% fee on DFI
        self.test_swap(self.symbolDUSD, self.symbolDFI, 0, Decimal('0.05'))

        # Set DFI and DUSD fee on in only
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_pct': '0.05',
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'in',
            f'v0/poolpairs/{self.idDD}/token_b_fee_direction': 'in',
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert_equal(result['dexFeePctTokenA'], Decimal('0.05000000'))
        assert_equal(result['dexFeeInPctTokenA'], Decimal('0.05000000'))
        assert('dexFeeOutPctTokenA' not in result)
        assert_equal(result['dexFeePctTokenB'], Decimal('0.05000000'))
        assert_equal(result['dexFeeInPctTokenB'], Decimal('0.05000000'))
        assert('dexFeeOutPctTokenB' not in result)

        # Test DFI to DUSD, 5% fee on DFI
        self.test_swap(self.symbolDFI, self.symbolDUSD, Decimal('0.05'), 0)

        # Test DUSD to DFI, 5% fee on DUSD
        self.test_swap(self.symbolDUSD, self.symbolDFI, Decimal('0.05'), 0)

        # Set DFI and DUSD fee on out only
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'out',
            f'v0/poolpairs/{self.idDD}/token_b_fee_direction': 'out',
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert_equal(result['dexFeePctTokenA'], Decimal('0.05000000'))
        assert('dexFeeInPctTokenA' not in result)
        assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.05000000'))
        assert_equal(result['dexFeePctTokenB'], Decimal('0.05000000'))
        assert('dexFeeInPctTokenB' not in result)
        assert_equal(result['dexFeeOutPctTokenB'], Decimal('0.05000000'))

        # Test DFI to DUSD, 5% fee on DUSD
        self.test_swap(self.symbolDFI, self.symbolDUSD, 0, Decimal('0.05'))

        # Test DUSD to DFI, 5% fee on DFI
        self.test_swap(self.symbolDUSD, self.symbolDFI, 0, Decimal('0.05'))

        # Set DFI and DUSD fee on both, normal behaviour.
        self.nodes[0].setgov({"ATTRIBUTES":{
            f'v0/poolpairs/{self.idDD}/token_a_fee_direction': 'both',
            f'v0/poolpairs/{self.idDD}/token_b_fee_direction': 'both',
        }})
        self.nodes[0].generate(1)

        # Check poolpair
        result = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        assert_equal(result['dexFeePctTokenA'], Decimal('0.05000000'))
        assert_equal(result['dexFeeInPctTokenA'], Decimal('0.05000000'))
        assert_equal(result['dexFeeOutPctTokenA'], Decimal('0.05000000'))
        assert_equal(result['dexFeePctTokenB'], Decimal('0.05000000'))
        assert_equal(result['dexFeeInPctTokenB'], Decimal('0.05000000'))
        assert_equal(result['dexFeeOutPctTokenB'], Decimal('0.05000000'))

        # Test DFI to DUSD, 5% fee on both
        self.test_swap(self.symbolDFI, self.symbolDUSD, Decimal('0.05'), Decimal('0.05'))

        # Test DUSD to DFI, 5% fee on both
        self.test_swap(self.symbolDUSD, self.symbolDFI, Decimal('0.05'), Decimal('0.05'))

    def setup_test_tokens(self):

        self.nodes[0].generate(101)

        # Symbols
        self.symbolDFI = 'DFI'
        self.symbolDUSD = 'DUSD'
        self.symbolDD = 'DUSD-DFI'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Create token
        self.nodes[0].createtoken({
            "symbol": self.symbolDUSD,
            "name": self.symbolDUSD,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]

        # Mint some loan tokens
        self.nodes[0].minttokens([
            f'1000000@{self.symbolDUSD}',
        ])
        self.nodes[0].generate(1)

        # Fund address with account DFI
        self.nodes[0].utxostoaccount({self.address: f'100000@{self.idDFI}'})
        self.nodes[0].generate(1)

    def setup_test_pools(self):

        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSD,
            "tokenB": self.symbolDFI,
            "commission": Decimal('0'),
            "status": True,
            "ownerAddress": self.address,
            "symbol" : self.symbolDD
        })
        self.nodes[0].generate(1)

        # Fund pools
        self.nodes[0].addpoolliquidity({
            self.address: [f'1000@{self.symbolDUSD}', f'100@{self.symbolDFI}']
        }, self.address)

        # Store pool ID
        self.idDD = list(self.nodes[0].gettoken(self.symbolDD).keys())[0]

    def test_swap(self, token_from, token_to, fee_in, fee_out):

        # Create address
        swap_address = self.nodes[0].getnewaddress("", "legacy")

        # Set amount to swap
        swap_amount = Decimal('1')

        # Define coin
        coin = 100000000

        # Pre-swap values
        pool = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        reserve_a = pool['reserveA']
        reserve_b = pool['reserveB']

        # Swap DFI to DUSD, no fees.
        self.nodes[0].poolswap({
            "from": self.address,
            "tokenFrom": token_from,
            "amountFrom": swap_amount,
            "to": swap_address,
            "tokenTo": token_to,
        })

        # Calculate dex in fee
        dex_in_fee = swap_amount * fee_in
        amount_in = swap_amount - dex_in_fee

        # Mint swap TX
        self.nodes[0].generate(1)

        # Check results
        pool = self.nodes[0].getpoolpair(self.idDD)[self.idDD]
        if token_from == self.symbolDFI:
            assert_equal(pool['reserveB'] - reserve_b, amount_in)
            swapped = self.nodes[0].getaccount(swap_address, {}, True)[self.idDUSD]
            reserve_diff = reserve_a - pool['reserveA']
        else:
            assert_equal(pool['reserveA'] - reserve_a, amount_in)
            swapped = self.nodes[0].getaccount(swap_address, {}, True)[self.idDFI]
            reserve_diff = reserve_b - pool['reserveB']

        # Check swap amount
        dex_out_fee = round(trunc(reserve_diff * fee_out * coin) / coin, 8)
        assert_equal(reserve_diff - Decimal(str(dex_out_fee)), swapped)

        # Check DEx fee token amounts matches burn amount
        burn_dex_tokens = self.nodes[0].getburninfo()['dexfeetokens']
        if burn_dex_tokens:
            for tokens in burn_dex_tokens:
                if 'DUSD' in tokens:
                    attributes = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
                    assert_equal(attributes['v0/live/economy/dexfeetokens'], [tokens])

        # Rewind
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

if __name__ == '__main__':
    PoolPairAsymmetricTest().main()

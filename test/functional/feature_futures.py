#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Futures contract RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time


def sort_history(e):
    return e['txn']


class FuturesTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1',
             '-fortcanninghillheight=1', '-fortcanningcrunchheight=150', '-fortcanningroadheight=150',
             '-fortcanningspringheight=500', '-subsidytest=1']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Set up oracles and tokens
        self.setup_test()

        # Test setting of futures Gov vars
        self.futures_setup()

        # Test dToken to DUSD
        self.test_dtoken_to_dusd()

        # Test DUSD to dToken
        self.test_dusd_to_dtoken()

        # Test futures block range
        self.check_swap_block_range()

        # Test multiple swaps per account
        self.check_multiple_swaps()

        # Test withdrawal
        self.check_withdrawals()

        # Test Satoshi swaps
        self.check_minimum_swaps()

        # Test changing Gov vars
        self.check_gov_var_change()

        # Test refunding of unpaid futures
        self.unpaid_contract()

        # Test list future swap history
        self.rpc_history()

        # Test start block
        self.start_block()

        # Test DFI-to-DUSD swap
        self.dfi_to_dusd()

        # Test DUSD withdrawals
        self.check_withdrawals_dusd()

        # Test refunding of unpaid DFI-to-DUSD contract
        self.unpaid_contract_dusd()

    def setup_test(self):

        # Store addresses
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.contract_address = 'bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqpsqgljc'
        self.contract_address_dusd = 'bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqz7nafu8'

        # Store interval
        self.futures_interval = 25

        # RPC history checks
        self.list_history = []

        # Set token symbols
        self.symbolDFI = 'DFI'
        self.symbolDUSD = 'DUSD'
        self.symbolTSLA = 'TSLA'
        self.symbolGOOGL = 'GOOGL'
        self.symbolTWTR = 'TWTR'
        self.symbolMSFT = 'MSFT'
        self.symbolBTC = 'BTC'

        # Setup oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.price_feeds = [
            {"currency": "USD", "token": self.symbolDFI},
            {"currency": "USD", "token": self.symbolTSLA},
            {"currency": "USD", "token": self.symbolGOOGL},
            {"currency": "USD", "token": self.symbolTWTR},
            {"currency": "USD", "token": self.symbolMSFT}
        ]
        self.oracle_id = self.nodes[0].appointoracle(oracle_address, self.price_feeds, 10)
        self.nodes[0].generate(1)

        # Create Oracle prices
        self.price_dfi = 1
        self.price_tsla = 870
        self.price_googl = 2600
        self.price_twtr = 37
        self.price_msft = 295

        # Calculate future swap prices
        self.prices = []
        self.prices.append({
            'premiumPrice': Decimal(str(Decimal(str(self.price_tsla)) * Decimal('1.05000000'))),
            'discountPrice': Decimal(str(Decimal(str(self.price_tsla)) * Decimal('0.95000000')))
        })
        self.prices.append({
            'premiumPrice': Decimal(str(Decimal(str(self.price_googl)) * Decimal('1.05000000'))),
            'discountPrice': Decimal(str(Decimal(str(self.price_googl)) * Decimal('0.95000000')))
        })
        self.prices.append({
            'premiumPrice': Decimal(str(Decimal(str(self.price_twtr)) * Decimal('1.05000000'))),
            'discountPrice': Decimal(str(Decimal(str(self.price_twtr)) * Decimal('0.95000000')))
        })
        self.prices.append({
            'premiumPrice': Decimal(str(Decimal(str(self.price_msft)) * Decimal('1.05000000'))),
            'discountPrice': Decimal(str(Decimal(str(self.price_msft)) * Decimal('0.95000000')))
        })

        # Feed oracle
        self.oracle_prices = [
            {"currency": "USD", "tokenAmount": f'{self.price_dfi}@{self.symbolDFI}'},
            {"currency": "USD", "tokenAmount": f'{self.price_tsla}@{self.symbolTSLA}'},
            {"currency": "USD", "tokenAmount": f'{self.price_googl}@{self.symbolGOOGL}'},
            {"currency": "USD", "tokenAmount": f'{self.price_twtr}@{self.symbolTWTR}'},
            {"currency": "USD", "tokenAmount": f'{self.price_msft}@{self.symbolMSFT}'},
        ]
        self.nodes[0].setoracledata(self.oracle_id, int(time.time()), self.oracle_prices)
        self.nodes[0].generate(10)

        # Set up non-loan token for failure test
        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": self.symbolBTC,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        # Setup loan tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolDUSD,
            'name': self.symbolDUSD,
            'fixedIntervalPriceId': f'{self.symbolDUSD}/USD',
            'mintable': True,
            'interest': 0})
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f'{self.symbolTSLA}/USD',
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolGOOGL,
            'name': self.symbolGOOGL,
            'fixedIntervalPriceId': f'{self.symbolGOOGL}/USD',
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolTWTR,
            'name': self.symbolTWTR,
            'fixedIntervalPriceId': f'{self.symbolTWTR}/USD',
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
            'symbol': self.symbolMSFT,
            'name': self.symbolMSFT,
            'fixedIntervalPriceId': f'{self.symbolMSFT}/USD',
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(1)

        # Set token ids
        self.idDFI = list(self.nodes[0].gettoken(self.symbolDFI).keys())[0]
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]
        self.idTWTR = list(self.nodes[0].gettoken(self.symbolTWTR).keys())[0]
        self.idMSFT = list(self.nodes[0].gettoken(self.symbolMSFT).keys())[0]
        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]

        # Mint tokens for swapping
        self.nodes[0].minttokens([f'100000@{self.idDUSD}'])
        self.nodes[0].minttokens([f'100000@{self.idTSLA}'])
        self.nodes[0].minttokens([f'100000@{self.idGOOGL}'])
        self.nodes[0].minttokens([f'100000@{self.idTWTR}'])
        self.nodes[0].minttokens([f'100000@{self.idMSFT}'])
        self.nodes[0].generate(1)

    def futures_setup(self):

        # Move to fork block
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Set DFI/DUSD fixed price interval
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/token/{self.idDFI}/fixed_interval_price_id': f'{self.symbolDFI}/USD'}})
        self.nodes[0].generate(1)

        # Try futureswap before feature is active
        assert_raises_rpc_error(-32600, "DFIP2203 not currently active", self.nodes[0].futureswap, address,
                                f'1@{self.symbolTWTR}')

        # Set partial futures attributes
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'true'}})
        self.nodes[0].generate(1)

        # Try futureswap before feature is fully active
        assert_raises_rpc_error(-32600, "DFIP2203 not currently active", self.nodes[0].futureswap, address,
                                f'1@{self.symbolTWTR}')

        # Set all futures attributes but set active to false
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'false',
                                             'v0/params/dfip2203/reward_pct': '0.05',
                                             'v0/params/dfip2203/block_period': f'{self.futures_interval}'}})
        self.nodes[0].generate(1)

        # Try futureswap with DFIP2203 active set to false
        assert_raises_rpc_error(-32600, "DFIP2203 not currently active", self.nodes[0].futureswap, address,
                                f'1@{self.symbolTWTR}')

        # Fully enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'true'}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2203/active'], 'true')
        assert_equal(result['v0/params/dfip2203/reward_pct'], '0.05')
        assert_equal(result['v0/params/dfip2203/fee_pct'], '0.05')
        assert_equal(result['v0/params/dfip2203/block_period'], str(self.futures_interval))

        # Disable DUSD
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{str(self.idDUSD)}/dfip2203': 'false'}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result[f'v0/token/{self.idDUSD}/dfip2203'], 'false')

        # Check futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        assert_equal(next_futures_block, self.nodes[0].getfutureswapblock())

    def test_dtoken_to_dusd(self):

        # Create addresses for futures
        address_msft = self.nodes[0].getnewaddress("", "legacy")
        address_googl = self.nodes[0].getnewaddress("", "legacy")
        address_tsla = self.nodes[0].getnewaddress("", "legacy")
        address_twtr = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address_msft: f'1@{self.symbolMSFT}'})
        self.nodes[0].accounttoaccount(self.address, {address_googl: f'1@{self.symbolGOOGL}'})
        self.nodes[0].accounttoaccount(self.address, {address_tsla: f'1@{self.symbolTSLA}'})
        self.nodes[0].accounttoaccount(self.address, {address_twtr: f'1@{self.symbolTWTR}'})
        self.nodes[0].generate(1)

        # Test futureswap failures
        assert_raises_rpc_error(-32600, f'Could not get source loan token {self.idBTC}', self.nodes[0].futureswap,
                                self.address, f'1@{self.symbolBTC}')
        assert_raises_rpc_error(-32600, f'DFIP2203 currently disabled for token {self.idDUSD}',
                                self.nodes[0].futureswap, self.address, f'1@{self.symbolDUSD}', int(self.idDUSD))
        assert_raises_rpc_error(-32600, f'Could not get destination loan token {self.idBTC}. Set valid destination.',
                                self.nodes[0].futureswap, self.address, f'1@{self.symbolDUSD}', int(self.idBTC))
        assert_raises_rpc_error(-32600, 'Destination should not be set when source amount is dToken or DFI',
                                self.nodes[0].futureswap, self.address, f'1@{self.symbolTSLA}', int(self.idBTC))
        assert_raises_rpc_error(-32600, 'amount 0.00000000 is less than 1.00000000', self.nodes[0].futureswap,
                                address_twtr, f'1@{self.symbolTSLA}')

        # Create user futures contracts
        self.nodes[0].futureswap(address_twtr, f'1@{self.symbolTWTR}')
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_tsla, f'1@{self.symbolTSLA}')
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_googl, f'1@{self.symbolGOOGL}')
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_msft, f'1@{self.symbolMSFT}')
        self.nodes[0].generate(1)

        # List user futures contracts
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(result[0]['owner'], address_msft)
        assert_equal(result[0]['source'], f'{Decimal("1.00000000")}@{self.symbolMSFT}')
        assert_equal(result[0]['destination'], self.symbolDUSD)
        assert_equal(result[1]['owner'], address_googl)
        assert_equal(result[1]['source'], f'{Decimal("1.00000000")}@{self.symbolGOOGL}')
        assert_equal(result[1]['destination'], self.symbolDUSD)
        assert_equal(result[2]['owner'], address_tsla)
        assert_equal(result[2]['source'], f'{Decimal("1.00000000")}@{self.symbolTSLA}')
        assert_equal(result[2]['destination'], self.symbolDUSD)
        assert_equal(result[3]['owner'], address_twtr)
        assert_equal(result[3]['source'], f'{Decimal("1.00000000")}@{self.symbolTWTR}')
        assert_equal(result[3]['destination'], self.symbolDUSD)

        # Get user MSFT futures swap by address
        result = self.nodes[0].getpendingfutureswaps(address_msft)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolMSFT}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Get user GOOGL futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_googl)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolGOOGL}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Get user TSLA futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolTSLA}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Get user TWTR futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolTWTR}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Check DFI2203 amounts do not show up as burns yet
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [])

        # Check DFI2203 address on listgovs, current shows pending, burn should be empty.
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}',
                      f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])
        assert ('v0/live/economy/dfip2203_burned' not in result)
        assert ('v0/live/economy/dfip2203_minted' not in result)

        # Get token total minted before future swap
        total_dusd = Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted'])

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check total minted incremented as expected
        new_total_dusd = Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted'])
        assert_equal(total_dusd + self.prices[0]["discountPrice"] + self.prices[1]["discountPrice"] + self.prices[2][
            "discountPrice"] + self.prices[3]["discountPrice"], new_total_dusd)

        # Check TXN ordering
        txn_first = 4294967295
        result = self.nodes[0].listaccounthistory('all', {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 0,
                                                          'txtype': 'q'})
        result.sort(key=sort_history, reverse=True)
        for result_entry in result:
            assert_equal(result_entry['txn'], txn_first)
            txn_first -= 1

        # Pending futures should now be empty
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(len(result), 0)
        result = self.nodes[0].getpendingfutureswaps(address_msft)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutureswaps(address_googl)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(len(result['values']), 0)

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}',
                              f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}',
                      f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}',
                                          f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [f'{self.prices[3]["discountPrice"]}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'{self.prices[1]["discountPrice"]}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'{self.prices[0]["discountPrice"]}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_twtr)
        assert_equal(result, [f'{self.prices[2]["discountPrice"]}@{self.symbolDUSD}'])

        # Populate RPC check
        self.list_history.append({'height': self.nodes[0].getblockcount(), 'swaps': [
            {'address': address_tsla, 'destination': f'{self.prices[0]["discountPrice"]}@{self.symbolDUSD}'},
            {'address': address_googl, 'destination': f'{self.prices[1]["discountPrice"]}@{self.symbolDUSD}'},
            {'address': address_twtr, 'destination': f'{self.prices[2]["discountPrice"]}@{self.symbolDUSD}'},
            {'address': address_msft, 'destination': f'{self.prices[3]["discountPrice"]}@{self.symbolDUSD}'},
        ]})

    def test_dusd_to_dtoken(self):

        # Create addresses for futures
        address_tsla = self.nodes[0].getnewaddress("", "legacy")
        address_twtr = self.nodes[0].getnewaddress("", "legacy")
        address_googl = self.nodes[0].getnewaddress("", "legacy")
        address_msft = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address,
                                       {address_tsla: f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_googl: f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_twtr: f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_msft: f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_msft, f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}', self.idMSFT)
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_twtr, f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}', self.idTWTR)
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_googl, f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}', self.symbolGOOGL)
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', self.symbolTSLA)
        self.nodes[0].generate(1)

        # List user futures contracts
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(result[0]['owner'], address_tsla)
        assert_equal(result[0]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[0]['destination'], self.symbolTSLA)
        assert_equal(result[1]['owner'], address_googl)
        assert_equal(result[1]['source'], f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[1]['destination'], self.symbolGOOGL)
        assert_equal(result[2]['owner'], address_twtr)
        assert_equal(result[2]['source'], f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[2]['destination'], self.symbolTWTR)
        assert_equal(result[3]['owner'], address_msft)
        assert_equal(result[3]['source'], f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[3]['destination'], self.symbolMSFT)

        # Get user TSLA futures swap by address
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(result['values'][0]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTSLA)

        # Get user GOOGL futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_googl)
        assert_equal(result['values'][0]['source'], f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolGOOGL)

        # Get user TWTR futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(result['values'][0]['source'], f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTWTR)

        # Get user MSFT futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_msft)
        assert_equal(result['values'][0]['source'], f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolMSFT)

        # Check new DFI2203 amounts do not show up as burns yet
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}',
                                          f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs, current shows pending if any, burned shows
        # deposits from executed swaps and minted shows output from executed swaps.
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                      f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                      f'1.00000000@{self.symbolMSFT}'])
        assert_equal(result['v0/live/economy/dfip2203_burned'],
                     [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}',
                      f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])
        assert_equal(result['v0/live/economy/dfip2203_minted'], [
            f'{self.prices[0]["discountPrice"] + self.prices[1]["discountPrice"] + self.prices[2]["discountPrice"] + self.prices[3]["discountPrice"]}@{self.symbolDUSD}'])

        # Get token total minted before future swap
        total_tsla = Decimal(self.nodes[0].gettoken(self.idTSLA)[self.idTSLA]['minted'])
        total_googl = Decimal(self.nodes[0].gettoken(self.idGOOGL)[self.idGOOGL]['minted'])
        total_twtr = Decimal(self.nodes[0].gettoken(self.idTWTR)[self.idTWTR]['minted'])
        total_msft = Decimal(self.nodes[0].gettoken(self.idMSFT)[self.idMSFT]['minted'])

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check minted totals incremented as expected
        new_total_tsla = Decimal(self.nodes[0].gettoken(self.idTSLA)[self.idTSLA]['minted'])
        new_total_googl = Decimal(self.nodes[0].gettoken(self.idGOOGL)[self.idGOOGL]['minted'])
        new_total_twtr = Decimal(self.nodes[0].gettoken(self.idTWTR)[self.idTWTR]['minted'])
        new_total_msft = Decimal(self.nodes[0].gettoken(self.idMSFT)[self.idMSFT]['minted'])
        assert_equal(total_tsla + Decimal('1.00000000'), new_total_tsla)
        assert_equal(total_googl + Decimal('1.00000000'), new_total_googl)
        assert_equal(total_twtr + Decimal('1.00000000'), new_total_twtr)
        assert_equal(total_msft + Decimal('1.00000000'), new_total_msft)

        # Pending futures should now be empty
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(len(result), 0)
        result = self.nodes[0].getpendingfutureswaps(address_msft)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutureswaps(address_googl)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(len(result['values']), 0)

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                      f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                      f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                                          f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                                          f'1.00000000@{self.symbolMSFT}'])

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [f'1.00000000@{self.symbolMSFT}'])
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'1.00000000@{self.symbolGOOGL}'])
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'1.00000000@{self.symbolTSLA}'])
        result = self.nodes[0].getaccount(address_twtr)
        assert_equal(result, [f'1.00000000@{self.symbolTWTR}'])

        # Populate RPC check
        self.list_history.append({'height': self.nodes[0].getblockcount(), 'swaps': [
            {'address': address_tsla, 'destination': f'1.00000000@{self.symbolTSLA}'},
            {'address': address_googl, 'destination': f'1.00000000@{self.symbolGOOGL}'},
            {'address': address_twtr, 'destination': f'1.00000000@{self.symbolTWTR}'},
            {'address': address_msft, 'destination': f'1.00000000@{self.symbolMSFT}'},
        ]})

    def check_swap_block_range(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address,
                                       {address: f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Move to just before futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount() - 1)

        # Create user futures contracts on futures block
        self.nodes[0].futureswap(address, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}'])

        # Check all pending swaps shows no entries
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(len(result), 0)

        # Check user pending swaps is empty
        result = self.nodes[0].getpendingfutureswaps(address)
        assert_equal(len(result['values']), 0)

        # Try and withdraw smallest amount now contract has been paid
        assert_raises_rpc_error(-32600, 'amount 0.00000000 is less than 0.00000001', self.nodes[0].withdrawfutureswap,
                                address, f'{Decimal("0.00000001")}@{self.symbolDUSD}', int(self.idTSLA))

        # Populate RPC check
        self.list_history.append({'height': self.nodes[0].getblockcount(), 'swaps': [
            {'address': address, 'destination': f'1.00000000@{self.symbolTSLA}'},
        ]})

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check that futures has not been executed again
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'913.50000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'4905.60000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

    def check_multiple_swaps(self):

        # Create addresses for futures
        address_tsla = self.nodes[0].getnewaddress("", "legacy")
        address_twtr = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address,
                                       {address_tsla: f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_twtr: f'{self.prices[2]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create two user futures contracts
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].futureswap(address_twtr, f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTWTR))
        self.nodes[0].futureswap(address_twtr, f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTWTR))
        self.nodes[0].generate(1)

        # Get user TSLA futures swap by address
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(result['values'][0]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTSLA)
        assert_equal(result['values'][1]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][1]['destination'], self.symbolTSLA)

        # Get user TWTR futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(result['values'][0]['source'], f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTWTR)
        assert_equal(result['values'][0]['source'], f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTWTR)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'2.00000000@{self.symbolTSLA}'])
        result = self.nodes[0].getaccount(address_twtr)
        assert_equal(result, [f'2.00000000@{self.symbolTWTR}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'6810.30000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

    def check_withdrawals(self):

        # Create addresses for futures
        address_tsla = self.nodes[0].getnewaddress("", "legacy")
        address_twtr = self.nodes[0].getnewaddress("", "legacy")
        address_googl = self.nodes[0].getnewaddress("", "legacy")
        address_msft = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address,
                                       {address_tsla: f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_googl: f'{self.prices[1]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_twtr: f'{self.prices[2]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_msft: f'{self.prices[3]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_msft, f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}', int(self.idMSFT))
        self.nodes[0].futureswap(address_msft, f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}', int(self.idMSFT))
        self.nodes[0].futureswap(address_twtr, f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTWTR))
        self.nodes[0].futureswap(address_twtr, f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTWTR))
        self.nodes[0].futureswap(address_googl, f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}',
                                 int(self.idGOOGL))
        self.nodes[0].futureswap(address_googl, f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}',
                                 int(self.idGOOGL))
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Get user MSFT futures swap by address
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(result['values'][0]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTSLA)
        assert_equal(result['values'][1]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][1]['destination'], self.symbolTSLA)

        # Get user GOOGL futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_googl)
        assert_equal(result['values'][0]['source'], f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolGOOGL)
        assert_equal(result['values'][1]['source'], f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][1]['destination'], self.symbolGOOGL)

        # Get user TSLA futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(result['values'][0]['source'], f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTWTR)
        assert_equal(result['values'][1]['source'], f'{self.prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][1]['destination'], self.symbolTWTR)

        # Get user TWTR futures contracts by address
        result = self.nodes[0].getpendingfutureswaps(address_msft)
        assert_equal(result['values'][0]['source'], f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolMSFT)
        assert_equal(result['values'][1]['source'], f'{self.prices[3]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][1]['destination'], self.symbolMSFT)

        # Check withdrawal failures
        assert_raises_rpc_error(-32600, f'amount 0.00000000 is less than {self.prices[2]["premiumPrice"] * 2}',
                                self.nodes[0].withdrawfutureswap, address_tsla,
                                f'{self.prices[2]["premiumPrice"] * 2}@{self.symbolDUSD}', int(self.idTWTR))
        assert_raises_rpc_error(-32600,
                                f'amount {self.prices[0]["premiumPrice"] * 2} is less than {(self.prices[0]["premiumPrice"] * 2) + Decimal("0.00000001")}',
                                self.nodes[0].withdrawfutureswap, address_tsla,
                                f'{(self.prices[0]["premiumPrice"] * 2) + Decimal("0.00000001")}@{self.symbolDUSD}',
                                int(self.idTSLA))

        # Withdraw both TSLA contracts
        self.nodes[0].withdrawfutureswap(address_tsla, f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}',
                                         int(self.idTSLA))
        self.nodes[0].generate(1)

        # Check user pending swap is empty
        result = self.nodes[0].getpendingfutureswaps(address_tsla)
        assert_equal(len(result['values']), 0)

        # Try and withdraw smallest amount now contract empty
        assert_raises_rpc_error(-32600, 'amount 0.00000000 is less than 0.00000001', self.nodes[0].withdrawfutureswap,
                                address_tsla, f'{Decimal("0.00000001")}@{self.symbolDUSD}', int(self.idTSLA))

        # Withdraw frm GOOGL everything but one Sat
        self.nodes[0].withdrawfutureswap(address_googl,
                                         f'{(self.prices[1]["premiumPrice"] * 2) - Decimal("0.00000001")}@{self.symbolDUSD}',
                                         int(self.idGOOGL))
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingfutureswaps(address_googl)
        assert_equal(result['values'][0]['source'], f'0.00000001@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolGOOGL)

        # Withdraw one TWTR contract plus 1 Sat of the second one
        self.nodes[0].withdrawfutureswap(address_twtr,
                                         f'{self.prices[2]["premiumPrice"] + Decimal("0.00000001")}@{self.symbolDUSD}',
                                         int(self.idTWTR))
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingfutureswaps(address_twtr)
        assert_equal(result['values'][0]['source'],
                     f'{self.prices[2]["premiumPrice"] - Decimal("0.00000001")}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTWTR)

        # Withdraw one Sat
        self.nodes[0].withdrawfutureswap(address_msft, f'{Decimal("0.00000001")}@{self.symbolDUSD}', int(self.idMSFT))
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingfutureswaps(address_msft)
        assert_equal(result['values'][0]['source'],
                     f'{(self.prices[3]["premiumPrice"] * 2) - Decimal("0.00000001")}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolMSFT)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check final balances
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_twtr)
        assert_equal(result, [f'{self.prices[2]["premiumPrice"] + Decimal("0.00000001")}@{self.symbolDUSD}',
                              f'0.99999999@{self.symbolTWTR}'])
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'{(self.prices[1]["premiumPrice"] * 2) - Decimal("0.00000001")}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [f'0.00000001@{self.symbolDUSD}', f'1.99999999@{self.symbolMSFT}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'7468.64999999@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'7468.64999999@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                      f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                      f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'7468.64999999@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                                          f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                                          f'1.00000000@{self.symbolMSFT}'])

    def check_minimum_swaps(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address: f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create user futures contract with 1 Satoshi
        self.nodes[0].futureswap(address, f'{Decimal("0.00000001")}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check one Satoshi swap yields no TSLA
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'{self.prices[0]["premiumPrice"] - Decimal("0.00000001")}@{self.symbolDUSD}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'7468.65000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Create user futures contract to purchase one Satoshi of TSLA
        min_purchase = round(self.prices[0]["premiumPrice"] / 100000000, 8)
        self.nodes[0].futureswap(address, f'{min_purchase}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check one Satoshi swap yields one TSLA Satoshi
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [
            f'{self.prices[0]["premiumPrice"] - Decimal("0.00000001") - Decimal(min_purchase)}@{self.symbolDUSD}',
            f'0.00000001@{self.symbolTSLA}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'7468.65000914@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

    def check_gov_var_change(self):

        # Set up for block range change, create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address: f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Move to before next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval)) - 1
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Create user futures contract with 1 Satoshi to invalidate block period change
        self.nodes[0].futureswap(address, f'{Decimal("0.00000001")}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Check contract address has updated
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'7468.65000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Test changing block period while DFIP2203 still active
        assert_raises_rpc_error(-32600, 'Cannot set block period while DFIP2203 is active', self.nodes[0].setgov,
                                {"ATTRIBUTES": {'v0/params/dfip2203/block_period': f'{self.futures_interval}'}})

        # Disable DFIP2203 to be able to change block period
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'false'}})
        self.nodes[0].generate(1)

        # Check contract address has not changed, no refund on disabling DFIP2203.
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'7468.65000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Now set the new block period
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/block_period': f'{self.futures_interval}'}})
        self.nodes[0].generate(1)

        # Enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'true'}})
        self.nodes[0].generate(1)

        # Create addresses
        address_tsla = self.nodes[0].getnewaddress("", "legacy")
        address_googl = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address,
                                       {address_tsla: f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address,
                                       {address_googl: f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_googl, f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}',
                                 int(self.idGOOGL))
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Disable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'false'}})
        self.nodes[0].generate(1)

        # Check TXN ordering on Gov var refunds
        txn_first = 4294967295
        result = self.nodes[0].listaccounthistory('all', {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 0,
                                                          'txtype': 'w'})
        result.sort(key=sort_history, reverse=True)
        assert_equal(len(result), 4)
        for result_entry in result:
            assert_equal(result_entry['blockHeight'], self.nodes[0].getblockcount())
            assert_equal(result_entry['type'], 'FutureSwapRefund')
            assert_equal(result_entry['txn'], txn_first)
            txn_first -= 1

        # Check other refund entries
        assert_equal(result[0]['owner'], self.contract_address)
        assert_equal(result[2]['owner'], self.contract_address)
        if result[0]['amounts'] != [f'{-self.prices[0]["premiumPrice"]}@{self.symbolDUSD}']:
            assert_equal(result[0]['amounts'], [f'{-self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])
        if result[2]['amounts'] != [f'{-self.prices[0]["premiumPrice"]}@{self.symbolDUSD}']:
            assert_equal(result[2]['amounts'], [f'{-self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])
        if result[1]['owner'] == address_googl:
            assert_equal(result[1]['amounts'], [f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])
        else:
            assert_equal(result[1]['owner'], address_tsla)
            assert_equal(result[1]['amounts'], [f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])
        if result[3]['owner'] == address_googl:
            assert_equal(result[3]['amounts'], [f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])
        else:
            assert_equal(result[3]['owner'], address_tsla)
            assert_equal(result[3]['amounts'], [f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])

        # Balances should be restored
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])

        # Check contract address remains the same
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'7468.65000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'true'}})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_googl, f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}',
                                 int(self.idGOOGL))
        self.nodes[0].futureswap(address_tsla, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Disable GOOGL
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{str(self.idGOOGL)}/dfip2203': 'false'}})
        self.nodes[0].generate(1)

        # Only TSLA contract should remain
        result = self.nodes[0].listpendingfutureswaps()
        assert_equal(len(result), 1)
        assert_equal(result[0]['owner'], address_tsla)
        assert_equal(result[0]['source'], f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[0]['destination'], self.symbolTSLA)

        # Balance should be restored
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])

        # TSLA balance should be empty
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [])

        # Enable GOOGL
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{str(self.idGOOGL)}/dfip2203': 'true'}})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_googl, f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}',
                                 int(self.idGOOGL))
        self.nodes[0].generate(1)

        # GOOGL balance should be empty
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [])

        # Disable GOOGL
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/token/{str(self.idGOOGL)}/dfip2203': 'false'}})
        self.nodes[0].generate(1)

        # Balance should be restored
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check all balances
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'{self.prices[1]["premiumPrice"]}@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'1.00000000@{self.symbolTSLA}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'8382.15000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'8382.15000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                      f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                      f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'8382.15000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                                          f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                                          f'1.00000000@{self.symbolMSFT}'])

    def unpaid_contract(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address,
                                       {address: f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create user futures contract
        self.nodes[0].futureswap(address, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].futureswap(address, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Remove Oracle
        self.nodes[0].removeoracle(self.oracle_id)
        self.nodes[0].generate(1)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (
                    self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check refund in history
        result = self.nodes[0].listaccounthistory('all', {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 0,
                                                          'txtype': 'w'})
        result.sort(key=sort_history, reverse=True)
        assert_equal(result[0]['owner'], self.contract_address)
        assert_equal(result[0]['type'], 'FutureSwapRefund')
        assert_equal(result[0]['amounts'], [f'{-self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])
        assert_equal(result[1]['owner'], address)
        assert_equal(result[1]['type'], 'FutureSwapRefund')
        assert_equal(result[1]['amounts'], [f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])
        assert_equal(result[2]['owner'], self.contract_address)
        assert_equal(result[2]['type'], 'FutureSwapRefund')
        assert_equal(result[2]['amounts'], [f'{-self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])
        assert_equal(result[3]['owner'], address)
        assert_equal(result[3]['type'], 'FutureSwapRefund')
        assert_equal(result[3]['amounts'], [f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'])

        # Check user has been refunded
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'{self.prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address)
        assert_equal(result, [f'8382.15000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                              f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                              f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2203_current'],
                     [f'8382.15000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                      f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                      f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'8382.15000915@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}',
                                          f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}',
                                          f'1.00000000@{self.symbolMSFT}'])

    def rpc_history(self):

        # Check some historical swaps
        for history in self.list_history:
            result = self.nodes[0].listaccounthistory('all',
                                                      {"maxBlockHeight": history['height'], 'depth': 0, 'txtype': 'q'})
            for history_entry in history['swaps']:
                found = False
                for result_entry in result:
                    assert_equal(history['height'], result_entry['blockHeight'])
                    if result_entry['owner'] == history_entry['address']:
                        assert_equal(result_entry['owner'], history_entry['address'])
                        assert_equal(result_entry['type'], 'FutureSwapExecution')
                        assert_equal(result_entry['amounts'], [history_entry['destination']])
                        found = True
                assert (found)

        # Check all swaps present
        result = self.nodes[0].listaccounthistory('all', {'txtype': 'q'})
        assert_equal(len(result), 17)

        # Check all swap refunds present
        result = self.nodes[0].listaccounthistory('all', {'txtype': 'w'})
        assert_equal(len(result), 12)

        # Check swap by specific address
        result = self.nodes[0].listaccounthistory(self.list_history[0]['swaps'][0]['address'], {'txtype': 'q'})
        assert_equal(len(result), 1)
        assert_equal(result[0]['blockHeight'], self.list_history[0]['height'])
        assert_equal(result[0]['owner'], self.list_history[0]['swaps'][0]['address'])
        assert_equal(result[0]['amounts'], [self.list_history[0]['swaps'][0]['destination']])

    def start_block(self):

        # Restore Oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle_id = self.nodes[0].appointoracle(oracle_address, self.price_feeds, 10)
        self.nodes[0].generate(1)
        self.nodes[0].setoracledata(self.oracle_id, int(time.time()), self.oracle_prices)
        self.nodes[0].generate(10)

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address: f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Move to fork block
        self.nodes[0].generate(500 - self.nodes[0].getblockcount())

        # Test setting start block while DFIP2203 still active
        assert_raises_rpc_error(-32600, 'Cannot set block period while DFIP2203 is active', self.nodes[0].setgov, {
            "ATTRIBUTES": {'v0/params/dfip2203/start_block': f'{self.nodes[0].getblockcount() + 1}'}})

        # Disable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'false'}})
        self.nodes[0].generate(1)

        # Set start block height
        self.start_block = self.nodes[0].getblockcount() + 10
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/start_block': f'{self.start_block}'}})
        self.nodes[0].generate(1)

        # Enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2203/active': 'true'}})
        self.nodes[0].generate(1)

        # Test cannot create a future swap until active
        assert_raises_rpc_error(-32600, f'DFIP2203 not active until block {self.start_block}', self.nodes[0].futureswap,
                                address, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))

        # Check next future swap block reported correctly via RPC
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (
                    (self.nodes[0].getblockcount() - self.start_block) % self.futures_interval)) + self.futures_interval
        assert_equal(next_futures_block, self.nodes[0].getfutureswapblock())

        # Move to futures start height
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Create user futures contract
        self.nodes[0].futureswap(address, f'{self.prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Move to one before next future swap block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (
                    (self.nodes[0].getblockcount() - self.start_block) % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount() - 1)

        # Check calculation is correct for second swap height after setting start block
        assert_equal(next_futures_block, self.nodes[0].getfutureswapblock())

        # Check account still empty
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [])

        # Move to future swap block
        self.nodes[0].generate(1)

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'1.00000000@{self.symbolTSLA}'])

    def dfi_to_dusd(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].utxostoaccount({address: f'10@{self.symbolDFI}'})
        self.nodes[0].generate(1)

        # Test swap before DFIP2206F defined
        assert_raises_rpc_error(-32600, f'DFIP2206F not currently active', self.nodes[0].futureswap, address,
                                f'1@{self.symbolDFI}')

        # Define DFI-to-DUSD swap period and start block
        self.futures_interval_dusd = 10
        self.start_block_dusd = self.nodes[0].getblockcount() + 10

        # Set DFI-to-DUSD Gov vars
        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/params/dfip2206f/reward_pct': '0.01',
            'v0/params/dfip2206f/block_period': f'{self.futures_interval_dusd}',
            'v0/params/dfip2206f/start_block': f'{self.start_block_dusd}'
        }})
        self.nodes[0].generate(1)

        # Enable DFIP2206F
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2206f/active': 'true'}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2206f/active'], 'true')
        assert_equal(result['v0/params/dfip2206f/reward_pct'], '0.01')
        assert_equal(result['v0/params/dfip2206f/fee_pct'], '0.01')
        assert_equal(result['v0/params/dfip2206f/block_period'], f'{self.futures_interval_dusd}')
        assert_equal(result['v0/params/dfip2206f/start_block'], f'{self.start_block_dusd}')

        # Test cannot create a future swap until active
        assert_raises_rpc_error(-32600, f'DFIP2206F not active until block {self.start_block_dusd}',
                                self.nodes[0].futureswap, address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')

        # Check next future swap block reported correctly via RPC
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval_dusd - ((self.nodes[
                                                                                                 0].getblockcount() - self.start_block_dusd) % self.futures_interval_dusd)) + self.futures_interval_dusd
        assert_equal(next_futures_block, self.nodes[0].getdusdswapblock())

        # Move to futures start height
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check error when DUSD destination not set or incorrect
        assert_raises_rpc_error(-32600,
                                f'Incorrect destination defined for DFI swap, DUSD destination expected id: {self.idDUSD}',
                                self.nodes[0].futureswap, address, f'1@{self.symbolDFI}')
        assert_raises_rpc_error(-32600,
                                f'Incorrect destination defined for DFI swap, DUSD destination expected id: {self.idDUSD}',
                                self.nodes[0].futureswap, address, f'1@{self.symbolDFI}', f'{self.symbolGOOGL}')

        # Test swap of DFI to DUSD
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].generate(1)

        # Check list pending swaps
        result = self.nodes[0].listpendingdusdswaps()
        assert_equal(len(result), 1)
        assert_equal(result[0]['owner'], address)
        assert_equal(result[0]['amount'], Decimal('1.00000000'))

        # Check get pending swaps
        result = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(result['owner'], address)
        assert_equal(result['amount'], Decimal('1.00000000'))

        # Move to swap height
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval_dusd - (
                    (self.nodes[0].getblockcount() - self.start_block_dusd) % self.futures_interval_dusd))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check swap in history
        result = self.nodes[0].listaccounthistory('all', {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 1,
                                                          'txtype': 'q'})
        assert_equal(result[0]['owner'], address)
        assert_equal(result[0]['type'], 'FutureSwapExecution')
        assert_equal(result[0]['amounts'], [f'0.99000000@{self.symbolDUSD}'])

        # Check result
        result = self.nodes[0].getaccount(address)[1]
        assert_equal(result, f'0.99000000@{self.symbolDUSD}')

        # Check get/list calls now empty
        list = self.nodes[0].listpendingdusdswaps()
        get = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(list, [])
        assert_equal(get, {})

        # Test swap when DFIP2203 disabled
        self.nodes[0].setgov({"ATTRIBUTES": {
            'v0/params/dfip2203/active': 'false',
        }})
        self.nodes[0].generate(1)

        # Test swap of DFI to DUSD
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].generate(1)

        # Move to swap height
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval_dusd - (
                    (self.nodes[0].getblockcount() - self.start_block_dusd) % self.futures_interval_dusd))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check result
        result = self.nodes[0].getaccount(address)[1]
        assert_equal(result, f'{Decimal("0.99000000") * 2}@{self.symbolDUSD}')

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address_dusd)
        assert_equal(result, [f'2.00000000@{self.symbolDFI}'])

        # Check live attrs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2206f_current'], [f'2.00000000@{self.symbolDFI}'])
        assert_equal(result['v0/live/economy/dfip2206f_burned'], [f'2.00000000@{self.symbolDFI}'])
        assert_equal(result['v0/live/economy/dfip2206f_minted'], [f'{Decimal("0.99000000") * 2}@{self.symbolDUSD}'])

        # Check burn info
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2206f'], [f'2.00000000@{self.symbolDFI}'])

    def check_withdrawals_dusd(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].utxostoaccount({address: f'10@{self.symbolDFI}'})
        self.nodes[0].generate(1)

        # Test swap of DFI to DUSD
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].generate(1)

        # Check withdrawal failures
        assert_raises_rpc_error(-32600, 'amount 2.00000000 is less than 2.00000001', self.nodes[0].withdrawfutureswap,
                                address, f'2.00000001@{self.symbolDFI}', self.symbolDUSD)

        # Withdraw both contracts
        self.nodes[0].withdrawfutureswap(address, f'2.00000000@{self.symbolDFI}', self.symbolDUSD)
        self.nodes[0].generate(1)

        # Check user pending swap is empty
        result = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(result, {})

        # Try and withdraw smallest amount now contract empty
        assert_raises_rpc_error(-32600, 'amount 0.00000000 is less than 0.00000001', self.nodes[0].withdrawfutureswap,
                                address, f'0.00000001@{self.symbolDFI}', self.symbolDUSD)

        # Create new future swap
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].generate(1)

        # Withdraw frm GOOGL everything but one Sat
        self.nodes[0].withdrawfutureswap(address, f'0.99999999@{self.symbolDFI}', self.symbolDUSD)
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(result['owner'], address)
        assert_equal(result['amount'], Decimal('0.00000001'))

        # Move to swap height
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval_dusd - (
                    (self.nodes[0].getblockcount() - self.start_block_dusd) % self.futures_interval_dusd))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check result, should just be DFI
        result = self.nodes[0].getaccount(address)
        assert_equal(len(result), 1)

        # Create two more test swaps
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].generate(1)

        # Withdraw one contract plus 1 Sat of the second one
        self.nodes[0].withdrawfutureswap(address, f'1.00000001@{self.symbolDFI}', self.symbolDUSD)
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(result['owner'], address)
        assert_equal(result['amount'], Decimal('0.99999999'))

        # Withdraw one Sat
        self.nodes[0].withdrawfutureswap(address, f'0.00000001@{self.symbolDFI}', self.symbolDUSD)
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(result['owner'], address)
        assert_equal(result['amount'], Decimal('0.99999998'))

        # Withdraw all but 2 Sats
        self.nodes[0].withdrawfutureswap(address, f'0.99999996@{self.symbolDFI}', self.symbolDUSD)
        self.nodes[0].generate(1)

        # Check user pending swap
        result = self.nodes[0].getpendingdusdswaps(address)
        assert_equal(result['owner'], address)
        assert_equal(result['amount'], Decimal('0.00000002'))

        # Move to swap height
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval_dusd - (
                    (self.nodes[0].getblockcount() - self.start_block_dusd) % self.futures_interval_dusd))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check result.
        result = self.nodes[0].getaccount(address)[1]
        assert_equal(result, f'0.00000001@{self.symbolDUSD}')

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address_dusd)
        assert_equal(result, [f'2.00000003@{self.symbolDFI}'])

        # Check live attrs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2206f_current'], [f'2.00000003@{self.symbolDFI}'])
        assert_equal(result['v0/live/economy/dfip2206f_burned'], [f'2.00000003@{self.symbolDFI}'])
        assert_equal(result['v0/live/economy/dfip2206f_minted'],
                     [f'{Decimal("0.99000000") * 2 + Decimal("0.00000001")}@{self.symbolDUSD}'])

        # Check burn info
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2206f'], [f'2.00000003@{self.symbolDFI}'])

    def unpaid_contract_dusd(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Fund address
        self.nodes[0].utxostoaccount({address: f'1@{self.symbolDFI}'})
        self.nodes[0].generate(1)

        # Create user futures contract
        self.nodes[0].futureswap(address, f'1@{self.symbolDFI}', f'{self.symbolDUSD}')
        self.nodes[0].generate(1)

        # Remove Oracle
        self.nodes[0].removeoracle(self.oracle_id)
        self.nodes[0].generate(1)

        # Check balance empty
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [])

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval_dusd - (
                    (self.nodes[0].getblockcount() - self.start_block_dusd) % self.futures_interval_dusd))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check user has been refunded
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'1.00000000@{self.symbolDFI}'])

        # Check refund in history
        result = self.nodes[0].listaccounthistory('all', {"maxBlockHeight": self.nodes[0].getblockcount(), 'depth': 1,
                                                          'txtype': 'w'})
        result.sort(key=sort_history, reverse=True)
        assert_equal(result[0]['owner'], self.contract_address_dusd)
        assert_equal(result[0]['type'], 'FutureSwapRefund')
        assert_equal(result[0]['amounts'], [f'-1.00000000@{self.symbolDFI}'])
        assert_equal(result[1]['owner'], address)
        assert_equal(result[1]['type'], 'FutureSwapRefund')
        assert_equal(result[1]['amounts'], [f'1.00000000@{self.symbolDFI}'])

        # Check contract address
        result = self.nodes[0].getaccount(self.contract_address_dusd)
        assert_equal(result, [f'2.00000003@{self.symbolDFI}'])

        # Check live attrs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip2206f_current'], [f'2.00000003@{self.symbolDFI}'])
        assert_equal(result['v0/live/economy/dfip2206f_burned'], [f'2.00000003@{self.symbolDFI}'])
        assert_equal(result['v0/live/economy/dfip2206f_minted'],
                     [f'{Decimal("0.99000000") * 2 + Decimal("0.00000001")}@{self.symbolDUSD}'])

        # Check burn info
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2206f'], [f'2.00000003@{self.symbolDFI}'])


if __name__ == '__main__':
    FuturesTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Futures's contract RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time

class FuturesTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=150', '-subsidytest=1']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Set up oracles and tokens
        self.setup_test()

        # Test setting of futures Gov vars
        self.futures_setup()

        # Test setting of futures prices
        self.futures_pricing()

        # Test dToken to DUSD
        self.test_dtoken_to_dusd()

        # Test DUSD to dToken
        self.test_dusd_to_dtoken()

        # Test futures block range
        self.check_block_range()

    def setup_test(self):

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Store interval
        self.futures_interval = 50

        # Setup oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "TSLA"},
            {"currency": "USD", "token": "GOOGL"},
            {"currency": "USD", "token": "TWTR"},
            {"currency": "USD", "token": "MSFT"},
            {"currency": "USD", "token": "DOGE"},
        ]
        oracle_id = self.nodes[0].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[0].generate(1)

        # Feed oracle
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "870@TSLA"},
            {"currency": "USD", "tokenAmount": "2600@GOOGL"},
            {"currency": "USD", "tokenAmount": "37@TWTR"},
            {"currency": "USD", "tokenAmount": "295@MSFT"},
            {"currency": "USD", "tokenAmount": "0.09@DOGE"},
        ]
        self.nodes[0].setoracledata(oracle_id, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

        # Set token symbols
        self.symbolDUSD  = 'DUSD'
        self.symbolTSLA  = 'TSLA'
        self.symbolGOOGL = 'GOOGL'
        self.symbolTWTR  = 'TWTR'
        self.symbolMSFT  = 'MSFT'
        self.symbolDOGE  = 'DOGE'
        self.symbolBTC   = 'BTC'

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

        self.nodes[0].setloantoken({
            'symbol': self.symbolDOGE,
            'name': self.symbolDOGE,
            'fixedIntervalPriceId': f'{self.symbolDOGE}/USD',
            'mintable': True,
            'interest': 1})
        self.nodes[0].generate(1)

        # Set token ids
        self.idDUSD  = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idTSLA  = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]
        self.idGOOGL = list(self.nodes[0].gettoken(self.symbolGOOGL).keys())[0]
        self.idTWTR  = list(self.nodes[0].gettoken(self.symbolTWTR).keys())[0]
        self.idMSFT  = list(self.nodes[0].gettoken(self.symbolMSFT).keys())[0]
        self.idDOGE  = list(self.nodes[0].gettoken(self.symbolDOGE).keys())[0]
        self.idBTC   = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]

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

        # Try futureswap before feature is active
        assert_raises_rpc_error(-32600, "DFIP2203 not currently active", self.nodes[0].futureswap, address, f'1@{self.symbolTWTR}')

        # Set partial futures attributes
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/active':'true'}})
        self.nodes[0].generate(1)

        # Try futureswap before feature is fully active
        assert_raises_rpc_error(-32600, "DFIP2203 not currently active", self.nodes[0].futureswap, address, f'1@{self.symbolTWTR}')

        # Set all futures attributes but set active to false
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/active':'false','v0/params/dfip2203/reward_pct':'0.05','v0/params/dfip2203/block_period':f'{self.futures_interval}'}})
        self.nodes[0].generate(1)

        # Try futureswap with DFIP2203 active set to false
        assert_raises_rpc_error(-32600, "DFIP2203 not currently active", self.nodes[0].futureswap, address, f'1@{self.symbolTWTR}')

        # Fully enable DFIP2203
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/active':'true'}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2203/active'], 'true')
        assert_equal(result['v0/params/dfip2203/reward_pct'], '0.05')
        assert_equal(result['v0/params/dfip2203/block_period'], '50')

    def futures_pricing(self):

        # Check futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        assert_equal(next_futures_block, self.nodes[0].getfuturesblock())

        # Futures should be empty
        result = self.nodes[0].listfutures()
        assert_equal(len(result), 0)

        # Disable DOGE
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{str(self.idDOGE)}/dfip2203_disabled':'true'}})
        self.nodes[0].generate(1)

        # Move to next futures block
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Futures should now be populated excluding DOGE
        result = self.nodes[0].listfutures()
        assert_equal(len(result), 5)
        assert_equal(result[0]['tokenSymbol'], self.symbolDUSD)
        assert_equal(result[0]['discountPrice'], Decimal('0.95000000'))
        assert_equal(result[0]['premiumPrice'], Decimal('1.05000000'))
        assert_equal(result[1]['tokenSymbol'], self.symbolTSLA)
        assert_equal(result[1]['discountPrice'], Decimal('826.50000000'))
        assert_equal(result[1]['premiumPrice'], Decimal('913.50000000'))
        assert_equal(result[2]['tokenSymbol'], self.symbolGOOGL)
        assert_equal(result[2]['discountPrice'], Decimal('2470.00000000'))
        assert_equal(result[2]['premiumPrice'], Decimal('2730.00000000'))
        assert_equal(result[3]['tokenSymbol'], self.symbolTWTR)
        assert_equal(result[3]['discountPrice'], Decimal('35.15000000'))
        assert_equal(result[3]['premiumPrice'], Decimal('38.85000000'))
        assert_equal(result[4]['tokenSymbol'], self.symbolMSFT)
        assert_equal(result[4]['discountPrice'], Decimal('280.25000000'))
        assert_equal(result[4]['premiumPrice'], Decimal('309.75000000'))

        # Futures should now be populated
        result = self.nodes[0].getfutures(self.symbolTSLA)
        assert_equal(result['tokenSymbol'], self.symbolTSLA)
        assert_equal(result['discountPrice'], Decimal('826.50000000'))
        assert_equal(result['premiumPrice'], Decimal('913.50000000'))

        # Disable DUSD
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/{str(self.idDUSD)}/dfip2203_disabled':'true'}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result[f'v0/token/{self.idDUSD}/dfip2203_disabled'], 'true')

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Make sure that DUSD no longer is in futures prices result
        result = self.nodes[0].listfutures()
        assert_equal(len(result), 4)
        for price in result:
            assert(price['tokenSymbol'] != self.symbolDUSD)

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
        assert_raises_rpc_error(-32600, f'Could not get source loan token {self.idBTC}', self.nodes[0].futureswap, self.address, f'1@{self.symbolBTC}')
        assert_raises_rpc_error(-32600, f'DFIP2203 currently disabled for token {self.idDUSD}', self.nodes[0].futureswap, self.address, f'1@{self.symbolDUSD}', int(self.idDUSD))
        assert_raises_rpc_error(-32600, f'Could not get destination loan token {self.idBTC}. Set valid destination.', self.nodes[0].futureswap, self.address, f'1@{self.symbolDUSD}', int(self.idBTC))
        assert_raises_rpc_error(-32600, 'Destination should not be set when source amount is a dToken', self.nodes[0].futureswap, self.address, f'1@{self.symbolTSLA}', int(self.idBTC))
        assert_raises_rpc_error(-32600, f'DFIP2203 currently disabled for token {self.idDOGE}', self.nodes[0].futureswap, self.address, f'1@{self.symbolDOGE}')
        assert_raises_rpc_error(-32600, 'amount 0.00000000 is less than 1.00000000', self.nodes[0].futureswap, address_twtr, f'1@{self.symbolTSLA}')

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
        result = self.nodes[0].listpendingfutures()
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
        result = self.nodes[0].getpendingfutures(address_msft)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolMSFT}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Get user GOOGL futures contracts by address
        result = self.nodes[0].getpendingfutures(address_googl)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolGOOGL}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Get user TSLA futures contracts by address
        result = self.nodes[0].getpendingfutures(address_tsla)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolTSLA}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Get user TWTR futures contracts by address
        result = self.nodes[0].getpendingfutures(address_twtr)
        assert_equal(result['values'][0]['source'], f'{Decimal("1.00000000")}@{self.symbolTWTR}')
        assert_equal(result['values'][0]['destination'], self.symbolDUSD)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Pending futures should now be empty
        result = self.nodes[0].listpendingfutures()
        assert_equal(len(result), 0)
        result = self.nodes[0].getpendingfutures(address_msft)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutures(address_googl)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutures(address_tsla)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutures(address_twtr)
        assert_equal(len(result['values']), 0)

        # Check contract address
        result = self.nodes[0].getaccount('bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqpsqgljc')
        assert_equal(result, [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip_tokens'], [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [f'280.25000000@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'2470.00000000@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'826.50000000@{self.symbolDUSD}'])
        result = self.nodes[0].getaccount(address_twtr)
        assert_equal(result, [f'35.15000000@{self.symbolDUSD}'])

    def test_dusd_to_dtoken(self):

        # Create addresses for futures
        address_tsla = self.nodes[0].getnewaddress("", "legacy")
        address_twtr = self.nodes[0].getnewaddress("", "legacy")
        address_googl = self.nodes[0].getnewaddress("", "legacy")
        address_msft = self.nodes[0].getnewaddress("", "legacy")

        # Get futures prices
        prices = self.nodes[0].listfutures()

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address_tsla: f'{prices[0]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {address_googl: f'{prices[1]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {address_twtr: f'{prices[2]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {address_msft: f'{prices[3]["premiumPrice"]}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Create user futures contracts
        self.nodes[0].futureswap(address_msft, f'{prices[3]["premiumPrice"]}@{self.symbolDUSD}', int(self.idMSFT))
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_twtr, f'{prices[2]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTWTR))
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_googl, f'{prices[1]["premiumPrice"]}@{self.symbolDUSD}', int(self.idGOOGL))
        self.nodes[0].generate(1)
        self.nodes[0].futureswap(address_tsla, f'{prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # List user futures contracts
        result = self.nodes[0].listpendingfutures()
        assert_equal(result[0]['owner'], address_tsla)
        assert_equal(result[0]['source'], f'{prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[0]['destination'], self.symbolTSLA)
        assert_equal(result[1]['owner'], address_googl)
        assert_equal(result[1]['source'], f'{prices[1]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[1]['destination'], self.symbolGOOGL)
        assert_equal(result[2]['owner'], address_twtr)
        assert_equal(result[2]['source'], f'{prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[2]['destination'], self.symbolTWTR)
        assert_equal(result[3]['owner'], address_msft)
        assert_equal(result[3]['source'], f'{prices[3]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result[3]['destination'], self.symbolMSFT)

        # Get user MSFT futures swap by address
        result = self.nodes[0].getpendingfutures(address_tsla)
        assert_equal(result['values'][0]['source'], f'{prices[0]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTSLA)

        # Get user GOOGL futures contracts by address
        result = self.nodes[0].getpendingfutures(address_googl)
        assert_equal(result['values'][0]['source'], f'{prices[1]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolGOOGL)

        # Get user TSLA futures contracts by address
        result = self.nodes[0].getpendingfutures(address_twtr)
        assert_equal(result['values'][0]['source'], f'{prices[2]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolTWTR)

        # Get user TWTR futures contracts by address
        result = self.nodes[0].getpendingfutures(address_msft)
        assert_equal(result['values'][0]['source'], f'{prices[3]["premiumPrice"]}@{self.symbolDUSD}')
        assert_equal(result['values'][0]['destination'], self.symbolMSFT)

        # Move to next futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Pending futures should now be empty
        result = self.nodes[0].listpendingfutures()
        assert_equal(len(result), 0)
        result = self.nodes[0].getpendingfutures(address_msft)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutures(address_googl)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutures(address_tsla)
        assert_equal(len(result['values']), 0)
        result = self.nodes[0].getpendingfutures(address_twtr)
        assert_equal(len(result['values']), 0)

        # Check contract address
        result = self.nodes[0].getaccount('bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqpsqgljc')
        assert_equal(result, [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on listgovs
        result = self.nodes[0].listgovs()[8][0]['ATTRIBUTES']
        assert_equal(result['v0/live/economy/dfip_tokens'], [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check DFI2203 address on getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['dfip2203'], [f'3992.10000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address_msft)
        assert_equal(result, [f'1.00000000@{self.symbolMSFT}'])
        result = self.nodes[0].getaccount(address_googl)
        assert_equal(result, [f'1.00000000@{self.symbolGOOGL}'])
        result = self.nodes[0].getaccount(address_tsla)
        assert_equal(result, [f'1.00000000@{self.symbolTSLA}'])
        result = self.nodes[0].getaccount(address_twtr)
        assert_equal(result, [f'1.00000000@{self.symbolTWTR}'])

    def check_block_range(self):

        # Create addresses for futures
        address = self.nodes[0].getnewaddress("", "legacy")

        # Get futures prices
        prices = self.nodes[0].listfutures()

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address: f'{prices[0]["premiumPrice"] * 2}@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Move to just before futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount() - 1)

        # Create user futures contracts on futures block
        self.nodes[0].futureswap(address, f'{prices[0]["premiumPrice"]}@{self.symbolDUSD}', int(self.idTSLA))
        self.nodes[0].generate(1)

        # Check that futures have been executed
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'913.50000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}'])

        # Move to just next futures block
        next_futures_block = self.nodes[0].getblockcount() + (self.futures_interval - (self.nodes[0].getblockcount() % self.futures_interval))
        self.nodes[0].generate(next_futures_block - self.nodes[0].getblockcount())

        # Check that futures has not been executed again
        result = self.nodes[0].getaccount(address)
        assert_equal(result, [f'913.50000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}'])

        # Check contract address
        result = self.nodes[0].getaccount('bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqpsqgljc')
        assert_equal(result, [f'4905.60000000@{self.symbolDUSD}', f'1.00000000@{self.symbolTSLA}', f'1.00000000@{self.symbolGOOGL}', f'1.00000000@{self.symbolTWTR}', f'1.00000000@{self.symbolMSFT}'])

if __name__ == '__main__':
    FuturesTest().main()

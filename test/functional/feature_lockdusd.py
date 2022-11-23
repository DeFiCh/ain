#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test LockDUSD contract RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal


def sort_history(e):
    return e['txn']


class LockDUSDTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1',
             '-fortcanninghillheight=1', '-fortcanningcrunchheight=1', '-fortcanningroadheight=1',
             '-grandcentralheight=150', '-subsidytest=1']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Set up oracles and tokens
        self.setup_test()

        # Test setting of futures Gov vars
        self.lock_setup()

        self.test_lock()

    def setup_test(self):
        # Store addresses
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # RPC history checks
        self.list_history = []

        # Set token symbols
        self.symbolDUSD = 'DUSD'
        self.symbolDUSDLOCK12 = 'DUSDL12'
        self.symbolDUSDLOCK24 = 'DUSDL24'

        # Set up tokens
        self.nodes[0].createtoken({
            "symbol": self.symbolDUSDLOCK12,
            "name": self.symbolDUSDLOCK12,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            "symbol": self.symbolDUSDLOCK24,
            "name": self.symbolDUSDLOCK24,
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

        # Set token ids
        self.idDUSD = list(self.nodes[0].gettoken(self.symbolDUSD).keys())[0]
        self.idDUSD12 = list(self.nodes[0].gettoken(self.symbolDUSDLOCK12).keys())[0]
        self.idDUSD24 = list(self.nodes[0].gettoken(self.symbolDUSDLOCK24).keys())[0]

        # TODO: setup pools

        self.symbolPair12 = self.symbolDUSDLOCK12 + "-" + self.symbolDUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSDLOCK12,
            "tokenB": self.symbolDUSD,
            "commission": 0.1,
            "status": False,
            "ownerAddress": self.address,
            "pairSymbol": self.symbolPair12,
        }, [])
        self.nodes[0].generate(1)

        self.symbolPair24 = self.symbolDUSDLOCK24 + "-" + self.symbolDUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSDLOCK24,
            "tokenB": self.symbolDUSD,
            "commission": 0.1,
            "status": False,
            "ownerAddress": self.address,
            "pairSymbol": self.symbolPair24,
        }, [])
        self.nodes[0].generate(1)

        # Mint tokens for locking
        self.nodes[0].minttokens([f'100000@{self.idDUSD}'])
        self.nodes[0].generate(1)

    def lock_setup(self):
        # Create addresses for lock
        address = self.nodes[0].getnewaddress("", "legacy")

        # Try lock before grand central
        assert_raises_rpc_error(-32600, "called before GrandCentral height", self.nodes[0].lockdusd, address, 1000, 12)

        # Move to fork block
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())


        # Try lock before feature is active
        assert_raises_rpc_error(-32600, "DFIP2211D not currently active", self.nodes[0].lockdusd, address, 1000, 12)

        # Set partial futures attributes
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/active': 'true'}})
        self.nodes[0].generate(1)

        # Try lock before setting token and limit for time
        assert_raises_rpc_error(-32600, "This locktime is currently not available", self.nodes[0].lockdusd, address, 1000, 12)

        # Set all lockdusd attributes but set active to false
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/active': 'false'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/limit/12': '10000'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/token/12': self.idDUSD12}})

        assert_raises_rpc_error(-32600, "No such token", self.nodes[0].setgov, {"ATTRIBUTES": {'v0/params/dfip2211d/token/12': '13'}})

        self.nodes[0].generate(1)

        # Try lockdusd with DFIP2211 active set to false
        assert_raises_rpc_error(-32600, "DFIP2211D not currently active", self.nodes[0].lockdusd, address, 1000, 12)

        # Fully enable DFIP2211
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/active': 'true'}})
        self.nodes[0].generate(1)

        # Try lockdusd with wrong wrong locktime
        assert_raises_rpc_error(-32600, "This locktime is currently not available", self.nodes[0].lockdusd, address, 1000, 24)

        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/limit/24': '5000'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/token/24': self.idDUSD24}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2211d/active'], 'true')
        assert_equal(result['v0/params/dfip2211d/limit/12'], '10000')
        assert_equal(result['v0/params/dfip2211d/token/12'], self.idDUSD12)
        assert_equal(result['v0/params/dfip2211d/limit/24'], '5000')
        assert_equal(result['v0/params/dfip2211d/token/24'], self.idDUSD24)

    def test_lock(self):
        # Create addresses for locks
        address_12 = self.nodes[0].getnewaddress("", "legacy")
        address_24 = self.nodes[0].getnewaddress("", "legacy")
        address_small_funds = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {address_12: f'40000@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {address_24: f'40000@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {address_small_funds: f'100@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Test lock failures
        assert_raises_rpc_error(-32600, f'amount 100.00000000 is less than 2000.00000000', self.nodes[0].lockdusd,
                                address_small_funds, 2000, 12)
        assert_raises_rpc_error(-32600, f'This locktime is currently not available', self.nodes[0].lockdusd, address_12, 100, 5)
        assert_raises_rpc_error(-32600, f'This locktime is currently not available', self.nodes[0].lockdusd, address_12, 100, -1)
        assert_raises_rpc_error(-1, f'JSON value is not an integer as expected', self.nodes[0].lockdusd, address_12, 100, "aa")
        assert_raises_rpc_error(-3, f'Amount out of range', self.nodes[0].lockdusd, address_12, -100, 12)
        assert_raises_rpc_error(-32600, f'must be positive', self.nodes[0].lockdusd, address_12, 0, 12)
        assert_raises_rpc_error(-32600, f'amount too small.', self.nodes[0].lockdusd, address_12, 0.00000001, 12)
        assert_raises_rpc_error(-3, f'Invalid amount', self.nodes[0].lockdusd, address_12, "abc", 12)

        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, address_12, 20002, 12)
        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, address_24, 10002, 24)

        # lock funds
        locked12 = 1000
        self.nodes[0].lockdusd(address_12, locked12, 12)
        self.nodes[0].generate(1)
        locked24 = 2000
        self.nodes[0].lockdusd(address_24, locked24, 24)
        self.nodes[0].generate(1)

        # check minted
        # dusd reduced, locks increased
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']),
                     100000 - locked12 / 2 - locked24 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD12)[self.idDUSD12]['minted']), locked12 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD24)[self.idDUSD24]['minted']), locked24 / 2)

        # off by 1000 fi due to initial LM logic
        result = self.nodes[0].getaccount(address_12)
        assert_equal(result, [f'{int(40000-locked12)}.00000000@{self.symbolDUSD}', f'499.99999000@{self.symbolPair12}'])

        result = self.nodes[0].getaccount(address_24)
        assert_equal(result, [f'{int(40000-locked24)}.00000000@{self.symbolDUSD}', f'999.99999000@{self.symbolPair24}'])

        #check limits when already partly filled
        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, address_24, 19002, 12)
        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, address_12, 8002, 24)

        #check that nothing changed
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']),
                     100000 - locked12 / 2 - locked24 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD12)[self.idDUSD12]['minted']), locked12 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD24)[self.idDUSD24]['minted']), locked24 / 2)


        result = self.nodes[0].getaccount(address_12)
        assert_equal(result, [f'{int(40000-locked12)}.00000000@{self.symbolDUSD}', f'499.99999000@{self.symbolPair12}'])

        result = self.nodes[0].getaccount(address_24)
        assert_equal(result, [f'{int(40000-locked24)}.00000000@{self.symbolDUSD}', f'999.99999000@{self.symbolPair24}'])


        # lock to the limit
        locked12 += 19000
        self.nodes[0].lockdusd(address_12, 19000, 12)
        self.nodes[0].generate(1)
        locked24 += 8000
        self.nodes[0].lockdusd(address_24, 8000, 24)
        self.nodes[0].generate(1)

        # check that nothing changed
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']), 85000)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD12)[self.idDUSD12]['minted']), 10000)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD24)[self.idDUSD24]['minted']), 5000)

        result = self.nodes[0].getaccount(address_12)
        assert_equal(result, [f'20000.00000000@{self.symbolDUSD}', f'9999.99999000@{self.symbolPair12}'])

        result = self.nodes[0].getaccount(address_24)
        assert_equal(result, [f'30000.00000000@{self.symbolDUSD}', f'4999.99999000@{self.symbolPair24}'])


if __name__ == '__main__':
    LockDUSDTest().main()

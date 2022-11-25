#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test LockDUSD contract RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import almost_equal, assert_equal, assert_raises_rpc_error
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

        self.fund_addresses_and_degen_tests()

        self.test_lock()

        self.test_withdraw_degen()

        self.test_withdrawal()


    def setup_test(self):
        # Store addresses
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # RPC history checks
        self.list_history = []

        # Set token symbols
        self.symbolDUSD = 'DUSD'
        self.symbolDUSDLOCK1 = 'DUSDL1'
        self.symbolDUSDLOCK2 = 'DUSDL2'

        # Set up tokens
        self.nodes[0].createtoken({
            "symbol": self.symbolDUSDLOCK1,
            "name": self.symbolDUSDLOCK1,
            "isDAT": True,
            "collateralAddress": self.address
        })
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            "symbol": self.symbolDUSDLOCK2,
            "name": self.symbolDUSDLOCK2,
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
        self.idDUSD1 = list(self.nodes[0].gettoken(self.symbolDUSDLOCK1).keys())[0]
        self.idDUSD2 = list(self.nodes[0].gettoken(self.symbolDUSDLOCK2).keys())[0]


        self.symbolPair1 = self.symbolDUSDLOCK1 + "-" + self.symbolDUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSDLOCK1,
            "tokenB": self.symbolDUSD,
            "commission": 0.1,
            "status": False,
            "ownerAddress": self.address,
            "pairSymbol": self.symbolPair1,
        }, [])
        self.nodes[0].generate(1)

        self.symbolPair2 = self.symbolDUSDLOCK2 + "-" + self.symbolDUSD
        self.nodes[0].createpoolpair({
            "tokenA": self.symbolDUSDLOCK2,
            "tokenB": self.symbolDUSD,
            "commission": 0.1,
            "status": False,
            "ownerAddress": self.address,
            "pairSymbol": self.symbolPair2,
        }, [])
        self.nodes[0].generate(1)

        # Mint tokens for locking
        self.nodes[0].minttokens([f'100000@{self.idDUSD}'])
        self.nodes[0].generate(1)

    def lock_setup(self):
        # Create addresses for lock
        address = self.nodes[0].getnewaddress("", "legacy")

        # Try lock before grand central
        assert_raises_rpc_error(-32600, "called before GrandCentral height", self.nodes[0].lockdusd, address, "1000@DUSD", 12)

        # Move to fork block
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())


        # Try lock before feature is active
        assert_raises_rpc_error(-32600, "DFIP2211D not currently active", self.nodes[0].lockdusd, address, "1000@DUSD", 12)

        # Set partial futures attributes
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/active': 'true'}})
        self.nodes[0].generate(1)

        # Try lock before setting token and limit for time
        assert_raises_rpc_error(-32600, "This batch is currently not available", self.nodes[0].lockdusd, address, "1000@DUSD", 12)

        # Set all lockdusd attributes but set active to false
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/active': 'false'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/limit/1': '10000'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/token/1': self.idDUSD1}})

        assert_raises_rpc_error(-32600, "No such token", self.nodes[0].setgov, {"ATTRIBUTES": {'v0/params/dfip2211d/token/1': '13'}})

        self.nodes[0].generate(1)

        # Try lockdusd with DFIP2211 active set to false
        assert_raises_rpc_error(-32600, "DFIP2211D not currently active", self.nodes[0].lockdusd, address, "1000@DUSD", 12)

        # Fully enable DFIP2211
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/active': 'true'}})
        self.nodes[0].generate(1)

        # Try lockdusd with wrong wrong locktime
        assert_raises_rpc_error(-32600, "This batch is currently not available", self.nodes[0].lockdusd, address, "1000@DUSD", 24)

        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/limit/2': '5000'}})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/token/2': self.idDUSD2}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2211d/active'], 'true')
        assert_equal(result['v0/params/dfip2211d/limit/1'], '10000')
        assert_equal(result['v0/params/dfip2211d/token/1'], self.idDUSD1)
        assert_equal(result['v0/params/dfip2211d/limit/2'], '5000')
        assert_equal(result['v0/params/dfip2211d/token/2'], self.idDUSD2)

    def fund_addresses_and_degen_tests(self):
        # Create addresses for locks
        self.address_1 = self.nodes[0].getnewaddress("", "legacy")
        self.address_2 = self.nodes[0].getnewaddress("", "legacy")
        address_small_funds = self.nodes[0].getnewaddress("", "legacy")

        # Fund addresses
        self.nodes[0].accounttoaccount(self.address, {self.address_1: f'40000@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {self.address_2: f'40000@{self.symbolDUSD}'})
        self.nodes[0].accounttoaccount(self.address, {address_small_funds: f'100@{self.symbolDUSD}'})
        self.nodes[0].generate(1)

        # Test lock failures
        assert_raises_rpc_error(-32600, f'amount 100.00000000 is less than 2000.00000000', self.nodes[0].lockdusd,
                                address_small_funds, "2000@DUSD", 1)
        assert_raises_rpc_error(-32600, f'This batch is currently not available', self.nodes[0].lockdusd, self.address_1, "100@DUSD", 5)
        assert_raises_rpc_error(-32600, f'This batch is currently not available', self.nodes[0].lockdusd, self.address_1, "100@DUSD", -1)
        assert_raises_rpc_error(-1, f'JSON value is not an integer as expected', self.nodes[0].lockdusd, self.address_1, "100@DUSD", "aa")
        assert_raises_rpc_error(-3, f'Amount out of range', self.nodes[0].lockdusd, self.address_1, "-100@DUSD", 2)
        assert_raises_rpc_error(-3, f'Amount out of range', self.nodes[0].lockdusd, self.address_1, "0@DUSD", 1)
        assert_raises_rpc_error(-32600, f'amount too small.', self.nodes[0].lockdusd, self.address_1, "0.00000001@DUSD", 1)
        assert_raises_rpc_error(-3, f'Invalid amount', self.nodes[0].lockdusd, self.address_1, "abc", 1)

        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, self.address_1, "20002@DUSD", 1)
        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, self.address_2, "10002@DUSD", 2)

    def test_lock(self):
        # lock funds
        locked1 = 1000
        self.nodes[0].lockdusd(self.address_1, f"{locked1}@DUSD", 1)
        self.nodes[0].generate(1)
        locked2 = 2000
        self.nodes[0].lockdusd(self.address_2, f"{locked2}@DUSD", 2)
        self.nodes[0].generate(1)

        # check minted
        # dusd reduced, locks increased
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']),
                     100000 - locked1 / 2 - locked2 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), locked1 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), locked2 / 2)

        # off by 1000 fi due to initial LM logic
        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'{int(40000-locked1)}.00000000@{self.symbolDUSD}', f'499.99999000@{self.symbolPair1}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'{int(40000-locked2)}.00000000@{self.symbolDUSD}', f'999.99999000@{self.symbolPair2}'])

        #check limits when already partly filled
        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, self.address_2, "19002@DUSD", 1)
        assert_raises_rpc_error(-32600, f'Limit reached for this lock token', self.nodes[0].lockdusd, self.address_1, "8002@DUSD", 2)

        #check that nothing changed
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']),
                     100000 - locked1 / 2 - locked2 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), locked1 / 2)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), locked2 / 2)


        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'{int(40000-locked1)}.00000000@{self.symbolDUSD}', f'499.99999000@{self.symbolPair1}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'{int(40000-locked2)}.00000000@{self.symbolDUSD}', f'999.99999000@{self.symbolPair2}'])


        # lock to the limit
        locked1 += 19000
        self.nodes[0].lockdusd(self.address_1, "19000@DUSD", 1)
        self.nodes[0].generate(1)
        locked2 += 8000
        self.nodes[0].lockdusd(self.address_2, "8000@DUSD", 2)
        self.nodes[0].generate(1)

        # check that nothing changed
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']), 85000)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), 10000)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), 5000)

        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'20000.00000000@{self.symbolDUSD}', f'9999.99999000@{self.symbolPair1}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'30000.00000000@{self.symbolDUSD}', f'4999.99999000@{self.symbolPair2}'])

        #remove part of the liquidity to have pure token later on
        self.nodes[0].removepoolliquidity(self.address_1, f"1000@{self.symbolPair1}")
        self.nodes[0].generate(1)


        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'1000.00000000@{self.symbolDUSDLOCK1}',f'21000.00000000@{self.symbolDUSD}', f'8999.99999000@{self.symbolPair1}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'30000.00000000@{self.symbolDUSD}', f'4999.99999000@{self.symbolPair2}'])

    def test_withdraw_degen(self):
        assert_raises_rpc_error(-32600, f'This batch is not open for withdrawal yet.', self.nodes[0].lockdusd, self.address_1, f"100@{self.symbolPair1}", 1)
        assert_raises_rpc_error(-32600, f'This batch is not open for withdrawal yet.', self.nodes[0].lockdusd, self.address_1, f"100@{self.symbolDUSDLOCK1}", 1)

        self.batch1Withdraw= self.nodes[0].getblockcount()+10

        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/withdraw-height/1': f"{self.batch1Withdraw}"}})
        self.nodes[0].generate(1)

        # Verify Gov vars
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2211d/active'], 'true')
        assert_equal(result['v0/params/dfip2211d/limit/1'], '10000')
        assert_equal(result['v0/params/dfip2211d/token/1'], self.idDUSD1)
        assert_equal(result['v0/params/dfip2211d/withdraw-height/1'], f"{self.batch1Withdraw}")
        assert_equal(result['v0/params/dfip2211d/limit/2'], '5000')
        assert_equal(result['v0/params/dfip2211d/token/2'], self.idDUSD2)

        assert_raises_rpc_error(-32600, f'This batch is not open for withdrawal yet.', self.nodes[0].lockdusd, self.address_1, f"100@{self.symbolPair1}", 1)
        assert_raises_rpc_error(-32600, f'This batch is not open for withdrawal yet.', self.nodes[0].lockdusd, self.address_1, f"100@{self.symbolDUSDLOCK1}", 1)

        self.nodes[0].generate(self.batch1Withdraw-self.nodes[0].getblockcount())

        assert_raises_rpc_error(-32600, f'Invalid token for this batch', self.nodes[0].lockdusd, self.address_1, f"100@{self.symbolPair1}", 2)
        assert_raises_rpc_error(-32600, f'Invalid token for this batch', self.nodes[0].lockdusd, self.address_1, f"100@{self.symbolDUSDLOCK1}",2)
        assert_raises_rpc_error(-32600, f'Invalid token for this batch', self.nodes[0].lockdusd, self.address_2, f"100@{self.symbolDUSDLOCK2}",1)

        assert_raises_rpc_error(-32600, f'amount 8999.99999000 is less than 10000.00000000', self.nodes[0].lockdusd, self.address_1, f"10000@{self.symbolPair1}", 1)
        assert_raises_rpc_error(-32600, f'amount 1000.00000000 is less than 10000.00000000', self.nodes[0].lockdusd, self.address_1, f"10000@{self.symbolDUSDLOCK1}",1)

        assert_raises_rpc_error(-32600, f'This batch is not open for withdrawal yet.', self.nodes[0].lockdusd, self.address_2, f"100@{self.symbolPair2}", 2)

    def test_withdrawal(self):

        self.nodes[0].lockdusd(self.address_1, f"100@{self.symbolPair1}", 1)
        self.nodes[0].generate(1)

        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']), 85100)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), 9900)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), 5000)

        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'1000.00000000@{self.symbolDUSDLOCK1}',f'21200.00000000@{self.symbolDUSD}', f'8899.99999000@{self.symbolPair1}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'30000.00000000@{self.symbolDUSD}', f'4999.99999000@{self.symbolPair2}'])


        self.nodes[0].lockdusd(self.address_1, f"1000@{self.symbolDUSDLOCK1}", 1)
        self.nodes[0].generate(1)

        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']), 86100)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), 8900)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), 5000)

        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'22200.00000000@{self.symbolDUSD}', f'8899.99999000@{self.symbolPair1}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'30000.00000000@{self.symbolDUSD}', f'4999.99999000@{self.symbolPair2}'])

        self.nodes[0].lockdusd(self.address_1, f"8899.99999000@{self.symbolPair1}", 1)
        self.nodes[0].generate(1)

        almost_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']), Decimal(94999.99999),0.000001)
        almost_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), Decimal(0.000001),0.000001)
        assert_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), 5000)

        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'39999.99998000@{self.symbolDUSD}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'30000.00000000@{self.symbolDUSD}', f'4999.99999000@{self.symbolPair2}'])


        height= self.nodes[0].getblockcount()

        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/dfip2211d/withdraw-height/2': f"{height+10}"}})
        self.nodes[0].generate(1)
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2211d/active'], 'true')
        assert_equal(result['v0/params/dfip2211d/limit/1'], '10000')
        assert_equal(result['v0/params/dfip2211d/token/1'], self.idDUSD1)
        assert_equal(result['v0/params/dfip2211d/withdraw-height/1'], f"{self.batch1Withdraw}")
        assert_equal(result['v0/params/dfip2211d/limit/2'], '5000')
        assert_equal(result['v0/params/dfip2211d/token/2'], self.idDUSD2)
        assert_equal(result['v0/params/dfip2211d/withdraw-height/2'], f"{height+10}")

        assert_raises_rpc_error(-32600, f'This batch is not open for withdrawal yet.', self.nodes[0].lockdusd, self.address_2, f"100@{self.symbolPair2}", 2)

        self.nodes[0].generate(9)

        self.nodes[0].lockdusd(self.address_2, f"4999.99999000@{self.symbolPair2}", 2)
        self.nodes[0].generate(1)

        almost_equal(Decimal(self.nodes[0].gettoken(self.idDUSD)[self.idDUSD]['minted']), Decimal(99999.99998),0.0000001)
        almost_equal(Decimal(self.nodes[0].gettoken(self.idDUSD1)[self.idDUSD1]['minted']), Decimal(0.000001),0.0000001)
        almost_equal(Decimal(self.nodes[0].gettoken(self.idDUSD2)[self.idDUSD2]['minted']), Decimal(0.000001),0.0000001)

        result = self.nodes[0].getaccount(self.address_1)
        assert_equal(result, [f'39999.99998000@{self.symbolDUSD}'])

        result = self.nodes[0].getaccount(self.address_2)
        assert_equal(result, [f'39999.99998000@{self.symbolDUSD}'])

        #TODO: refactor balance check into own method



if __name__ == '__main__':
    LockDUSDTest().main()

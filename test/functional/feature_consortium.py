#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - loan basics."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal

class ConsortiumTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1']]

    def run_test(self):

        print("Generating initial chain...")
        self.nodes[0].generate(100)
        self.sync_blocks()

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        account2 = self.nodes[2].get_genesis_keys().ownerAuthAddress
        account3 = self.nodes[3].get_genesis_keys().ownerAuthAddress

        self.nodes[1].generate(150)
        self.sync_blocks()

        symbolBTC = "BTC"
        symbolDOGE = "DOGE"

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": account0
        })

        self.nodes[0].createtoken({
            "symbol": symbolDOGE,
            "name": "DOGE token",
            "isDAT": True,
            "collateralAddress": account0
        })

        self.nodes[0].sendtoaddress(account2, 10)
        self.nodes[0].sendtoaddress(account3, 10)

        self.nodes[0].generate(1)
        self.sync_blocks()

        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        idDOGE = list(self.nodes[0].gettoken(symbolDOGE).keys())[0]

        assert_raises_rpc_error(-32600, "called before GreatWorld height", self.nodes[0].burntokens, {
            'amounts': "1@" + symbolBTC,
            'from': account0,
        })

        self.nodes[0].generate(2)
        self.sync_blocks()

        assert_raises_rpc_error(-5, "Need foundation or consortium member authorization", self.nodes[2].minttokens, ["1@" + symbolBTC])
        assert_raises_rpc_error(-5, "Need foundation or consortium member authorization", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idBTC + '/consortium_members' : '{"01":{"name":"test", \
                                                                                                  "ownerAddress":"' + account2 + '", \
                                                                                                  "backingId":"blablabla", \
                                                                                                  "mintLimit":10.00000000}, \
                                                                                            "02":{"name":"test123", \
                                                                                                  "ownerAddress":"' + account3 + '", \
                                                                                                  "backingId":"blablabla", \
                                                                                                  "mintLimit":4.00000000}}'}})

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idBTC + '/consortium_mint_limit' : '1000000000'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_members'], '{"01":{"name":"test","ownerAddress":"' + account2 +'","backingId":"blablabla","mintLimit":10.00000000,"status":0},"02":{"name":"test123","ownerAddress":"' + account3 +'","backingId":"blablabla","mintLimit":4.00000000,"status":0}}')
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_mint_limit'], '1000000000')

        assert_raises_rpc_error(-5, "Need foundation or consortium member authorization", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        self.nodes[2].minttokens(["1@" + symbolBTC])
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2)[0], '1.00000000@' + symbolBTC)
        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], {'minted': ['1.00000000@BTC'], 'burnt': [], 'supply': ['1.00000000@BTC']})

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idDOGE + '/consortium_members' : '{"01":{"name":"test", \
                                                                                                   "ownerAddress":"' + account2 +'", \
                                                                                                   "backingId":"blablabla", \
                                                                                                   "mintLimit":5.00000000}}'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idDOGE + '/consortium_mint_limit' : '600000000'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_members'], '{"01":{"name":"test","ownerAddress":"' + account2 +'","backingId":"blablabla","mintLimit":10.00000000,"status":0},"02":{"name":"test123","ownerAddress":"' + account3 +'","backingId":"blablabla","mintLimit":4.00000000,"status":0}}')
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_mint_limit'], '1000000000')

        assert_equal(attribs['v0/token/' + idDOGE + '/consortium_members'], '{"01":{"name":"test","ownerAddress":"' + account2 +'","backingId":"blablabla","mintLimit":5.00000000,"status":0}}')
        assert_equal(attribs['v0/token/' + idDOGE + '/consortium_mint_limit'], '600000000')

        self.nodes[2].minttokens(["2@" + symbolDOGE])
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2), ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE])

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], {'minted': ['1.00000000@BTC', '2.00000000@DOGE'], 'burnt': [], 'supply': ['1.00000000@BTC', '2.00000000@DOGE']})
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], {'01': {'minted' : ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE], 'burnt': [], 'supply': ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE]}})

        assert_raises_rpc_error(-32600, "You will exceed your maximum mint limit for " + symbolDOGE + " token by minting this amount!", self.nodes[2].minttokens, ["3.00000001@" + symbolDOGE])

        self.nodes[2].burntokens({
            'amounts': "1@" + symbolDOGE,
            'from': account2,
        })

        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2), ['1.00000000@' + symbolBTC, '1.00000000@' + symbolDOGE])

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], {'minted': ['1.00000000@BTC', '2.00000000@DOGE'], 'burnt': ['1.00000000@DOGE'], 'supply': ['1.00000000@BTC', '1.00000000@DOGE']})
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], {'01': {'minted' : ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE], 'burnt': ['1.00000000@DOGE'], 'supply': ['1.00000000@' + symbolBTC, '1.00000000@' + symbolDOGE]}})

        self.nodes[2].accounttoaccount(account2, {account0: "1@" + symbolDOGE})
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[0].burntokens({
            'amounts': "0.5@" + symbolDOGE,
            'from': account0,
            'context': account2
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(account2), ['1.00000000@' + symbolBTC])
        assert_equal(self.nodes[0].getaccount(account0), ['0.50000000@' + symbolDOGE])

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], {'minted': ['1.00000000@BTC', '2.00000000@DOGE'], 'burnt': ['1.50000000@DOGE'], 'supply': ['1.00000000@BTC', '0.50000000@DOGE']})
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], {'01': {'minted' : ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE], 'burnt': ['1.50000000@DOGE'], 'supply': ['1.00000000@' + symbolBTC, '0.50000000@' + symbolDOGE]}})

        self.nodes[0].burntokens({
            'amounts': "0.5@" + symbolDOGE,
            'from': account0,
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(account0), [])

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], {'minted': ['1.00000000@BTC', '2.00000000@DOGE'], 'burnt': ['1.50000000@DOGE'], 'supply': ['1.00000000@BTC', '0.50000000@DOGE']})
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], {'01': {'minted' : ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE], 'burnt': ['1.50000000@DOGE'], 'supply': ['1.00000000@' + symbolBTC, '0.50000000@' + symbolDOGE]}})

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idDOGE + '/consortium_members' : '{"01":{"name":"test", \
                                                                                                   "ownerAddress":"' + account2 +'", \
                                                                                                   "backingId":"blablabla", \
                                                                                                   "mintLimit":5.00000000, \
                                                                                                   "status":1}}'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/token/' + idDOGE + '/consortium_members'], '{"01":{"name":"test","ownerAddress":"' + account2 +'","backingId":"blablabla","mintLimit":5.00000000,"status":1}}')
        assert_equal(self.nodes[0].getburninfo(), {'address': 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG', 'amount': Decimal('0E-8'), 'tokens': [], 'consortiumtokens': ['2.00000000@DOGE'], 'feeburn': Decimal('2.00000000'), 'auctionburn': Decimal('0E-8'), 'paybackburn': Decimal('0E-8'), 'dexfeetokens': [], 'dfipaybackfee': Decimal('0E-8'), 'dfipaybacktokens': [], 'paybackfees': [], 'paybacktokens': [], 'emissionburn': Decimal('4831.15500000'), 'dfip2203': []})

        assert_raises_rpc_error(-32600, "Cannot mint token, not an active member of consortium for DOGE!", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        # can mint because it is a founder
        self.nodes[0].minttokens(["1@" + symbolBTC])
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[2].minttokens(["6@" + symbolBTC])
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[3].minttokens(["2@" + symbolBTC])
        self.nodes[3].generate(1)
        self.sync_blocks()

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], {'minted': ['9.00000000@BTC', '2.00000000@DOGE'], 'burnt': ['1.50000000@DOGE'], 'supply': ['9.00000000@BTC', '0.50000000@DOGE']})
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], {'01': {'minted' : ['7.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE], 'burnt': ['1.50000000@DOGE'], 'supply': ['7.00000000@' + symbolBTC, '0.50000000@' + symbolDOGE]},
                                                                            '02': {'minted' : ['2.00000000@' + symbolBTC], 'burnt': [], 'supply': ['2.00000000@' + symbolBTC]}})

        assert_raises_rpc_error(-32600, "You will exceed your maximum mint limit for " + symbolBTC + " token by minting this amount!", self.nodes[3].minttokens, ["2.00000001@" + symbolBTC])
        assert_raises_rpc_error(-32600, "You will exceed global maximum mint limit for " + symbolBTC + " token by minting this amount!", self.nodes[3].minttokens, ["1.00000001@" + symbolBTC])

if __name__ == '__main__':
    ConsortiumTest().main()

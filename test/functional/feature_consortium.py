#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - loan basics."""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error

class ConsortiumTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-greatworldheight=253', '-txindex=1']]

    def run_test(self):

        print("Generating initial chain...")
        self.nodes[0].generate(100)
        self.sync_blocks()

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        account2 = self.nodes[2].get_genesis_keys().ownerAuthAddress

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

        self.nodes[0].sendtoaddress(account2, 100)

        self.nodes[0].generate(1)
        self.sync_blocks()

        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        idDOGE = list(self.nodes[0].gettoken(symbolDOGE).keys())[0]

        try:
            self.nodes[2].minttokens(["1@" + symbolBTC])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect authorization for " + account0 in errorString)

        try:
            self.nodes[2].minttokens(["1@" + symbolDOGE])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect authorization for " + account0 in errorString)

        assert_raises_rpc_error(-32600, "Cannot be set before GreatWorld", self.nodes[0].setgov,{"ATTRIBUTES":{'v0/token/' + idBTC + '/consortium_members' : '{\"01\":{\"name\":\"test\",' +
                                                                                                                                                                      '\"ownerAddress\":\"' + account2 +'\",' +
                                                                                                                                                                      '\"backingId\":\"blablabla\",' +
                                                                                                                                                                      '\"mintLimit\":10.00000000}}'}})
        assert_raises_rpc_error(-32600, "Cannot be set before GreatWorld", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/' + idBTC + '/consortium_mint_limit':'1000000000'}})

        assert_raises_rpc_error(-32600, "called before GreatWorld height", self.nodes[0].burntokens, {
            'amounts': "1@" + symbolBTC,
            'from': account0,
        })

        self.nodes[0].generate(2)
        self.sync_blocks()

        assert_raises_rpc_error(-32600, "You are not a foundation or consortium member and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolBTC])
        assert_raises_rpc_error(-32600, "You are not a foundation or consortium member and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idBTC + '/consortium_members' : '{\"01\":{\"name\":\"test\",' +
                                                                                                    '\"ownerAddress\":\"' + account2 +'\",' +
                                                                                                    '\"backingId\":\"blablabla\",' +
                                                                                                    '\"mintLimit\":10.00000000}}'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idBTC + '/consortium_mint_limit' : '1000000000'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_members'], '{\"01\":{\"name\":\"test\",' +
                                                                                    '\"ownerAddress\":\"' + account2 +'\",' +
                                                                                    '\"backingId\":\"blablabla\",' +
                                                                                    '\"mintLimit\":10.00000000,' +
                                                                                    '\"status\":0}}')
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_mint_limit'], '1000000000')

        assert_raises_rpc_error(-32600, "You are not a foundation or consortium member and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        self.nodes[2].minttokens(["1@" + symbolBTC])
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2)[0], '1.00000000@' + symbolBTC)
        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], ['1.00000000@' + symbolBTC])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idDOGE + '/consortium_members' : '{\"01\":{\"name\":\"test\",' +
                                                                                                    '\"ownerAddress\":\"' + account2 +'\",' +
                                                                                                    '\"backingId\":\"blablabla\",' +
                                                                                                    '\"mintLimit\":5.00000000}}'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/' + idDOGE + '/consortium_mint_limit' : '600000000'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_members'], '{\"01\":{\"name\":\"test\",' +
                                                                                    '\"ownerAddress\":\"' + account2 +'\",' +
                                                                                    '\"backingId\":\"blablabla\",' +
                                                                                    '\"mintLimit\":10.00000000,' +
                                                                                    '\"status\":0}}')
        assert_equal(attribs['v0/token/' + idBTC + '/consortium_mint_limit'], '1000000000')
        assert_equal(attribs['v0/token/' + idDOGE + '/consortium_members'], '{\"01\":{\"name\":\"test\",' +
                                                                                     '\"ownerAddress\":\"' + account2 +'\",' +
                                                                                     '\"backingId\":\"blablabla\",' +
                                                                                     '\"mintLimit\":5.00000000,' +
                                                                                     '\"status\":0}}')
        assert_equal(attribs['v0/token/' + idDOGE + '/consortium_mint_limit'], '600000000')

        self.nodes[2].minttokens(["2@" + symbolDOGE])
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2)[0], '1.00000000@' + symbolBTC)
        assert_equal(self.nodes[2].getaccount(account2)[1], '2.00000000@' + symbolDOGE)

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE])
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], '{\"01\":[\"1.00000000@' + symbolBTC + '\",\"2.00000000@' + symbolDOGE + '\"]}')

        assert_raises_rpc_error(-32600, "You will exceed your maximum mint limit for " + symbolDOGE + " token by minting this amount!", self.nodes[2].minttokens, ["3.00000001@" + symbolDOGE])

        self.nodes[2].burntokens({
            'amounts': "1@" + symbolDOGE,
            'from': account2,
        })

        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2)[0], '1.00000000@' + symbolBTC)
        assert_equal(self.nodes[2].getaccount(account2)[1], '1.00000000@' + symbolDOGE)

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_minted'], ['1.00000000@' + symbolBTC, '1.00000000@' + symbolDOGE])
        assert_equal(attribs['v0/live/economy/consortium_members_minted'], '{\"01\":[\"1.00000000@' + symbolBTC + '\",\"1.00000000@' + symbolDOGE + '\"]}')


if __name__ == '__main__':
    ConsortiumTest().main()

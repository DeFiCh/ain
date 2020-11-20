#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test autoauth

- verify creation and funding of special "chained" add-on auth tx for every custom tx that needs authtorization utxos
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
from decimal import Decimal

class TokensAutoAuthTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50'],
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50']]

    def step(self, node = None, blocks = 1):
        if node is None:
            node = self.nodes[1]
        self.sync_mempools()
        node.generate(blocks)
        self.sync_blocks()

    def run_test(self):
        n0 = self.nodes[0]
        n1 = self.nodes[1]

        self.step(blocks=102) # for 2 matured utxos

        # initial funds:
        funds0 = n0.getnewaddress("", "legacy")
        n1.sendmany("", { funds0 : 50} )
        self.step()


        #==== Masternodes auth:
        # RPC 'resignmasternode'
        mnCollateral = n0.getnewaddress("", "legacy")
        mnId = n0.createmasternode(mnCollateral)
        self.step()
        assert_equal(len(n0.listmasternodes()), 9)
        assert_equal(len(n0.getrawmempool()), 0)
        try:
            n0.resignmasternode(mnId, [ n0.listunspent()[0] ])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from masternode owner" in errorString)

        n0.resignmasternode(mnId)
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(n0.listmasternodes()[mnId]['state'], 'PRE_RESIGNED')
        assert_equal(len(n0.getrawmempool()), 0)


        #==== Tokens auth:
        # RPC 'createtoken'
        collateralGold = self.nodes[0].getnewaddress("", "legacy")
        try:
            n0.createtoken({
                "symbol": "GOLD",
                "name": "shiny gold",
                "isDAT": True,
                "collateralAddress": collateralGold
            }, [ n0.listunspent()[0] ])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx not from foundation member" in errorString)
        n0.createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "isDAT": True,
            "collateralAddress": collateralGold
        })
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.listtokens()), 2)
        assert_equal(len(n0.getrawmempool()), 0)


        # RPC 'updatetoken'
        try:
            n0.updatetoken("GOLD", {"isDAT": False}, [ n0.listunspent()[0] ])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from token owner" in errorString)
        n0.updatetoken(
            "GOLD", {"isDAT": False}
            )
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(n0.listtokens()['1']["isDAT"], False)
        assert_equal(len(n0.getrawmempool()), 0)


        # RPC 'minttoken'
        # create one more token (for multiple mint and latter LP)
        collateralSilver = self.nodes[0].getnewaddress("", "legacy")
        n0.createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "isDAT": True,
            "collateralAddress": collateralSilver
        })
        self.step()
        assert_equal(len(n0.listtokens()), 3)

        try:
            n0.minttokens(["1000@GOLD#1", "1000@SILVER"], [ n0.listunspent()[0] ])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from token owner" in errorString)
        n0.minttokens(["1000@GOLD#1", "5000@SILVER"])
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(n0.getaccount(collateralGold,   {}, True)['1'], 1000)
        assert_equal(n0.getaccount(collateralSilver, {}, True)['2'], 5000)


        #==== Liquidity Pools auth:
        # RPC 'createpoolpair'
        poolOwner = n0.getnewaddress("", "legacy")
        try:
            n0.createpoolpair({
                "tokenA": "GOLD#1",
                "tokenB": "SILVER",
                "comission": 0.001,
                "status": True,
                "ownerAddress": poolOwner,
                "pairSymbol": "GS"
            }, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx not from foundation member" in errorString)
        n0.createpoolpair({
            "tokenA": "GOLD#1",
            "tokenB": "SILVER",
            "comission": 0.001,
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "GS"
        })
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)


        # RPC 'addpoolliquidity'
        poolShare = n0.getnewaddress("", "legacy")
        try:
            n0.addpoolliquidity({
                collateralGold: "100@GOLD#1", collateralSilver: "500@SILVER"
            }, poolShare, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        n0.addpoolliquidity({
            collateralGold: "100@GOLD#1", collateralSilver: "500@SILVER"
        }, poolShare)
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)
        assert(n0.getaccount(poolShare, {}, True)['3'] > 200) # 223....


        # RPC 'poolswap'
        swapped = n0.getnewaddress("", "legacy")
        try:
            n0.poolswap({
                "from": collateralGold,
                "tokenFrom": "GOLD#1",
                "amountFrom": 10,
                "to": swapped,
                "tokenTo": "SILVER"
            }, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            # print (errorString)
        assert("tx must have at least one input from account owner" in errorString)
        n0.poolswap({
            "from": collateralGold,
            "tokenFrom": "GOLD#1",
            "amountFrom": 10,
            "to": swapped,
            "tokenTo": "SILVER"
        })
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)
        assert(n0.getaccount(swapped, {}, True)['2'] > 45)


        # RPC 'removepoolliquidity'
        try:
            n0.removepoolliquidity(
                poolShare, "200@GS", [ n0.listunspent()[0] ]
            )
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        n0.removepoolliquidity(poolShare, "200@GS")
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(len(n0.getaccount(poolShare, {}, True)), 3) # so gold and silver appears


        # RPC 'updatepoolpair'
        try:
            self.nodes[0].updatepoolpair({
                "pool": "GS",
                "status": False
            }, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx not from foundation member" in errorString)
        self.nodes[0].updatepoolpair({
            "pool": "GS",
            "status": False
        })
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)


        # RPC 'setgov'
        try:
            n0.setgov({ "LP_DAILY_DFI_REWARD": 35.5 }, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx not from foundation member" in errorString)

        n0.setgov({ "LP_DAILY_DFI_REWARD": 35.5 })
        n0.setgov({ "LP_SPLITS": { "3": 1 } })

        assert_equal(len(n0.getrawmempool()), 4)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)


        #==== Transfer auths:
        # RPC 'accounttoaccount'
        try:
            n0.accounttoaccount(poolShare, {swapped: "1@GS"}, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        n0.accounttoaccount(poolShare, {swapped: "10@GS"})
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(n0.getaccount(swapped, {}, True)['3'], 10)

        # turn on the pool to get some DFI rewards on LP accounts
        n0.updatepoolpair({
            "pool": "GS",
            "status": True
        })
        self.step()
        print("swapped", n0.getaccount(swapped, {}, True))
        print("poolShare", n0.getaccount(poolShare, {}, True))

        # RPC 'accounttoutxos'
        try:
            self.nodes[0].accounttoutxos(swapped, {swapped: "0.2"}, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        n0.accounttoutxos(swapped, {swapped: "0.2"})
        assert_equal(len(n0.getrawmempool()), 2)
        self.step()
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(n0.listunspent(addresses = [swapped] )[0]['amount'], Decimal('0.2'))


if __name__ == '__main__':
    TokensAutoAuthTest ().main ()

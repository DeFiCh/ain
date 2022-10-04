#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test autoauth

- verify creation and funding of special "chained" add-on auth tx for every custom tx that needs authtorization utxos
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
from decimal import Decimal

class TokensAutoAuthTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50']]

    # Move all coins to new address and change address to test auto auth
    def clear_auth_utxos(self):
        non_auth_address = self.nodes[0].getnewaddress("", "legacy")
        balance = self.nodes[0].getbalance()
        self.nodes[0].sendtoaddress(non_auth_address, balance - Decimal("0.1")) # 0.1 to cover fee
        self.nodes[0].generate(1, 1000000, non_auth_address)

    def run_test(self):
        n0 = self.nodes[0]

        coinbase = n0.getnewaddress("", "legacy")
        n0.generate(102, 1000000, coinbase)

        #==== Masternodes auth:
        # RPC 'resignmasternode'
        mnCollateral = n0.getnewaddress("", "legacy")
        mnId = n0.createmasternode(mnCollateral)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        assert_equal(len(n0.listmasternodes()), 9)
        assert_equal(len(n0.getrawmempool()), 0)
        try:
            n0.resignmasternode(mnId, [ n0.listunspent()[0] ])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from the owner" in errorString)
        errorString = ""

        n0.resignmasternode(mnId)
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
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
        errorString = ""

        n0.createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "isDAT": True,
            "collateralAddress": collateralGold
        })
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.listtokens()), 2)
        assert_equal(len(n0.getrawmempool()), 0)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # RPC 'updatetoken'
        try:
            n0.updatetoken("GOLD", {"isDAT": False}, [ n0.listunspent()[0] ])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from the owner" in errorString)
        errorString = ""

        n0.updatetoken(
            "GOLD", {"isDAT": False}
            )
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(n0.listtokens()['1']["isDAT"], False)
        assert_equal(len(n0.getrawmempool()), 0)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # RPC 'minttoken'
        # create one more token (for multiple mint and latter LP)
        collateralSilver = self.nodes[0].getnewaddress("", "legacy")
        n0.createtoken({
            "symbol": "SILVER",
            "name": "just silver",
            "isDAT": True,
            "collateralAddress": collateralSilver
        })
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.listtokens()), 3)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        try:
            n0.minttokens(["1000@GOLD#1", "1000@SILVER"], [ n0.listunspent()[0] ])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from token owner" in errorString)
        errorString = ""

        n0.minttokens(["1000@GOLD#1", "5000@SILVER"])
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
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
                "commission": 0.001,
                "status": True,
                "ownerAddress": poolOwner,
                "pairSymbol": "GS"
            }, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx not from foundation member" in errorString)
        errorString = ""

        n0.createpoolpair({
            "tokenA": "GOLD#1",
            "tokenB": "SILVER",
            "commission": 0.001,
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "GS"
        })
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)

        # Clear auth UTXOs
        self.clear_auth_utxos()

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
        errorString = ""

        n0.addpoolliquidity({
            collateralGold: "100@GOLD#1", collateralSilver: "500@SILVER"
        }, poolShare)
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
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
        assert("tx must have at least one input from account owner" in errorString)
        errorString = ""

        n0.poolswap({
            "from": collateralGold,
            "tokenFrom": "GOLD#1",
            "amountFrom": 10,
            "to": swapped,
            "tokenTo": "SILVER"
        })
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)
        assert(n0.getaccount(swapped, {}, True)['2'] > 45)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # RPC 'removepoolliquidity'
        try:
            n0.removepoolliquidity(
                poolShare, "200@GS", [ n0.listunspent()[0] ]
            )
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        errorString = ""

        n0.removepoolliquidity(poolShare, "200@GS")
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(len(n0.getaccount(poolShare, {}, True)), 3) # so gold and silver appears

        # Clear auth UTXOs
        self.clear_auth_utxos()


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
        errorString = ""

        self.nodes[0].updatepoolpair({
            "pool": "GS",
            "status": False
        })
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)

        # Clear auth UTXOs
        self.clear_auth_utxos()


        # RPC 'setgov'
        try:
            n0.setgov({ "LP_DAILY_DFI_REWARD": 35.5 }, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx not from foundation member" in errorString)
        errorString = ""

        n0.setgov({ "LP_DAILY_DFI_REWARD": 35.5 })

        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)


        #==== Transfer auths:
        # RPC 'accounttoaccount'
        try:
            n0.accounttoaccount(poolShare, {swapped: "1@GS"}, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        errorString = ""

        n0.accounttoaccount(poolShare, {swapped: "10@GS"})
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(n0.getaccount(swapped, {}, True)['3'], 10)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # turn on the pool to get some DFI rewards on LP accounts
        n0.updatepoolpair({
            "pool": "GS",
            "status": True
        })
        n0.generate(1, 1000000, coinbase)
        print("swapped", n0.getaccount(swapped, {}, True))
        print("poolShare", n0.getaccount(poolShare, {}, True))

        # RPC 'accounttoutxos'
        n0.utxostoaccount({swapped: "1@DFI"})
        n0.generate(1, 1000000, coinbase)
        try:
            self.nodes[0].accounttoutxos(swapped, {swapped: "0.2"}, [ n0.listunspent()[0] ])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("tx must have at least one input from account owner" in errorString)
        errorString = ""

        n0.accounttoutxos(swapped, {swapped: "0.2"})
        assert_equal(len(n0.getrawmempool()), 2)
        n0.generate(1, 1000000, coinbase)
        assert_equal(len(n0.getrawmempool()), 0)
        assert_equal(n0.listunspent(addresses = [swapped] )[1]['amount'], Decimal('0.2'))


if __name__ == '__main__':
    TokensAutoAuthTest ().main ()

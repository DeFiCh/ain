#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test pool's RPC.

- verify basic accounts operation
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

from decimal import Decimal, ROUND_DOWN

from pprint import pprint

def make_rounded_decimal(value) :
    return Decimal(value).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)

class PoolLiquidityTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main
        # node1: secondary tester
        # node2: revert create (all)
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.nodes[0].generate(25)
        self.nodes[1].generate(25)
        self.sync_all()
        self.nodes[0].generate(100)
        self.sync_all()

        # stop node #2 for future revert
        self.stop_node(2)
        connect_nodes_bi(self.nodes, 0, 3)

        self.nodes[1].createtoken({
            "symbol": "BTC",
            "name": "BTC DFI token",
            "collateralAddress": self.nodes[1].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[1].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1]])

        symbolDFI = "DFI"
        symbolBTC = "BTC#" + self.get_id_token("BTC")

        self.nodes[1].minttokens("100@" + symbolBTC)

        self.nodes[1].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1]])

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        accountDFI = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountBTC = self.nodes[1].get_genesis_keys().ownerAuthAddress

        self.nodes[0].utxostoaccount({accountDFI: "500@" + symbolDFI})
        self.nodes[1].utxostoaccount({accountBTC: "50@" + symbolDFI})
        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1]])

        # initialDFI = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]
        # initialBTC = self.nodes[1].getaccount(accountBTC, {}, True)[idBTC]
        # print("Initial DFI:", initialDFI, ", id", idDFI)
        # print("Initial BTC:", initialBTC, ", id", idBTC)

        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        # transfer DFI
        self.nodes[1].accounttoaccount(accountBTC, {accountDFI: "1@" + symbolBTC})
        self.nodes[1].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1]])

        # create pool
        self.nodes[0].createpoolpair({
            "tokenA": symbolDFI,
            "tokenB": symbolBTC,
            "commission": 1,
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DFIBTC",
        }, [])
        self.nodes[0].generate(1)

        # check tokens id
        pool = self.nodes[0].getpoolpair("DFIBTC")
        idDFIBTC = list(self.nodes[0].gettoken("DFIBTC").keys())[0]
        assert(pool[idDFIBTC]['idTokenA'] == idDFI)
        assert(pool[idDFIBTC]['idTokenB'] == idBTC)

        # transfer
        self.nodes[0].addpoolliquidity({
            accountDFI: ["100@" + symbolDFI, "1@" + symbolBTC]
        }, accountDFI, [])
        self.nodes[0].generate(1)

        result = self.nodes[0].setgov({"ICX_DFIBTC_POOLPAIR":int(idDFIBTC)})

        self.nodes[0].generate(1)

        result = self.nodes[0].getgov("ICX_DFIBTC_POOLPAIR")
        idDFIBTC = result["ICX_DFIBTC_POOLPAIR"]

        orderTx = self.nodes[0].icx_createorder({"tokenFrom": idDFI, "chainTo": "BTC", "amountFrom": 15, "ownerAddress": accountDFI, "orderPrice":0.01})

        self.nodes[0].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1]])

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 1)
        assert_equal(order[orderTx]["tokenFrom"], symbolDFI)
        assert_equal(order[orderTx]["chainTo"], "BTC")
        assert_equal(order[orderTx]["amountFrom"], Decimal('15'))
        assert_equal(order[orderTx]["amountToFill"], Decimal('15'))
        assert_equal(order[orderTx]["orderPrice"], Decimal('0.01000000'))
        assert_equal(order[orderTx]["amountToFillInToAsset"], Decimal('0.1500000'))
        assert_equal(order[orderTx]["ownerAddress"], accountDFI)

        offerTx = self.nodes[1].icx_makeoffer({"orderTx": orderTx, "amount": 0.15, "receiveAddress": accountBTC})

        self.nodes[1].generate(1)
        self.sync_all([self.nodes[0], self.nodes[1]])

        offer = self.nodes[1].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 1)
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.15000000'))
        assert_equal(offer[offerTx]["receiveAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('1.50000000'))

if __name__ == '__main__':
    PoolLiquidityTest ().main ()

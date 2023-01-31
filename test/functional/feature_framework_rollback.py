#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - loan basics."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class RollbackFrameworkTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1']]


    def init_chain(self):
        print("Generating initial chain...")
        self.nodes[0].generate(100)
        self.sync_blocks()

    @DefiTestFramework.rollback
    def set_accounts(self, rollback=True):
        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.account1 = self.nodes[1].getnewaddress("", "legacy")
        self.account2 = self.nodes[2].get_genesis_keys().ownerAuthAddress
        self.account3 = self.nodes[3].get_genesis_keys().ownerAuthAddress
        self.nodes[1].generate(20)
        self.sync_blocks()

        self.nodes[0].sendtoaddress(self.account1, 10)
        self.nodes[0].sendtoaddress(self.account2, 10)
        self.nodes[0].sendtoaddress(self.account3, 10)
        self.nodes[0].generate(1)
        self.sync_blocks()

    @DefiTestFramework.rollback
    def create_tokens(self, rollback=None):
        self.symbolBTC = "BTC"
        self.symbolDOGE = "DOGE"

        self.nodes[0].createtoken({
            "symbol": self.symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.account0
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].minttokens(["2@" + self.symbolBTC])

        self.nodes[0].createtoken({
            "symbol": self.symbolDOGE,
            "name": "DOGE token",
            "isDAT": True,
            "collateralAddress": self.account3
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.idBTC = list(self.nodes[0].gettoken(self.symbolBTC).keys())[0]
        self.idDOGE = list(self.nodes[0].gettoken(self.symbolDOGE).keys())[0]

        assert_raises_rpc_error(-32600, "called before GrandCentral height", self.nodes[0].burntokens, {
            'amounts': "1@" + self.symbolBTC,
            'from': self.account0,
        })

        self.nodes[0].generate(254 - self.nodes[0].getblockcount())
        self.sync_blocks()

        # DAT owner can mint
        self.nodes[3].minttokens(["1@" + self.symbolDOGE])
        self.nodes[3].generate(1)
        self.sync_blocks()

    @DefiTestFramework.rollback
    def mint_extra(self, rollback=None):
        self.nodes[3].minttokens(["1@" + self.symbolDOGE])
        self.nodes[3].generate(1)
        self.sync_blocks()

    def run_test(self):

        self.init_chain()
        height = self.nodes[0].getblockcount() # block 100

        # rollback
        self.set_accounts()
        height1 = self.nodes[1].getblockcount()
        assert_equal(height, height1)

        # rollback
        self.set_accounts(rollback=True)
        height2 = self.nodes[2].getblockcount()
        assert_equal(height, height2)

        # No rollback
        self.set_accounts(rollback=False)
        height3 = self.nodes[3].getblockcount()
        assert(height != height3)

        # Print warning + No rollback
        # Default rollback = None -> not boolean
        self.create_tokens()
        height4 = self.nodes[0].getblockcount()
        assert(height4 != height3)

        # Print warning + No rollback
        # rollback = "wrongType" -> not boolean
        self.mint_extra(rollback="wrongType")
        height5 = self.nodes[0].getblockcount()
        assert(height5 != height4)

        # rollback
        self.mint_extra(rollback=True)
        height6 = self.nodes[0].getblockcount()
        assert_equal(height6, height5)

        # No rollback
        self.mint_extra(rollback=False)
        height7 = self.nodes[0].getblockcount()
        assert(height6 != height7)


if __name__ == '__main__':
    RollbackFrameworkTest().main()

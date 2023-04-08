#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# NOTE: These functions are too unstable to be a part of the base framework. 
# - These functions do not yet take into account the multi-node scenario. 
# - These functions do not yet take into account pre-fork and post-fork scenarios 
#   where these will crash when called unexpectedly.
# - Better to start these off out of framework simple helpers first that address
#   use cases and that can be easily reused and overtime refactor 
#   them into the framework if even needed.
# - Very likely that just having them out of tree helpers that's optionally pulled
#   in where needed can accomplish the same in a cleaner way but non intrusive 
#   way to the framework without the error scenarios when part of the base 
#   framework. It's generally not a good idea to add something to the framework
#   code unless these framework helpers are valid for the entire lifetime of
#   the blockchain unchanged

# # build the data obj to be checked pre and post rollback
# def _get_chain_data(self):
#     return [
#         self.nodes[0].logaccountbalances(),
#         self.nodes[0].logstoredinterests(),
#         self.nodes[0].listvaults(),
#         self.nodes[0].listtokens(),
#         self.nodes[0].listgovs(),
#         self.nodes[0].listmasternodes(),
#         self.nodes[0].listaccounthistory(),
#         self.nodes[0].getburninfo(),
#         self.nodes[0].getloaninfo(),
#         self.nodes[0].listanchors(),
#         self.nodes[0].listgovproposals(),
#         self.nodes[0].listburnhistory(),
#         self.nodes[0].listcommunitybalances()
#     ]

# # Captures the chain data, does a rollback and checks data has been restored
# def _check_rollback(self, func, *args, **kwargs):
#     init_height = self.nodes[0].getblockcount()
#     init_data = self._get_chain_data()
#     result = func(self, *args, **kwargs)
#     self.rollback_to(init_height)
#     final_data = self._get_chain_data()
#     final_height = self.nodes[0].getblockcount()
#     assert (init_data == final_data)
#     assert (init_height == final_height)
#     return result

# # WARNING: This decorator uses _get_chain_data() internally which can be an expensive call if used in large test scenarios.
# @classmethod
# def capture_rollback_verify(cls, func):
#     def wrapper(self, *args, **kwargs):
#         return self._check_rollback(func, *args, **kwargs)

#     return wrapper

class RollbackFrameworkTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1',
             '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50',
             '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50',
             '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350',
             '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1',
             '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50',
             '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50',
             '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350',
             '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1',
             '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50',
             '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50',
             '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350',
             '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1',
             '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50',
             '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50',
             '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-grandcentralepilogueheight=350',
             '-regtest-minttoken-simulate-mainnet=1', '-txindex=1']]

    def init_chain(self):
        print("Generating initial chain...")
        self.nodes[0].generate(100)
        self.sync_blocks()

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

    # @DefiTestFramework.capture_rollback_verify
    def set_accounts_with_rollback(self):
        self.set_accounts()

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

    # @DefiTestFramework.capture_rollback_verify
    def create_tokens_with_rollback(self):
        self.create_tokens()

    def mint_extra(self, rollback=None):
        self.nodes[3].minttokens(["1@" + self.symbolDOGE])
        self.nodes[3].generate(1)
        self.sync_blocks()

    # @DefiTestFramework.capture_rollback_verify
    def mint_extra_with_rollback(self):
        self.mint_extra()

    def run_test(self):
        self.init_chain()
        height = self.nodes[0].getblockcount()  # block 100

        # rollback
        self.set_accounts_with_rollback()
        height1 = self.nodes[1].getblockcount()
        assert_equal(height, height1)

        # no rollback
        self.set_accounts(rollback=False)
        height2 = self.nodes[3].getblockcount()
        assert (height != height2)

        # rollback
        self.create_tokens_with_rollback()
        height3 = self.nodes[0].getblockcount()
        assert_equal(height2, height3)

        # no rollback
        self.create_tokens()
        height4 = self.nodes[0].getblockcount()
        assert (height3 != height4)

        # rollback
        self.mint_extra_with_rollback()
        height5 = self.nodes[0].getblockcount()
        assert_equal(height5, height4)

        # no rollback
        self.mint_extra()
        height6 = self.nodes[0].getblockcount()
        assert (height6 != height5)


if __name__ == '__main__':
    RollbackFrameworkTest().main()

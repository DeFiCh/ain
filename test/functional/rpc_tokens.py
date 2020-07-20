#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

class TokensRpcBasicTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(100)
        self.sync_all()

        # Stop node #2 for future revert
        self.stop_node(2)

        # CREATION:
        #========================
        # @todo: check for different types of addresses (in fact, they were already checked, just extend test cases)
        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        # Fail to create: Insufficient funds (not matured coins)
        try:
            createTokenTx = self.nodes[0].createtoken([], {
                "symbol": "GOLD",
                "name": "shiny gold",
                "collateralAddress": collateral0
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        # Create token 'GOLD' (128)
        self.nodes[0].generate(1)
        createTokenTx = self.nodes[0].createtoken([], {
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": collateral0
        })

        # Create and sign (only) collateral spending tx
        spendTx = self.nodes[0].createrawtransaction([{'txid':createTokenTx, 'vout':1}],[{collateral0:9.999}])
        signedTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signedTx['complete'], True)

        # Try to spend collateral of mempooled creattoken tx
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("collateral-locked-in-mempool," in errorString)

        self.nodes[0].generate(1)
        # At this point, token was created
        tokens = self.nodes[0].listtokens()
        assert_equal(len(tokens), 2)
        assert_equal(tokens['128']["symbol"], "GOLD")
        assert_equal(tokens['128']["creationTx"], createTokenTx)

        # Check 'listtokens' output
        assert_equal(len(self.nodes[0].listtokens()), 2)
        t0 = self.nodes[0].gettoken(0)
        assert_equal(t0['0']['symbol'], "DFI")
        assert_equal(self.nodes[0].gettoken("DFI"), t0)
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['symbol'], "GOLD")
        assert_equal(self.nodes[0].gettoken("GOLD"), t128)
        assert_equal(self.nodes[0].gettoken(createTokenTx), t128)


        self.sync_blocks(self.nodes[0:2])
        # Stop node #1 for future revert
        self.stop_node(1)

        # Try to spend locked collateral again
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("collateral-locked," in errorString)


        # # RESIGNING:
        # #========================
        try:
            self.nodes[0].destroytoken([], "GOLD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO's" in errorString)

        # Funding auth addresses (for minting and resigning)
        fundingTx = self.nodes[0].sendtoaddress(collateral0, 1)
        fundingTx2 = self.nodes[0].sendtoaddress(collateral0, 1)
        self.nodes[0].generate(1)

        mintingTx = self.nodes[0].minttokens([], "GOLD", { self.nodes[0].getnewaddress("", "legacy"): 100 })
        self.nodes[0].generate(1)

        # input ("pause")
        print (self.nodes[0].listunspent(0, 9999999, [], True, {"tokenId": 128}))
        # input ("pause")
        tokenAddr = self.nodes[0].getnewaddress("", "legacy")
        sendcreateTokenTx = self.nodes[0].sendtoaddress("GOLD" + "@" + tokenAddr, 10)
        self.nodes[0].generate(1)
        utxos = self.nodes[0].listunspent(0, 9999999, [], True, {"tokenId": 128})
        if (utxos[0]['amount'] == 90):
            assert(utxos[1]['amount'] == 10)
        else:
            assert(utxos[0]['amount'] == 10)
            assert(utxos[1]['amount'] == 90)

        destroyTx = self.nodes[0].destroytoken([], "GOLD")
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listtokens()['128']['destructionTx'], destroyTx)

        # Spend unlocked collateral
        # This checks two cases at once:
        # 1) Finally, we should not fail on accept to mempool
        # 2) But we don't mine blocks after it, so, after chain reorg (on 'REVERTING'), we should not fail: tx should be removed from mempool!
        sendedTxHash = self.nodes[0].sendrawtransaction(signedTx['hex'])
        # Don't mine here, check mempool after reorg!
        # self.nodes[0].generate(1)


        # REVERTING:
        #========================

        # Revert token destruction!
        self.start_node(1)
        self.nodes[1].generate(5)
        # Check that collateral spending tx is still in the mempool
        assert_equal(sendedTxHash, self.nodes[0].getrawmempool()[0])

        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])

        assert_equal(sorted(self.nodes[0].getrawmempool()), sorted([fundingTx, destroyTx, fundingTx2, mintingTx, sendcreateTokenTx]))
        assert_equal(self.nodes[0].listtokens()['128']['destructionHeight'], -1)
        assert_equal(self.nodes[0].listtokens()['128']['destructionTx'], '0000000000000000000000000000000000000000000000000000000000000000')

        # Revert creation!
        self.start_node(2)

        self.nodes[2].generate(8)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_blocks(self.nodes[0:3])
        assert_equal(len(self.nodes[0].listtokens()), 1)
        assert_equal(sorted(self.nodes[0].getrawmempool()), sorted([createTokenTx, fundingTx, destroyTx, fundingTx2]))

if __name__ == '__main__':
    TokensRpcBasicTest ().main ()

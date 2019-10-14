#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

from test_framework.test_framework import BitcoinTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

import pprint
import time

class MasternodesRpcBasicTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def run_test(self):
        pp = pprint.PrettyPrinter(indent=4)

        assert_equal(len(self.nodes[0].mn_list()), 8)
        self.nodes[0].generate(100)
        self.sync_all()

        # Stop node #2 for future revert
        self.stop_node(2)

        # CREATION:
        #========================

        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        # Fail to create: Insufficient funds (not matured coins)
        try:
            idnode0 = self.nodes[0].mn_create([], {
                # "operatorAuthAddress": operator0,
                "collateralAddress": collateral0
            })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        # Create node0
        self.nodes[0].generate(1)
        idnode0 = self.nodes[0].mn_create([], {
            # "operatorAuthAddress": operator0,
            "collateralAddress": collateral0
        })

        # Create and sign (only) collateral spending tx
        spendTx = self.nodes[0].createrawtransaction([{'txid':idnode0, 'vout':1}],[{collateral0:9.999}])
        signedTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signedTx['complete'], True)

        # Try to spend collateral of mempooled mn_create tx
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("mn-collateral-locked-in-mempool," in errorString)

        self.nodes[0].generate(1)
        # At this point, mn was created
        assert_equal(self.nodes[0].mn_list([idnode0], False), { idnode0: "created"} )

        self.sync_blocks(self.nodes[0:2])
        # Stop node #1 for future revert
        self.stop_node(1)

        # Try to spend locked collateral again
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("mn-collateral-locked," in errorString)


        # RESIGNING:
        #========================

        # Fail to resign: Forget to place params in config
        try:
            self.nodes[0].mn_resign([], idnode0)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("You are not the owner" in errorString)

        # Restart with new params, but have no money on ownerauth address
        self.restart_node(0, extra_args=['-masternode_owner='+collateral0])
        self.nodes[0].generate(1) # to broke "initial block downloading"
        try:
            self.nodes[0].mn_resign([], idnode0)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO's" in errorString)

        # Funding auth address and successful resign
        fundingTx = self.nodes[0].sendtoaddress(collateral0, 1)
        self.nodes[0].generate(1)
        resignTx = self.nodes[0].mn_resign([], idnode0)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].mn_list()[idnode0]['status'], "created, resigned")

        # Spend unlocked collateral
        # This checks two cases at once:
        # 1) Finally, we should not fail on accept to mempool
        # 2) But we don't mine blocks after it, so, after chain reorg (on 'REVERTING'), we should not fail: tx should be removed from mempool!
        self.nodes[0].generate(12)
        sendedTxHash = self.nodes[0].sendrawtransaction(signedTx['hex'])
        # Don't mine here, check mempool after reorg!
        # self.nodes[0].generate(1)


        # REVERTING:
        #========================

        # Revert resign!
        self.start_node(1)
        self.nodes[1].generate(20)
        # Check that collateral spending tx is still in the mempool
        assert_equal(sendedTxHash, self.nodes[0].getrawmempool()[0])

        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])

        # Check that collateral spending tx was deleted
        # print ("CreateTx", idnode0)
        # print ("ResignTx", resignTx)
        # print ("FundingTx", fundingTx)
        # print ("SpendTx", sendedTxHash)
        assert_equal(self.nodes[0].getrawmempool(), [fundingTx, resignTx])
        assert_equal(self.nodes[0].mn_list()[idnode0]['status'], "active")

        # Revert creation!
        self.start_node(2)
        self.nodes[2].generate(25)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_blocks(self.nodes[0:3])
        assert_equal(len(self.nodes[0].mn_list()), 8)
        assert_equal(self.nodes[0].getrawmempool(), [idnode0, fundingTx, resignTx])

if __name__ == '__main__':
    MasternodesRpcBasicTest ().main ()

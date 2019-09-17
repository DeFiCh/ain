#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) 2019 The IntegralTeam
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

from test_framework.test_framework import BitcoinTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, \
    connect_nodes_bi, disconnect_nodes, \
    sync_blocks, sync_mempools

from decimal import Decimal
import pprint
import time

class MasternodesRpcBasicTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def gen(self, count=1, node_id=0):
        self.nodes[node_id].generatetoaddress(count, self.nodes[node_id].get_deterministic_priv_key().address)


    def run_test(self):
        pp = pprint.PrettyPrinter(indent=4)

        assert_equal(self.nodes[0].mn_list(), {})
        self.gen(100)
        self.sync_blocks()

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
        self.gen(2)
        idnode0 = self.nodes[0].mn_create([], {
            # "operatorAuthAddress": operator0,
            "collateralAddress": collateral0
        })
        self.gen()

        self.sync_blocks(self.nodes[0:2])
        # Stop node #1 for future revert
        self.stop_node(1)

        # Try to spend locked collateral
        spendTx = self.nodes[0].createrawtransaction([{'txid':idnode0, 'vout':1}],[{collateral0:9.9}])
        signTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signTx['complete'], True)
        try:
            self.nodes[0].sendrawtransaction(signTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("mn-collateral-locked," in errorString)

        # pp.pprint(signTx)

        # RESIGNING:
        #========================

        # Fail to resign: Forget to place params in config
        try:
            self.nodes[0].mn_resign([], idnode0)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("You are not the owner" in errorString)

        # Restart with new params, but have no money on auth address
        self.restart_node(0, extra_args=['-masternode_owner='+collateral0]) 
        try:
            self.nodes[0].mn_resign([], idnode0)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO's" in errorString)

        # Funding auth address and successful resign        
        self.nodes[0].sendtoaddress(collateral0, 1)
        self.gen()
        resignTx = self.nodes[0].mn_resign([], idnode0)
        self.gen()
        assert_equal(self.nodes[0].mn_list()[idnode0]['status'], "created, resigned")


        # Spend unlocked collateral
        self.gen(12)
        self.nodes[0].sendrawtransaction(signTx['hex'])
        # /// @todo @maxb Investigate bug with mempool!
        self.gen(1)


        # REVERTING:
        #========================

        # Revert resign!
        self.start_node(1)
        self.gen(20, 1)
        # pp.pprint(self.nodes[0].getrawmempool())
        # pp.pprint(self.nodes[1].getrawmempool())
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])
        assert_equal(self.nodes[0].mn_list()[idnode0]['status'], "active")

        # Revert creation!
        self.start_node(2)
        self.gen(25, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_blocks(self.nodes[0:3])
        assert_equal(self.nodes[0].mn_list(), {})

        # pp.pprint(self.nodes[0].mn_list())


if __name__ == '__main__':
    MasternodesRpcBasicTest ().main ()

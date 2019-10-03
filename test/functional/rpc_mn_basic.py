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
from test_framework.util import assert_equal, assert_greater_than, \
    connect_nodes_bi, disconnect_nodes, \
    sync_blocks, sync_mempools, \
    set_node_times

from decimal import Decimal
import pprint
import time

class MasternodesRpcBasicTest (BitcoinTestFramework):
    def set_genesis_keys(self):
        self.genesis = []
        self.genesis.append({'ownerAuthAddress'    : "bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny",
                             'ownerPrivKey'        : "cR4qgUdPhANDVF3bprcp5N9PNW2zyogDx6DGu2wHh2qtJB1L1vQj",
                             'operatorAuthAddress' : "bcrt1qmfvw3dp3u6fdvqkdc0y3lr0e596le9cf22vtsv",
                             'operatorPrivKey'     : "cVsa2wQvCjZZ54jGteQ8qiQbQLJQmZSBWriYUYyXbcaqUJFqK5HR" })
        self.genesis.append({'ownerAuthAddress'    : "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU",
                             'ownerPrivKey'        : "cRiRQ9cHmy5evDqNDdEV8f6zfbK6epi9Fpz4CRZsmLEmkwy54dWz",
                             'operatorAuthAddress' : "mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy",
                             'operatorPrivKey'     : "cPGEaz8AGiM71NGMRybbCqFNRcuUhg3uGvyY4TFE1BZC26EW2PkC" })
        self.genesis.append({'ownerAuthAddress'    : "bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu",
                             'ownerPrivKey'        : "cUX8AEUZYsZxNUh5fTS7ZGnF6SPQuTeTDTABGrp5dbPftCga2zcp",
                             'operatorAuthAddress' : "bcrt1qurwyhta75n2g75u2u5nds9p6w9v62y8wr40d2r",
                             'operatorPrivKey'     : "cUp5EVEjuAGpemSuejP36TWWuFKzuCbUJ4QAKJTiSSB2vXzDLsJW" })
        self.genesis.append({'ownerAuthAddress'    : "msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7",
                             'ownerPrivKey'        : "cSCmN1tjcR2yR1eaQo9WmjTMR85SjEoNPqMPWGAApQiTLJH8JF7W",
                             'operatorAuthAddress' : "mps7BdmwEF2vQ9DREDyNPibqsuSRZ8LuwQ",
                             'operatorPrivKey'     : "cVNTRYV43guugJoDgaiPZESvNtnfnUW19YEjhybihwDbLKjyrZNV" })


    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.mocktime = 0

    def gen(self, count=1, node_id=0):
        minted = 0
        while minted < count:
            # minted += node.mint(1, 1)
            self.nodes[node_id].setmocktime(self.mocktime)

            minted += self.nodes[node_id].generatetoaddress(1, self.genesis[node_id]['operatorAuthAddress'])
            self.mocktime += 2 # 2s per try

            # set time only for one node!!!
            self.nodes[node_id].setmocktime(self.mocktime)
            # set_node_times(self.nodes, self.mocktime)


    def setup_nodes(self):
        self.add_nodes(self.num_nodes)
        self.start_nodes()

        self.mocktime = self.nodes[0].getblockheader(self.nodes[0].getbestblockhash())["time"]  + 1
        # print("Initial mocktime (genesis)", self.mocktime)

        self.set_genesis_keys()
        for i in range(self.num_nodes):
            # print (self.genesis[i]['operatorPrivKey'])
            self.nodes[i].importprivkey(self.genesis[i]['operatorPrivKey'])
            self.nodes[i].importprivkey(self.genesis[i]['ownerPrivKey'])
            self.restart_node(i, extra_args=['-masternode_operator='+self.genesis[i]['operatorAuthAddress']])
            set_node_times(self.nodes, self.mocktime)

    def run_test(self):
        pp = pprint.PrettyPrinter(indent=4)

        assert_equal(len(self.nodes[0].mn_list()), 4)
        self.gen(100)
        time.sleep(2)
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
        self.gen(1)
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

        self.gen()
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
        self.restart_node(0, extra_args=['-masternode_owner='+collateral0, '-masternode_operator='+self.genesis[0]['operatorAuthAddress']])
        self.gen() # to broke "initial block downloading"
        try:
            self.nodes[0].mn_resign([], idnode0)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO's" in errorString)

        # Funding auth address and successful resign        
        fundingTx = self.nodes[0].sendtoaddress(collateral0, 1)
        self.gen()
        resignTx = self.nodes[0].mn_resign([], idnode0)
        self.gen()
        assert_equal(self.nodes[0].mn_list()[idnode0]['status'], "created, resigned")

        # Spend unlocked collateral
        # This checks two cases at once:
        # 1) Finally, we should not fail on accept to mempool
        # 2) But we don't mine blocks after it, so, after chain reorg (on 'REVERTING'), we should not fail: tx should be removed from mempool!
        self.gen(12)
        sendedTxHash = self.nodes[0].sendrawtransaction(signedTx['hex'])
        # Don't mine here, check mempool after reorg!
        # self.gen(1)


        # REVERTING:
        #========================

        # Revert resign!
        self.start_node(1, extra_args=['-masternode_operator='+self.genesis[1]['operatorAuthAddress']])
        self.gen(20, 1)
        # Check that collateral spending tx is still in the mempool
        assert_equal(sendedTxHash, self.nodes[0].getrawmempool()[0]);

        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_blocks(self.nodes[0:2])

        # Check that collateral spending tx was deleted
        # print ("CreateTx", idnode0)
        # print ("ResignTx", resignTx)
        # print ("FundingTx", fundingTx)
        # print ("SpendTx", sendedTxHash)
        assert_equal(self.nodes[0].getrawmempool(), [fundingTx, resignTx]);
        assert_equal(self.nodes[0].mn_list()[idnode0]['status'], "active")

        # Revert creation!
        self.start_node(2, extra_args=['-masternode_operator='+self.genesis[2]['operatorAuthAddress']])
        self.gen(25, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_blocks(self.nodes[0:3])
        assert_equal(len(self.nodes[0].mn_list()), 4)
        assert_equal(self.nodes[0].getrawmempool(), [idnode0, fundingTx, resignTx]);

if __name__ == '__main__':
    MasternodesRpcBasicTest ().main ()

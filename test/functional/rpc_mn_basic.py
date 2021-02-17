#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

class MasternodesRpcBasicTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=136'],
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=136'],
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=136']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)
        self.nodes[0].generate(100)
        self.sync_all()

        # Stop node #2 for future revert
        self.stop_node(2)

        # CREATION:
        #========================

        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        # Fail to create: Insufficient funds (not matured coins)
        try:
            idnode0 = self.nodes[0].createmasternode(
                collateral0
            )
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        # Create node0
        self.nodes[0].generate(1)
        idnode0 = self.nodes[0].createmasternode(
            collateral0
        )

        # Create and sign (only) collateral spending tx
        spendTx = self.nodes[0].createrawtransaction([{'txid':idnode0, 'vout':1}],[{collateral0:9.999}])
        signedTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signedTx['complete'], True)

        # Try to spend collateral of mempooled createmasternode tx
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("collateral-locked-in-mempool," in errorString)

        balance_before_creation = self.nodes[0].getbalance()
        self.nodes[0].generate(1)
        # At this point, mn was created
        assert_equal(balance_before_creation - 10 + 50, self.nodes[0].getbalance())
        assert_equal(self.nodes[0].listmasternodes({}, False)[idnode0], "PRE_ENABLED")
        assert_equal(self.nodes[0].getmasternode(idnode0)[idnode0]["state"], "PRE_ENABLED")
        self.nodes[0].generate(10)
        assert_equal(self.nodes[0].listmasternodes({}, False)[idnode0], "ENABLED")

        self.sync_blocks(self.nodes[0:2])
        # Stop node #1 for future revert
        self.stop_node(1)

        # Try to spend locked collateral again
        try:
            self.nodes[0].sendrawtransaction(signedTx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("collateral-locked," in errorString)


        # RESIGNING:
        #========================
        # Fail to resign: Have no money on ownerauth address

        # Deprecated due to auth automation
        # try:
        #     self.nodes[0].resignmasternode(idnode0)
        # except JSONRPCException as e:
        #     errorString = e.error['message']
        # assert("Can't find any UTXO's" in errorString)

        # Funding auth address and successful resign
        fundingTx = self.nodes[0].sendtoaddress(collateral0, 1)
        self.nodes[0].generate(1)
        resignTx = self.nodes[0].resignmasternode(idnode0)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listmasternodes()[idnode0]['state'], "PRE_RESIGNED")
        self.nodes[0].generate(10)
        assert_equal(self.nodes[0].listmasternodes()[idnode0]['state'], "RESIGNED")

        # Spend unlocked collateral
        # This checks two cases at once:
        # 1) Finally, we should not fail on accept to mempool
        # 2) But we don't mine blocks after it, so, after chain reorg (on 'REVERTING'), we should not fail: tx should be removed from mempool!
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
        assert_equal(self.nodes[0].listmasternodes()[idnode0]['state'], "ENABLED")

        # Revert creation!
        self.start_node(2)

        self.nodes[2].generate(35)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_blocks(self.nodes[0:3])
        assert_equal(len(self.nodes[0].listmasternodes()), 8)
        assert_equal(self.nodes[0].getrawmempool(), [idnode0, fundingTx, resignTx])

        collateral0 = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createmasternode(collateral0)
        balance_before_creation = self.nodes[0].getbalance()
        self.nodes[0].generate(1)
        # At this point, mn was created
        assert_equal(self.nodes[0].getblockcount(), 136) # Dakota height
        assert_equal(balance_before_creation - 2 + 50, self.nodes[0].getbalance())

if __name__ == '__main__':
    MasternodesRpcBasicTest ().main ()

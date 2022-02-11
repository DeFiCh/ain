#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class MasternodesRpcBasicTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=136', '-eunosheight=140', '-eunospayaheight=140', '-fortcanningheight=145'],
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=136', '-eunosheight=140', '-eunospayaheight=140', '-fortcanningheight=145'],
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=136', '-eunosheight=140', '-eunospayaheight=140', '-fortcanningheight=145']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)
        self.nodes[0].generate(100)
        self.sync_blocks()

        # CREATION:
        #========================

        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        # Create node0
        self.nodes[0].generate(1)
        collateral1 = self.nodes[1].getnewaddress("", "legacy")
        assert_raises_rpc_error(-8, "Address ({}) is not owned by the wallet".format(collateral1),  self.nodes[0].createmasternode, collateral1)

        idnode0 = self.nodes[0].createmasternode(
            collateral0
        )

        # Create and sign (only) collateral spending tx
        spendTx = self.nodes[0].createrawtransaction([{'txid':idnode0, 'vout':1}],[{collateral0:9.999}])
        signedTx = self.nodes[0].signrawtransactionwithwallet(spendTx)
        assert_equal(signedTx['complete'], True)

        self.nodes[0].generate(1)
        # At this point, mn was created
        assert_equal(self.nodes[0].listmasternodes({}, False)[idnode0], "PRE_ENABLED")
        assert_equal(self.nodes[0].getmasternode(idnode0)[idnode0]["state"], "PRE_ENABLED")
        self.nodes[0].generate(10)
        self.sync_blocks()

        assert_equal(self.nodes[0].listmasternodes({}, False)[idnode0], "ENABLED")
        assert_equal(self.nodes[1].listmasternodes()[idnode0]["operatorAuthAddress"], collateral0)

        # UPDATING
        #========================
        #assert_raises_rpc_error(-8, "updatemasternode cannot be called before Fortcanning hard fork", self.nodes[0].updatemasternode, idnode0, collateral0)

        #self.nodes[0].generate(50)

        #assert_raises_rpc_error(-32600, "The new operator is same as existing operator", self.nodes[0].updatemasternode, idnode0, collateral0)

        # node 1 try to update node 0 which should be rejected.
        #assert_raises_rpc_error(-5, "Incorrect authorization", self.nodes[1].updatemasternode, idnode0, collateral1)

        #self.nodes[0].updatemasternode(idnode0, collateral1)
        #self.nodes[0].generate(1)
        #self.sync_blocks()

        #assert_equal(self.nodes[1].listmasternodes()[idnode0]["operatorAuthAddress"], collateral1)

        # Test rollback
        #blockhash = self.nodes[1].getblockhash(self.nodes[1].getblockcount())
        #self.nodes[1].invalidateblock(blockhash)
        #self.nodes[1].reconsiderblock(blockhash)

        # RESIGNING:
        #========================

        # Funding auth address and successful resign
        self.nodes[0].sendtoaddress(collateral0, 1)
        self.nodes[0].generate(1)
        # resignTx
        self.nodes[0].resignmasternode(idnode0)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listmasternodes()[idnode0]['state'], "PRE_RESIGNED")
        self.nodes[0].generate(40)
        self.sync_blocks()
        assert_equal(self.nodes[0].listmasternodes()[idnode0]['state'], "RESIGNED")

        # Spend unlocked collateral
        # This checks two cases at once:
        # 1) Finally, we should not fail on accept to mempool
        # 2) But we don't mine blocks after it, so, after chain reorg (on 'REVERTING'), we should not fail: tx should be removed from mempool!
        self.nodes[0].sendrawtransaction(signedTx['hex'])
        # Don't mine here, check mempool after reorg!
        # self.nodes[0].generate(1)

if __name__ == '__main__':
    MasternodesRpcBasicTest ().main ()

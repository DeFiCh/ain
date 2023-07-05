#!/usr/bin/env python3
"""Test the masternodes create RPC.

- verify MN creation with custom reward address and resign
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class MasternodesRpcCreateRewardTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120',
             '-eunosheight=120', '-eunospayaheight=120', '-fortcanningheight=120', '-fortcanninghillheight=122',
             '-grandcentralheight=189', "-nextnetworkupgradeheight=120"],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120',
             '-eunosheight=120', '-eunospayaheight=120', '-fortcanningheight=120', '-fortcanninghillheight=122',
             '-grandcentralheight=189', '-txindex=1', "-nextnetworkupgradeheight=120"]]

    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)
        self.nodes[0].generate(100)
        self.sync_blocks()

        collateral0 = self.nodes[0].getnewaddress("", "legacy")

        # Fail to create: Insufficient funds (not matured coins)
        assert_raises_rpc_error(-4, "Insufficient funds", self.nodes[0].createmasternode, collateral0)

        # Create node0
        self.nodes[0].generate(20)
        collateral1 = self.nodes[1].getnewaddress("", "legacy")
        reward = self.nodes[1].getnewaddress("reward", "legacy")
        assert_raises_rpc_error(-8, "Address ({}) is not owned by the wallet".format(collateral1),
                                self.nodes[0].createmasternode, collateral1)

        # Fail to create: Wrong reward address format
        assert_raises_rpc_error(-8, "does not refer to a P2PKH or P2WPKH address", self.nodes[0].createmasternode, collateral0, '', [], 'TENYEARTIMELOCK', self.nodes[0].getnewaddress("", "eth"))

        # Fail to create: Invalid reward address
        assert_raises_rpc_error(-8, "does not refer to a P2PKH or P2WPKH address", self.nodes[0].createmasternode, collateral0, '', [], 'TENYEARTIMELOCK', "test")

        idnode0 = self.nodes[0].createmasternode(collateral0, '', [], 'TENYEARTIMELOCK', reward)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listmasternodes()[idnode0]['rewardAddress'], reward)

        # Should create: masternode without reward address
        idnode1 = self.nodes[0].createmasternode(self.nodes[0].getnewaddress("", "legacy"))
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listmasternodes()[idnode1]['rewardAddress'], '')


if __name__ == '__main__':
    MasternodesRpcCreateRewardTest().main()

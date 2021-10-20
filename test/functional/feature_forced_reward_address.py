#!/usr/bin/env python3
# Copyright (c) 2021 The DeFi Blockchain developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

class TestForcedRewardAddress(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=1'],
            ['-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=1'],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    @staticmethod
    def list_unspent_tx(node, address):
        result = []
        vals = node.listunspent()
        for i in range(0, len(vals)):
            if vals[i]['address'] == address:
                result.append(vals[i])
        return result

    @staticmethod
    def unspent_amount(node, address):
        result = 0
        vals = node.listunspent()
        for i in range(0, len(vals)):
            if vals[i]['address'] == address:
                result += vals[i]['amount']
        return result

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_all([self.nodes[0], self.nodes[1]])

        self.log.info("Create new masternode for test...")
        num_mns = len(self.nodes[0].listmasternodes())
        mn_owner = self.nodes[0].getnewaddress("", "legacy")
        self.log.info(mn_owner)

        mn_id = self.nodes[0].createmasternode(mn_owner)
        self.nodes[0].generate(101, 1000000, mn_owner)

        assert_equal(len(self.nodes[0].listmasternodes()), num_mns + 1)
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['rewardAddress'], '')
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['ownerAuthAddress'], mn_owner)
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['operatorAuthAddress'], mn_owner)

        self.log.info("Generate new reward address..")
        forced_reward_address = self.nodes[0].getnewaddress("", "legacy")
        self.log.info(forced_reward_address)

        self.log.info("Set forced reward address..")
        assert_raises_rpc_error(-8,
            "Masternode ownerAddress ({}) is not owned by the wallet".format(mn_owner),
            self.nodes[1].setforcedrewardaddress, mn_id, forced_reward_address
        )
        assert_raises_rpc_error(-8,
            "The masternode {} does not exist".format("some_bab_mn_id"),
            self.nodes[0].setforcedrewardaddress, "some_bab_mn_id", forced_reward_address
        )
        assert_raises_rpc_error(-8,
            "rewardAddress ({}) does not refer to a P2PKH or P2WPKH address".format("some_bab_address"),
            self.nodes[0].setforcedrewardaddress, mn_id, "some_bab_address"
        )

        self.nodes[0].setforcedrewardaddress(mn_id, forced_reward_address)
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].listmasternodes()[mn_id]['rewardAddress'], forced_reward_address)
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['ownerAuthAddress'], mn_owner)
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['operatorAuthAddress'], mn_owner)

        self.log.info("Remove forced reward address...")
        assert_raises_rpc_error(-8,
            "Masternode ownerAddress ({}) is not owned by the wallet".format(mn_owner),
            self.nodes[1].remforcedrewardaddress, mn_id
        )
        assert_raises_rpc_error(-8,
            "The masternode {} does not exist".format("some_bab_mn_id"),
            self.nodes[0].remforcedrewardaddress, "some_bab_mn_id"
        )

        self.nodes[0].remforcedrewardaddress(mn_id)
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].listmasternodes()[mn_id]['rewardAddress'], '')
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['ownerAuthAddress'], mn_owner)
        assert_equal(self.nodes[0].getmasternode(mn_id)[mn_id]['operatorAuthAddress'], mn_owner)

        self.nodes[0].setforcedrewardaddress(mn_id, forced_reward_address)
        self.nodes[0].generate(1)

        fra_amount = self.unspent_amount(self.nodes[0], forced_reward_address)
        fra_unspent = self.list_unspent_tx(self.nodes[0], forced_reward_address)
        assert_equal(len(fra_unspent), 0)
        assert_equal(fra_amount, 0)

        self.log.info("Restarting node with -gen params...")
        self.stop_node(1)
        self.restart_node(0, ['-gen', '-masternode_operator='+mn_owner, '-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=1'])

        self.log.info('Mining blocks...')
        self.nodes[0].generate(300)

        self.nodes[0].remforcedrewardaddress(mn_id)
        self.nodes[0].generate(1)

        assert(len(self.list_unspent_tx(self.nodes[0], forced_reward_address)) > len(fra_unspent))
        assert(self.unspent_amount(self.nodes[0], forced_reward_address) > fra_amount)

        self.log.info("CLI Reward address for test -rewardaddress")
        cli_reward_address = self.nodes[0].getnewaddress("", "legacy")
        self.log.info(cli_reward_address)

        self.restart_node(0, ['-gen', '-masternode_operator='+mn_owner, '-rewardaddress='+cli_reward_address, '-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-fortcanningheight=1'])

        cra_unspent = self.list_unspent_tx(self.nodes[0], cli_reward_address)
        cra_amount = self.unspent_amount(self.nodes[0], cli_reward_address)
        assert_equal(len(cra_unspent), 0)
        assert_equal(cra_amount, 0)

        self.log.info('Mining blocks...')
        self.nodes[0].generate(400)

        assert(len(self.list_unspent_tx(self.nodes[0], cli_reward_address)) > len(fra_unspent))
        assert(self.unspent_amount(self.nodes[0], cli_reward_address) > fra_amount)

if __name__ == '__main__':
    TestForcedRewardAddress().main()

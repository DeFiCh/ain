#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test foundation member migration to database"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error

class FoundationMemberTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.grand_central = 202
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1', f'-grandcentralheight={self.grand_central}', '-subsidytest=1'],
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningcrunchheight=1', f'-grandcentralheight={self.grand_central}', '-subsidytest=1']
        ]

    def run_test(self):

        # Define node 0 foundation address
        address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Move to fork height
        self.nodes[0].generate(101)
        self.sync_blocks()

        # Change address before fork
        assert_raises_rpc_error(-32600, "Cannot be set before GrandCentralHeight", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/foundation/members':[address]}})
        assert_raises_rpc_error(-32600, "Cannot be set before GrandCentralHeight", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/feature/gov-foundation':'true'}})

        # Generate blocks on other node so both nodes have available UTXOs
        self.nodes[1].generate(self.grand_central - self.nodes[0].getblockcount())
        self.sync_blocks()

        # Check addresses now in DB
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/params/foundation/members'], ["bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny","bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu","msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7","mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU"])

        # Add already existant address
        assert_raises_rpc_error(-32600, "Member to add already present", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/foundation/members':[address]}})

        # Remove an address that is not present
        assert_raises_rpc_error(-32600, "Member to remove not present", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/foundation/members':[f"-{self.nodes[0].getnewaddress()}"]}})

        # Add nonsense address
        assert_raises_rpc_error(-5, "Invalid address provided", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/foundation/members':["NOTANADDRESS"]}})

        # Enable foundation address in attributes
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/gov-foundation':'true'}})
        self.nodes[0].generate(1)

        # Remove address
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/foundation/members':[f"-{address}"]}})
        self.nodes[0].generate(1)

        # Check address no longer present
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/params/foundation/members'], ["bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny","bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu","msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7"])

        # Try and add self back in without foundation auth
        assert_raises_rpc_error(-32600, "tx not from foundation member", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/foundation/members':[address]}})

        # Add address in from another node
        self.sync_blocks()
        self.nodes[1].setgov({"ATTRIBUTES":{'v0/params/foundation/members':[address]}})
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Check address present again
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']['v0/params/foundation/members'], ["bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny","bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu","msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7","mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU"])

        # Removing address again, add new address and set another variable
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/foundation/members':[f"-{address}","+2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs"],'v0/params/dfip2201/active':'false'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check address no longer present
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/params/foundation/members'], ["bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny","bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu","2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs","msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7"])
        assert_equal(attributes['v0/params/dfip2201/active'], 'false')

        # Set stored lock
        activation_height = self.nodes[1].getblockcount() + 5
        self.nodes[1].setgovheight({"ATTRIBUTES":{'v0/params/foundation/members':["-2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs","2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2"],'v0/params/dfip2203/active':'false'}}, activation_height)
        self.nodes[1].generate(1)

        # Check pending changes show as expected
        attributes = self.nodes[1].listgovs()[8]
        assert_equal(attributes[1][str(activation_height)]['v0/params/foundation/members'], ["-2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs","2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2"])
        assert_equal(attributes[1][str(activation_height)]['v0/params/dfip2203/active'], 'false')

        # Move to fork height and check changes
        self.nodes[1].generate(5)
        self.sync_blocks()
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/params/foundation/members'], ["bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny","bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu","2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2","msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7"])
        assert_equal(attributes['v0/params/dfip2203/active'], 'false')

        # Check disabling feature restores original usage. Node 0 fails here.
        assert_raises_rpc_error(-32600, "tx not from foundation member", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/foundation/members':[address]}})
        self.nodes[1].setgov({"ATTRIBUTES":{'v0/params/feature/gov-foundation':'false'}})
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Can now add to Gov attributes again. Adding removing address allowed
        # while feature disabled,but addresses will not be used for auth.
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/foundation/members':[address]}})
        self.nodes[0].generate(1)

        # Check address still present
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/params/foundation/members'], ["bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny","bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu","2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2","msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7","mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU"])

if __name__ == '__main__':
    FoundationMemberTest().main()


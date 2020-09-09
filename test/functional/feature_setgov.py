#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- governance variables test
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

class GovsetTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0']]


    def run_test(self):
        # fast check for debug
        print (self.nodes[0].getgov("LP_SPLITS"))
        print (self.nodes[0].getgov("LP_DAILY_DFI_REWARD"))
        # return

        print("Generating initial chain...")
        self.setup_tokens()

        # set|get not existent variable:
        try:
            self.nodes[0].setgov({"REWARD": "any"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not registered" in errorString)
        try:
            self.nodes[0].getgov("REWARD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not registered" in errorString)

        # set LP_SPLITS with total >100%
        try:
            self.nodes[0].setgov({
                "LP_SPLITS": { "0": 0.5, "1": 0.4, "2": 0.2}
                })
        except JSONRPCException as e:
            errorString = e.error['message']
            print (errorString)
        # assert("expected" in errorString)


        self.nodes[0].setgov({
            "LP_SPLITS": { "0": 0.5, "1": 0.4, "2": 0.1}
        })

        self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35.5})
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        print(g1)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        print(g2)


        # set multuple:
        self.nodes[0].setgov({
            "LP_SPLITS": { "4": 1 },
            "LP_DAILY_DFI_REWARD": 45
        })
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        print(g1)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        print(g2)



        # # REVERTING:
        # #========================
        # print ("Reverting...")
        # # Reverting creation!
        # self.start_node(2)
        # self.nodes[3].generate(30)

        # connect_nodes_bi(self.nodes, 0, 3)
        # self.sync_blocks()
        # assert_equal(len(self.nodes[0].listpoolpairs()), 0)

if __name__ == '__main__':
    GovsetTest ().main ()

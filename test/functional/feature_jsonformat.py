#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test jsonformat flag"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

class JsonFormatTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101'],
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101', '-rpcjsonformat=array'],
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101', '-rpcjsonformat=object'],
        ]

    def run_test(self):
        default_listmasternodes = self.nodes[0].listmasternodes({}, False)
        array_listmasternodes = self.nodes[1].listmasternodes({}, False)
        object_listmasternodes = self.nodes[2].listmasternodes({}, False)

        assert(type (array_listmasternodes) is list)
        assert_equal(default_listmasternodes, object_listmasternodes)

        default_listtokens = self.nodes[0].listtokens()
        array_listtokens = self.nodes[1].listtokens()
        object_listtokens = self.nodes[2].listtokens()

        assert(type (array_listtokens) is list)
        assert_equal(default_listtokens, object_listtokens)

        default_listaccounts = self.nodes[0].listaccounts()
        array_listaccounts = self.nodes[1].listaccounts()
        object_listaccounts = self.nodes[2].listaccounts()

        assert(type (object_listaccounts) is dict)
        assert_equal(default_listaccounts, array_listaccounts)

if __name__ == '__main__':
    JsonFormatTest().main ()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, \
    connect_nodes_bi, assert_raises_rpc_error

class AnchorsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-clarkequayheight=0"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-clarkequayheight=0"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1", "-clarkequayheight=0"],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def check_rpc_fails(self):
        print ("Node0: Check fails")
        assert_raises_rpc_error(None, "Not enough money", self.nodes[0].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 1000,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        # Check some params:
        assert_raises_rpc_error(None, "Expected type array, got object", self.nodes[0].spv_createanchor,
            {
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }, "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "txid must be of length 64", self.nodes[0].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963aa",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "value is not an integer", self.nodes[0].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': "aa",
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "Can't parse WIF privkey", self.nodes[0].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "1_cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "does not refer to a P2PKH or P2WPKH address", self.nodes[0].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "__mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        assert_raises_rpc_error(None, "does not refer to a P2PKH or P2WPKH address", self.nodes[0].spv_createanchor,
            [{
                'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
                'vout': 3,
                'amount': 2262303,
                'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"
            }], "")

        # all is Ok, but don't send!
        self.nodes[0].spv_createanchor([{
            'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu", False)
        assert_equal(len(self.nodes[0].spv_listanchors()), 0)


    def run_test(self):
        assert_equal(len(self.nodes[0].listmasternodes()), 8)

        chain0 = 17+15
        self.nodes[0].generate(chain0)
        assert_equal(len(self.nodes[0].spv_listanchors()), 0)

        self.check_rpc_fails()

        # This test only checks spv_createanchor failures. New anchor checks are handled
        # by feature_anchor_rewards.py and feature_anchor_dakota.py.

if __name__ == '__main__':
    AnchorsTest ().main ()

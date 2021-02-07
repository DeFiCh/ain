#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
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

        estimated = self.nodes[0].spv_estimateanchorcost()
        assert_equal(self.nodes[0].spv_estimateanchorcost(2000), estimated+662)
        assert_equal(self.nodes[0].spv_estimateanchorcost(2500), estimated+662+331)

        print ("Node0: Setting anchor")
        self.nodes[0].spv_setlastheight(1)
        txinfo = self.nodes[0].spv_createanchor([{
            'txid': "a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963",
            'vout': 3,
            'amount': 2262303,
            'privkey': "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP"}],
            "mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

        self.nodes[0].spv_setlastheight(10)
        assert_equal(txinfo['defiHash'], self.nodes[0].getblockhash(15))
        assert_equal(txinfo['defiHeight'], 15)
        assert_equal(txinfo['cost'], estimated)

        print ("Anc 0: ", self.nodes[0].spv_listanchors())
        anc0 = self.nodes[0].spv_listanchors()
        assert_equal(anc0[0]['btcTxHash'], txinfo['txHash'])
        assert_equal(anc0[0]['defiBlockHash'], txinfo['defiHash'])
        assert_equal(anc0[0]['defiBlockHeight'], txinfo['defiHeight'])
        assert_equal(anc0[0]['confirmations'], 10)
        assert_equal(anc0[0]['active'], True)

if __name__ == '__main__':
    AnchorsTest ().main ()

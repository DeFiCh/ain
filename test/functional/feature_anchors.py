#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.

- verify basic MN creation and resign
"""

import time

from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import assert_equal, \
    connect_nodes_bi, disconnect_nodes, assert_raises_rpc_error

class AnchorsTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.extra_args = [
            [ "-dummypos=1", "-spv=1", "-fakespv=1"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1"],
            [ "-dummypos=1", "-spv=1", "-fakespv=1"],
        ]
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def dumphashes(self, nodes=None, block = None):
        if nodes is None:
            nodes = range(self.num_nodes)
        for i in nodes:
            bl = self.nodes[i].getblockcount() if block is None else block
            print ("Node%d: [%d] %s" % (i, bl, self.nodes[i].getblockhash(bl)))

    def dumpheights(self):
        print ("Heights:", self.nodes[0].getblockcount(), "\t", self.nodes[1].getblockcount(), "\t", self.nodes[2].getblockcount())
        # pass

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

        disconnect_nodes(self.nodes[0], 1)
        self.nodes[0].generate(17)
        assert_equal(len(self.nodes[0].spv_listanchors()), 0)

        self.check_rpc_fails()

        estimated = self.nodes[0].spv_estimateanchorcost()
        assert_equal(self.nodes[0].spv_estimateanchorcost(2000), estimated+490)
        assert_equal(self.nodes[0].spv_estimateanchorcost(2500), estimated+490+245)

        self.nodes[0].spv_createanchortemplate("mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu")

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

        self.nodes[1].generate(29)
        # print ("Anc 1: ", self.nodes[1].spv_listanchors())
        self.dumpheights()
        connect_nodes_bi(self.nodes, 0, 1)

        time.sleep(2)
        print ("Connect 0 & 1, heights should stay the same")
        self.dumpheights()
        self.dumphashes(block=14)
        self.sync_blocks(self.nodes[1:3], timeout=3)
        assert_equal(self.nodes[0].getblockcount(), 17)
        assert_equal(self.nodes[1].getblockcount(), 29)

        print ("Node1: Set the same anchor")
        self.nodes[1].spv_setlastheight(1)
        self.nodes[1].spv_sendrawtx(txinfo['txHex'])
        self.nodes[1].spv_setlastheight(10)
        # print ("Anc 1: ", self.nodes[1].spv_listanchors())
        anc1 = self.nodes[1].spv_listanchors()
        assert_equal(anc0, anc1)

        print ("Reorg here, 0 & 1 should be equal")
        self.sync_blocks(self.nodes[0:2], timeout=3)
        assert_equal(self.nodes[1].getblockcount(), 17)
        # time.sleep(2)
        self.dumpheights()
        self.dumphashes(block=14)

        print ("Node1: still can generate")
        self.nodes[1].generate(1)
        self.sync_blocks(self.nodes[0:2], timeout=3)
        assert_equal(self.nodes[0].getblockcount(), 18)
        self.dumpheights()
        self.dumphashes([0])

        self.dumpheights()
        self.nodes[2].generate(20)
        # disconnect_nodes(self.nodes[1], 2)
        # connect_nodes_bi(self.nodes, 1, 2)
        time.sleep(1)
        assert_equal(self.nodes[0].getblockcount(), 18)
        assert_equal(self.nodes[1].getblockcount(), 18)
        assert_equal(self.nodes[2].getblockcount(), 49)
        self.dumpheights()
        self.dumphashes([2], 14)

        print ("Node2: Setting anchor")
        self.nodes[2].spv_setlastheight(1)
        txinfo = self.nodes[2].spv_sendrawtx(txinfo['txHex'])
        self.nodes[2].spv_setlastheight(10)
        # print ("Anc 2: ", self.nodes[2].spv_listanchors())
        assert_equal(anc1, self.nodes[2].spv_listanchors())
        # disconnect_nodes(self.nodes[1], 2)
        # connect_nodes_bi(self.nodes, 1, 2)

        self.sync_blocks(self.nodes[0:3], timeout=3)
        assert_equal(self.nodes[2].getblockcount(), 18)

        # time.sleep(1)
        self.dumpheights()
        self.dumphashes([2], 14)

        print ("Node2: Deactivating anchor")
        self.nodes[2].spv_setlastheight(0)
        time.sleep(2)
        # print ("Anc 2: ", self.nodes[2].spv_listanchors())
        self.dumpheights()
        self.dumphashes([2], 14)
        self.dumphashes([2])
        assert_equal(self.nodes[2].getblockcount(), 49)

        print ("Node2: Reactivating anchor")
        self.nodes[2].spv_setlastheight(10)
        # disconnect_nodes(self.nodes[1], 2)
        # connect_nodes_bi(self.nodes, 1, 2)
        # print ("Anc 2: ", self.nodes[2].spv_listanchors())
        self.dumpheights()
        self.dumphashes([2], 14)
        self.sync_blocks(self.nodes[0:3], timeout=3)
        assert_equal(self.nodes[2].getblockcount(), 18)
        assert_equal(anc1, self.nodes[2].spv_listanchors())

        print ("Node2: Deactivating anchor, no peers")
        disconnect_nodes(self.nodes[1], 2)
        self.nodes[2].spv_setlastheight(0)
        time.sleep(2)
        # print ("Anc 2: ", self.nodes[2].spv_listanchors())
        self.dumpheights()
        self.dumphashes([2], 14)
        assert_equal(self.nodes[2].getblockcount(), 49)


if __name__ == '__main__':
    AnchorsTest ().main ()

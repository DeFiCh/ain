#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the masternodes RPC.
"""

from test_framework.blocktools import add_witness_commitment, create_block, create_coinbase
from test_framework.test_framework import BitcoinTestFramework

class SimpleHashTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        tip = node.getbestblockhash()
        height = node.getblockcount() + 1
        block_time = node.getblockheader(tip)["mediantime"] + 1
        block = create_block(int(tip, 16), create_coinbase(height), block_time)
        # block.vtx.append(ctx)
        block.stakeModifier = 1
        block.nHeight = height
        block.nMintedBlocks = 100
        block.rehash()
        block.hashMerkleRoot = block.calc_merkle_root()
        add_witness_commitment(block)
        block.solve()
        node.submitblock(block.serialize().hex())

        self.log.info("Python hash: {}".format(block.hash))
        self.log.info("C++ hash:    {}".format(node.getbestblockhash()))
        # import pprint
        # pp = pprint.PrettyPrinter(indent=4)
        # pp.pprint (node.getblockheader(node.getbestblockhash()))
        assert(block.hash == node.getbestblockhash())
        assert(height == node.getblockcount())

if __name__ == '__main__':
    SimpleHashTest ().main ()

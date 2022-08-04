#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DMC related functionalities

- block serde with dmcPayload"""

import copy
from io import BytesIO

from test_framework.blocktools import (
    create_coinbase,
    TIME_GENESIS_BLOCK,
)
from test_framework.messages import (
    CBlock,
)

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
)
from test_framework.script import CScriptNum


def assert_template(node, block, expect, rehash=True):
    if rehash:
        block.hashMerkleRoot = block.calc_merkle_root()
    rsp = node.getblocktemplate(template_request={'data': block.serialize().hex(), 'mode': 'proposal', 'rules': ['segwit']})
    assert_equal(rsp, expect)

DMC_PAYLOAD = b"\x44\x4D\x43\x20\x52\x4f\x43\x4b\x53" # DMC ROCKS

class DMCTests(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def mine_chain(self):
        self.log.info('Create some old blocks')
        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        for t in range(TIME_GENESIS_BLOCK+1, TIME_GENESIS_BLOCK + 200 * 600 +1, 600):
            self.nodes[0].setmocktime(t)
            self.nodes[0].generatetoaddress(1, address)

        self.restart_node(0)
        connect_nodes_bi(self.nodes, 0, 1)

    def test_testing_framework_serde(self, block):
        self.log.info('test testing framework serde')
        block.rehash()
        blockSerialized = block.serialize()
        blockDeserialized = CBlock()
        blockDeserialized.deserialize(BytesIO(blockSerialized))
        blockDeserialized.rehash()

        assert_equal(blockDeserialized.hash, block.hash)
        assert_equal(blockDeserialized.dmcPayload, block.dmcPayload)
        assert_equal(blockDeserialized.dmcPayload, DMC_PAYLOAD)

    def run_test(self):
        self.mine_chain()
        node = self.nodes[0]

        # Mine a block to leave initial block download
        node.generate(1)
        tmpl = node.getblocktemplate({'rules': ['segwit']})
        self.log.info("getblocktemplate: Test capability advertised")
        assert 'proposal' in tmpl['capabilities']
        assert 'coinbasetxn' not in tmpl

        next_height = int(tmpl["height"])
        coinbase_tx = create_coinbase(height=next_height)
        # sequence numbers must not be max for nLockTime to have effect
        coinbase_tx.vin[0].nSequence = 2 ** 32 - 2
        coinbase_tx.rehash()

        # round-trip the encoded bip34 block height commitment
        assert_equal(CScriptNum.decode(coinbase_tx.vin[0].scriptSig), next_height)
        # round-trip negative and multi-byte CScriptNums to catch python regression
        assert_equal(CScriptNum.decode(CScriptNum.encode(CScriptNum(1500))), 1500)
        assert_equal(CScriptNum.decode(CScriptNum.encode(CScriptNum(-1500))), -1500)
        assert_equal(CScriptNum.decode(CScriptNum.encode(CScriptNum(-1))), -1)

        # construct the block to send
        block = CBlock()
        block.nVersion = tmpl["version"]
        block.hashPrevBlock = int(tmpl["previousblockhash"], 16)
        block.nTime = tmpl["curtime"]
        block.nBits = int(tmpl["bits"], 16)
        block.vtx = [coinbase_tx]
        block.dmcPayload = DMC_PAYLOAD

        # test testing framework serde
        self.test_testing_framework_serde(block)

        self.log.info("getblocktemplate: segwit rule must be set")
        assert_raises_rpc_error(-8, "getblocktemplate must be called with the segwit rule set", node.getblocktemplate)

        self.log.info("getblocktemplate: Test valid block")
        assert_template(node, block, None)

        block.nTime += 1
        block.solve()

        def chain_tip(b_hash, *, status='headers-only', branchlen=1):
            return {'hash': b_hash, 'height': 202, 'branchlen': branchlen, 'status': status}

        node.submitheader(hexdata=block.serialize().hex())
        assert chain_tip(block.hash) in node.getchaintips()

        assert_equal(node.submitblock(hexdata=block.serialize().hex()), None)
        assert_equal(node.submitblock(hexdata=block.serialize().hex()), 'duplicate')
        assert chain_tip(block.hash, status='active', branchlen=0) in node.getchaintips()

        # Fetch the best block and check
        bestBlockHexString  = self.nodes[0].getblock(node.getbestblockhash(), 0)
        blockDeserialized = CBlock()
        blockDeserialized.deserialize(BytesIO(bytearray.fromhex(bestBlockHexString)))
        blockDeserialized.solve()

        assert_equal(blockDeserialized.hash, block.hash)
        assert_equal(blockDeserialized.dmcPayload, block.dmcPayload)
        assert_equal(blockDeserialized.dmcPayload, DMC_PAYLOAD)


if __name__ == '__main__':
    DMCTests().main()

#!/usr/bin/env python3
# Copyright (c) 2016-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test segwit transactions and blocks on P2P network."""
import time

from test_framework.blocktools import create_block, create_coinbase, add_witness_commitment
from test_framework.messages import (
    CBlockHeader,
    CInv,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxInWitness,
    CTxOut,
    MAX_BLOCK_BASE_SIZE,
    NODE_NETWORK,
    NODE_WITNESS,
    msg_no_witness_block,
    msg_getdata,
    msg_headers,
    msg_inv,
    msg_tx,
    msg_block,
    msg_witness_tx,
    ser_uint256,
    sha256,
    uint256_from_str,
)
from test_framework.mininode import (
    P2PInterface,
    mininode_lock,
)
from test_framework.script import (
    CScript,
    CScriptOp,
    OP_0,
    OP_2DROP,
    OP_CHECKSIG,
    OP_DROP,
    OP_DUP,
    OP_EQUALVERIFY,
    OP_HASH160,
    OP_TRUE,
    SegwitVersion1SignatureHash,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    connect_nodes,
    softfork_active,
)

# The versionbit bit used to signal activation of SegWit
VB_WITNESS_BIT = 1
VB_PERIOD = 144
VB_TOP_BITS = 0x20000000

MAX_SIGOP_COST = 80000 * 16

SEGWIT_HEIGHT = 120

class UTXO():
    """Used to keep track of anyone-can-spend outputs that we can use in the tests."""
    def __init__(self, sha256, n, value):
        self.sha256 = sha256
        self.n = n
        self.nValue = value

def get_p2pkh_script(pubkeyhash):
    """Get the script associated with a P2PKH."""
    return CScript([CScriptOp(OP_DUP), CScriptOp(OP_HASH160), pubkeyhash, CScriptOp(OP_EQUALVERIFY), CScriptOp(OP_CHECKSIG)])

def sign_p2pk_witness_input(script, tx_to, in_idx, hashtype, value, key):
    """Add signature for a P2PK witness program."""
    tx_hash = SegwitVersion1SignatureHash(script, tx_to, in_idx, hashtype, value)
    signature = key.sign_ecdsa(tx_hash) + chr(hashtype).encode('latin-1')
    tx_to.wit.vtxinwit[in_idx].scriptWitness.stack = [signature, script]
    tx_to.rehash()

def get_virtual_size(witness_block):
    """Calculate the virtual size of a witness block.

    Virtual size is base + witness/4."""
    base_size = len(witness_block.serialize(with_witness=False))
    total_size = len(witness_block.serialize())
    # the "+3" is so we round up
    vsize = int((3 * base_size + total_size + 3) / 4)
    return vsize

def test_transaction_acceptance(node, p2p, tx, with_witness, accepted, reason=None):
    """Send a transaction to the node and check that it's accepted to the mempool

    - Submit the transaction over the p2p interface
    - use the getrawmempool rpc to check for acceptance."""
    reason = [reason] if reason else []
    with node.assert_debug_log(expected_msgs=reason):
        p2p.send_message(msg_witness_tx(tx) if with_witness else msg_tx(tx))
        p2p.sync_with_ping()
        assert_equal(tx.hash in node.getrawmempool(), accepted)

def test_witness_block(node, p2p, block, accepted, with_witness=True, reason=None):
    """Send a block to the node and check that it's accepted

    - Submit the block over the p2p interface
    - use the getbestblockhash rpc to check for acceptance."""
    reason = [reason] if reason else []
    with node.assert_debug_log(expected_msgs=reason):
        p2p.send_message(msg_block(block) if with_witness else msg_no_witness_block(block))
        p2p.sync_with_ping()
        assert_equal(node.getbestblockhash() == block.hash, accepted)


class TestP2PConn(P2PInterface):
    def __init__(self):
        super().__init__()
        self.getdataset = set()

    def on_getdata(self, message):
        for inv in message.inv:
            self.getdataset.add(inv.hash)

    def announce_tx_and_wait_for_getdata(self, tx, timeout=60, success=True):
        with mininode_lock:
            self.last_message.pop("getdata", None)
        self.send_message(msg_inv(inv=[CInv(1, tx.sha256)]))
        if success:
            self.wait_for_getdata(timeout)
        else:
            time.sleep(timeout)
            assert not self.last_message.get("getdata")

    def announce_block_and_wait_for_getdata(self, block, use_header, timeout=60):
        with mininode_lock:
            self.last_message.pop("getdata", None)
            self.last_message.pop("getheaders", None)
        msg = msg_headers()
        msg.headers = [CBlockHeader(block)]
        if use_header:
            self.send_message(msg)
        else:
            self.send_message(msg_inv(inv=[CInv(2, block.sha256)]))
            self.wait_for_getheaders()
            self.send_message(msg)
        self.wait_for_getdata()

    def request_block(self, blockhash, inv_type, timeout=60):
        with mininode_lock:
            self.last_message.pop("block", None)
        self.send_message(msg_getdata(inv=[CInv(inv_type, blockhash)]))
        self.wait_for_block(blockhash, timeout)
        return self.last_message["block"].block

class SegWitTest2(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        # This test tests SegWit both pre and post-activation, so use the normal BIP9 activation.
        self.extra_args = [
            ["-whitelist=127.0.0.1", "-acceptnonstdtxn=1", "-segwitheight={}".format(SEGWIT_HEIGHT)],
            ["-whitelist=127.0.0.1", "-acceptnonstdtxn=0", "-segwitheight={}".format(SEGWIT_HEIGHT)],
            ["-whitelist=127.0.0.1", "-acceptnonstdtxn=1", "-segwitheight=-1"]
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        self.setup_nodes()
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[0], 2)
        self.sync_all()

    # Helper functions

    def build_next_block(self, version=4):
        """Build a block on top of node0's tip."""
        tip = self.nodes[0].getbestblockhash()
        height = self.nodes[0].getblockcount() + 1
        block_time = self.nodes[0].getblockheader(tip)["mediantime"] + 1
        block = create_block(int(tip, 16), create_coinbase(height), block_time)
        block.nVersion = version
        block.rehash()
        return block

    def update_witness_block_with_transactions(self, block, tx_list, nonce=0):
        """Add list of transactions to block, adds witness commitment, then solves."""
        block.vtx.extend(tx_list)
        add_witness_commitment(block, nonce)
        block.solve()

    def run_test(self):
        # Setup the p2p connections
        # self.test_node sets NODE_WITNESS|NODE_NETWORK
        self.test_node = self.nodes[0].add_p2p_connection(TestP2PConn(), services=NODE_NETWORK | NODE_WITNESS)
        # self.old_node sets only NODE_NETWORK
        self.old_node = self.nodes[0].add_p2p_connection(TestP2PConn(), services=NODE_NETWORK)
        # self.std_node is for testing node1 (fRequireStandard=true)
        self.std_node = self.nodes[1].add_p2p_connection(TestP2PConn(), services=NODE_NETWORK | NODE_WITNESS)

        assert self.test_node.nServices & NODE_WITNESS != 0

        # Keep a place to store utxo's that can be used in later tests
        self.utxo = []

        # self.log.info("Starting tests before segwit activation")
        self.segwit_active = False

        # this is only for providing utxo for next subtest
        self.test_non_witness_transaction()

        self.log.info("Advancing to segwit activation")
        self.advance_to_segwit_active()

        # Segwit status 'active'

        self.test_witness_block_size()

    # Individual tests

    def subtest(func):  # noqa: N805
        """Wraps the subtests for logging and state assertions."""
        def func_wrapper(self, *args, **kwargs):
            self.log.info("Subtest: {} (Segwit active = {})".format(func.__name__, self.segwit_active))
            # Assert segwit status is as expected
            assert_equal(softfork_active(self.nodes[0], 'segwit'), self.segwit_active)
            func(self, *args, **kwargs)
            # Each subtest should leave some utxos for the next subtest
            assert self.utxo
            self.sync_blocks()
            # Assert segwit status is as expected at end of subtest
            assert_equal(softfork_active(self.nodes[0], 'segwit'), self.segwit_active)

        return func_wrapper

    @subtest
    def test_non_witness_transaction(self):
        """See if sending a regular transaction works, and create a utxo to use in later tests."""
        # Mine a block with an anyone-can-spend coinbase,
        # let it mature, then try to spend it.

        block = self.build_next_block(version=1)
        block.solve()
        self.test_node.send_message(msg_no_witness_block(block))
        self.test_node.sync_with_ping()  # make sure the block was processed
        txid = block.vtx[0].sha256

        self.nodes[0].generate(99)  # let the block mature

        # Create a transaction that spends the coinbase
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(txid, 0), b""))
        tx.vout.append(CTxOut(49 * 100000000, CScript([OP_TRUE, OP_DROP] * 15 + [OP_TRUE])))
        tx.calc_sha256()

        # Check that serializing it with or without witness is the same
        # This is a sanity check of our testing framework.
        assert_equal(msg_tx(tx).serialize(), msg_witness_tx(tx).serialize())

        self.test_node.send_message(msg_witness_tx(tx))
        self.test_node.sync_with_ping()  # make sure the tx was processed
        assert tx.hash in self.nodes[0].getrawmempool()
        # Save this transaction for later
        self.utxo.append(UTXO(tx.sha256, 0, 49 * 100000000))
        self.nodes[0].generate(1)

    @subtest
    def advance_to_segwit_active(self):
        """Mine enough blocks to activate segwit."""
        assert not softfork_active(self.nodes[0], 'segwit')
        height = self.nodes[0].getblockcount()
        self.nodes[0].generate(SEGWIT_HEIGHT - height - 2)
        assert not softfork_active(self.nodes[0], 'segwit')
        self.nodes[0].generate(1)
        assert softfork_active(self.nodes[0], 'segwit')
        self.segwit_active = True

    @subtest
    def test_witness_block_size(self):
        # TODO: Test that non-witness carrying blocks can't exceed 1MB
        # Skipping this test for now; this is covered in p2p-fullblocktest.py

        # Test that witness-bearing blocks are limited at ceil(base + wit/4) <= 1MB.
        block = self.build_next_block()

        assert len(self.utxo) > 0

        # Create a P2WSH transaction.
        # The witness program will be a bunch of OP_2DROP's, followed by OP_TRUE.
        # This should give us plenty of room to tweak the spending tx's
        # virtual size.
        NUM_DROPS = 200  # 201 max ops per script!
        NUM_OUTPUTS = 50

        witness_program = CScript([OP_2DROP] * NUM_DROPS + [OP_TRUE])
        witness_hash = uint256_from_str(sha256(witness_program))
        script_pubkey = CScript([OP_0, ser_uint256(witness_hash)])

        prevout = COutPoint(self.utxo[0].sha256, self.utxo[0].n)
        value = self.utxo[0].nValue

        parent_tx = CTransaction()
        parent_tx.vin.append(CTxIn(prevout, b""))
        child_value = int(value / NUM_OUTPUTS)
        for i in range(NUM_OUTPUTS):
            parent_tx.vout.append(CTxOut(child_value, script_pubkey))
        parent_tx.vout[0].nValue -= 50000
        assert parent_tx.vout[0].nValue > 0
        parent_tx.rehash()

        filler_size = 3150
        child_tx = CTransaction()
        for i in range(NUM_OUTPUTS):
            child_tx.vin.append(CTxIn(COutPoint(parent_tx.sha256, i), b""))
        child_tx.vout = [CTxOut(value - 100000, CScript([OP_TRUE]))]
        for i in range(NUM_OUTPUTS):
            child_tx.wit.vtxinwit.append(CTxInWitness())
            child_tx.wit.vtxinwit[-1].scriptWitness.stack = [b'a' * filler_size] * (2 * NUM_DROPS) + [witness_program]
        child_tx.rehash()
        self.update_witness_block_with_transactions(block, [parent_tx, child_tx])

        vsize = get_virtual_size(block)
        assert_greater_than(MAX_BLOCK_BASE_SIZE, vsize)
        additional_bytes = (MAX_BLOCK_BASE_SIZE - vsize) * 4
        i = 0
        while additional_bytes > 0:
            # Add some more bytes to each input until we hit MAX_BLOCK_BASE_SIZE+1
            extra_bytes = min(additional_bytes + 1, 55)
            block.vtx[-1].wit.vtxinwit[int(i / (2 * NUM_DROPS))].scriptWitness.stack[i % (2 * NUM_DROPS)] = b'a' * (filler_size + extra_bytes)
            additional_bytes -= extra_bytes
            i += 1

        block.vtx[0].vout.pop()  # Remove old commitment
        add_witness_commitment(block)
        block.solve()
        vsize = get_virtual_size(block)
        assert_equal(vsize, MAX_BLOCK_BASE_SIZE + 1)
        # Make sure that our test case would exceed the old max-network-message
        # limit
        assert len(block.serialize()) > 2 * 1024 * 1024

        test_witness_block(self.nodes[0], self.test_node, block, accepted=False)

        # Now resize the second transaction to make the block fit.
        cur_length = len(block.vtx[-1].wit.vtxinwit[0].scriptWitness.stack[0])
        block.vtx[-1].wit.vtxinwit[0].scriptWitness.stack[0] = b'a' * (cur_length - 1)
        block.vtx[0].vout.pop()
        add_witness_commitment(block)
        block.solve()
        assert get_virtual_size(block) == MAX_BLOCK_BASE_SIZE

        test_witness_block(self.nodes[0], self.test_node, block, accepted=True)

        # Update available utxo's
        self.utxo.pop(0)
        self.utxo.append(UTXO(block.vtx[-1].sha256, 0, block.vtx[-1].vout[0].nValue))


if __name__ == '__main__':
    SegWitTest2().main()

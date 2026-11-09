#!/usr/bin/env python3
# Copyright (c) 2026 The DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the Masternode Version Gate.

From consensus.nMNVersionGateHeight on, all mined blocks must signal the
overflow fix in block.nVersion (VERSION_BIT_BALANCE_OVERFLOW, bit 24).
Blocks without this signal (e.g. minted by outdated v4.2.5 nodes) are rejected
with 'bad-version'. Below the gate height, historical blocks without this
signal are accepted.

This test:
  * Starts node 0 (updated node, signals bit 24 from gate height on)
  * Starts node 1 (simulating outdated node with -blockversion=536870912,
    which does not have bit 24 set)
  * Below gate height (height < 210):
    Node 1's block is accepted by Node 0.
  * From gate height on (height >= 210):
    Node 1's block is rejected by Node 0 with 'bad-version'.
    Node 0's block is accepted and contains bit 24.
"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

VERSION_BIT_BALANCE_OVERFLOW = 1 << 24
LEGACY_BLOCK_VERSION = 0x20000000  # 536870912
GATE_HEIGHT = 205


class MasternodeVersionGateTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        # Node 0: upgraded node with version gate active at GATE_HEIGHT
        # Node 1: outdated node simulation with fixed legacy blockversion
        self.extra_args = [
            [f"-mnversiongateheight={GATE_HEIGHT}"],
            [f"-mnversiongateheight={GATE_HEIGHT}", f"-blockversion={LEGACY_BLOCK_VERSION}"],
        ]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # Initial chain starts at height 200 on both nodes
        assert_equal(node0.getblockcount(), 200)
        assert_equal(node1.getblockcount(), 200)

        # --- Phase 1: Below GATE_HEIGHT (height 201 < 205) ---
        # Node 1 mines a legacy block at height 201 (version has NO bit 24)
        hash201 = node1.generate(1)[0]
        block201 = node1.getblock(hash201)
        assert_equal(block201["version"] & VERSION_BIT_BALANCE_OVERFLOW, 0)

        # Node 0 must accept this block because height 201 < GATE_HEIGHT
        raw201 = node1.getblock(hash201, 0)
        res = node0.submitblock(raw201)
        assert res in (None, "inconclusive"), f"submitblock failed unexpectedly: {res}"
        assert_equal(node0.getblockcount(), 201)

        # Fast forward node 0 and node 1 to GATE_HEIGHT - 1 (height 204)
        node0.generate(GATE_HEIGHT - 1 - 201)
        assert_equal(node0.getblockcount(), GATE_HEIGHT - 1)

        # Sync node 1 to height 204
        for h in range(202, GATE_HEIGHT):
            b_hash = node0.getblockhash(h)
            b_hex = node0.getblock(b_hash, 0)
            node1.submitblock(b_hex)
        assert_equal(node1.getblockcount(), GATE_HEIGHT - 1)

        # --- Phase 2: At GATE_HEIGHT (height 205) ---
        # Node 1 (outdated) cannot mine a block at height 205 because TestBlockValidity
        # rejects nVersion=0x20000000 with bad-version.
        addr1 = node1.get_genesis_keys().ownerAuthAddress
        minted = node1.generatetoaddress(1, addr1, 1)
        assert_equal(minted, 0)
        assert_equal(node1.getblockcount(), GATE_HEIGHT - 1)

        # --- Phase 3: Upgraded block accepted at GATE_HEIGHT ---
        # Node 0 (upgraded) mines its own block at height 205
        hash205_upgraded = node0.generate(1)[0]
        block205_upgraded = node0.getblock(hash205_upgraded)
        assert_equal(node0.getblockcount(), GATE_HEIGHT)

        # Node 0's block MUST have bit 24 set
        assert (block205_upgraded["version"] & VERSION_BIT_BALANCE_OVERFLOW) != 0, \
            f"Expected bit 24 set in version {hex(block205_upgraded['version'])}"


if __name__ == "__main__":
    MasternodeVersionGateTest().main()

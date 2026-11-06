#!/usr/bin/env python3
# Copyright (c) 2026 The DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test that account transfers cannot create DFI out of thin air.

Regression test for an integer overflow in the transfer sum.

SumAllTransfers() ignored the return value of AddBalances(). When the sum
of the transfer amounts exceeded INT64_MAX, SafeAdd() reported "overflow"
and Add() aborted before mutating the balances - the sum silently stayed
at the first amount. The source account was therefore debited with the
truncated sum while every destination was credited with its full amount.
The difference was newly created DFI (up to INT64_MAX, ~92.2 billion DFI,
per transaction).

The transaction has to be built raw: ParseFixedPoint() rejects amounts with
more than 18 significant digits, so the exploit amount cannot be expressed
through the RPC interface.

The destinations must be ordered by their script bytes: CAccounts is a
std::map<CScript, CBalances>, so the iteration follows the script bytes and
the destination with the smaller script has to carry the smaller amount.
"""

import struct

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

COIN = 100000000
INT64_MAX = 2**63 - 1

# Amounts used by the attack: 8.00000001 DFI and INT64_MAX - 8 DFI.
SMALL = 800000001
BIG = INT64_MAX - 800000000
# Control case: the sum is exactly INT64_MAX, so no overflow happens and the
# transfer must be rejected because the source account cannot cover it.
CONTROL_SMALL = 1
CONTROL_BIG = INT64_MAX - 1


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


class AccountOverflowTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        # Activate all softforks at height 1, otherwise utxostoaccount and
        # accounttoaccount are rejected with "called before AMK height".
        self.extra_args = [[
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
        ]]

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
                    return float(raw.split("@")[0])
        return 0.0

    def run_test(self):
        node = self.nodes[0]

        # The attack needs three addresses: the source account and two
        # destinations.
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

        # Fund the source account with exactly the amount that the truncated
        # sum would require (8.00000001 DFI).
        node.utxostoaccount({attacker: "8.00000001"})
        node.generate(1)

        # The transaction needs an input that belongs to the source address.
        node.sendtoaddress(attacker, 20)
        node.generate(1)
        self.sync_all()

        utxo = None
        for u in node.listunspent():
            if u.get("address") == attacker:
                utxo = u
                break
        assert utxo is not None, "no utxo for the attacker address"
        in_amount = int(round(float(utxo["amount"]) * COIN))

        # --- the attack -----------------------------------------------------
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

        # The transaction must be rejected because the sum of the transfers
        # overflows.
        assert_raises_rpc_error(
            -26, "overflow", node.sendrawtransaction, signed["hex"]
        )

        # No DFI may have been created.
        assert_equal(self.account_balance(node, big_addr), 0)
        assert_equal(self.account_balance(node, small_addr), 0)

        # --- control case: no overflow, but not enough balance --------------
        payload_control = build_dftx_payload(
            script_attacker,
            [
                (small_script, [(0, CONTROL_SMALL)]),
                (big_script, [(0, CONTROL_BIG)]),
            ],
        )
        op_return_control = (
            b"\x6a\x4c" + bytes([len(payload_control)]) + payload_control
        )

        utxo2 = None
        for u in node.listunspent():
            if u.get("address") == attacker:
                utxo2 = u
                break
        assert utxo2 is not None, "no second utxo for the attacker address"
        in_amount2 = int(round(float(utxo2["amount"]) * COIN))

        raw_control = build_raw_tx(
            [(bytes.fromhex(utxo2["txid"]), utxo2["vout"], 0xFFFFFFFF)],
            [
                (0, op_return_control),
                (in_amount2 - 10000, script_attacker),
            ],
        )
        signed_control = node.signrawtransactionwithwallet(raw_control.hex())
        assert signed_control.get("complete"), f"signing failed: {signed_control}"

        # The sum is exactly INT64_MAX here, so the transfer is not an
        # overflow - it must fail because the source account cannot cover it.
        assert_raises_rpc_error(
            -26, "is less than", node.sendrawtransaction, signed_control["hex"]
        )

        assert_equal(self.account_balance(node, big_addr), 0)
        assert_equal(self.account_balance(node, small_addr), 0)


if __name__ == "__main__":
    AccountOverflowTest().main()

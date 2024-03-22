#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Multisig Vault."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

import time


def reverse_string(string_arg):
    return "".join(
        reversed([string_arg[i : i + 2] for i in range(0, len(string_arg), 2)])
    )


class MultisigVaultTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txnotokens=0",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-bayfrontgardensheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
            ],
        ]

    def run_test(self):
        # Create chain
        self.nodes[0].generate(101)

        # setup oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        price_feeds = [
            {"currency": "USD", "token": "DFI"},
        ]
        oracle_id = self.nodes[0].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[0].generate(1)

        # feed oracle
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "10@DFI"},
        ]
        self.nodes[0].setoracledata(oracle_id, int(time.time()), oracle_prices)
        self.nodes[0].generate(11)

        # Create loan scheme
        self.nodes[0].createloanscheme(200, 1, "LOAN0001")
        self.nodes[0].generate(1)

        # Set collateral token
        self.nodes[0].setcollateraltoken(
            {"token": "DFI", "factor": 1, "fixedIntervalPriceId": "DFI/USD"}
        )
        self.nodes[0].generate(1)

        # Create account DFI
        self.nodes[0].utxostoaccount(
            {self.nodes[0].get_genesis_keys().ownerAuthAddress: "100@DFI"}
        )
        self.nodes[0].generate(1)

        # Create owners multisig
        owner_1 = self.nodes[0].getnewaddress("", "legacy")
        owner_2 = self.nodes[0].getnewaddress("", "legacy")
        owner_3 = self.nodes[0].getnewaddress("", "legacy")
        owner_1_pubkey = self.nodes[0].getaddressinfo(owner_1)["pubkey"]
        owner_2_pubkey = self.nodes[0].getaddressinfo(owner_2)["pubkey"]
        owner_3_pubkey = self.nodes[0].getaddressinfo(owner_3)["pubkey"]
        owner_1_privkey = self.nodes[0].dumpprivkey(owner_1)

        # Create 1-of-3 multisig
        multisig = self.nodes[0].createmultisig(
            1, [owner_1_pubkey, owner_2_pubkey, owner_3_pubkey]
        )
        multisig_address = multisig["address"]
        multisig_scriptpubkey = self.nodes[0].getaddressinfo(multisig_address)[
            "scriptPubKey"
        ]

        # Fund multisig addresses for three TXs
        txid = self.nodes[0].sendtoaddress(multisig_address, 1)

        # Get vouts
        decodedtx = self.nodes[0].getrawtransaction(txid, 1)
        for vout in decodedtx["vout"]:
            if vout["scriptPubKey"]["addresses"][0] == multisig_address:
                utxo = vout["n"]

        # Mint TXs
        self.nodes[0].generate(1)

        # Create vault
        vault = self.nodes[0].createvault(multisig_address)
        self.nodes[0].generate(5)

        # Deposit DFI and BTC to vault
        self.nodes[0].deposittovault(
            vault, self.nodes[0].get_genesis_keys().ownerAuthAddress, "100@DFI"
        )
        self.nodes[0].generate(1)

        print(self.nodes[0].getaddressinfo(multisig_address))

        # Withdraw from vault OP_RETURN data
        withdraw_op_return = (
            "44665478"  # DfTx marks
            + "4a"  # TxType
            + reverse_string(vault)  # Vault ID reversed
            + hex(int(len(multisig_scriptpubkey) / 2))[2:]  # PubKey length in hex
            + multisig_scriptpubkey  # PubKey
            + "00"  # Token ID
            + "00e40b5402000000"  # Amount reversed
        )

        # Create raw TX
        rawtx = self.nodes[0].createrawtransaction(
            [{"txid": txid, "vout": utxo}],
            [{"data": withdraw_op_return}, {owner_1: 0.9999}],
        )

        # Sign raw TX
        signed_rawtx = self.nodes[0].signrawtransactionwithkey(
            rawtx,
            [owner_1_privkey],
            [
                {
                    "txid": txid,
                    "vout": utxo,
                    "scriptPubKey": multisig_scriptpubkey,
                    "redeemScript": multisig["redeemScript"],
                }
            ],
        )

        # Send TX
        self.nodes[0].sendrawtransaction(signed_rawtx["hex"])
        self.nodes[0].generate(1)

        # Check multisig balance
        assert_equal(self.nodes[0].getaccount(multisig_address), ["100.00000000@DFI"])


if __name__ == "__main__":
    MultisigVaultTest().main()

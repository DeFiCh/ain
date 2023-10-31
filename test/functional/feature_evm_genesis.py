#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
import os

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256,
)

TESTSDIR = os.path.dirname(os.path.realpath(__file__))


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.nodes[0].importprivkey(self.ethPrivKey)  # ethAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(
            -32600,
            "called before Metachain height",
            self.nodes[0].evmtx,
            self.ethAddress,
            0,
            21,
            21000,
            self.toAddress,
            0.1,
        )

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(1)

    def tx_should_not_go_into_genesis_block(self):
        # Send transferdomain tx on genesis block
        hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)
        # Transferdomain tx should not be minted after genesis block
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]), 1)
        balance = self.nodes[0].eth_getBalance(self.ethAddress)
        assert_equal(balance, int_to_eth_u256(0))
        assert_equal(self.nodes[0].getrawmempool()[0], hash)

        # Transferdomain tx should be minted after genesis block
        self.nodes[0].generate(1)
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]), 2)
        tx_info = block_info["tx"][1]
        assert_equal(tx_info["vm"]["txtype"], "TransferDomain")
        assert_equal(tx_info["txid"], hash)
        balance = self.nodes[0].eth_getBalance(self.ethAddress)
        assert_equal(balance, int_to_eth_u256(100))

    def test_start_state_from_json(self):
        genesis = os.path.join(TESTSDIR, "data/evm-genesis.json")
        self.extra_args = [
            [
                "-ethstartstate={}".format(genesis),
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]
        self.stop_nodes()
        self.start_nodes(self.extra_args)
        self.nodes[0].generate(111)

        ethblock0 = self.nodes[0].eth_getBlockByNumber(0)
        assert_equal(ethblock0["difficulty"], "0x400000")
        assert_equal(ethblock0["extraData"], "0x686f727365")
        assert_equal(ethblock0["gasLimit"], "0x1388")
        assert_equal(
            ethblock0["parentHash"],
            "0x0000000000000000000000000000000000000000000000000000000000000000",
        )
        # NOTE(canonbrother): overwritten by block.header.hash
        # assert_equal(ethblock0['mixHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(ethblock0["nonce"], "0x123123123123123f")
        assert_equal(ethblock0["timestamp"], "0x539")

        balance = self.nodes[0].eth_getBalance(
            "a94f5374fce5edbc8e2a8697c15331677e6ebf0b"
        )
        assert_equal(balance, "0x9184e72a000")

    def run_test(self):
        self.setup()

        self.tx_should_not_go_into_genesis_block()

        self.test_start_state_from_json()


if __name__ == "__main__":
    EVMTest().main()

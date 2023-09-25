#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM mempool behaviour"""
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


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
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address_erc55 = self.nodes[0].addressmap(self.address, 1)["format"][
            "erc55"
        ]
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.toPrivKey = (
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )
        self.nodes[0].importprivkey(self.ethPrivKey)  # ethAddress
        self.nodes[0].importprivkey(self.toPrivKey)  # toAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(
            -32600,
            "called before NextNetworkUpgrade height",
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
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(2)
        self.nodes[0].transferdomain(
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
        self.start_height = self.nodes[0].getblockcount()

    def same_nonce_transferdomain_and_evm_txs(self):
        self.rollback_to(self.start_height)
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        self.nodes[0].evmtx(self.ethAddress, nonce, 21, 21001, self.toAddress, 1)
        self.nodes[0].evmtx(self.ethAddress, nonce, 30, 21001, self.toAddress, 1)
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "nonce": nonce,
                }
            ]
        )
        assert_equal(self.nodes[0].getrawmempool().count(tx), True)
        self.nodes[0].generate(1)
        block_height = self.nodes[0].getblockcount()
        assert_equal(block_height, self.start_height + 1)

    def evm_tx_rbf_with_transferdomain_should_fail(self):
        self.rollback_to(self.start_height)
        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)
        self.nodes[0].transfer_domain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )

        # Ensure transferdomain tx has highest priority over evm tx
        assert_raises_rpc_error(
            -32001,
            "evm-low-fee",
            self.nodes[0].eth_sendTransaction,
            {
                "nonce": self.nodes[0].w3.to_hex(nonce),
                "from": self.address_erc55,
                "to": "0x0000000000000000000000000000000000000000",
                "value": "0x1",
                "gas": "0x100000",
                "gasPrice": "0xfffffffffff",
            },
        )

        # Manually set nonce in transferdomain
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "nonce": nonce + 100,
                }
            ]
        )
        # Ensure transferdomain tx has highest priority over evm tx
        assert_raises_rpc_error(
            -32001,
            "evm-low-fee",
            self.nodes[0].eth_sendTransaction,
            {
                "nonce": self.nodes[0].w3.to_hex(nonce + 100),
                "from": self.address_erc55,
                "to": "0x0000000000000000000000000000000000000000",
                "value": "0x1",
                "gas": "0x100000",
                "gasPrice": "0xfffffffffff",
            },
        )

    def run_test(self):
        self.setup()

        # # Test for transferdomain and evmtx with same nonce
        self.same_nonce_transferdomain_and_evm_txs()


if __name__ == "__main__":
    EVMTest().main()

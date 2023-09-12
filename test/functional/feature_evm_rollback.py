#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, hex_to_decimal


class EVMRolllbackTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
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
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = self.nodes[0].getnewaddress("", "erc55")
        self.toAddress = self.nodes[0].getnewaddress("", "erc55")

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

        self.creationAddress = "0xe61a3a6eb316d773c773f4ce757a542f673023c6"
        self.nodes[0].importprivkey(
            "957ac3be2a08afe1fafb55bd3e1d479c4ae6d7bf1c9b2a0dcc5caad6929e6617"
        )

    def test_rollback_block(self):
        self.nodes[0].generate(1)
        initialBlockHash = self.nodes[0].getbestblockhash()
        blockNumberPreInvalidation = self.nodes[0].eth_blockNumber()
        blockPreInvalidation = self.nodes[0].eth_getBlockByNumber(
            blockNumberPreInvalidation
        )
        assert_equal(blockNumberPreInvalidation, "0x2")
        assert_equal(blockPreInvalidation["number"], blockNumberPreInvalidation)

        self.nodes[0].invalidateblock(initialBlockHash)

        assert_raises_rpc_error(
            -32001,
            "Custom error: header not found",
            self.nodes[0].eth_getBlockByNumber,
            blockNumberPreInvalidation,
        )
        blockByHash = self.nodes[0].eth_getBlockByHash(blockPreInvalidation["hash"])
        assert_equal(blockByHash, None)
        block = self.nodes[0].eth_getBlockByNumber("latest")
        assert_equal(block["number"], "0x1")

        self.nodes[0].reconsiderblock(initialBlockHash)
        blockNumber = self.nodes[0].eth_blockNumber()
        block = self.nodes[0].eth_getBlockByNumber(blockNumber)
        assert_equal(blockNumber, blockNumberPreInvalidation)
        assert_equal(block, blockPreInvalidation)

    def test_rollback_transactions(self):
        initialBlockHash = self.nodes[0].getbestblockhash()

        hash = self.nodes[0].eth_sendTransaction(
            {
                "from": self.ethAddress,
                "to": self.toAddress,
                "value": "0xa",
                "gas": "0x7a120",
                "gasPrice": "0x2540BE400",
            }
        )
        self.nodes[0].generate(1)
        blockHash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        # Check accounting of EVM fees
        tx = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xa",
            "gas": "0x7a120",  # 500_000
            "gasPrice": "0x2540BE400",  # 10_000_000_000,
        }
        fees = self.nodes[0].debug_feeEstimate(tx)
        self.burnt_fee = hex_to_decimal(fees["burnt_fee"])
        self.priority_fee = hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"], self.priority_fee
        )

        blockNumberPreInvalidation = self.nodes[0].eth_blockNumber()
        blockPreInvalidation = self.nodes[0].eth_getBlockByNumber(
            blockNumberPreInvalidation
        )
        assert_equal(blockNumberPreInvalidation, "0x3")
        assert_equal(blockPreInvalidation["number"], blockNumberPreInvalidation)

        txPreInvalidation = self.nodes[0].eth_getTransactionByHash(hash)
        receiptPreInvalidation = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_equal(blockPreInvalidation["transactions"][0], txPreInvalidation["hash"])
        assert_equal(
            blockPreInvalidation["transactions"][0],
            receiptPreInvalidation["transactionHash"],
        )

        self.nodes[0].invalidateblock(initialBlockHash)

        tx = self.nodes[0].eth_getTransactionByHash(hash)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_equal(tx, None)
        assert_equal(receipt, None)

        self.nodes[0].reconsiderblock(initialBlockHash)
        tx = self.nodes[0].eth_getTransactionByHash(hash)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_equal(blockPreInvalidation["transactions"][0], tx["hash"])
        assert_equal(
            blockPreInvalidation["transactions"][0], receipt["transactionHash"]
        )

    def run_test(self):
        self.setup()


        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.creationAddress,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
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

        self.test_rollback_block()

        self.test_rollback_transactions()


if __name__ == "__main__":
    EVMRolllbackTest().main()

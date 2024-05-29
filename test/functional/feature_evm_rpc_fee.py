#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256,
)

from decimal import Decimal

# pragma solidity ^0.8.2;
# contract Multiply {
#     function multiply(uint a, uint b) public pure returns (uint) {
#         return a * b;
#     }
# }
CONTRACT_BYTECODE = "0x608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033"


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
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.nodes[0].importprivkey(
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )  # ethAddress
        self.nodes[0].importprivkey(
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )  # toAddress

        # Generate chain
        self.nodes[0].generate(105)

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
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(balance, int_to_eth_u256(50))

        self.startHeight = self.nodes[0].getblockcount()
        self.priorityFees = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]

    def mine_block_with_eip1559_txs(self, numBlocks):
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for _ in range(numBlocks):
            for priorityFee in self.priorityFees:
                tx = {
                    "from": self.ethAddress,
                    "value": "0x0",
                    "data": CONTRACT_BYTECODE,
                    "gas": "0x18e70",  # 102_000
                    "maxPriorityFeePerGas": hex(priorityFee),
                    "maxFeePerGas": "0x22ecb25c00",  # 150_000_000_000
                    "type": "0x2",
                    "nonce": hex(nonce),
                }
                nonce += 1
                self.nodes[0].eth_sendTransaction(tx)
            self.nodes[0].generate(1)

    # Assumes that block base fee is at initial block base fee = 10_000_000_000
    def mine_block_with_legacy_txs(self):
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for priorityFee in self.priorityFees:
            txFee = 10_000_000_000 + priorityFee
            tx = {
                "from": self.ethAddress,
                "value": "0x0",
                "data": CONTRACT_BYTECODE,
                "gas": "0x18e70",  # 102_000
                "gasPrice": hex(txFee),
                "type": "0x2",
                "nonce": hex(nonce),
            }
            nonce += 1
            self.nodes[0].eth_sendTransaction(tx)
        self.nodes[0].generate(1)

    def test_suggest_priority_fee(self):
        self.rollback_to(self.startHeight)

        numBlocks = 10
        self.mine_block_with_eip1559_txs(numBlocks)

        # Default suggested priority fee calculation is at 60%
        correctPriorityFeeIdx = int((len(self.priorityFees) - 1) * 0.6)
        suggestedFee = self.nodes[0].eth_maxPriorityFeePerGas()
        assert_equal(suggestedFee, hex(self.priorityFees[correctPriorityFeeIdx]))

    def test_incremental_suggest_priority_fee(self):
        self.rollback_to(self.startHeight)

        numBlocks = 20
        priorityFee = 0
        for _ in range(numBlocks):
            nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
            for _ in range(10):
                tx = {
                    "from": self.ethAddress,
                    "value": "0x0",
                    "data": CONTRACT_BYTECODE,
                    "gas": "0x18e70",  # 102_000
                    "maxPriorityFeePerGas": hex(priorityFee),
                    "maxFeePerGas": "0x22ecb25c00",  # 150_000_000_000
                    "type": "0x2",
                    "nonce": hex(nonce),
                }
                nonce += 1
                priorityFee += 1
                self.nodes[0].eth_sendTransaction(tx)
            self.nodes[0].generate(1)

        # Default suggested priority fee calculation is at 60%
        priorityFee -= 1
        correctPriorityFee = int(priorityFee * 0.6)
        suggestedFee = self.nodes[0].eth_maxPriorityFeePerGas()
        assert_equal(suggestedFee, hex(correctPriorityFee))

    def test_suggest_priority_fee_empty_blocks(self):
        self.rollback_to(self.startHeight)

        # generate empty blocks
        self.nodes[0].generate(20)
        suggested_fee = self.nodes[0].eth_maxPriorityFeePerGas()
        assert_equal("0x0", suggested_fee)

    def test_fee_history_eip1559_txs(self):
        self.rollback_to(self.startHeight)

        numBlocks = 10
        self.mine_block_with_eip1559_txs(numBlocks)

        current = self.nodes[0].eth_blockNumber()
        rewardPercentiles = [20, 30, 50, 70, 85, 100]

        history = self.nodes[0].eth_feeHistory(
            hex(numBlocks), "latest", rewardPercentiles
        )
        assert_equal(history["oldestBlock"], hex(int(current, 16) - numBlocks + 1))
        # Include next block base fee
        assert_equal(len(history["baseFeePerGas"]), numBlocks + 1)

        startNum = int(current, 16) - numBlocks + 1
        for baseFee in history["baseFeePerGas"]:
            block = self.nodes[0].eth_getBlockByNumber(hex(startNum))
            assert_equal(block["baseFeePerGas"], baseFee)

        for gasUsedRatio in history["gasUsedRatio"]:
            assert_equal(Decimal(str(gasUsedRatio)), Decimal("0.033868333333333334"))

        assert_equal(len(history["reward"]), numBlocks)
        for reward in history["reward"]:
            assert_equal(len(reward), len(rewardPercentiles))
            assert_equal(reward, ["0x2", "0x3", "0x5", "0x7", "0x9", "0xa"])

    def test_fee_history_legacy_txs(self):
        self.rollback_to(self.startHeight)

        self.mine_block_with_legacy_txs()
        current = self.nodes[0].eth_blockNumber()
        rewardPercentiles = [20, 30, 50, 70, 85, 100]

        history = self.nodes[0].eth_feeHistory(hex(1), "latest", rewardPercentiles)
        assert_equal(history["oldestBlock"], hex(int(current, 16)))
        # Include next block base fee
        assert_equal(len(history["baseFeePerGas"]), 2)

        block = self.nodes[0].eth_getBlockByNumber("latest")
        assert_equal(block["baseFeePerGas"], history["baseFeePerGas"][0])
        assert_equal(
            Decimal(str(history["gasUsedRatio"][0])), Decimal("0.033868333333333334")
        )
        assert_equal(len(history["reward"]), 1)
        assert_equal(history["reward"][0], ["0x2", "0x3", "0x5", "0x7", "0x9", "0xa"])

    def test_fee_history_empty_percentile(self):
        self.rollback_to(self.startHeight)

        numBlocks = 10
        self.mine_block_with_eip1559_txs(numBlocks)

        current = self.nodes[0].eth_blockNumber()
        rewardPercentiles = []

        history = self.nodes[0].eth_feeHistory(
            hex(numBlocks), "latest", rewardPercentiles
        )
        assert_equal(history["oldestBlock"], hex(int(current, 16) - numBlocks + 1))
        # Include next block base fee
        assert_equal(len(history["baseFeePerGas"]), numBlocks + 1)

        startNum = int(current, 16) - numBlocks + 1
        for baseFee in history["baseFeePerGas"]:
            block = self.nodes[0].eth_getBlockByNumber(hex(startNum))
            assert_equal(block["baseFeePerGas"], baseFee)

        for gasUsedRatio in history["gasUsedRatio"]:
            assert_equal(Decimal(str(gasUsedRatio)), Decimal("0.033868333333333334"))

        assert_equal(history["reward"], None)

    def test_invalid_fee_history_rpc(self):
        self.rollback_to(self.startHeight)

        numBlocks = 10
        self.mine_block_with_eip1559_txs(numBlocks)
        rewardPercentiles = []
        aboveLimitPercentiles = [101, 20, 30, 40, 100]
        belowLimitPercentiles = [-1, 20, 30, 40, 100]
        notIncreasingPercentiles = [10, 20, 30, 50, 40, 100]
        tooManyPercentiles = [0]
        for i in range(100):
            tooManyPercentiles.append(i)

        # Test invalid feeHistory call, block count set as 0
        assert_raises_rpc_error(
            -32001,
            "Block count requested smaller than minimum allowed range of 1",
            self.nodes[0].eth_feeHistory,
            hex(0),
            "latest",
            rewardPercentiles,
        )

        # Test invalid feeHistory call, block count set past limit
        assert_raises_rpc_error(
            -32001,
            "Block count requested larger than maximum allowed range of 1024",
            self.nodes[0].eth_feeHistory,
            hex(1025),
            "latest",
            rewardPercentiles,
        )

        # Test invalid feeHistory call, percentiles list exceed max size
        assert_raises_rpc_error(
            -32001,
            "List of percentile values exceeds maximum allowed size of 100",
            self.nodes[0].eth_feeHistory,
            hex(numBlocks),
            "latest",
            tooManyPercentiles,
        )

        # Test invalid feeHistory call, percentile value less than inclusive range
        assert_raises_rpc_error(
            -32001,
            "Percentile value less than inclusive range of 0",
            self.nodes[0].eth_feeHistory,
            hex(numBlocks),
            "latest",
            belowLimitPercentiles,
        )

        # Test invalid feeHistory call, percentile value exceed inclusive range
        assert_raises_rpc_error(
            -32001,
            "Percentile value more than inclusive range of 100",
            self.nodes[0].eth_feeHistory,
            hex(numBlocks),
            "latest",
            aboveLimitPercentiles,
        )

        # Test invalid feeHistory call, percentile values not monotonically increasing
        assert_raises_rpc_error(
            -32001,
            "List of percentile values are not monotonically increasing",
            self.nodes[0].eth_feeHistory,
            hex(numBlocks),
            "latest",
            notIncreasingPercentiles,
        )

    def run_test(self):
        self.setup()

        self.nodes[0].generate(1)

        self.test_suggest_priority_fee()

        self.test_incremental_suggest_priority_fee()

        # Also checks rollback pipeline to ensure cache is cleared
        self.test_suggest_priority_fee_empty_blocks()

        self.test_fee_history_eip1559_txs()

        self.test_fee_history_legacy_txs()

        self.test_fee_history_empty_percentile()

        self.test_invalid_fee_history_rpc()


if __name__ == "__main__":
    EVMTest().main()

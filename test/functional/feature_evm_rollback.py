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
                "-ethdebug=1",
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
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        self.startBlockNum = self.nodes[0].eth_blockNumber()
        self.startBlock = self.nodes[0].eth_getBlockByNumber(self.startBlockNum)
        self.start_height = self.nodes[0].getblockcount()

    def test_rollback_block(self):
        self.rollback_to(self.start_height)
        assert_equal(self.nodes[0].eth_blockNumber(), self.startBlockNum)
        assert_equal(self.nodes[0].eth_getBlockByNumber("latest"), self.startBlock)

        self.nodes[0].generate(1)
        nextblockHash = self.nodes[0].getbestblockhash()
        nextBlockNum = self.nodes[0].eth_blockNumber()
        nextBlock = self.nodes[0].eth_getBlockByNumber(nextBlockNum)
        assert_equal(nextBlock["number"], nextBlockNum)

        self.nodes[0].invalidateblock(nextblockHash)
        assert_raises_rpc_error(
            -32001,
            "Custom error: header not found",
            self.nodes[0].eth_getBlockByNumber,
            nextBlockNum,
        )
        blockByHash = self.nodes[0].eth_getBlockByHash(nextBlock["hash"])
        assert_equal(blockByHash, None)

        currBlockNum = self.nodes[0].eth_blockNumber()
        currBlock = self.nodes[0].eth_getBlockByNumber(currBlockNum)
        assert_equal(currBlockNum, self.startBlockNum)
        assert_equal(currBlock, self.startBlock)

        self.nodes[0].reconsiderblock(nextblockHash)
        reconsiderBlockNum = self.nodes[0].eth_blockNumber()
        reconsiderBlock = self.nodes[0].eth_getBlockByNumber(reconsiderBlockNum)
        assert_equal(reconsiderBlockNum, nextBlockNum)
        assert_equal(reconsiderBlock, nextBlock)

    def test_rollback_transactions(self):
        self.rollback_to(self.start_height)
        assert_equal(self.nodes[0].eth_blockNumber(), self.startBlockNum)
        assert_equal(self.nodes[0].eth_getBlockByNumber("latest"), self.startBlock)

        txHash = self.nodes[0].eth_sendTransaction(
            {
                "from": self.ethAddress,
                "to": self.toAddress,
                "value": "0xa",
                "gas": "0x7a120",
                "gasPrice": "0x2540BE400",
            }
        )
        self.nodes[0].generate(1)
        currBlockNum = self.nodes[0].getblockcount()
        currblockHash = self.nodes[0].getblockhash(currBlockNum)

        # Check accounting of EVM fees
        txInfo = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xa",
            "gas": "0x7a120",  # 500_000
            "gasPrice": "0x2540BE400",  # 10_000_000_000,
        }
        fees = self.nodes[0].debug_feeEstimate(txInfo)
        self.burntFee = hex_to_decimal(fees["burnt_fee"])
        self.priorityFee = hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burntFee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burntFee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], currblockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burntFee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], currblockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priorityFee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"], self.priorityFee
        )

        evmBlockNum = self.nodes[0].eth_blockNumber()
        evmBlock = self.nodes[0].eth_getBlockByNumber(evmBlockNum)
        assert_equal(evmBlock["number"], evmBlockNum)

        tx = self.nodes[0].eth_getTransactionByHash(txHash)
        txReceipt = self.nodes[0].eth_getTransactionReceipt(txHash)
        assert_equal(evmBlock["transactions"][0], tx["hash"])
        assert_equal(
            evmBlock["transactions"][0],
            txReceipt["transactionHash"],
        )

        # Check that chain tip is back to the starting block
        self.nodes[0].invalidateblock(currblockHash)
        currEvmBlockNum = self.nodes[0].eth_blockNumber()
        currEvmBlock = self.nodes[0].eth_getBlockByNumber(currEvmBlockNum)
        assert_equal(currEvmBlockNum, self.startBlockNum)
        assert_equal(currEvmBlock, self.startBlock)

        # Check that txs are no longer valid
        tx = self.nodes[0].eth_getTransactionByHash(txHash)
        receipt = self.nodes[0].eth_getTransactionReceipt(txHash)
        assert_equal(tx, None)
        assert_equal(receipt, None)

        self.nodes[0].reconsiderblock(currblockHash)
        reconsiderBlockNum = self.nodes[0].eth_blockNumber()
        reconsiderBlock = self.nodes[0].eth_getBlockByNumber(reconsiderBlockNum)
        tx = self.nodes[0].eth_getTransactionByHash(txHash)
        receipt = self.nodes[0].eth_getTransactionReceipt(txHash)
        assert_equal(reconsiderBlockNum, evmBlockNum)
        assert_equal(reconsiderBlock, evmBlock)
        assert_equal(reconsiderBlock["transactions"][0], tx["hash"])
        assert_equal(
            reconsiderBlock["transactions"][0],
            receipt["transactionHash"],
        )

    def test_state_rollback(self):
        self.rollback_to(self.start_height)
        assert_equal(self.nodes[0].eth_blockNumber(), self.startBlockNum)
        assert_equal(self.nodes[0].eth_getBlockByNumber("latest"), self.startBlock)
        startBlockHash = self.nodes[0].getbestblockhash()
        startdbdump = self.nodes[0].dumpevmdb({"dumparg": "all"})
        assert_equal((startdbdump is not None), True)

        evmAddresses = []
        numEvmAddresses = 10
        for i in range(numEvmAddresses):
            evmAddresses.append(self.nodes[0].getnewaddress("", "erc55"))

        # Transferdomain txs
        tdHashes = []
        for i in range(numEvmAddresses):
            hash = self.nodes[0].transferdomain(
                [
                    {
                        "src": {
                            "address": self.address,
                            "amount": "10@DFI",
                            "domain": 2,
                        },
                        "dst": {
                            "address": evmAddresses[i],
                            "amount": "10@DFI",
                            "domain": 3,
                        },
                        "singlekeycheck": False,
                    }
                ]
            )
            tdHashes.append(hash)
        self.nodes[0].generate(1)

        # first block (transferdomain txs)
        firstBlockNum = self.nodes[0].eth_blockNumber()
        firstBlock = self.nodes[0].eth_getBlockByNumber(firstBlockNum)
        firstBlockHash = self.nodes[0].getbestblockhash()
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        tdtx_id = 0
        for tx_info in block_info["tx"][1:]:
            if tx_info["vm"]["txtype"] == "TransferDomain":
                # Check that all transferdomain txs are minted in the first block
                assert_equal(tx_info["txid"], tdHashes[tdtx_id])
                tdtx_id += 1
        assert_equal(tdtx_id, numEvmAddresses)
        firstdbdump = self.nodes[0].dumpevmdb({"dumparg": "all"})
        assert_equal((firstdbdump is not None), True)

        # second block (transfer txs)
        hashes = []
        counts = [0] * numEvmAddresses
        for i in range(numEvmAddresses):
            for j in range(numEvmAddresses):
                if i == j:
                    continue

                hash = self.nodes[0].eth_sendTransaction(
                    {
                        "nonce": hex(counts[i]),
                        "from": evmAddresses[i],
                        "to": evmAddresses[j],
                        "value": "0xDE0B6B3A7640000",  # 1 DFI
                        "gas": "0x5209",
                        "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                    }
                )
                counts[i] += 1
                hashes.append(hash)

        self.nodes[0].generate(1)

        # Second block (evm txs)
        secondBlockNum = self.nodes[0].eth_blockNumber()
        secondBlock = self.nodes[0].eth_getBlockByNumber(secondBlockNum)
        secondBlockHash = self.nodes[0].getbestblockhash()
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        for idx, tx_info in enumerate(block_info["tx"][1:]):
            assert_equal(tx_info["vm"]["vmtype"], "evm")
            assert_equal(tx_info["vm"]["txtype"], "Evm")
            assert_equal(tx_info["vm"]["msg"]["hash"], hashes[idx][2:])
        assert_equal(len(block_info["tx"][1:]), numEvmAddresses * (numEvmAddresses - 1))
        seconddbdump = self.nodes[0].dumpevmdb({"dumparg": "all"})
        assert_equal((seconddbdump is not None), True)

        # Invalidate second block
        self.nodes[0].invalidateblock(secondBlockHash)
        blockByHash = self.nodes[0].eth_getBlockByHash(secondBlock["hash"])
        assert_equal(blockByHash, None)
        currBlockNum = self.nodes[0].eth_blockNumber()
        currBlock = self.nodes[0].eth_getBlockByNumber(currBlockNum)
        currBlockHash = self.nodes[0].getbestblockhash()
        assert_equal(currBlockNum, firstBlockNum)
        assert_equal(currBlock, firstBlock)
        assert_equal(currBlockHash, firstBlockHash)
        currdbdump = self.nodes[0].dumpevmdb({"dumparg": "all"})
        assert_equal((currdbdump is not None), True)
        assert_equal(currdbdump, firstdbdump)

        # Invalidate first block
        self.nodes[0].invalidateblock(firstBlockHash)
        blockByHash = self.nodes[0].eth_getBlockByHash(firstBlock["hash"])
        assert_equal(blockByHash, None)
        currBlockNum = self.nodes[0].eth_blockNumber()
        currBlock = self.nodes[0].eth_getBlockByNumber(currBlockNum)
        currBlockHash = self.nodes[0].getbestblockhash()
        assert_equal(currBlockNum, self.startBlockNum)
        assert_equal(currBlock, self.startBlock)
        assert_equal(currBlockHash, startBlockHash)
        currdbdump = self.nodes[0].dumpevmdb({"dumparg": "all"})
        assert_equal((currdbdump is not None), True)
        assert_equal(currdbdump, startdbdump)

        # Reconsider blocks
        self.nodes[0].reconsiderblock(firstBlockHash)
        reconsiderBlockNum = self.nodes[0].eth_blockNumber()
        reconsiderBlock = self.nodes[0].eth_getBlockByNumber(reconsiderBlockNum)
        reconsiderBlockHash = self.nodes[0].getbestblockhash()
        assert_equal(reconsiderBlockNum, secondBlockNum)
        assert_equal(reconsiderBlock, secondBlock)
        assert_equal(reconsiderBlockHash, secondBlockHash)
        currdbdump = self.nodes[0].dumpevmdb({"dumparg": "all"})
        assert_equal((currdbdump is not None), True)
        assert_equal(currdbdump, seconddbdump)

    def run_test(self):
        self.setup()

        self.test_rollback_block()

        self.test_rollback_transactions()

        self.test_state_rollback()


if __name__ == "__main__":
    EVMRolllbackTest().main()

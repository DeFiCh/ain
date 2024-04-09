#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256,
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
                "-metachainheight=105",
                "-subsidytest=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.evm_key_pair = EvmKeyPair.from_node(self.nodes[0])
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(
            -32600,
            "called before Metachain height",
            self.nodes[0].evmtx,
            self.evm_key_pair.address,
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

    def setup_transferdomain(self):
        # Test get logs for contract deployment
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.td_address = "0xdf00000000000000000000000000000000000001"
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(self.evm_key_pair.address, "latest")
        assert_equal(balance, int_to_eth_u256(50))

        self.num_td_logs = 2
        logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": "earliest",
                "toBlock": "latest",
                "address": self.td_address,
            }
        )
        assert_equal(len(logs), self.num_td_logs)

    def deploy_contract(self):
        self.contract = EVMContract.from_file("Events.sol", "TestEvents")
        abi, bytecode, _ = self.contract.compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)
        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 500_000,
                "gas": 1_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        self.contract_address = receipt["contractAddress"]
        self.contract = self.nodes[0].w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def block_with_contract_calls(self, count):
        hashes = []
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.evm_key_pair.address)
        for i in range(count):
            tx = self.contract.functions.store(10).build_transaction(
                {
                    "chainId": self.nodes[0].w3.eth.chain_id,
                    "nonce": nonce + i,
                    "gasPrice": 10_000_000_000,
                }
            )
            signed = self.nodes[0].w3.eth.account.sign_transaction(
                tx, self.evm_key_pair.privkey
            )
            hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
            hashes.append(hash)

        self.nodes[0].generate(1)

        # Verify receipts
        for hash in hashes:
            receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
            assert_equal(len(receipt["logs"]), 1)

    def create_blocks(self):
        # Create 5 blocks
        self.num_blocks = 5
        self.num_logs_in_each_block = 10
        for _ in range(self.num_blocks):
            self.block_with_contract_calls(self.num_logs_in_each_block)

    def test_get_logs_rpc(self):
        self.rollback_to(self.start_height)

        # Populate fromBlock and toBlock with string default parameters
        logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": "earliest",
                "toBlock": "latest",
            }
        )
        assert_equal(
            len(logs), self.num_td_logs + self.num_blocks * self.num_logs_in_each_block
        )

        # Populate fromBlock and toBlock field with decimal values
        curr_block = int(self.nodes[0].eth_blockNumber(), 16)
        logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
            }
        )
        assert_equal(
            len(logs), self.num_td_logs + self.num_blocks * self.num_logs_in_each_block
        )

        # Populate fromBlock and toBlock field with future block numbers
        logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": curr_block + 1,
                "toBlock": curr_block + 500,
            }
        )
        assert_equal(len(logs), 0)

        # Populate block hash
        curr_block_hash = self.nodes[0].eth_getBlockByNumber(curr_block)["hash"]
        logs = self.nodes[0].eth_getLogs(
            {
                "blockHash": curr_block_hash,
            }
        )
        assert_equal(len(logs), self.num_logs_in_each_block)

        # Populate address field
        contract_logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
            }
        )
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)

        contract_logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": [self.contract_address],
            }
        )
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)

        # Populate topics field
        topics = contract_logs[0]["topics"]
        contract_logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": topics,
            }
        )
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)

        nested_topics = []
        for topic in topics:
            nested_topics.append([topic])
        contract_logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": nested_topics,
            }
        )
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)

        nested_single_topic = [[topics[0]]]
        contract_logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": nested_single_topic,
            }
        )
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)

        many_nested_topics = []
        for _ in range(len(topics)):
            many_nested_topics.append(topics)
        contract_logs = self.nodes[0].eth_getLogs(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": many_nested_topics,
            }
        )
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)

    def test_get_filter_logs_rpc(self):
        self.rollback_to(self.start_height)
        self.filter_log_ids = []

        # Populate fromBlock and toBlock with string default parameters
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": "earliest",
                "toBlock": "latest",
            }
        )
        logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(
            len(logs), self.num_td_logs + self.num_blocks * self.num_logs_in_each_block
        )
        self.filter_log_ids.append(id)
        self.last_id += 1

        # Populate fromBlock and toBlock field with decimal values
        curr_block = int(self.nodes[0].eth_blockNumber(), 16)
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
            }
        )
        logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(
            len(logs), self.num_td_logs + self.num_blocks * self.num_logs_in_each_block
        )
        self.filter_log_ids.append(id)
        self.last_id += 1

        # Populate address field
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        self.filter_log_ids.append(id)
        self.last_id += 1

        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": [self.contract_address],
            }
        )
        contract_logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        self.filter_log_ids.append(id)
        self.last_id += 1

        # Populate topics field
        topics = contract_logs[0]["topics"]
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": topics,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        self.filter_log_ids.append(id)
        self.last_id += 1

        nested_topics = []
        for topic in topics:
            nested_topics.append([topic])
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": nested_topics,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        self.filter_log_ids.append(id)
        self.last_id += 1

        nested_single_topic = [[topics[0]]]
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": nested_single_topic,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        self.filter_log_ids.append(id)
        self.last_id += 1

        many_nested_topics = []
        for _ in range(len(topics)):
            many_nested_topics.append(topics)
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": curr_block,
                "address": self.contract_address,
                "topics": many_nested_topics,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterLogs(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        self.filter_log_ids.append(id)
        self.last_id += 1

    def test_uninstall_filter_logs_rpc(self):
        self.rollback_to(self.start_height)

        for id in self.filter_log_ids:
            res = self.nodes[0].eth_uninstallFilter(id)
            assert_equal(res, True)

    def test_get_filter_changes_logs_rpc(self):
        self.rollback_to(self.start_height)
        ids = []

        # Populate fromBlock and toBlock field with decimal values
        curr_block = int(self.nodes[0].eth_blockNumber(), 16)
        target_block = curr_block + self.num_blocks
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
            }
        )
        logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(
            len(logs), self.num_td_logs + self.num_blocks * self.num_logs_in_each_block
        )
        self.last_id += 1

        # Populate address field
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
                "address": self.contract_address,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        ids.append(id)
        self.last_id += 1

        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
                "address": [self.contract_address],
            }
        )
        contract_logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        ids.append(id)
        self.last_id += 1

        # Populate topics field
        topics = contract_logs[0]["topics"]
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
                "address": self.contract_address,
                "topics": topics,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        ids.append(id)
        self.last_id += 1

        nested_topics = []
        for topic in topics:
            nested_topics.append([topic])
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
                "address": self.contract_address,
                "topics": nested_topics,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        ids.append(id)
        self.last_id += 1

        nested_single_topic = [[topics[0]]]
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
                "address": self.contract_address,
                "topics": nested_single_topic,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        ids.append(id)
        self.last_id += 1

        many_nested_topics = []
        for _ in range(len(topics)):
            many_nested_topics.append(topics)
        id = self.nodes[0].eth_newFilter(
            {
                "fromBlock": 0,
                "toBlock": target_block,
                "address": self.contract_address,
                "topics": many_nested_topics,
            }
        )
        contract_logs = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(int(id, 16), self.last_id)
        assert_equal(len(contract_logs), self.num_blocks * self.num_logs_in_each_block)
        ids.append(id)
        self.last_id += 1

        # Mint blocks to test for changes
        self.create_blocks()
        for id in ids:
            contract_logs = self.nodes[0].eth_getFilterChanges(id)
            # Assert only logs from the minted blocks is returned
            assert_equal(
                len(contract_logs), self.num_blocks * self.num_logs_in_each_block
            )
            total_contract_logs = self.nodes[0].eth_getFilterLogs(id)
            # Assert all logs from the contract address is returned
            assert_equal(
                len(total_contract_logs),
                2 * self.num_blocks * self.num_logs_in_each_block,
            )

    def test_get_filter_changes_blocks_rpc(self):
        self.rollback_to(self.start_height)

        id = self.nodes[0].eth_newBlockFilter()
        blocks = self.nodes[0].eth_getFilterChanges(id)
        # Assert empty
        assert_equal(len(blocks), 0)

        # Create blocks
        self.create_blocks()
        blocks = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(len(blocks), self.num_blocks)

        # Get changes again, assert empty
        blocks = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(len(blocks), 0)

        # Create blocks twice
        self.create_blocks()
        self.create_blocks()
        blocks = self.nodes[0].eth_getFilterChanges(id)
        assert_equal(len(blocks), self.num_blocks * 2)

    def test_get_filter_changes_txs_rpc(self):
        self.rollback_to(self.start_height)

    def test_invalid_get_logs_rpc(self):
        self.rollback_to(self.start_height)
        curr_block = int(self.nodes[0].eth_blockNumber(), 16)
        blockhash = self.nodes[0].eth_getBlockByNumber(curr_block)["hash"]

        # Populate both blockHash and block range
        assert_raises_rpc_error(
            -32001,
            "invalid filter",
            self.nodes[0].eth_getLogs,
            {"blockHash": blockhash, "fromBlock": "0x1", "toBlock": "0x0"},
        )

        # Invalid block range
        assert_raises_rpc_error(
            -32001,
            "fromBlock is greater than toBlock",
            self.nodes[0].eth_getLogs,
            {"fromBlock": "0x1", "toBlock": "0x0"},
        )

        # Exceed max topics limit
        topics = self.nodes[0].eth_getLogs({"fromBlock": "latest"})[0]["topics"]
        assert_equal(len(topics) > 0, True)
        invalid_topics = []
        invalid_topics.append(topics[0])
        invalid_topics.append(topics[0])
        for topic in topics:
            invalid_topics.append(topic)
        assert_equal(len(invalid_topics) > 4, True)

        assert_raises_rpc_error(
            -32001,
            "exceed max topics",
            self.nodes[0].eth_getLogs,
            {"topics": invalid_topics},
        )

        # Exceed max block range limit
        assert_raises_rpc_error(
            -32001,
            "block range exceed max limit",
            self.nodes[0].eth_getLogs,
            # 0 to 2001, invalid as default max range = 2000
            {"fromBlock": "0x0", "toBlock": "0x7D1"},
        )

    def test_invalid_get_filter_logs_rpc(self):
        self.rollback_to(self.start_height)

        # Invalid block range
        assert_raises_rpc_error(
            -32001,
            "fromBlock is greater than toBlock",
            self.nodes[0].eth_newFilter,
            {"fromBlock": "0x1", "toBlock": "0x0"},
        )

        # Exceed max topics limit
        topics = self.nodes[0].eth_getLogs({"fromBlock": "latest"})[0]["topics"]
        assert_equal(len(topics) > 0, True)
        invalid_topics = []
        invalid_topics.append(topics[0])
        invalid_topics.append(topics[0])
        for topic in topics:
            invalid_topics.append(topic)
        assert_equal(len(invalid_topics) > 4, True)

        assert_raises_rpc_error(
            -32001,
            "exceed max topics",
            self.nodes[0].eth_newFilter,
            {"topics": invalid_topics},
        )

        # Exceed max block range limit
        assert_raises_rpc_error(
            -32001,
            "block range exceed max limit",
            self.nodes[0].eth_newFilter,
            # 0 to 2001, invalid as default max range = 2000
            {"fromBlock": "0x0", "toBlock": "0x7D1"},
        )

    def run_test(self):
        self.setup()

        self.setup_transferdomain()

        self.deploy_contract()

        self.create_blocks()

        # Set starting test state
        self.start_height = self.nodes[0].getblockcount()
        self.last_id = 1

        self.test_get_logs_rpc()

        self.test_get_filter_logs_rpc()

        self.test_uninstall_filter_logs_rpc()

        self.test_get_filter_changes_logs_rpc()

        self.test_get_filter_changes_blocks_rpc()

        self.test_get_filter_changes_txs_rpc()

        self.test_invalid_get_logs_rpc()

        self.test_invalid_get_filter_logs_rpc()


if __name__ == "__main__":
    EVMTest().main()

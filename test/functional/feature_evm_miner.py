#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
from test_framework.evm_contract import EVMContract
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
from decimal import Decimal


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
        self.nodes[0].generate(1)
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

    def mempool_block_limit(self):
        abi, bytecode = EVMContract.from_file("Loop.sol", "Loop").compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)
        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        contract = self.nodes[0].w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

        hashes = []
        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(40):
            # tx call actual used gas - 1_761_626.
            # tx should always pass evm tx validation, but may return early in construct block.
            tx = contract.functions.loop(10_000).build_transaction(
                {
                    "chainId": self.nodes[0].w3.eth.chain_id,
                    "nonce": start_nonce + i,
                    "gasPrice": 25_000_000_000,
                    "gas": 30_000_000,
                }
            )
            signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
            hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
            hashes.append(signed.hash.hex().lower()[2:])

        hash = self.nodes[0].eth_sendTransaction(
            {
                "nonce": hex(start_nonce + 40),
                "from": self.ethAddress,
                "to": self.toAddress,
                "value": "0xDE0B6B3A7640000",  # 1 DFI
                "gas": "0x5209",
                "gasPrice": "0x5D21DBA00",  # 25_000_000_000
            }
        )
        hashes.append(hash.lower()[2:])
        hash = self.nodes[0].eth_sendTransaction(
            {
                "nonce": hex(start_nonce + 41),
                "from": self.ethAddress,
                "to": self.toAddress,
                "value": "0xDE0B6B3A7640000",  # 1 DFI
                "gas": "0x5209",
                "gasPrice": "0x5D21DBA00",  # 25_000_000_000
            }
        )
        hashes.append(hash.lower()[2:])

        first_block_total_txs = 17
        second_block_total_txs = 17
        third_block_total_txs = 8

        self.nodes[0].generate(1)
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        # Check that the first 17 evm contract call txs is minted in the current block
        assert_equal(len(block_info["tx"]) - 1, first_block_total_txs)
        for idx, tx_info in enumerate(block_info["tx"][1:]):
            if idx == 0:
                continue
            assert_equal(tx_info["vm"]["vmtype"], "evm")
            assert_equal(tx_info["vm"]["txtype"], "EvmTx")
            assert_equal(tx_info["vm"]["msg"]["sender"], self.ethAddress)
            assert_equal(tx_info["vm"]["msg"]["nonce"], start_nonce + idx)
            assert_equal(tx_info["vm"]["msg"]["hash"], hashes[idx])
            assert_equal(tx_info["vm"]["msg"]["to"], receipt["contractAddress"].lower())

        self.nodes[0].generate(1)
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        # Check that the next 17 of the evm contract call txs is minted in the next block
        assert_equal(len(block_info["tx"]) - 1, second_block_total_txs)
        for idx, tx_info in enumerate(block_info["tx"][1:]):
            assert_equal(tx_info["vm"]["vmtype"], "evm")
            assert_equal(tx_info["vm"]["txtype"], "EvmTx")
            assert_equal(tx_info["vm"]["msg"]["sender"], self.ethAddress)
            assert_equal(
                tx_info["vm"]["msg"]["nonce"], start_nonce + first_block_total_txs + idx
            )
            assert_equal(
                tx_info["vm"]["msg"]["hash"], hashes[first_block_total_txs + idx]
            )
            assert_equal(tx_info["vm"]["msg"]["to"], receipt["contractAddress"].lower())

        # Check that the remaining evm txs is minted into this block
        self.nodes[0].generate(1)
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]) - 1, third_block_total_txs)
        # Check ordering of evm txs - first 6 evm txs are evm contract all txs
        tx_infos = block_info["tx"][1:]
        for idx in range(1, (40 - first_block_total_txs - second_block_total_txs)):
            assert_equal(tx_infos[idx]["vm"]["vmtype"], "evm")
            assert_equal(tx_infos[idx]["vm"]["txtype"], "EvmTx")
            assert_equal(tx_infos[idx]["vm"]["msg"]["sender"], self.ethAddress)
            assert_equal(
                tx_infos[idx]["vm"]["msg"]["nonce"],
                start_nonce + first_block_total_txs + second_block_total_txs + idx,
            )
            assert_equal(
                tx_infos[idx]["vm"]["msg"]["hash"],
                hashes[first_block_total_txs + second_block_total_txs + idx],
            )
            assert_equal(
                tx_infos[idx]["vm"]["msg"]["to"], receipt["contractAddress"].lower()
            )
        for idx in range(6, third_block_total_txs):
            assert_equal(tx_infos[idx]["vm"]["vmtype"], "evm")
            assert_equal(tx_infos[idx]["vm"]["txtype"], "EvmTx")
            assert_equal(tx_infos[idx]["vm"]["msg"]["sender"], self.ethAddress)
            assert_equal(
                tx_infos[idx]["vm"]["msg"]["nonce"],
                start_nonce + first_block_total_txs + second_block_total_txs + idx,
            )
            assert_equal(
                tx_infos[idx]["vm"]["msg"]["hash"],
                hashes[first_block_total_txs + second_block_total_txs + idx],
            )
            assert_equal(tx_infos[idx]["vm"]["msg"]["to"], self.toAddress)

    def invalid_evm_tx_in_block_creation(self):
        before_balance = Decimal(
            self.nodes[0].getaccount(self.ethAddress)[0].split("@")[0]
        )
        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for idx in range(20):
            self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + idx),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0x8AC7230489E80000",  # 10 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )

        self.nodes[0].generate(1)

        # Accounting
        # Each tx transfers 10 DFI
        tx_value = Decimal("10")
        # Each tx gas fees: 25_000_000_000 * 21_000
        gas_fee = Decimal("0.000525")
        # 9 txs should succeed, the remaining 11 txs fail due to out of funds
        correct_transfer = tx_value * Decimal("9")
        # Miner gets paid the gas fees for all txs executed, even if they fail
        correct_gas_fees = gas_fee * Decimal("20")
        correct_balance = correct_transfer + correct_gas_fees
        deducted_balance = before_balance - Decimal(
            self.nodes[0].getaccount(self.ethAddress)[0].split("@")[0]
        )
        assert_equal(deducted_balance, correct_balance)

        # Check to ensure both custom evm txs were minted on DVM side
        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]) - 1, 20)

    def run_test(self):
        self.setup()

        # Multiple mempool fee replacement
        self.mempool_block_limit()

        # Test invalid tx in block creation
        self.invalid_evm_tx_in_block_creation()


if __name__ == "__main__":
    EVMTest().main()

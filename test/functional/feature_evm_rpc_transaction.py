#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_is_hex_string,
    assert_raises_rpc_error,
    int_to_eth_u256,
    hex_to_decimal,
)

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
                "-ethdebug=1",
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

    def test_sign_and_send_raw_transaction(self):
        txLegacy = {
            "nonce": "0x0",
            "from": self.ethAddress,
            "value": "0x00",
            "data": CONTRACT_BYTECODE,
            "gas": "0x7a120",  # 500_000
            "gasPrice": "0x22ecb25c00",  # 150_000_000_000,
        }
        signed = self.nodes[0].eth_signTransaction(txLegacy)

        # LEGACY_TX = {
        #     nonce: 0,
        #     from: "0x9b8a4af42140d8a4c153a822f02571a1dd037e89",
        #     value: 0, // must be set else error https://github.com/rust-blockchain/evm/blob/a14b6b02452ebf8e8a039b92ab1191041f806794/src/executor/stack/memory.rs#L356
        #     data: CONTRACT_BYTECODE,
        #     gasLimit: 500_000,
        #     gasPrice: 150_000_000_000,
        #     chainId: 1133, // must be set else error https://github.com/rust-blockchain/ethereum/blob/0ffbe47d1da71841be274442a3050da9c895e10a/src/transaction.rs#L74
        # }
        rawtx = "0xf90152808522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c634300080200338208fda0f6d889435dbfe9ea49b984fe1cab94cae59fa774b602256abc9f657c2abdd5bea0609176ffd6023896913cd9e12b61a004a970187cc53623ef49afb0417cb44929"
        assert_equal(f"0x{signed}", rawtx)

        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt["contractAddress"])
        self.blockHash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        # Check accounting of EVM fees
        fees = self.nodes[0].debug_feeEstimate(txLegacy)
        self.burnt_fee = self.burnt_fee_min = hex_to_decimal(fees["burnt_fee"])
        self.priority_fee = self.priority_fee_min = hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash,
        )

        tx2930 = {
            "from": self.ethAddress,
            "nonce": "0x1",
            "value": "0x0",
            "data": CONTRACT_BYTECODE,
            "gas": "0x7a120",  # 500_000
            "gasPrice": "0x22ecb25c00",  # 150_000_000_000,
            "accessList": [
                {
                    "address": self.ethAddress,
                    "storageKeys": [
                        "0x0000000000000000000000000000000000000000000000000000000000000000"
                    ],
                },
            ],
            "type": "0x1",
        }
        signed = self.nodes[0].eth_signTransaction(tx2930)

        # EIP2930_TX = {
        #     nonce: 1,
        #     value: 0,
        #     data: CONTRACT_BYTECODE,
        #     gasLimit: 500_000,
        #     gasPrice: 150_000_000_000,
        #     accessList: [
        #     {
        #         address: 0x9b8a4af42140d8a4c153a822f02571a1dd037e89,
        #         storageKeys: [
        #             "0x0000000000000000000000000000000000000000000000000000000000000000"
        #         ]
        #     },
        #     ], type: 1, chainId: 1133
        # }
        rawtx = "0x01f9018d82046d018522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033f838f7949b8a4af42140d8a4c153a822f02571a1dd037e89e1a0000000000000000000000000000000000000000000000000000000000000000001a0f69240ab7127d845ad35a450d377f9d7d81c3e9d350b12bf53d14dc44a9e7bf2a067c2e6d1d30f20c670fbb9b79296c26ff372086f2ca3aead851bcc3510b638a7"
        assert_equal(f"0x{signed}", rawtx)

        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt["contractAddress"])
        self.blockHash1 = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        # Check accounting of EVM fees
        fees = self.nodes[0].debug_feeEstimate(tx2930)
        self.burnt_fee_max = hex_to_decimal(fees["burnt_fee"])
        self.burnt_fee += self.burnt_fee_max
        self.priority_fee_max = hex_to_decimal(fees["priority_fee"])
        self.priority_fee += self.priority_fee_max
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee_min
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee_max
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash1
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"],
            self.priority_fee_min,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            self.priority_fee_max,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash1,
        )

        tx1559 = {
            "nonce": "0x2",
            "from": self.ethAddress,
            "value": "0x0",
            "data": CONTRACT_BYTECODE,
            "gas": "0x7a120",  # 500_000
            "maxPriorityFeePerGas": "0x2363e7f000",  # 152_000_000_000
            "maxFeePerGas": "0x22ecb25c00",  # 150_000_000_000
            "type": "0x2",
        }
        signed = self.nodes[0].eth_signTransaction(tx1559)

        # EIP1559_TX = {
        #     nonce: 2,
        #     from: "0x9b8a4af42140d8a4c153a822f02571a1dd037e89",
        #     value: 0,
        #     data: CONTRACT_BYTECODE,
        #     gasLimit: 3_000,
        #     maxPriorityFeePerGas: 152_000_000_000, # 152 gwei
        #     maxFeePerGas: 150_000_000_000, type: 2,
        # }
        rawtx = "0x02f9015a82046d02852363e7f0008522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033c080a05eceb8ff0ce33c95106051d82896aaf41bc899f625189922d12edeb7ce6fd56da07c37f48b1f4dfbf89a81cbe62c93ab23eec00808f7daca0cc5cf29860d112759"
        assert_equal(f"0x{signed}", rawtx)

        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt["contractAddress"])

        # Check accounting of EVM fees
        fees = self.nodes[0].debug_feeEstimate(tx1559)
        self.burnt_fee += hex_to_decimal(fees["burnt_fee"])
        self.priority_fee += hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee_min
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee_max
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash1
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"],
            self.priority_fee_min,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            self.priority_fee_max,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash1,
        )

    def test_send_transaction(self):
        txLegacy = {
            "from": self.ethAddress,
            "value": "0x00",
            "data": CONTRACT_BYTECODE,
            "gas": "0x7a120",  # 500_000
            "gasPrice": "0x22ecb25c00",  # 150_000_000_000,
        }
        hash = self.nodes[0].eth_sendTransaction(txLegacy)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt["contractAddress"])

        # Check accounting of EVM fees
        fees = self.nodes[0].debug_feeEstimate(txLegacy)
        self.burnt_fee += hex_to_decimal(fees["burnt_fee"])
        self.priority_fee += hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee_min
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee_max
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash1
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"],
            self.priority_fee_min,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            self.priority_fee_max,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash1,
        )

        tx2930 = {
            "from": self.ethAddress,
            "value": "0x00",
            "data": CONTRACT_BYTECODE,
            "gas": "0x7a120",  # 500_000
            "gasPrice": "0x22ecb25c00",  # 150_000_000_000,
            "accessList": [
                {
                    "address": self.ethAddress,
                    "storageKeys": [
                        "0x0000000000000000000000000000000000000000000000000000000000000000"
                    ],
                },
            ],
            "type": "0x1",
        }
        hash = self.nodes[0].eth_sendTransaction(tx2930)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt["contractAddress"])

        # Check accounting of EVM fees
        fees = self.nodes[0].debug_feeEstimate(tx2930)
        self.burnt_fee += hex_to_decimal(fees["burnt_fee"])
        self.priority_fee += hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee_min
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee_max
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash1
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"],
            self.priority_fee_min,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            self.priority_fee_max,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash1,
        )

        tx1559 = {
            "from": self.ethAddress,
            "value": "0x0",
            "data": CONTRACT_BYTECODE,
            "gas": "0x18e70",  # 102_000
            "maxPriorityFeePerGas": "0x2363e7f000",  # 152_000_000_000
            "maxFeePerGas": "0x22ecb25c00",  # 150_000_000_000
            "type": "0x2",
        }
        hash = self.nodes[0].eth_sendTransaction(tx1559)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt["contractAddress"])

        # Check accounting of EVM fees
        fees = self.nodes[0].debug_feeEstimate(tx1559)
        self.burnt_fee += hex_to_decimal(fees["burnt_fee"])
        self.priority_fee += hex_to_decimal(fees["priority_fee"])
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], self.burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], self.burnt_fee_min
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], self.burnt_fee_max
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash1
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"],
            self.priority_fee_min,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            self.priority_fee_max,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash1,
        )

    def test_auto_nonce_for_multiple_transaction(self):
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "nonce": nonce + 2,
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].evmtx(self.ethAddress, nonce, 21, 21001, self.toAddress, 1)
        self.nodes[0].evmtx(self.ethAddress, nonce + 5, 21, 21001, self.toAddress, 1)
        self.nodes[0].eth_sendTransaction(
            {
                "from": self.ethAddress,
                "to": self.toAddress,
                "nonce": hex(nonce + 3),
                "value": "0xDE0B6B3A7640000",  # 1 DFI
                "gas": "0x7a120",
                "gasPrice": "0x2540BE400",
            }
        )
        # test auto nonce for the 2nd evm tx (nonce + 1)
        hash = self.nodes[0].eth_sendTransaction(
            {
                "from": self.ethAddress,
                "to": self.toAddress,
                "value": "0xDE0B6B3A7640000",  # 1 DFI
                "gas": "0x7a120",
                "gasPrice": "0x2540BE400",
            }
        )
        # test auto nonce for the 5th transferdomain tx (nonce + 4)
        tf_hash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        block_tx_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)[
            "tx"
        ]

        # assert all 6 txs are minted (including 2 auto-auth tx and coinbase)
        assert_equal(len(block_tx_info), 9)
        # test 2nd evm tx
        assert_equal(block_tx_info[4]["vm"]["txtype"], "Evm")
        assert_equal(block_tx_info[4]["vm"]["msg"]["hash"], hash[2:])
        # test 5th transfer domain
        assert_equal(block_tx_info[7]["vm"]["txtype"], "TransferDomain")
        assert_equal(block_tx_info[7]["txid"], tf_hash)

    def run_test(self):
        self.setup()

        self.test_sign_and_send_raw_transaction()

        self.test_send_transaction()

        self.test_auto_nonce_for_multiple_transaction()


if __name__ == "__main__":
    EVMTest().main()

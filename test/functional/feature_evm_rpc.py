#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

import re
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    assert_raises_web3_error,
    int_to_eth_u256,
    hex_to_decimal,
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
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.toPrivKey = (
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.nodes[0].importprivkey(self.ethPrivKey)
        self.nodes[0].importprivkey(self.toPrivKey)

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
        self.start_height = self.nodes[0].getblockcount()
        self.eth_start_height = int(self.nodes[0].eth_blockNumber(), 16)

        # Eth call for balance transfer with gas specified
        self.valid_balance_transfer_tx_with_gas = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gas": "0x7a120",
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }

        # Eth call for balance transfer with exact gas specified
        self.valid_balance_transfer_tx_with_exact_gas = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gas": "0x5208",
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }

        # Valid eth call for balance transfer without gas specified
        self.valid_balance_transfer_tx_without_gas = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }

        # Invalid eth call for balance transfer with both gasPrice and maxFeePerGas specified
        self.invalid_balance_transfer_tx_specified_gas_1 = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
            "maxFeePerGas": "0x37E11D600",  # 15_000_000_000
        }

        # Invalid eth call for balance transfer with both gasPrice and priorityFeePerGas specified
        self.invalid_balance_transfer_tx_specified_gas_2 = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
            "maxPriorityFeePerGas": "0x37E11D600",  # 15_000_000_000
        }

        # Invalid eth call for balance transfer with both data and input fields specified
        self.invalid_balance_transfer_tx_specified_data_and_input = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
            "data": "0xffffffffffffffff",
            "input": "0xffffffffffffffff",
        }

        # Invalid eth call from insufficient balance for balance transfer
        self.invalid_balance_transfer_tx_insufficient_funds = {
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0x152D02C7E14AF6800000",  # 100_000 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }

    def test_node_params(self):
        self.rollback_to(self.start_height)

        is_miningA = self.nodes[0].eth_mining()
        assert_equal(is_miningA, False)

        hashrate = self.nodes[0].eth_hashrate()
        assert_equal(hashrate, "0x0")

        netversion = self.nodes[0].net_version()
        assert_equal(netversion, "1133")

        chainid = self.nodes[0].eth_chainId()
        assert_equal(chainid, "0x46d")

    def test_web3_client_version(self):
        self.rollback_to(self.start_height)

        res = self.nodes[0].web3_clientVersion()
        match = re.search(r"(DeFiChain)/v(.*)/(.*)/(.*)", res)
        assert_equal(match.group(1), "DeFiChain")
        assert_equal(match.group(2).startswith("4."), True)
        assert_equal(match.group(3).find("-") != -1, True)
        assert_equal(len(match.group(3)) > 0, True)
        assert_equal(match.group(4).startswith("rustc-"), True)
        assert_equal(len(match.group(4)) > 7, True)

    def test_eth_call_transfer(self):
        self.rollback_to(self.start_height)

        # Test valid eth call using tx with gas specified, ExitReason::Succeed
        res = self.nodes[0].eth_call(self.valid_balance_transfer_tx_with_gas)
        assert_equal(res, "0x")

        # Test valid eth call using tx with exact gas specified, ExitReason::Succeed
        res = self.nodes[0].eth_call(self.valid_balance_transfer_tx_with_exact_gas)
        assert_equal(res, "0x")

        # Test valid eth call using tx without gas specified, ExitReason::Succeed
        res = self.nodes[0].eth_call(self.valid_balance_transfer_tx_without_gas)
        assert_equal(res, "0x")

        # Test invalid eth call, both gasPrice and maxFeePerGas specified
        assert_raises_rpc_error(
            -32001,
            "both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
            self.nodes[0].eth_call,
            self.invalid_balance_transfer_tx_specified_gas_1,
        )

        # Test invalid eth call, both gasPrice and maxPriorityFeePerGas specified
        assert_raises_rpc_error(
            -32001,
            "Custom error: both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
            self.nodes[0].eth_call,
            self.invalid_balance_transfer_tx_specified_gas_2,
        )

        # Test invalid eth call, both data and input field specified
        assert_raises_rpc_error(
            -32001,
            "Custom error: data and input fields are mutually exclusive",
            self.nodes[0].eth_call,
            self.invalid_balance_transfer_tx_specified_data_and_input,
        )

        # Test invalid eth call, insufficient funds
        assert_raises_rpc_error(
            -32001,
            "Custom error: exit error OutOfFund",
            self.nodes[0].eth_call,
            self.invalid_balance_transfer_tx_insufficient_funds,
        )

        # Should pass with state override
        self.nodes[0].eth_call(
            self.invalid_balance_transfer_tx_insufficient_funds,
            "latest",
            {self.ethAddress: {"balance": "0x152D02C7E14AF6800000"}},
        )

    def test_eth_call_contract(self):
        self.rollback_to(self.start_height)

        abi, bytecode, deployed = EVMContract.from_file("Loop.sol", "Loop").compile()
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
        # Test valid contract function eth call
        res = contract.functions.loop(10_000).call()
        assert_equal(res, [])

    def test_eth_call_contract_override(self):
        self.rollback_to(self.start_height)

        contractAddress = self.nodes[0].getnewaddress("", "erc55")
        abi, _, deployed = EVMContract.from_file("Loop.sol", "Loop").compile()

        contract = self.nodes[0].w3.eth.contract(address=contractAddress, abi=abi)

        # Test valid contract function eth call overriding contract code
        res = contract.functions.loop(10_000).call(
            {}, "latest", {contractAddress: {"code": "0x" + deployed}}
        )
        assert_equal(res, [])

    def test_eth_call_revert(self):
        self.rollback_to(self.start_height)

        abi, bytecode, _ = EVMContract.from_file("Require.sol", "Require").compile()
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

        # Test valid contract function eth call
        res = contract.functions.value_check(1).call()
        assert_equal(res, [])

        # Test invalid contract function eth call with revert
        assert_raises_web3_error(
            3,
            "execution reverted: Value must be greater than 0",
            contract.functions.value_check(0).call,
        )

    def test_eth_call_revert_override(self):
        self.rollback_to(self.start_height)

        contractAddress = self.nodes[0].getnewaddress("", "erc55")
        abi, _, deployed = EVMContract.from_file("Require.sol", "Require").compile()

        contract = self.nodes[0].w3.eth.contract(address=contractAddress, abi=abi)

        # Test valid contract function eth call overriding contract code
        res = contract.functions.value_check(1).call(
            {}, "latest", {contractAddress: {"code": "0x" + deployed}}
        )
        assert_equal(res, [])

        # Test invalid contract function eth call with revert overriding contract code
        assert_raises_web3_error(
            3,
            "execution reverted: Value must be greater than 0",
            contract.functions.value_check(0).call,
            {},
            "latest",
            {contractAddress: {"code": "0x" + deployed}},
        )

    def test_accounts(self):
        self.rollback_to(self.start_height)

        eth_accounts = self.nodes[0].eth_accounts()
        assert_equal(eth_accounts.sort(), [self.ethAddress, self.toAddress].sort())

    def test_address_state(self):
        self.rollback_to(self.start_height)

        assert_raises_rpc_error(
            -32602,
            "invalid length 7, expected a (both 0x-prefixed or not) hex string or byte array containing 20 bytes at line 1 column 9",
            self.nodes[0].eth_getBalance,
            "test123",
        )

        balance = self.nodes[0].eth_getBalance(self.ethAddress)
        assert_equal(balance, int_to_eth_u256(100))

        code = self.nodes[0].eth_getCode(self.ethAddress)
        assert_equal(code, "0x")

        blockNumber = self.nodes[0].eth_blockNumber()
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
        assert_equal(balance, int_to_eth_u256(150))

        # Test querying previous block
        balance = self.nodes[0].eth_getBalance(self.ethAddress, blockNumber)
        assert_equal(balance, int_to_eth_u256(100))

    def test_block(self):
        self.rollback_to(self.start_height)

        latest_block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(latest_block["number"], hex(self.eth_start_height))

        # Test full transaction block
        self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)

        # Test evm tx RPC
        block_hash = self.nodes[0].getbestblockhash()
        block = self.nodes[0].getblock(block_hash)
        res = self.nodes[0].getcustomtx(block["tx"][1], block_hash)
        assert_equal(
            res["results"]["hash"],
            "8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497",
        )
        # Note: This will fail. Re-evaluate
        assert_equal(res["results"]["sender"].lower(), self.ethAddress)
        assert_equal(res["results"]["gasPrice"], Decimal("2"))
        assert_equal(res["results"]["gasLimit"], 21000)
        assert_equal(res["results"]["createTx"], False)
        assert_equal(res["results"]["to"].lower(), self.toAddress)

        latest_block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(latest_block["number"], hex(self.eth_start_height + 1))
        assert_equal(
            latest_block["transactions"][0],
            "0x8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497",
        )

        latest_full_block = self.nodes[0].eth_getBlockByNumber("latest", True)
        assert_equal(latest_full_block["number"], hex(self.eth_start_height + 1))
        assert_equal(
            latest_full_block["transactions"][0]["blockHash"], latest_full_block["hash"]
        )
        assert_equal(
            latest_full_block["transactions"][0]["blockNumber"],
            latest_full_block["number"],
        )
        assert_equal(latest_full_block["transactions"][0]["from"], self.ethAddress)
        assert_equal(latest_full_block["transactions"][0]["gas"], "0x5208")
        assert_equal(latest_full_block["transactions"][0]["gasPrice"], "0x4e3b29200")
        assert_equal(
            latest_full_block["transactions"][0]["hash"],
            "0x8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497",
        )
        assert_equal(latest_full_block["transactions"][0]["input"], "0x")
        assert_equal(latest_full_block["transactions"][0]["nonce"], "0x0")
        assert_equal(latest_full_block["transactions"][0]["to"], self.toAddress)
        assert_equal(latest_full_block["transactions"][0]["transactionIndex"], "0x0")
        assert_equal(latest_full_block["transactions"][0]["value"], "0xde0b6b3a7640000")
        assert_equal(latest_full_block["transactions"][0]["v"], "0x25")
        assert_equal(
            latest_full_block["transactions"][0]["r"],
            "0x37f41c543402c9b02b35b45ef43ac31a63dcbeba0c622249810ecdec00aee376",
        )

        # state check
        block = self.nodes[0].eth_getBlockByHash(latest_block["hash"])
        assert_equal(block, latest_block)
        blockHash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        # Check accounting of EVM fees
        txLegacy = {
            "nonce": "0x1",
            "from": self.ethAddress,
            "value": "0x1",
            "gas": "0x5208",  # 21000
            "gasPrice": "0x4e3b29200",  # 21_000_000_000,
        }
        fees = self.nodes[0].debug_feeEstimate(txLegacy)
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
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"], blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"], self.priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"], blockHash
        )

    def run_test(self):
        self.setup()

        self.test_node_params()

        self.test_eth_call_transfer()

        self.test_eth_call_contract()

        self.test_eth_call_contract_override()

        self.test_eth_call_revert()

        self.test_eth_call_revert_override()

        self.test_accounts()

        self.test_address_state()  # TODO test smart contract

        self.test_block()

        self.test_web3_client_version()


if __name__ == "__main__":
    EVMTest().main()

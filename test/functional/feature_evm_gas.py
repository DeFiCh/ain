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
    assert_raises_web3_error,
)
from test_framework.evm_contract import EVMContract


class EVMGasTest(DefiTestFramework):
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
                "-evmestimategaserrorratio=0",
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

    def generate_test_txs(self, address):
        dict_txs = dict()
        # Eth call for balance transfer with gas specified
        valid_balance_transfer_tx_with_gas = {
            "from": address,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gas": "0x7a120",
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }
        dict_txs["valid_balance_transfer_tx_with_gas"] = (
            valid_balance_transfer_tx_with_gas
        )

        # Eth call for balance transfer with exact gas specified
        valid_balance_transfer_tx_with_exact_gas = {
            "from": address,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gas": "0x5208",
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }
        dict_txs["valid_balance_transfer_tx_with_exact_gas"] = (
            valid_balance_transfer_tx_with_exact_gas
        )

        # Valid eth call for balance transfer without gas specified
        valid_balance_transfer_tx_without_gas = {
            "from": address,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }
        dict_txs["valid_balance_transfer_tx_without_gas"] = (
            valid_balance_transfer_tx_without_gas
        )

        # Invalid eth call for balance transfer with both gasPrice and maxFeePerGas specified
        invalid_balance_transfer_tx_specified_gas_1 = {
            "from": address,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
            "maxFeePerGas": "0x37E11D600",  # 15_000_000_000
        }
        dict_txs["invalid_balance_transfer_tx_specified_gas_1"] = (
            invalid_balance_transfer_tx_specified_gas_1
        )

        # Invalid eth call for balance transfer with both gasPrice and priorityFeePerGas specified
        invalid_balance_transfer_tx_specified_gas_2 = {
            "from": address,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
            "maxPriorityFeePerGas": "0x37E11D600",  # 15_000_000_000
        }
        dict_txs["invalid_balance_transfer_tx_specified_gas_2"] = (
            invalid_balance_transfer_tx_specified_gas_2
        )

        # Invalid eth call for balance transfer with both data and input fields specified
        invalid_balance_transfer_tx_specified_data_and_input = {
            "from": address,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
            "data": "0xffffffffffffffff",
            "input": "0xffffffffffffffff",
        }
        dict_txs["invalid_balance_transfer_tx_specified_data_and_input"] = (
            invalid_balance_transfer_tx_specified_data_and_input
        )

        # Invalid eth call from insufficient balance for balance transfer
        invalid_balance_transfer_tx_insufficient_funds = {
            "from": address,
            "to": self.toAddress,
            "value": "0x152D02C7E14AF6800000",  # 100_000 DFI
            "gasPrice": "0x37E11D600",  # 15_000_000_000
        }
        dict_txs["invalid_balance_transfer_tx_insufficient_funds"] = (
            invalid_balance_transfer_tx_insufficient_funds
        )
        return dict_txs

    def test_estimate_gas_balance_transfer(self):
        self.rollback_to(self.start_height)
        dict_txs = self.generate_test_txs(self.ethAddress)

        # Test valid estimateGas call for transfer tx with gas specified
        gas = self.nodes[0].eth_estimateGas(
            dict_txs["valid_balance_transfer_tx_with_gas"]
        )
        assert_equal(gas, "0x5208")

        # Test valid estimateGas call for transfer tx with exact gas specified
        gas = self.nodes[0].eth_estimateGas(
            dict_txs["valid_balance_transfer_tx_with_exact_gas"],
        )
        assert_equal(gas, "0x5208")

        # Test valid estimateGas call for transfer tx without gas specified
        gas = self.nodes[0].eth_estimateGas(
            dict_txs["valid_balance_transfer_tx_without_gas"]
        )
        assert_equal(gas, "0x5208")

        # Test invalid estimateGas call, both gasPrice and maxFeePerGas specified
        assert_raises_rpc_error(
            -32001,
            "both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_specified_gas_1"],
        )

        # Test invalid estimateGas call, both gasPrice and maxPriorityFeePerGas specified
        assert_raises_rpc_error(
            -32001,
            "Custom error: both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_specified_gas_2"],
        )

        # Test invalid estimateGas call, both data and input field specified
        assert_raises_rpc_error(
            -32001,
            "Custom error: data and input fields are mutually exclusive",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_specified_data_and_input"],
        )

        # Test invalid estimateGas call, insufficient funds
        assert_raises_rpc_error(
            -32001,
            "Custom error: insufficient funds for transfer",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_insufficient_funds"],
        )

    def test_estimate_gas_contract(self):
        self.rollback_to(self.start_height)

        abi, bytecode, _ = EVMContract.from_file("Loop.sol", "Loop").compile()
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
        # Test valid contract function eth call with varying gas used
        hashes = []
        gas_estimates = []
        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        loops = [2572, 2899, 3523, 5311, 5824, 7979, 8186, 10000]

        for i, num in enumerate(loops):
            gas = contract.functions.loop(num).estimate_gas()
            gas_estimates.append(gas)

            # Do actual contract function call without specifying gas
            tx = contract.functions.loop(num).build_transaction(
                {
                    "chainId": self.nodes[0].w3.eth.chain_id,
                    "nonce": start_nonce + i,
                    "gasPrice": 25_000_000_000,
                }
            )
            signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
            hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
            hashes.append(hash)
        self.nodes[0].generate(1)

        block_info = self.nodes[0].getblock(self.nodes[0].getbestblockhash(), 4)
        assert_equal(len(block_info["tx"]) - 1, len(loops))

        # Verify gas estimation with tx receipts
        for idx, hash in enumerate(hashes):
            tx_receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
            assert_equal(tx_receipt["status"], 1)
            assert_equal(tx_receipt["gasUsed"], gas_estimates[idx])

    def test_estimate_gas_contract_exact_gas(self):
        self.rollback_to(self.start_height)

        abi, bytecode, _ = EVMContract.from_file("Loop.sol", "Loop").compile()
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

        exact_gas = contract.functions.loop(5_000).estimate_gas()
        # Do actual contract function call
        tx = contract.functions.loop(5_000).build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "gasPrice": 25_000_000_000,
                "gas": exact_gas,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        tx_receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(tx_receipt["gasUsed"], exact_gas)
        assert_equal(tx_receipt["status"], 1)

    def test_estimate_gas_revert(self):
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
        gas = contract.functions.value_check(1).estimate_gas()

        # Do actual contract function call
        tx = contract.functions.value_check(1).build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "gasPrice": 25_000_000_000,
                "gas": 30_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)
        tx_receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(gas, tx_receipt["gasUsed"])

        # Test invalid contract function eth call with revert
        assert_raises_web3_error(
            -32001,
            "Custom error: transaction execution failed",
            contract.functions.value_check(0).estimate_gas,
        )

    def test_estimate_gas_state_override(self):
        self.rollback_to(self.start_height)

        emptyAddress = self.nodes[0].getnewaddress("", "erc55")
        self.nodes[0].generate(1)
        balance = self.nodes[0].eth_getBalance(emptyAddress, "latest")
        assert_equal(int(balance[2:], 16), 0)

        dict_txs = self.generate_test_txs(emptyAddress)
        state_override = {emptyAddress: {"balance": "0x8AC7230489E80000"}}  # 10 DFI

        # Test valid estimateGas call for transfer tx with gas specified and state override
        gas = self.nodes[0].eth_estimateGas(
            dict_txs["valid_balance_transfer_tx_with_gas"], "latest", state_override
        )
        assert_equal(gas, "0x5208")

        # Test valid estimateGas call for transfer tx with exact gas specified and state override
        gas = self.nodes[0].eth_estimateGas(
            dict_txs["valid_balance_transfer_tx_with_exact_gas"],
            "latest",
            state_override,
        )
        assert_equal(gas, "0x5208")

        # Test valid estimateGas call for transfer tx without gas specified and state override
        gas = self.nodes[0].eth_estimateGas(
            dict_txs["valid_balance_transfer_tx_without_gas"], "latest", state_override
        )
        assert_equal(gas, "0x5208")

        # Test invalid estimateGas call, both gasPrice and maxFeePerGas specified and state override
        assert_raises_rpc_error(
            -32001,
            "both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_specified_gas_1"],
            "latest",
            state_override,
        )

        # Test invalid estimateGas call, both gasPrice and maxPriorityFeePerGas specified and state override
        assert_raises_rpc_error(
            -32001,
            "Custom error: both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_specified_gas_2"],
            "latest",
            state_override,
        )

        # Test invalid estimateGas call, both data and input field specified and state override
        assert_raises_rpc_error(
            -32001,
            "Custom error: data and input fields are mutually exclusive",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_specified_data_and_input"],
            "latest",
            state_override,
        )

        # Test invalid estimateGas call, insufficient funds and state override
        assert_raises_rpc_error(
            -32001,
            "Custom error: insufficient funds for transfer",
            self.nodes[0].eth_estimateGas,
            dict_txs["invalid_balance_transfer_tx_insufficient_funds"],
            "latest",
            state_override,
        )

    def run_test(self):
        self.setup()

        self.test_estimate_gas_balance_transfer()

        self.test_estimate_gas_contract()

        self.test_estimate_gas_contract_exact_gas()

        self.test_estimate_gas_revert()

        self.test_estimate_gas_state_override()


if __name__ == "__main__":
    EVMGasTest().main()

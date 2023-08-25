#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract"""

from test_framework.util import assert_equal, assert_greater_than
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair


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
        self.nodes[0].generate(1)

    def should_deploy_contract_less_than_1KB(self):
        node = self.nodes[0]
        self.evm_key_pair = EvmKeyPair.from_node(node)

        node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        node.generate(1)

        abi, bytecode = EVMContract.from_file("SimpleStorage.sol", "Test").compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )
        size_of_runtime_bytecode = len(node.w3.eth.get_code(self.contract.address))
        assert_greater_than(size_of_runtime_bytecode, 0)
        assert_greater_than(1_000, size_of_runtime_bytecode)

    def should_contract_get_set(self):
        # set variable
        node = self.nodes[0]
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        node.w3.eth.wait_for_transaction_receipt(hash)

        # get variable
        assert_equal(self.contract.functions.retrieve().call(), 10)

    def failed_tx_should_increment_nonce(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file("Reverter.sol", "Reverter").compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        contract = node.w3.eth.contract(address=receipt["contractAddress"], abi=abi)

        # for successful TX
        before_tx_count = node.w3.eth.get_transaction_count(self.evm_key_pair.address)

        tx = contract.functions.trySuccess().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        node.w3.eth.wait_for_transaction_receipt(hash)

        after_tx_count = node.w3.eth.get_transaction_count(self.evm_key_pair.address)

        assert_equal(before_tx_count + 1, after_tx_count)

        # for failed TX
        before_tx_count = node.w3.eth.get_transaction_count(self.evm_key_pair.address)

        tx = contract.functions.tryRevert().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        node.w3.eth.wait_for_transaction_receipt(hash)

        after_tx_count = node.w3.eth.get_transaction_count(self.evm_key_pair.address)

        assert_equal(before_tx_count + 1, after_tx_count)

    def should_deploy_contract_1KB_To_10KB(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file(
            "ContractSize1KBTo10KB.sol", "ContractWithSize1KBTo10KB"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        size_of_runtime_bytecode = len(node.w3.eth.get_code(receipt["contractAddress"]))
        assert_equal(receipt["status"], 1)
        assert_greater_than(size_of_runtime_bytecode, 1_000)
        assert_greater_than(10_000, size_of_runtime_bytecode)

    def should_deploy_contract_10KB_To_19KB(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file(
            "ContractSize10KBTo19KB.sol", "ContractWithSize10KBTo19KB"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        size_of_runtime_bytecode = len(node.w3.eth.get_code(receipt["contractAddress"]))
        assert_equal(receipt["status"], 1)
        assert_greater_than(size_of_runtime_bytecode, 10_000)
        assert_greater_than(19_000, size_of_runtime_bytecode)

    def should_deploy_contract_20KB_To_29KB(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file(
            "ContractSize20KBTo29KB.sol", "ContractWithSize20KBTo29KB"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        size_of_runtime_bytecode = len(node.w3.eth.get_code(receipt["contractAddress"]))
        assert_equal(receipt["status"], 1)
        assert_greater_than(size_of_runtime_bytecode, 20_000)
        assert_greater_than(29_000, size_of_runtime_bytecode)

    # EIP 170, contract size is limited to 24576 bytes
    # this test deploys a smart contract with an estimated size larger than this number
    def fail_deploy_contract_extremely_large_runtime_code(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file(
            "ContractLargeRunTimeCode.sol", "ContractLargeRunTimeCode"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        size_of_runtime_bytecode = len(node.w3.eth.get_code(receipt["contractAddress"]))
        assert_equal(receipt["status"], 0)
        assert_equal(size_of_runtime_bytecode, 0)

    # EIP 3860, contract initcode is limited up till 49152 bytes
    # This test takes in a contract with init code of 247646 bytes
    # However, because the current implementation of DMC limits the size of EVM transaction to 32768 bytes
    # the error returned is evm tx size too large
    def fail_deploy_contract_extremely_large_init_code(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file(
            "ContractLargeInitCode.sol", "ContractLargeInitCode"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)

        try:
            node.w3.eth.send_raw_transaction(signed.rawTransaction)
        except Exception as e:
            error_code = e.args[0]["code"]
            error_message = e.args[0]["message"]
            assert_equal(error_code, -32001)
            assert_equal(
                "Custom error: Could not publish raw transaction:" in error_message,
                True,
            )
            assert_equal(
                "reason: Test EvmTxTx execution failed:\nevm tx size too large"
                in error_message,
                True,
            )

    def run_test(self):
        self.setup()

        self.should_deploy_contract_less_than_1KB()

        self.should_contract_get_set()

        self.failed_tx_should_increment_nonce()

        self.should_deploy_contract_1KB_To_10KB()

        self.should_deploy_contract_10KB_To_19KB()

        self.should_deploy_contract_20KB_To_29KB()

        self.fail_deploy_contract_extremely_large_runtime_code()

        self.fail_deploy_contract_extremely_large_init_code()


if __name__ == "__main__":
    EVMTest().main()

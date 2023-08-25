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
from test_framework.test_node import TestNode
from solcx import compile_source


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

    def generate_contract(self, node: TestNode, num_functions: int, contract_name: str):
        contract_start = """
pragma solidity ^0.8.0;

contract {} {{
    
    """.format(
            contract_name
        )

        function_template = lambda index: """
    function func{}() public pure returns(uint256) {{
        return {};
    }}""".format(
            index, index
        )

        contract_end = """
}"""

        list_sig = []
        contract_body = ""

        for i in range(0, num_functions):
            func_sig = "func${}()".format(i)
            sig_hash = node.w3.keccak(text=func_sig)[:4]
            if sig_hash in list_sig:
                continue
            list_sig.append(sig_hash)
            contract_body += function_template(i)

        utf8SourceCode = contract_start + contract_body + contract_end

        compiled_output = compile_source(
            source=utf8SourceCode,
            output_values=["abi", "bin"],
            solc_version="0.8.20",
        )

        abi = compiled_output["<stdin>:{}".format(contract_name)]["abi"]
        bytecode = compiled_output["<stdin>:{}".format(contract_name)]["bin"]
        compiled_contract = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        return compiled_contract

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

        compiled_contract = self.generate_contract(
            node, 2**7, "ContractSize1KBTo10KB"
        )

        tx = compiled_contract.constructor().build_transaction(
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

        compiled_contract = self.generate_contract(
            node, 2**8, "ContractSize10KBTo19KB"
        )

        tx = compiled_contract.constructor().build_transaction(
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

        compiled_contract = self.generate_contract(node, 400, "ContractSize20KBTo29KB")

        tx = compiled_contract.constructor().build_transaction(
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

        compiled_contract = self.generate_contract(
            node, 2**9 - 1, "ContractLargeRunTimeCode"
        )

        tx = compiled_contract.constructor().build_transaction(
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
    # This test takes in a contract with init code of 243542 bytes
    # However, because the current implementation of DMC limits the size of EVM transaction to 32768 bytes
    # the error returned is evm tx size too large
    def fail_deploy_contract_extremely_large_init_code(self):
        node = self.nodes[0]

        compiled_contract = self.generate_contract(
            node, 2**12 - 1, "ContractLargeInitCode"
        )

        tx = compiled_contract.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        # to check the init code is larger than 49152
        assert_greater_than((len(tx["data"]) - 2) / 2, 49152)
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

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract"""

from test_framework.util import assert_equal
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair
from test_framework.test_node import TestNode
from test_framework.util import assert_raises_web3_error


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
        self.nodes[0].generate(2)

    def generate_contract(self, node: TestNode, num_functions: int, contract_name: str):
        contract_start = f"""
        pragma solidity ^0.8.0;
        contract {contract_name}  {{
        """
        function_template = (
            lambda index: f"""
        function func{index}() public pure returns(uint256) {{
            return {index};
        }}"""
        )
        contract_end = "\n}"

        list_sig = []
        contract_body = ""

        for i in range(0, num_functions):
            func_sig = f"func${i}()"
            sig_hash = self.node.w3.keccak(text=func_sig)[:4]
            if sig_hash in list_sig:
                continue
            list_sig.append(sig_hash)
            contract_body += function_template(i)

        utf8_source_code = contract_start + contract_body + contract_end

        abi, bytecode, runtime_bytecode = EVMContract.from_str(
            utf8_source_code, contract_name
        ).compile()
        compiled_contract = self.node.w3.eth.contract(abi=abi, bytecode=bytecode)

        return compiled_contract, runtime_bytecode

    def should_deploy_contract_less_than_1KB(self):
        self.evm_key_pair = EvmKeyPair.from_node(self.node)

        self.node.transferdomain(
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
        self.node.generate(1)

        abi, bytecode, _ = EVMContract.from_file("SimpleStorage.sol", "Test").compile()
        compiled = self.node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract = self.node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )
        size_of_runtime_bytecode = len(self.node.w3.eth.get_code(self.contract.address))
        assert_equal(size_of_runtime_bytecode, 323)

    def should_contract_get_set(self):
        # set variable
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        self.node.w3.eth.wait_for_transaction_receipt(hash)

        # get variable
        assert_equal(self.contract.functions.retrieve().call(), 10)

    def failed_tx_should_increment_nonce(self):
        abi, bytecode, _ = EVMContract.from_file("Reverter.sol", "Reverter").compile()
        compiled = self.node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        contract = self.node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

        # for successful TX
        before_tx_count = self.node.w3.eth.get_transaction_count(
            self.evm_key_pair.address
        )

        tx = contract.functions.trySuccess().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        self.node.generate(1)
        self.node.w3.eth.wait_for_transaction_receipt(hash)

        after_tx_count = self.node.w3.eth.get_transaction_count(
            self.evm_key_pair.address
        )

        assert_equal(before_tx_count + 1, after_tx_count)

        # for failed TX
        before_tx_count = self.node.w3.eth.get_transaction_count(
            self.evm_key_pair.address
        )

        tx = contract.functions.tryRevert().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        self.node.generate(1)
        self.node.w3.eth.wait_for_transaction_receipt(hash)

        after_tx_count = self.node.w3.eth.get_transaction_count(
            self.evm_key_pair.address
        )

        assert_equal(before_tx_count + 1, after_tx_count)

    def should_deploy_contract_with_different_sizes(self):
        test_data = [
            (128, "ContractSize1KBTo10KB", 6901),
            (256, "ContractSize10KBTo19KB", 13685),
            (400, "ContractSize20KBTo29KB", 21140),
        ]
        for iteration, contract_name, expected_runtime_bytecode_size in test_data:
            compiled_contract, compiler_runtime_bytecode = self.generate_contract(
                self.node, iteration, contract_name
            )

            tx = compiled_contract.constructor().build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "maxFeePerGas": 10_000_000_000,
                    "maxPriorityFeePerGas": 1_500_000_000,
                }
            )
            signed = self.node.w3.eth.account.sign_transaction(
                tx, self.evm_key_pair.privkey
            )
            hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

            self.node.generate(1)
            receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
            runtime_bytecode = self.node.w3.eth.get_code(receipt["contractAddress"])
            size_of_runtime_bytecode = len(runtime_bytecode)
            assert_equal(receipt["status"], 1)
            assert_equal(size_of_runtime_bytecode, expected_runtime_bytecode_size)
            # sanity check for the equality between the runtime bytecode generated by the compiler
            # and the runtime code deployed
            assert_equal(compiler_runtime_bytecode, runtime_bytecode.hex()[2:])

    # EIP 170, contract size is limited to 24576 bytes
    # this test deploys a smart contract with an estimated size larger than this number
    def fail_deploy_contract_extremely_large_runtime_code(self):
        compiled_contract, compiler_runtime_bytecode = self.generate_contract(
            self.node, 2**9 - 1, "ContractLargeRunTimeCode"
        )
        assert_equal(len(compiler_runtime_bytecode) / 2, 27458)

        tx = compiled_contract.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        size_of_runtime_bytecode = len(
            self.node.w3.eth.get_code(receipt["contractAddress"])
        )
        assert_equal(receipt["status"], 0)
        assert_equal(size_of_runtime_bytecode, 0)

    # EIP 3860, contract initcode is limited up till 49152 bytes
    # This test takes in a contract with init code of 243542 bytes
    # However, because the current implementation of DMC limits the size of EVM transaction to 32768 bytes
    # the error returned is evm tx size too large
    def fail_deploy_contract_extremely_large_init_code(self):
        compiled_contract, _ = self.generate_contract(
            self.node, 2**12 - 1, "ContractLargeInitCode"
        )

        tx = compiled_contract.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
            }
        )
        # to check the init code is larger than 49152
        assert_equal((len(tx["data"]) - 2) / 2, 243542)
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )

        assert_raises_web3_error(
            -32001,
            "Test EvmTxTx execution failed:\nevm tx size too large",
            self.node.w3.eth.send_raw_transaction,
            signed.rawTransaction,
        )

    def run_test(self):
        self.setup()

        self.node = self.nodes[0]

        self.should_deploy_contract_less_than_1KB()

        self.should_contract_get_set()

        self.failed_tx_should_increment_nonce()

        self.should_deploy_contract_with_different_sizes()

        self.fail_deploy_contract_extremely_large_runtime_code()

        self.fail_deploy_contract_extremely_large_init_code()


if __name__ == "__main__":
    EVMTest().main()

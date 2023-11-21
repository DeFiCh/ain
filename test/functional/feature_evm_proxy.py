#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract proxy"""

from test_framework.util import assert_equal
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair
from web3.contract import Contract as web3Contract


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

    def should_deploy_implementation_smart_contract(self) -> web3Contract:
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
                    "singlekeycheck": False,
                }
            ]
        )
        node.generate(1)

        implementation_abi, bytecode, _ = EVMContract.from_file(
            "SimpleProxy.sol", "SimpleImplementation"
        ).compile()
        compiled = node.w3.eth.contract(abi=implementation_abi, bytecode=bytecode)
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
        implementation_contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=implementation_abi
        )
        return implementation_contract

    def should_deploy_proxy_smart_contract(
        self, implementation_contract: web3Contract
    ) -> web3Contract:
        node = self.nodes[0]
        abi, bytecode, _ = EVMContract.from_file(
            "SimpleProxy.sol", "SimpleERC1967Proxy"
        ).compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor(
            implementation_contract.address,
            implementation_contract.encodeABI(
                "initialize", [self.evm_key_pair.address, 10]
            ),
        ).build_transaction(
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
        proxy_contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=implementation_contract.abi
        )
        assert_equal(proxy_contract.functions.randomVar().call(), 10)
        return proxy_contract

    def should_delegatecall_function_in_implementation_contract(
        self, proxy_contract: web3Contract
    ):
        node = self.nodes[0]
        tx = proxy_contract.functions.setRandomVar(20).build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 1)
        assert_equal(proxy_contract.functions.randomVar().call(), 20)

    def fail_send_money_to_smart_contract(self, proxy_contract: web3Contract):
        node = self.nodes[0]
        transaction = {
            "to": proxy_contract.address,
            "value": node.w3.to_wei(1, "ether"),
            "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
            "gas": 1_000_000,
            "chainId": node.w3.eth.chain_id,
            "maxFeePerGas": 10_000_000_000,
            "maxPriorityFeePerGas": 1_500_000_000,
        }
        signed = node.w3.eth.account.sign_transaction(
            transaction, self.evm_key_pair.privkey
        )
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 0)

    def fail_delegatecall_to_non_existing_function_in_implementation(
        self, proxy_contract: web3Contract
    ):
        node = self.nodes[0]
        transaction = {
            "to": proxy_contract.address,
            # signature of function randomFunction()
            "data": "0xf6376d45",
            "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
            "gas": 1_000_000,
            "chainId": node.w3.eth.chain_id,
            "maxFeePerGas": 10_000_000_000,
            "maxPriorityFeePerGas": 1_500_000_000,
        }
        signed = node.w3.eth.account.sign_transaction(
            transaction, self.evm_key_pair.privkey
        )
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 0)

    def fail_send_eth_when_delegatecall_to_non_payable_function(
        self, proxy_contract: web3Contract
    ):
        node = self.nodes[0]
        transaction = {
            "to": proxy_contract.address,
            # setRandomVar(19)
            "data": "0x9b6e6b170000000000000000000000000000000000000000000000000000000000000013",
            "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
            "value": node.w3.to_wei(1, "ether"),
            "gas": 1_000_000,
            "chainId": node.w3.eth.chain_id,
            "maxFeePerGas": 10_000_000_000,
            "maxPriorityFeePerGas": 1_500_000_000,
        }
        signed = node.w3.eth.account.sign_transaction(
            transaction, self.evm_key_pair.privkey
        )
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 0)

    def should_deploy_new_implementation_smart_contract(self) -> web3Contract:
        node = self.nodes[0]
        new_implementation_abi, bytecode, _ = EVMContract.from_file(
            "SimpleProxy.sol", "NewSimpleImplementation"
        ).compile()
        compiled = node.w3.eth.contract(abi=new_implementation_abi, bytecode=bytecode)
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
        new_implementation_contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=new_implementation_abi
        )
        return new_implementation_contract

    def fail_unauthorized_upgrade(
        self, proxy_contract: web3Contract, new_implementation_contract: web3Contract
    ):
        node = self.nodes[0]
        second_evm_key_pair = EvmKeyPair.from_node(node)

        transaction = {
            "to": second_evm_key_pair.address,
            "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
            "value": node.w3.to_wei(1, "ether"),
            "chainId": node.w3.eth.chain_id,
            "maxFeePerGas": 10_000_000_000,
            "maxPriorityFeePerGas": 1_500_000_000,
            "gas": 1_000_000,
        }

        signed = node.w3.eth.account.sign_transaction(
            transaction, self.evm_key_pair.privkey
        )
        node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)

        assert_equal(
            node.w3.eth.get_balance(second_evm_key_pair.address),
            node.w3.to_wei(1, "ether"),
        )

        # Upgrade the smart contract but in an unauthorized way
        tx = proxy_contract.functions.upgradeTo(
            new_implementation_contract.address,
            new_implementation_contract.encodeABI("reinitialize", [1000]),
        ).build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(second_evm_key_pair.address),
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, second_evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 0)

    def should_upgrade_and_interact_with_smart_contract(
        self, proxy_contract: web3Contract, new_implementation_contract: web3Contract
    ):
        node = self.nodes[0]
        # Upgrade and interact with the smart contract
        tx = proxy_contract.functions.upgradeTo(
            new_implementation_contract.address,
            new_implementation_contract.encodeABI("reinitialize", [1000]),
        ).build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 1)
        proxy_with_new_implementation_abi = node.w3.eth.contract(
            address=proxy_contract.address, abi=new_implementation_contract.abi
        )

        # check view functions of the new proxy smart contract
        assert_equal(
            proxy_with_new_implementation_abi.functions.admin().call(),
            self.evm_key_pair.address,
        )
        assert_equal(
            proxy_with_new_implementation_abi.functions.secondRandomVar().call(), 1000
        )
        assert_equal(proxy_with_new_implementation_abi.functions.randomVar().call(), 20)

        # Should be able to send money to the new smart contract
        assert_equal(
            node.w3.eth.get_balance(proxy_with_new_implementation_abi.address), 0
        )
        transaction = {
            "to": proxy_with_new_implementation_abi.address,
            "value": node.w3.to_wei(1, "ether"),
            "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
            "gas": 1_000_000,
            "chainId": node.w3.eth.chain_id,
            "maxFeePerGas": 10_000_000_000,
            "maxPriorityFeePerGas": 1_500_000_000,
        }
        signed = node.w3.eth.account.sign_transaction(
            transaction, self.evm_key_pair.privkey
        )

        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
        node.generate(1)
        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 1)
        assert_equal(
            node.w3.eth.get_balance(proxy_with_new_implementation_abi.address),
            node.w3.to_wei(1, "ether"),
        )

    def run_test(self):
        self.setup()

        implementation_contract = self.should_deploy_implementation_smart_contract()

        proxy_contract = self.should_deploy_proxy_smart_contract(
            implementation_contract
        )

        self.should_delegatecall_function_in_implementation_contract(proxy_contract)

        self.fail_send_money_to_smart_contract(proxy_contract)

        self.fail_delegatecall_to_non_existing_function_in_implementation(
            proxy_contract
        )

        self.fail_send_eth_when_delegatecall_to_non_payable_function(proxy_contract)

        new_implementation_contract = (
            self.should_deploy_new_implementation_smart_contract()
        )

        self.fail_unauthorized_upgrade(proxy_contract, new_implementation_contract)

        self.should_upgrade_and_interact_with_smart_contract(
            proxy_contract, new_implementation_contract
        )


if __name__ == "__main__":
    EVMTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""


from test_framework.evm_key_pair import EvmKeyPair
from test_framework.evm_contract import EVMContract
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_web3_error,
)


class EIP1559Fees(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txordering=2",
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
            ]
        ]

    def send_tx(self, tx):
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        self.node.generate(1)
        return self.node.w3.eth.wait_for_transaction_receipt(hash)

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.node = self.nodes[0]
        self.w3 = self.node.w3
        self.node.generate(105)
        self.evm_key_pair = EvmKeyPair.from_node(self.node)

        # Activate EVM
        self.node.setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                }
            }
        )
        self.node.utxostoaccount({self.address: "10@DFI"})
        self.node.generate(2)

        self.node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "10@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "10@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.node.generate(1)

        self.contract = EVMContract.from_file("Events.sol", "TestEvents")
        abi, bytecode, _ = self.contract.compile()
        compiled = self.node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 500_000,
                "gas": 1_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract_address = receipt["contractAddress"]
        self.contract = self.node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

        self.rollback_height = self.node.getblockcount()

    def no_priority_fee(self):
        self.rollback_to(self.rollback_height)

        before_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        receipt = self.send_tx(
            self.contract.functions.store(10).build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "maxPriorityFeePerGas": "0x0",
                }
            )
        )  # send TX
        after_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        assert_equal(
            before_balance - after_balance,
            receipt["gasUsed"] * receipt["effectiveGasPrice"],
        )

        miner = self.node.w3.eth.get_block("latest")["miner"]
        assert_equal(self.w3.eth.get_balance(miner), 0)  # no miner tip

    def low_priority_fee(self):
        self.rollback_to(self.rollback_height)
        priority_fee = self.node.w3.to_wei("10", "gwei")

        before_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        receipt = self.send_tx(
            self.contract.functions.store(10).build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "maxPriorityFeePerGas": priority_fee,
                }
            )
        )  # send TX
        after_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)

        assert_equal(
            before_balance - after_balance,
            receipt["gasUsed"] * receipt["effectiveGasPrice"],
        )

        block = self.node.w3.eth.get_block("latest")
        miner = block["miner"]
        base_fee = block["baseFeePerGas"]
        miner_balance = self.w3.eth.get_balance(miner)
        assert_equal(
            miner_balance, priority_fee * receipt["gasUsed"]
        )  # priority fee should be sent to miner
        assert_equal(
            before_balance - after_balance - miner_balance,
            base_fee * receipt["gasUsed"],
        )  # base fee should be burnt

    def max_fee_cap(self):
        # Tests case where priority fee is capped by maxFeePerGas
        self.rollback_to(self.rollback_height)
        priority_fee = self.node.w3.to_wei("10", "gwei")
        max_fee = self.node.w3.to_wei(
            "15", "gwei"
        )  # we should pay 10 gwei base + 5 gwei priority

        before_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        receipt = self.send_tx(
            self.contract.functions.store(10).build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "maxPriorityFeePerGas": priority_fee,
                    "maxFeePerGas": max_fee,
                }
            )
        )  # send TX
        after_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)

        assert_equal(
            before_balance - after_balance,
            receipt["gasUsed"] * receipt["effectiveGasPrice"],
        )

        block = self.node.w3.eth.get_block("latest")
        miner = block["miner"]
        base_fee = block["baseFeePerGas"]
        miner_balance = self.w3.eth.get_balance(miner)

        assert_equal(
            receipt["effectiveGasPrice"],
            base_fee + min(max_fee - base_fee, priority_fee),
        )
        assert_equal(
            before_balance - after_balance,
            receipt["effectiveGasPrice"] * receipt["gasUsed"],
        )
        assert_equal(
            miner_balance, (max_fee - base_fee) * receipt["gasUsed"]
        )  # capped priority fee should be sent to miner
        assert_equal(
            before_balance - after_balance - miner_balance,
            base_fee * receipt["gasUsed"],
        )  # base fee should be burnt

    def priority_fee_higher_max_fee(self):
        # Tests case where priority fee is capped by maxFeePerGas
        self.rollback_to(self.rollback_height)
        priority_fee = self.node.w3.to_wei("20", "gwei")
        max_fee = self.node.w3.to_wei(
            "15", "gwei"
        )  # we should pay 10 gwei base + 5 gwei priority

        before_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        receipt = self.send_tx(
            self.contract.functions.store(10).build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "maxPriorityFeePerGas": priority_fee,
                    "maxFeePerGas": max_fee,
                }
            )
        )  # send TX
        after_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)

        assert_equal(
            before_balance - after_balance,
            receipt["gasUsed"] * receipt["effectiveGasPrice"],
        )

        block = self.node.w3.eth.get_block("latest")
        miner = block["miner"]
        base_fee = block["baseFeePerGas"]
        miner_balance = self.w3.eth.get_balance(miner)

        assert_equal(
            receipt["effectiveGasPrice"],
            base_fee + min(max_fee - base_fee, priority_fee),
        )
        assert_equal(
            before_balance - after_balance,
            receipt["effectiveGasPrice"] * receipt["gasUsed"],
        )
        assert_equal(
            miner_balance, (max_fee - base_fee) * receipt["gasUsed"]
        )  # capped priority fee should be sent to miner
        assert_equal(
            before_balance - after_balance - miner_balance,
            base_fee * receipt["gasUsed"],
        )  # base fee should be burnt

    def replace_by_fee(self):
        self.rollback_to(self.rollback_height)
        before_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)

        # Test that RBF for EIP1559 txs should be by promised miner tip fees
        priority_fee = self.node.w3.to_wei("20", "gwei")
        max_fee = self.node.w3.to_wei("50", "gwei")

        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee,
                "maxFeePerGas": max_fee,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        assert_equal(len(self.nodes[0].getrawmempool()), 1)

        # RBF should happen
        priority_fee1 = self.node.w3.to_wei("30", "gwei")
        max_fee1 = self.node.w3.to_wei("50", "gwei")
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee1,
                "maxFeePerGas": max_fee1,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        assert_equal(len(self.nodes[0].getrawmempool()), 1)

        # send lower priority fee tx, RBF should not happen
        priority_fee2 = self.node.w3.to_wei("25", "gwei")
        max_fee2 = self.node.w3.to_wei("50", "gwei")
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee2,
                "maxFeePerGas": max_fee2,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        assert_raises_web3_error(
            -32001,
            "evm-low-fee",
            self.node.w3.eth.send_raw_transaction,
            signed.rawTransaction,
        )
        assert_equal(len(self.nodes[0].getrawmempool()), 1)

        # send same priority fees, RBF should not happen
        priority_fee3 = self.node.w3.to_wei("30", "gwei")
        max_fee3 = self.node.w3.to_wei("100", "gwei")
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee3,
                "maxFeePerGas": max_fee3,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        assert_raises_web3_error(
            -32001,
            "evm-low-fee",
            self.node.w3.eth.send_raw_transaction,
            signed.rawTransaction,
        )

        # send higher max fee tx but lower priority fee tx, RBF should not happen
        priority_fee4 = self.node.w3.to_wei("25", "gwei")
        max_fee4 = self.node.w3.to_wei("100", "gwei")
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee4,
                "maxFeePerGas": max_fee4,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        assert_raises_web3_error(
            -32001,
            "evm-low-fee",
            self.node.w3.eth.send_raw_transaction,
            signed.rawTransaction,
        )

        # send less than 10% increase in priority fees, RBF should not happen
        priority_fee5 = self.node.w3.to_wei("32", "gwei")
        max_fee5 = self.node.w3.to_wei("100", "gwei")
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee5,
                "maxFeePerGas": max_fee5,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        assert_raises_web3_error(
            -32001,
            "evm-low-fee",
            self.node.w3.eth.send_raw_transaction,
            signed.rawTransaction,
        )

        # send exactly 10% increase in priority fees, RBF should happen
        priority_fee6 = self.node.w3.to_wei("33", "gwei")
        max_fee6 = self.node.w3.to_wei("100", "gwei")
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxPriorityFeePerGas": priority_fee6,
                "maxFeePerGas": max_fee6,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        assert_equal(len(self.nodes[0].getrawmempool()), 1)

        self.nodes[0].generate(1)
        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        after_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)

        block = self.node.w3.eth.get_block("latest")
        miner = block["miner"]
        base_fee = block["baseFeePerGas"]
        miner_balance = self.w3.eth.get_balance(miner)

        assert_equal(
            receipt["effectiveGasPrice"],
            base_fee + min(max_fee6 - base_fee, priority_fee6),
        )
        assert_equal(
            before_balance - after_balance,
            receipt["effectiveGasPrice"] * receipt["gasUsed"],
        )
        assert_equal(
            miner_balance, min(max_fee6 - base_fee, priority_fee6) * receipt["gasUsed"]
        )  # priority fee should be sent to miner
        assert_equal(
            before_balance - after_balance - miner_balance,
            base_fee * receipt["gasUsed"],
        )  # base fee should be burnt

    def run_test(self):
        self.setup()

        self.no_priority_fee()

        self.low_priority_fee()

        self.max_fee_cap()

        self.priority_fee_higher_max_fee()

        self.replace_by_fee()


if __name__ == "__main__":
    EIP1559Fees().main()

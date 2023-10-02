#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM full blocks"""

from test_framework.util import assert_equal
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair


class EVMFullBlockTest(DefiTestFramework):
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
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def run_test(self):
        # Set up test
        self.setup()

        # Create stress test contract
        self.create_stress_test_contract()

        # Save start height for test reset
        self.start_height = self.nodes[0].getblockcount()

        # Create full block of small TXs
        self.bench_full_block()

        # Create full block of large TXs
        self.bench_large_txs()

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

        self.evm_key_pairs = []
        for x in range(21):
            self.evm_key_pairs.append(EvmKeyPair.from_node(self.nodes[0]))
            self.nodes[0].transferdomain(
                [
                    {
                        "src": {
                            "address": self.address,
                            "amount": "5@DFI",
                            "domain": 2,
                        },
                        "dst": {
                            "address": self.evm_key_pairs[x].address,
                            "amount": "5@DFI",
                            "domain": 3,
                        },
                    }
                ]
            )
        self.nodes[0].generate(1)

    def create_stress_test_contract(self):
        # Compile contract
        abi, bytecode, _ = EVMContract.from_file(
            "StressTest.sol", "StressTest"
        ).compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)

        # Build contract TX
        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(
                    self.evm_key_pairs[0].address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )

        # Sign contract TX
        signed = self.nodes[0].w3.eth.account.sign_transaction(
            tx, self.evm_key_pairs[0].privkey
        )

        # Send contract TX
        tx_hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        # Wait for TX receipt
        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(tx_hash)
        self.contract = self.nodes[0].w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def bench_full_block(self):
        # Reset test
        self.rollback_to(self.start_height)

        for evm_key_pair in self.evm_key_pairs:
            # Get start nonce
            start_nonce = self.nodes[0].w3.eth.get_transaction_count(
                evm_key_pair.address
            )

            for i in range(64):
                # Build contract TX
                tx = self.contract.functions.burnGas(2).build_transaction(
                    {
                        "chainId": self.nodes[0].w3.eth.chain_id,
                        "nonce": start_nonce + i,
                        "gasPrice": 10_000_000_000,
                    }
                )

                # Sign contract TX
                signed = self.nodes[0].w3.eth.account.sign_transaction(
                    tx, evm_key_pair.privkey
                )

                # Send contract TX
                self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)

        # Check mempool
        assert_equal(
            self.nodes[0].getmempoolinfo()["size"], len(self.evm_key_pairs) * 64
        )

        # Mint block
        self.nodes[0].generate(1)

        # Get block and check TX count
        block_txs = self.nodes[0].getblock(self.nodes[0].getbestblockhash())["tx"]
        assert_equal(
            len(block_txs), len(self.evm_key_pairs) * 64 - 2 + 1
        )  # -2 oversize TXs 1 coinbase

        # Check leftover TXs in mempool
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 2)

        # Check block gas used
        eth_block = self.nodes[0].vmmap(self.nodes[0].getbestblockhash(), 3)["output"]
        eth_block = self.nodes[0].eth_getBlockByHash(eth_block)
        assert_equal(29997726, int(eth_block["gasUsed"], 16))

    def bench_large_txs(self):
        # Reset test
        self.rollback_to(self.start_height)

        for evm_key_pair in self.evm_key_pairs:
            # Get start nonce
            start_nonce = self.nodes[0].w3.eth.get_transaction_count(
                evm_key_pair.address
            )

            for i in range(64):
                # Build contract TX
                tx = self.contract.functions.burnGas(27189).build_transaction(
                    {
                        "chainId": self.nodes[0].w3.eth.chain_id,
                        "nonce": start_nonce + i,
                        "gasPrice": 10_000_000_000,
                    }
                )

                # Sign contract TX
                signed = self.nodes[0].w3.eth.account.sign_transaction(
                    tx, evm_key_pair.privkey
                )

                # Send contract TX
                self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)

        # Check mempool
        assert_equal(
            self.nodes[0].getmempoolinfo()["size"], len(self.evm_key_pairs) * 64
        )

        # Mint block
        self.nodes[0].generate(1)

        # Get block and check TX count 3x 10M Eth TXs + coinbase
        block_txs = self.nodes[0].getblock(self.nodes[0].getbestblockhash())["tx"]
        assert_equal(len(block_txs), 3 + 1)

        # Check leftover TXs in mempool
        assert_equal(
            self.nodes[0].getmempoolinfo()["size"], len(self.evm_key_pairs) * 64 - 3
        )

        # Check block gas used
        eth_block = self.nodes[0].vmmap(self.nodes[0].getbestblockhash(), 3)["output"]
        eth_block = self.nodes[0].eth_getBlockByHash(eth_block)
        assert_equal(29999982, int(eth_block["gasUsed"], 16))


if __name__ == "__main__":
    EVMFullBlockTest().main()

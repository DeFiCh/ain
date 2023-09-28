#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM mempool behaviour"""
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    hex_to_decimal,
)


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
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address_erc55 = self.nodes[0].addressmap(self.address, 1)["format"][
            "erc55"
        ]
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
                }
            ]
        )
        self.nodes[0].generate(1)
        self.start_height = self.nodes[0].getblockcount()

    def same_nonce_transferdomain_and_evm_txs(self):
        self.rollback_to(self.start_height)
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        self.nodes[0].evmtx(self.ethAddress, nonce, 21, 21001, self.toAddress, 1)
        self.nodes[0].evmtx(self.ethAddress, nonce, 30, 21001, self.toAddress, 1)
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@DFI", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@DFI",
                        "domain": 2,
                    },
                    "nonce": nonce,
                }
            ]
        )
        assert_equal(self.nodes[0].getrawmempool().count(tx), True)
        self.nodes[0].generate(1)
        block_height = self.nodes[0].getblockcount()
        assert_equal(block_height, self.start_height + 1)

    def evm_tx_rbf_with_transferdomain_should_fail(self):
        self.rollback_to(self.start_height)
        self.nodes[0].utxostoaccount({self.address: "200@DFI"})
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.address_erc55)
        self.nodes[0].transfer_domain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )

        # Ensure transferdomain tx has highest priority over evm tx
        assert_raises_rpc_error(
            -32001,
            "evm-low-fee",
            self.nodes[0].eth_sendTransaction,
            {
                "nonce": self.nodes[0].w3.to_hex(nonce),
                "from": self.address_erc55,
                "to": "0x0000000000000000000000000000000000000000",
                "value": "0x1",
                "gas": "0x100000",
                "gasPrice": "0xfffffffffff",
            },
        )

        # Manually set nonce in transferdomain
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.address_erc55,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "nonce": nonce + 100,
                }
            ]
        )
        # Ensure transferdomain tx has highest priority over evm tx
        assert_raises_rpc_error(
            -32001,
            "evm-low-fee",
            self.nodes[0].eth_sendTransaction,
            {
                "nonce": self.nodes[0].w3.to_hex(nonce + 100),
                "from": self.address_erc55,
                "to": "0x0000000000000000000000000000000000000000",
                "value": "0x1",
                "gas": "0x100000",
                "gasPrice": "0xfffffffffff",
            },
        )

    def mempool_tx_limit(self):
        self.rollback_to(self.start_height)
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)

        # Test max limit of TX from a specific sender
        for i in range(64):
            self.nodes[0].evmtx(
                self.ethAddress, nonce + i, 21, 21001, self.toAddress, 1
            )

        # Test error at the 64th EVM TX
        assert_raises_rpc_error(
            -26,
            "too-many-evm-txs-by-sender",
            self.nodes[0].evmtx,
            self.ethAddress,
            nonce + 64,
            21,
            21001,
            self.toAddress,
            1,
        )

        # Mint a block
        self.nodes[0].generate(1)
        self.blockHash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        block_txs = self.nodes[0].getblock(
            self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        )["tx"]
        assert_equal(len(block_txs), 65)

        # Check accounting of EVM fees
        txLegacy = {
            "nonce": "0x1",
            "from": self.ethAddress,
            "value": "0x1",
            "gas": "0x5208",  # 21000
            "gasPrice": "0x4e3b29200",  # 21_000_000_000,
        }
        fees = self.nodes[0].debug_feeEstimate(txLegacy)
        burnt_fee = hex_to_decimal(fees["burnt_fee"])
        priority_fee = hex_to_decimal(fees["priority_fee"])

        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], burnt_fee * 64)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min"], burnt_fee * 64
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], burnt_fee * 64
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], priority_fee * 64
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"],
            priority_fee * 64,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            priority_fee * 64,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash,
        )

        # Check Eth balances after transfer
        assert_equal(
            int(self.nodes[0].eth_getBalance(self.ethAddress)[2:], 16),
            35971776000000000000,
        )
        assert_equal(
            int(self.nodes[0].eth_getBalance(self.toAddress)[2:], 16),
            64000000000000000000,
        )

        # Try and send another TX to make sure mempool has removed entries
        tx = self.nodes[0].evmtx(
            self.ethAddress, nonce + 64, 21, 21001, self.toAddress, 1
        )
        self.nodes[0].generate(1)
        self.blockHash1 = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        # Check accounting of EVM fees
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt"], burnt_fee * 65)
        assert_equal(attributes["v0/live/economy/evm/block/fee_burnt_min"], burnt_fee)
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_min_hash"], self.blockHash1
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max"], burnt_fee * 64
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_burnt_max_hash"], self.blockHash
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority"], priority_fee * 65
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min"], priority_fee
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_min_hash"],
            self.blockHash1,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max"],
            priority_fee * 64,
        )
        assert_equal(
            attributes["v0/live/economy/evm/block/fee_priority_max_hash"],
            self.blockHash,
        )

        # Check TX is in block
        block_txs = self.nodes[0].getblock(
            self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        )["tx"]
        assert_equal(block_txs[1], tx)

    def rbf_sender_mempool_limit(self):
        self.rollback_to(self.start_height)
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        tx = self.nodes[0].evmtx(self.ethAddress, nonce, 21, 21001, self.toAddress, 1)
        mempool_info = self.nodes[0].getrawmempool()
        assert_equal(len(mempool_info), 1)
        assert_equal(mempool_info.count(tx), True)

        for i in range(40):
            # Check evmtx RBF succeeds
            tx = self.nodes[0].evmtx(
                self.ethAddress, nonce, 22 + i, 21001, self.toAddress, 1
            )
            mempool_info = self.nodes[0].getrawmempool()
            assert_equal(len(mempool_info), 1)
            assert_equal(mempool_info.count(tx), True)

        # Check mempool rejects the 41st RBF evmtx by the same sender
        assert_raises_rpc_error(
            -26,
            "too-many-evm-rbf-txs-by-sender",
            self.nodes[0].evmtx,
            self.ethAddress,
            nonce,
            62,
            21001,
            self.toAddress,
            1,
        )

        self.nodes[0].generate(1)
        # Check TX is in block
        block_txs = self.nodes[0].getblock(
            self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        )["tx"]
        assert_equal(block_txs[1], tx)

        # Check mempool allows sender to do RBF once sender's evm tx is minted
        tx = self.nodes[0].evmtx(
            self.ethAddress, nonce + 1, 21, 21001, self.toAddress, 1
        )
        for i in range(40):
            # Check evmtx RBF succeeds
            tx = self.nodes[0].evmtx(
                self.ethAddress, nonce + 1, 22 + i, 21001, self.toAddress, 1
            )
            mempool_info = self.nodes[0].getrawmempool()
            assert_equal(len(mempool_info), 1)
            assert_equal(mempool_info.count(tx), True)

        # Check mempool rejects the 41st RBF evmtx by the same sender
        assert_raises_rpc_error(
            -26,
            "too-many-evm-rbf-txs-by-sender",
            self.nodes[0].evmtx,
            self.ethAddress,
            nonce + 1,
            62,
            21001,
            self.toAddress,
            1,
        )

    def run_test(self):
        self.setup()

        # Test for transferdomain and evmtx with same nonce
        self.same_nonce_transferdomain_and_evm_txs()

        # Mempool limit of 64 TXs
        self.mempool_tx_limit()

        # Test for RBF sender mempool limit
        self.rbf_sender_mempool_limit()


if __name__ == "__main__":
    EVMTest().main()

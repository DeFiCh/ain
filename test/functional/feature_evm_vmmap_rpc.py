#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test vmmap behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class VMMapType:
    Auto = 0
    BlockNumberDVMToEVM = 1
    BlockNumberEVMToDVM = 2
    BlockHashDVMToEVM = 3
    BlockHashEVMToDVM = 4
    TxHashDVMToEVM = 5
    TxHashEVMToDVM = 6


class VMMapTests(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        extra_args = [
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
        self.extra_args = [extra_args, extra_args]

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
        # We generate 2 to ensure we have one valid evm block
        # One enabling evm and one with evm enabled
        self.nodes[0].generate(2)
        self.start_block_height = self.nodes[0].getblockcount()

    def vmmap_valid_tx_should_succeed(self):
        self.rollback_to(self.start_block_height)
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
        tx_maps = []
        nonce = 0
        for i in range(5):
            # generate 5 txs in the block
            num_txs = 5
            for j in range(num_txs):
                self.nodes[0].evmtx(
                    self.ethAddress, nonce, 21, 21000, self.toAddress, 1
                )
                nonce += 1
            self.nodes[0].generate(1)
            dfi_block = self.nodes[0].getblock(self.nodes[0].getbestblockhash())
            eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)
            for j in range(num_txs):
                # note dfi block is j+1 since we ignore coinbase
                tx_maps.append([dfi_block["tx"][j + 1], eth_block["transactions"][j]])
        for item in tx_maps:
            res = self.nodes[0].vmmap(item[0], VMMapType.TxHashDVMToEVM)
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "TxHashDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[1], VMMapType.TxHashEVMToDVM)
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "TxHashEVMToDVM")
            assert_equal(res["output"], item[0])

            res = self.nodes[0].vmmap(item[0], VMMapType.Auto)
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "TxHashDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[0])
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "TxHashDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[1], VMMapType.Auto)
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "TxHashEVMToDVM")
            assert_equal(res["output"], item[0])

            res = self.nodes[0].vmmap(item[1])
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "TxHashEVMToDVM")
            assert_equal(res["output"], item[0])

    def vmmap_invalid_should_fail(self):
        self.rollback_to(self.start_block_height)
        latest_eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)["hash"]
        fake_evm_tx = (
            "0x0000000000000000000000000000000000000000000000000000000000000000"
        )
        assert_err = lambda *args: assert_raises_rpc_error(
            -32600, None, self.nodes[0].vmmap, *args
        )
        for map_type in range(7):
            if map_type in [0, 1, 2]:
                continue  # auto and num are ignored for this test
            assert_raises_rpc_error(
                -32600,
                "Key not found: " + fake_evm_tx[2:],
                self.nodes[0].vmmap,
                fake_evm_tx,
                map_type,
            )
            assert_err("0x00", map_type)
            assert_err("garbage", map_type)
            assert_err(latest_eth_block + "0x00", map_type)  # invalid suffix
            assert_err("0x00" + latest_eth_block, map_type)  # invalid prefix

    def vmmap_valid_block_should_succeed(self):
        self.rollback_to(self.start_block_height)
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
        block_maps = []
        for i in range(5):
            self.nodes[0].evmtx(self.ethAddress, i, 21, 21000, self.toAddress, 1)
            self.nodes[0].generate(1)
            dfi_block = self.nodes[0].getblock(self.nodes[0].getbestblockhash())
            eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)
            block_maps.append([dfi_block["hash"], eth_block["hash"]])
        for item in block_maps:
            res = self.nodes[0].vmmap(item[0], VMMapType.BlockHashDVMToEVM)
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "BlockHashDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[1], VMMapType.BlockHashEVMToDVM)
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "BlockHashEVMToDVM")
            assert_equal(res["output"], item[0])

            res = self.nodes[0].vmmap(item[0], VMMapType.Auto)
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "BlockHashDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[0])
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "BlockHashDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[1], VMMapType.Auto)
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "BlockHashEVMToDVM")
            assert_equal(res["output"], item[0])

            res = self.nodes[0].vmmap(item[1])
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "BlockHashEVMToDVM")
            assert_equal(res["output"], item[0])

    def vmmap_valid_block_number_should_succeed(self):
        self.rollback_to(self.start_block_height)
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
        block_maps = []
        for i in range(5):
            self.nodes[0].evmtx(self.ethAddress, i, 21, 21000, self.toAddress, 1)
            self.nodes[0].generate(1)
            dfi_block = self.nodes[0].getblock(self.nodes[0].getbestblockhash())
            eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)
            block_maps.append(
                [str(dfi_block["height"]), str(int(eth_block["number"], 16))]
            )
        for item in block_maps:
            res = self.nodes[0].vmmap(item[0], VMMapType.BlockNumberDVMToEVM)
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "BlockNumberDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[1], VMMapType.BlockNumberEVMToDVM)
            assert_equal(res["input"], item[1])
            assert_equal(res["type"], "BlockNumberEVMToDVM")
            assert_equal(res["output"], item[0])

            res = self.nodes[0].vmmap(item[0], VMMapType.Auto)
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "BlockNumberDVMToEVM")
            assert_equal(res["output"], item[1])

            res = self.nodes[0].vmmap(item[0])
            assert_equal(res["input"], item[0])
            assert_equal(res["type"], "BlockNumberDVMToEVM")
            assert_equal(res["output"], item[1])

            assert_raises_rpc_error(
                -8,
                "Automatic detection not viable for input",
                self.nodes[0].vmmap,
                item[1],
                VMMapType.Auto,
            )

            assert_raises_rpc_error(
                -8,
                "Automatic detection not viable for input",
                self.nodes[0].vmmap,
                item[1],
            )

    def vmmap_invalid_block_number_should_fail(self):
        assert_invalid = lambda *args: assert_raises_rpc_error(
            -8, "Invalid block number", self.nodes[0].vmmap, *args
        )
        for x in ["-1", "garbage", "1000000000"]:
            assert_invalid(x, VMMapType.BlockNumberDVMToEVM)
            assert_invalid(x, VMMapType.BlockNumberEVMToDVM)

    def vmmap_auto_invalid_input_should_fail(self):
        self.rollback_to(self.start_block_height)
        assert_raises_rpc_error(
            -8,
            "Automatic detection not viable for input",
            self.nodes[0].vmmap,
            "test",
            VMMapType.Auto,
        )
        assert_raises_rpc_error(
            -8, "Automatic detection not viable for input", self.nodes[0].vmmap, "test"
        )

    def vmmap_rollback_should_succeed(self):
        self.rollback_to(self.start_block_height)
        # Check if invalidate block is working for mapping. After invalidating block, the transaction and block shouldn't be mapped anymore.
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
        base_block = self.nodes[0].eth_getBlockByNumber("latest", False)["hash"]
        tx = self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)
        base_block_dvm = self.nodes[0].getbestblockhash()
        new_block = self.nodes[0].eth_getBlockByNumber("latest", False)["hash"]
        list_blocks = self.nodes[0].logvmmaps(0)
        list_blocks = list(list_blocks["indexes"].values())
        list_tx = self.nodes[0].logvmmaps(2)
        list_tx = list(list_tx["indexes"].keys())
        assert_equal(base_block[2:] in list_blocks, True)
        assert_equal(new_block[2:] in list_blocks, True)
        assert_equal(tx in list_tx, True)
        self.nodes[0].invalidateblock(base_block_dvm)
        list_blocks = self.nodes[0].logvmmaps(0)
        list_blocks = list(list_blocks["indexes"].values())
        list_tx = self.nodes[0].logvmmaps(2)
        list_tx = list(list_tx["indexes"].keys())
        assert_equal(base_block[2:] in list_blocks, True)
        assert_equal(new_block[2:] in list_blocks, False)
        assert_equal(tx in list_tx, False)

    def logvmmaps_tx_exist(self):
        self.rollback_to(self.start_block_height)
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
        tx = self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)
        list_tx = self.nodes[0].logvmmaps(2)
        eth_tx = self.nodes[0].eth_getBlockByNumber("latest", False)["transactions"][0]
        assert_equal(eth_tx[2:] in list(list_tx["indexes"].values()), True)
        assert_equal(tx in list(list_tx["indexes"].keys()), True)

    def logvmmaps_invalid_tx_should_fail(self):
        list_tx = self.nodes[0].logvmmaps(2)
        assert_equal("invalid tx" not in list(list_tx["indexes"].values()), True)
        assert_equal(
            "0x0000000000000000000000000000000000000000000000000000000000000000"
            not in list(list_tx["indexes"].values()),
            True,
        )
        assert_equal("garbage" not in list(list_tx["indexes"].values()), True)
        assert_equal("0x" not in list(list_tx["indexes"].values()), True)

    def logvmmaps_block_exist(self):
        list_blocks = self.nodes[0].logvmmaps(0)
        eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)["hash"]
        assert_equal(eth_block[2:] in list(list_blocks["indexes"].values()), True)
        dfi_block = self.nodes[0].vmmap(eth_block, VMMapType.BlockHashEVMToDVM)[
            "output"
        ]
        assert_equal(dfi_block in list(list_blocks["indexes"].keys()), True)

    def logvmmaps_invalid_block_should_fail(self):
        list_block = self.nodes[0].logvmmaps(2)
        assert_equal("invalid tx" not in list(list_block["indexes"].values()), True)
        assert_equal(
            "0x0000000000000000000000000000000000000000000000000000000000000000"
            not in list(list_block["indexes"].values()),
            True,
        )
        assert_equal("garbage" not in list(list_block["indexes"].values()), True)
        assert_equal("0x" not in list(list_block["indexes"].values()), True)

    def vmmap_transfer_domain(self):
        self.rollback_to(self.start_block_height)

        # Evm in
        tx = self.nodes[0].transferdomain(
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

        evm_block = self.nodes[0].eth_getBlockByNumber("latest")
        evm_tx = self.nodes[0].vmmap(tx, VMMapType.TxHashDVMToEVM)["output"]
        assert_equal(evm_block["transactions"][0], evm_tx)
        native_tx = self.nodes[0].vmmap(evm_tx, VMMapType.TxHashEVMToDVM)["output"]
        assert_equal(tx, native_tx)

        # Evm out
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.ethAddress,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address,
                        "amount": "100@DFI",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        evm_block = self.nodes[0].eth_getBlockByNumber("latest")
        evm_tx = self.nodes[0].vmmap(tx, VMMapType.TxHashDVMToEVM)["output"]
        assert_equal(evm_block["transactions"][0], evm_tx)
        native_tx = self.nodes[0].vmmap(evm_tx, VMMapType.TxHashEVMToDVM)["output"]
        assert_equal(tx, native_tx)

    def vmmap_transfer_domain_dst20(self):
        self.rollback_to(self.start_block_height)

        self.nodes[0].createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)
        self.nodes[0].minttokens("1@BTC")
        self.nodes[0].generate(1)

        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                }
            }
        )
        self.nodes[0].generate(1)

        # Evm in
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@BTC", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "1@BTC",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        evm_block = self.nodes[0].eth_getBlockByNumber("latest")
        evm_tx = self.nodes[0].vmmap(tx, VMMapType.TxHashDVMToEVM)["output"]
        assert_equal(evm_block["transactions"][0], evm_tx)
        native_tx = self.nodes[0].vmmap(evm_tx, VMMapType.TxHashEVMToDVM)["output"]
        assert_equal(tx, native_tx)

        # Evm out
        tx = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.ethAddress, "amount": "1@BTC", "domain": 3},
                    "dst": {
                        "address": self.address,
                        "amount": "1@BTC",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        evm_block = self.nodes[0].eth_getBlockByNumber("latest")
        evm_tx = self.nodes[0].vmmap(tx, VMMapType.TxHashDVMToEVM)["output"]
        assert_equal(evm_block["transactions"][0], evm_tx)
        native_tx = self.nodes[0].vmmap(evm_tx, VMMapType.TxHashEVMToDVM)["output"]
        assert_equal(tx, native_tx)

    def run_test(self):
        self.setup()
        # vmmap tests
        self.vmmap_valid_tx_should_succeed()
        self.vmmap_valid_block_should_succeed()
        self.vmmap_invalid_should_fail()
        self.vmmap_valid_block_number_should_succeed()
        self.vmmap_invalid_block_number_should_fail()
        self.vmmap_rollback_should_succeed()
        self.vmmap_auto_invalid_input_should_fail()
        self.vmmap_transfer_domain()
        self.vmmap_transfer_domain_dst20()

        # logvmmap tests
        self.logvmmaps_tx_exist()
        self.logvmmaps_invalid_tx_should_fail()
        self.logvmmaps_block_exist()
        self.logvmmaps_invalid_block_should_fail()


if __name__ == "__main__":
    VMMapTests().main()

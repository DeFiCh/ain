#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test eth_createAccessList RPC behaviour"""

from test_framework.evm_key_pair import EvmKeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.util import (
    assert_equal,
    get_solc_artifact_path,
)

from decimal import Decimal
import math
import json
from web3 import Web3


class AccessListTest(DefiTestFramework):
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
                "-metachainheight=153",
                "-df23height=155",
                "-subsidytest=1",
            ]
        ]

    def setup(self):
        self.w0 = self.nodes[0].w3
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.erc55_address = self.nodes[0].addressmap(self.address, 1)["format"][
            "erc55"
        ]

        # Contract addresses
        self.contract_address_transfer_domain = self.w0.to_checksum_address(
            "0xdf00000000000000000000000000000000000001"
        )
        self.contract_address_usdt = self.w0.to_checksum_address(
            "0xff00000000000000000000000000000000000001"
        )

        # Contract ABI
        # Implementation ABI since we want to call functions from the implementation
        self.abi = open(
            get_solc_artifact_path("dst20_v1", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # Proxy bytecode since we want to check proxy deployment
        self.bytecode = json.loads(
            open(
                get_solc_artifact_path("dst20", "deployed_bytecode.json"),
                "r",
                encoding="utf8",
            ).read()
        )["object"]

        self.reserved_bytecode = json.loads(
            open(
                get_solc_artifact_path("dfi_reserved", "deployed_bytecode.json"),
                "r",
                encoding="utf8",
            ).read()
        )["object"]

        # Generate chain
        self.nodes[0].generate(153)
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                }
            }
        )
        self.nodes[0].generate(2)

        self.nodes[0].utxostoaccount({self.address: "1000@DFI"})

        # Create token before EVM
        self.nodes[0].createtoken(
            {
                "symbol": "USDT",
                "name": "USDT token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        self.nodes[0].minttokens("10@USDT")
        self.nodes[0].generate(1)

        self.key_pair = EvmKeyPair.from_node(self.nodes[0])
        self.key_pair2 = EvmKeyPair.from_node(self.nodes[0])

        self.usdt = self.nodes[0].w3.eth.contract(
            address=self.contract_address_usdt, abi=self.abi
        )

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "1@USDT", "domain": 2},
                    "dst": {
                        "address": self.key_pair.address,
                        "amount": "1@USDT",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        balance = self.usdt.functions.balanceOf(
            self.key_pair.address
        ).call() / math.pow(10, self.usdt.functions.decimals().call())
        assert_equal(balance, Decimal(1))

        self.start_height = self.nodes[0].getblockcount()
        self.transfer_data = self.usdt.encodeABI(
            fn_name="transfer",
            args=[self.key_pair2.address, Web3.to_wei("0.5", "ether")],
        )

    def test_rpc_contract_call(self):
        self.rollback_to(self.start_height)

        call_tx = {
            "from": self.key_pair.address,
            "to": self.contract_address_usdt,
            "data": self.transfer_data,
        }
        access_list = self.nodes[0].eth_createAccessList(call_tx)["accessList"]
        assert_equal(len(access_list), 1)
        assert_equal(len(access_list[0]["storageKeys"]), 3)
        assert_equal(
            access_list[0]["address"], "0xff00000000000000000000000000000000000001"
        )

    def test_rpc_contract_creation(self):
        self.rollback_to(self.start_height)

        _, bytecode, _ = EVMContract.from_file("Reverter.sol", "Reverter").compile()
        call_tx = {
            "from": self.key_pair.address,
            "data": "0x" + bytecode,
        }
        access_list = self.nodes[0].eth_createAccessList(call_tx)["accessList"]
        assert_equal(access_list, [])

    def run_test(self):
        self.setup()

        self.test_rpc_contract_call()

        self.test_rpc_contract_creation()


if __name__ == "__main__":
    AccessListTest().main()

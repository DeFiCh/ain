#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DFI intrinsics contract"""

import json

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, get_solc_artifact_path
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair


class DFIIntrinsicsTest(DefiTestFramework):
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
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def run_test(self):
        node = self.nodes[0]
        node.generate(105)

        # Activate EVM
        node.setgov({"ATTRIBUTES": {"v0/params/feature/evm": "true"}})
        node.generate(2)

        # check reserved address space
        reserved_bytecode = json.loads(
            open(
                get_solc_artifact_path("dfi_reserved", "deployed_bytecode.json"),
                "r",
                encoding="utf8",
            ).read()
        )["object"]

        for i in range(5, 128):
            address = node.w3.to_checksum_address(generate_formatted_string(i))
            code_at_addr = self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(address)
            )
            assert_equal(code_at_addr, reserved_bytecode)

        assert (
            self.nodes[0].w3.to_hex(
                self.nodes[0].w3.eth.get_code(
                    node.w3.to_checksum_address(generate_formatted_string(128))
                )
            )
            != reserved_bytecode
        )

        # check intrinsics contract
        registry_abi = open(
            get_solc_artifact_path("dfi_intrinsics_registry", "abi.json"),
            "r",
            encoding="utf8",
        ).read()
        abi = open(
            get_solc_artifact_path("dfi_intrinsics", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        registry = node.w3.eth.contract(
            address=node.w3.to_checksum_address("0xdf00000000000000000000000000000000000000"),
            abi=registry_abi
        )
        v1_address = registry.functions.getAddressForVersion(0).call()

        contract_0 = node.w3.eth.contract(
            address=v1_address,
            abi=abi,
        )

        num_blocks = 5
        state_roots = set()
        for i in range(num_blocks):
            node.generate(1)
            block = node.w3.eth.get_block("latest")
            state_roots.add(node.w3.to_hex(block["stateRoot"]))

            # check evmBlockCount variable
            assert_equal(
                contract_0.functions.evmBlockCount().call(),
                node.w3.eth.get_block_number(),
            )

            # check version variable
            assert_equal(contract_0.functions.version().call(), 1)

            # check dvmBlockCount variable
            assert_equal(
                contract_0.functions.dvmBlockCount().call(), node.getblockcount()
            )

        assert_equal(len(state_roots), num_blocks)


def generate_formatted_string(input_number):
    hex_representation = hex(input_number)[2:]  # Convert to hex and remove '0x' prefix

    if len(hex_representation) > 32:
        hex_representation = hex_representation[:32]  # Truncate if too long

    padding = "0" * (38 - len(hex_representation))
    formatted_string = f"0xdf{padding}{hex_representation}"

    return formatted_string


if __name__ == "__main__":
    DFIIntrinsicsTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test DFI intrinsics contract"""

import os

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal
)

class DFIIntrinsicsTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def run_test(self):
        node = self.nodes[0]
        node.generate(105)

        # Activate EVM
        node.setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        node.generate(1)

        # check counter contract
        from web3 import Web3
        w3 = Web3(Web3.HTTPProvider(self.nodes[0].get_evm_rpc()))
        # Temp. workaround
        abi = open(f"{os.path.dirname(__file__)}/../../lib/ain-contracts/dfi_intrinsics/output/abi.json", "r", encoding="utf8").read()
        counter_contract = w3.eth.contract(
            address="0x0000000000000000000000000000000000000301", abi=abi
        )

        num_blocks = 5
        state_roots = set()
        for i in range(num_blocks):
            node.generate(1)
            block = w3.eth.get_block('latest')
            state_roots.add(Web3.to_hex(block["stateRoot"]))

            # check evmBlockCount variable
            assert_equal(counter_contract.functions.evmBlockCount().call(), w3.eth.get_block_number())

            # check version variable
            assert_equal(counter_contract.functions.version().call(), 1)

            # check dvmBlockCount variable
            assert_equal(counter_contract.functions.dvmBlockCount().call(), node.getblockcount())

        assert_equal(len(state_roots), num_blocks)


if __name__ == '__main__':
    DFIIntrinsicsTest().main()

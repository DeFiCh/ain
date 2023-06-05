#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract"""

from test_framework.test_framework import DefiTestFramework
from contracts import EVMContract

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(105)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

    def run_test(self):
        self.setup()
        evmcontract = EVMContract("SimpleStorage.sol", "Test", self.nodes[0].get_evm_rpc(), self.nodes[0].generate)
        address = evmcontract.get_address()

        self.nodes[0].transferdomain(1,{self.address:["50@DFI"]}, {address:["50@DFI"]})
        self.nodes[0].generate(1)

        contract = evmcontract.deploy_contract(None)

        # set variable
        evmcontract.sign_and_send(contract.functions.store(10))

        # get variable
        print(contract.functions.retrieve().call())


if __name__ == '__main__':
    EVMTest().main()

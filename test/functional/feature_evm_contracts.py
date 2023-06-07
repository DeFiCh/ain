#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract"""

from test_framework.util import assert_equal
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import KeyPair


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80',
             '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86',
             '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94',
             '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105',
             '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        
        key_pair = KeyPair.from_node(self.nodes[0])
        address = key_pair.address

        # Generate chain
        self.nodes[0].generate(105)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)
        
        self.nodes[0].transferdomain(1, {self.address: ["50@DFI"]}, {address: ["50@DFI"]})
        self.nodes[0].generate(1)
        
        return key_pair
        
    def test_create_and_call_contract(self, key_pair):
        node = self.nodes[0]
        
        evm_contract = EVMContract.from_file("SimpleStorage.sol", "Test").compile()
        contract = node.evm.deploy_compiled_contract(key_pair, evm_contract)

        # set variable
        node.evm.sign_and_send(contract.functions.store(10), key_pair)

        # get variable
        assert_equal(contract.functions.retrieve().call(), 10)
        
    def test_contracts_interaction(self, key_pair):
        node = self.nodes[0]
        
        counter_json = EVMContract.from_file("Counter.sol", "Counter").compile()
        counter_contract = node.evm.deploy_compiled_contract(key_pair, counter_json)
        # print('counter_contract: ', counter_contract)
        # print('counter_contract: ', counter_contract)
        
        # mul = node.evm.sign_and_send(contract.functions.mul(2,3), key_pair)
        # print('mul: ', mul)
        
    def run_test(self):
        key_pair = self.setup()
        
        # self.test_create_and_call_contract(key_pair)
        
        self.test_contracts_interaction(key_pair)
        




if __name__ == '__main__':
    EVMTest().main()

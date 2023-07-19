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
             '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105',
             '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(105)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true', 'v0/params/feature/transferdomain': 'true', 'v0/transferdomain/allowed/dvm-evm': 'true'}})
        self.nodes[0].generate(1)

    def failed_tx_should_increment_nonce(self, key_pair, node):
        from web3 import Web3
        web3 = Web3(Web3.HTTPProvider(node.get_evm_rpc()))

        evm_contract = EVMContract.from_file("Reverter.sol", "Reverter").compile()
        contract = node.evm.deploy_compiled_contract(key_pair, evm_contract)

        # for successful TX
        before_tx_count = web3.eth.get_transaction_count(key_pair.address)

        node.evm.sign_and_send(contract.functions.trySuccess(), key_pair)
        after_tx_count = web3.eth.get_transaction_count(key_pair.address)

        assert_equal(before_tx_count + 1, after_tx_count)

        # for failed TX
        before_tx_count = web3.eth.get_transaction_count(key_pair.address)

        node.evm.sign_and_send(contract.functions.tryRevert(), key_pair)
        after_tx_count = web3.eth.get_transaction_count(key_pair.address)

        assert_equal(before_tx_count + 1, after_tx_count)

    def run_test(self):
        node = self.nodes[0]
        self.setup()

        key_pair = KeyPair.from_node(node)
        address = key_pair.address

        node.transferdomain([{"src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                              "dst": {"address": address, "amount": "50@DFI", "domain": 3}}])
        node.generate(1)

        evm_contract = EVMContract.from_file("SimpleStorage.sol", "Test").compile()
        contract = node.evm.deploy_compiled_contract(key_pair, evm_contract)

        # set variable
        node.evm.sign_and_send(contract.functions.store(10), key_pair)

        # get variable
        assert_equal(contract.functions.retrieve().call(), 10)

        self.failed_tx_should_increment_nonce(key_pair, node)


if __name__ == '__main__':
    EVMTest().main()

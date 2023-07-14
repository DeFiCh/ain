#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
from test_framework.evm_key_pair import KeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256
)
from test_framework.evm_contract import EVMContract

from decimal import Decimal
from web3 import Web3


class DST20(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1'],
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1'],
        ]

    def run_test(self):
        node = self.nodes[0]
        address = node.get_genesis_keys().ownerAuthAddress

        # Generate chain
        node.generate(105)
        self.nodes[0].utxostoaccount({address: "100@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)
        node.transferdomain([{"src": {"address": address, "amount": "50@DFI", "domain": 2},
                              "dst": {"address": "0xeB4B222C3dE281d40F5EBe8B273106bFcC1C1b94", "amount": "50@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        from web3 import Web3
        web3 = Web3(Web3.HTTPProvider(node.get_evm_rpc()))
        web3_n2 = Web3(Web3.HTTPProvider(self.nodes[1].get_evm_rpc()))

        node.createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": address
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        # node.createtoken({
        #     "symbol": "ETH",
        #     "name": "ETH token",
        #     "isDAT": True,
        #     "collateralAddress": address
        # })
        # node.createtoken({
        #     "symbol": "DUSD",
        #     "name": "DUSD token",
        #     "isDAT": True,
        #     "collateralAddress": address
        # })
        # self.nodes[0].generate(1)
        # self.sync_blocks()

        contract_address = "0x0000000000000000000000000000000000000100"
        # print(Web3.to_hex(web3.eth.get_code(contract_address)))

        # try to mint
        key_pair = KeyPair.from_node(node)
        system = KeyPair.from_str("074016d5336e9bb4f204ea5bb536ba5c222ae836b92881a8de8de44e1dfea3a2", "0xeB4B222C3dE281d40F5EBe8B273106bFcC1C1b94")

        nonce = web3.eth.get_transaction_count(system.address)
        # assert_equal(nonce, 3)

        abi = open("./lib/ain-rs-exports/dst20/output/abi.json").read()
        contract = web3.eth.contract(address=contract_address, abi=abi)

        chain_id = web3.eth.chain_id
        call = contract.functions.mint(key_pair.address, Web3.to_wei("1", "ether")).build_transaction({"chainId": chain_id, "from": system.address, "nonce": nonce})
        signed_tx = web3.eth.account.sign_transaction(call, private_key=system.pkey)
        send_tx = web3.eth.send_raw_transaction(signed_tx.rawTransaction)

        node.generate(1)
        self.sync_blocks()

        print(Web3.to_hex(web3.eth.get_code(contract_address, "latest")))
        print(contract.functions.name().call())

        node.generate(1)
        self.sync_blocks()



if __name__ == '__main__':
    DST20().main()

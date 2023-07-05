#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
)
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.nodes[0].generate(200)

        self.nodes[0].utxostoaccount({self.address: "1000@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {
            "v0/params/feature/evm": "true",
            "v0/params/feature/transferdomain": "true",
            'v0/transferdomain/dvm-evm/enabled': 'true',
            'v0/transferdomain/dvm-evm/dat-enabled': 'true',
            'v0/transferdomain/evm-dvm/dat-enabled': 'true',
        }})
        self.nodes[0].generate(1)

        self.key_pair = EvmKeyPair.from_node(self.nodes[0])

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"300@DFI", "domain": 2},
                                       "dst":{"address":self.key_pair.address, "amount":"300@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

    def should_create_counter_contract(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file("Counter.sol", "Counter").compile()

        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.key_pair.address),
                'maxFeePerGas': 10_000_000_000,
                'maxPriorityFeePerGas': 1_500_000_000,
                'gas': 1_000_000,
                # "gasPrice": node.w3.eth.gas_price,
            }
        )

        signed = node.w3.eth.account.sign_transaction(tx, self.key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.Counter = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def should_create_counter_caller_contract(self):
        node = self.nodes[0]

        abi, bytecode = EVMContract.from_file("CounterCaller.sol", "CounterCaller").compile()

        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor(self.Counter.address).build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.key_pair.address),
                'maxFeePerGas': 10_000_000_000,
                'maxPriorityFeePerGas': 1_500_000_000,
                'gas': 1_000_000,
                # "gasPrice": node.w3.eth.gas_price,
            }
        )

        signed = node.w3.eth.account.sign_transaction(tx, self.key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.CounterCaller = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def test_contract_env_vars(self):
        node = self.nodes[0]

        b1hash = self.Counter.functions.getBlockHash(1).call().hex()
        b1 = node.eth_getBlockByNumber(1, False)
        assert_equal(f"0x{b1hash}", b1["hash"])

        n = self.Counter.functions.getBlockNumber().call()
        bn = node.eth_blockNumber()
        assert_equal(f"0x{n}", bn)

        gas_limit = self.Counter.functions.getGasLimit().call()
        assert_equal(gas_limit, 30_000_000)

    # def test_contract_state(self):
    #     node = self.nodes[0]

    #     count = self.Counter.functions.getCount().call()
    #     assert_equal(count, 0)

    #     tx = self.Counter.functions.setCount(5).build_transaction({
    #         'nonce': node.evm.get_transaction_count(self.key_pair.address),
    #         'gas': 500_000,
    #         'maxPriorityFeePerGas': 152_000_000_000,
    #         'maxFeePerGas': 150_000_000_000,
    #     })

    #     signed = node.evm.sign(tx, self.key_pair.pkey)

    #     hash = node.evm.send_raw_tx(signed.rawTransaction)

    #     node.generate(1)

    #     node.evm.wait(hash)

    #     count = self.Counter.functions.getCount().call()
    #     assert_equal(count, 5)

    #     tx = self.Counter.functions.incr().build_transaction({
    #         'nonce': node.evm.get_transaction_count(self.key_pair.address),
    #         'gas': 500_000,
    #         'maxPriorityFeePerGas': 152_000_000_000,
    #         'maxFeePerGas': 150_000_000_000,
    #     })

    #     signed = node.evm.sign(tx, self.key_pair.pkey)

    #     hash = node.evm.send_raw_tx(signed.rawTransaction)

    #     node.generate(1)

    #     node.evm.wait(hash)

    #     count = self.Counter.functions.getCount().call()
    #     assert_equal(count, 6)

    # def should_counter_caller_calls_counter(self):
    #     node = self.nodes[0]

    #     # equal if same owner
    #     # assert_equal(self.Counter.address, self.CounterCaller.address)

    #     count = self.CounterCaller.functions.getCount().call()
    #     assert_equal(count, 6)

    #     tx = self.CounterCaller.functions.incr().build_transaction({
    #         'nonce': node.evm.get_transaction_count(self.key_pair.address),
    #         'gas': 500_000,
    #         'maxPriorityFeePerGas': 152_000_000_000,
    #         'maxFeePerGas': 150_000_000_000,
    #     })

    #     signed = node.evm.sign(tx, self.key_pair.pkey)

    #     hash = node.evm.send_raw_tx(signed.rawTransaction)

    #     node.generate(1)

    #     node.evm.wait(hash)

    #     count = self.CounterCaller.functions.getCount().call()
    #     assert_equal(count, 7)

    def run_test(self):
        self.setup()

        self.should_create_counter_contract()
        
        self.should_create_counter_caller_contract()
        
        self.test_contract_env_vars()

        # self.test_contract_state()

        # self.should_counter_caller_calls_counter()
        

        # simply get contract properties
        # name = self.Counter.functions.name().call()
        # calleraddr = self.Counter.caller().address
        # print('calleraddr: ', calleraddr)
        # assert_equal(name, "Counter")

        # owner = self.Counter.functions.owner().call()
        # print('owner: ', owner)

        # simply computing
        # res = self.Counter.functions.mul(2,3).call()
        # assert_equal(res, 6)

        # # validation
        # res = self.Counter.functions.max10(11).call()
        # print('res: ', res)

        # inspect diff between msg.sender and tx.origin
        # sender = self.Counter.functions.inspectSender().call()
        # print('sender: ', sender)

        # origin = self.Counter.functions.inspectOrigin().call()
        # print('origin: ', origin)

        # csender = self.CounterCaller.functions.inspectSender().call()
        # print('csender: ', csender)

        # corigin = self.CounterCaller.functions.inspectOrigin().call()
        # print('corigin: ', corigin)



if __name__ == '__main__':
    EVMTest().main()

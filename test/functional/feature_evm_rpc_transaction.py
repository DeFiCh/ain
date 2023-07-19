#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_is_hex_string,
    assert_raises_rpc_error,
    int_to_eth_u256
)


# pragma solidity ^0.8.2;
# contract Multiply {
#     function multiply(uint a, uint b) public pure returns (uint) {
#         return a * b;
#     }
# }
CONTRACT_BYTECODE = '0x608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033'

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        self.toAddress = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'
        self.nodes[0].importprivkey('af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23') # ethAddress
        self.nodes[0].importprivkey('17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35') # toAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, self.ethAddress, 0, 21, 21000, self.toAddress, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true', 'v0/params/feature/transferdomain': 'true', 'v0/transferdomain/allowed/dvm-evm': 'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"50@DFI", "domain": 2}, "dst":{"address":self.ethAddress, "amount":"50@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(balance, int_to_eth_u256(50))

    def test_sign_and_send_raw_transaction(self):
        txLegacy = {
            'nonce': '0x0',
            'from': self.ethAddress,
            'value': '0x00',
            'data': CONTRACT_BYTECODE,
            'gas': '0x7a120', # 500_000
            'gasPrice': '0x22ecb25c00', # 150_000_000_000,
        }
        signed = self.nodes[0].eth_signTransaction(txLegacy)

        # LEGACY_TX = {
        #     nonce: 0,
        #     from: "0x9b8a4af42140d8a4c153a822f02571a1dd037e89",
        #     value: 0, // must be set else error https://github.com/rust-blockchain/evm/blob/a14b6b02452ebf8e8a039b92ab1191041f806794/src/executor/stack/memory.rs#L356
        #     data: CONTRACT_BYTECODE,
        #     gasLimit: 500_000,
        #     gasPrice: 150_000_000_000,
        #     chainId: 1133, // must be set else error https://github.com/rust-blockchain/ethereum/blob/0ffbe47d1da71841be274442a3050da9c895e10a/src/transaction.rs#L74
        # }
        rawtx = '0xf90152808522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c634300080200338208fda0f6d889435dbfe9ea49b984fe1cab94cae59fa774b602256abc9f657c2abdd5bea0609176ffd6023896913cd9e12b61a004a970187cc53623ef49afb0417cb44929'
        assert_equal(f"0x{signed}", rawtx)

        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

        tx2930 = {
            'from': self.ethAddress,
            'nonce': '0x1',
            'value': '0x0',
            'data': CONTRACT_BYTECODE,
            'gas': '0x7a120', # 500_000
            'gasPrice': '0x22ecb25c00', # 150_000_000_000,
            'accessList': [
                {
                    'address': self.ethAddress,
                    'storageKeys': [
                        "0x0000000000000000000000000000000000000000000000000000000000000000"
                    ]
                },
            ],
            'type': '0x1'
        }
        signed = self.nodes[0].eth_signTransaction(tx2930)

        # EIP2930_TX = {
        #     nonce: 1,
        #     value: 0,
        #     data: CONTRACT_BYTECODE,
        #     gasLimit: 3_000,
        #     gasPrice: 150_000_000_000,
        #     accessList: [
        #     {
        #         address: 0x9b8a4af42140d8a4c153a822f02571a1dd037e89,
        #         storageKeys: [
        #             "0x0000000000000000000000000000000000000000000000000000000000000000"
        #         ]
        #     },
        #     ], type: 1, chainId: 1133
        # }
        rawtx = '0x01f9018d82046d018522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033f838f7949b8a4af42140d8a4c153a822f02571a1dd037e89e1a0000000000000000000000000000000000000000000000000000000000000000001a0f69240ab7127d845ad35a450d377f9d7d81c3e9d350b12bf53d14dc44a9e7bf2a067c2e6d1d30f20c670fbb9b79296c26ff372086f2ca3aead851bcc3510b638a7'
        assert_equal(f"0x{signed}", rawtx)

        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

        tx1559 = {
            'nonce': '0x2',
            'from': self.ethAddress,
            'value': '0x0',
            'data': CONTRACT_BYTECODE,
            'gas': '0xbb8', # 3_000
            'maxPriorityFeePerGas': '0x2363e7f000', # 152_000_000_000
            'maxFeePerGas': '0x22ecb25c00', # 150_000_000_000
            'type': '0x2',
        }
        signed = self.nodes[0].eth_signTransaction(tx1559)

        # EIP1559_TX = {
        #     nonce: 2,
        #     from: "0x9b8a4af42140d8a4c153a822f02571a1dd037e89",
        #     value: 0,
        #     data: CONTRACT_BYTECODE,
        #     gasLimit: 3_000,
        #     maxPriorityFeePerGas: 152_000_000_000, # 152 gwei
        #     maxFeePerGas: 150_000_000_000, type: 2,
        # }
        rawtx = '0x02f9015982046d02852363e7f0008522ecb25c00820bb88080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033c080a047375bb73a006fdfcbecaf027305e0889b49277a151763799d68cfcbd06a84c6a0255b585e390c63f8d2c501606aa6fa1e1f3dffa7a8705a431f6a0ad99a1cc7e4'
        assert_equal(f"0x{signed}", rawtx)

        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

    def test_send_transaction(self):
        txLegacy = {
            'from': self.ethAddress,
            'value': '0x00',
            'data': CONTRACT_BYTECODE,
            'gas': '0x7a120', # 500_000
            'gasPrice': '0x22ecb25c00', # 150_000_000_000,
        }
        hash = self.nodes[0].eth_sendTransaction(txLegacy)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

        tx2930 = {
            'from': self.ethAddress,
            'value': '0x00',
            'data': CONTRACT_BYTECODE,
            'gas': '0x7a120', # 500_000
            'gasPrice': '0x22ecb25c00', # 150_000_000_000,
            'accessList': [
                {
                    'address': self.ethAddress,
                    'storageKeys': [
                        "0x0000000000000000000000000000000000000000000000000000000000000000"
                    ]
                },
            ],
            'type': '0x1'
        }
        hash = self.nodes[0].eth_sendTransaction(tx2930)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

        tx1559 = {
            'from': self.ethAddress,
            'value': '0x0',
            'data': CONTRACT_BYTECODE,
            'gas': '0x18e70', # 102_000
            'maxPriorityFeePerGas': '0x2363e7f000', # 152_000_000_000
            'maxFeePerGas': '0x22ecb25c00', # 150_000_000_000
            'type': '0x2'
        }
        hash = self.nodes[0].eth_sendTransaction(tx1559)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

    def run_test(self):
        self.setup()

        self.test_sign_and_send_raw_transaction()

        self.test_send_transaction()



if __name__ == '__main__':
    EVMTest().main()

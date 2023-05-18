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
    assert_raises_rpc_error
)

# Utility function to assert value of U256 output
def int_to_eth_u256(value):
    """
    Convert a non-negative integer to an Ethereum U256-compatible format.

    The input value is multiplied by a fixed factor of 10^18 (1 ether in wei)
    and represented as a hexadecimal string. This function validates that the
    input is a non-negative integer and checks if the converted value is within
    the range of U256 values (0 to 2^256 - 1). If the input is valid and within
    range, it returns the corresponding U256-compatible hexadecimal representation.

    Args:
        value (int): The non-negative integer to convert.

    Returns:
        str: The U256-compatible hexadecimal representation of the input value.

    Raises:
        ValueError: If the input is not a non-negative integer or if the
                    converted value is outside the U256 range.
    """
    if not isinstance(value, int) or value < 0:
        raise ValueError("Value must be a non-negative integer")

    max_u256_value = 2**256 - 1
    factor = 10**18

    converted_value = value * factor
    if converted_value > max_u256_value:
        raise ValueError(f"Value must be less than or equal to {max_u256_value}")

    return hex(converted_value)

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
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        self.nodes[0].transferdomain(1,{self.address:["50@DFI"]}, {self.ethAddress:["50@DFI"]})
        self.nodes[0].generate(1)
        
        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(balance, int_to_eth_u256(50))
        
    def test_send_raw_transaction(self):
        # LEGACY_TX = {
        #     value: 0,
        #     data: contractBytecode,
        #     gasLimit: 72_000,
        #     gasPrice: 8,
        # }
        # TODO(): debugging signing_hash err causes connection refused
        # rawtx = '0xf9014b8008830119408080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c634300080200331ba0f531cca7810ea999647ff5da417365083cce67ff4c985ba7ff8c9e34f34eaf58a04233cfa0f0afb10a0d617ca15fdae36df7bfae34a29ac2135843c526460cd93c'
        # hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        # print('hash:', hash)
        
        # EIP2930_TX = {
        #     value: 0,
        #     data: contractBytecode,
        #     gasLimit: 500_000,
        #     gasPrice: 150_000_000_000,
        #     accessList: [
        #     {
        #         address: 0x9b8a4af42140d8a4c153a822f02571a1dd037e89,
        #         storageKeys: [
        #             "0x0000000000000000000000000000000000000000000000000000000000000000"
        #         ]
        #     },
        #     ],
        #     type: 1
        # }
        rawtx = '0x01f9018b80808522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033f838f7949b8a4af42140d8a4c153a822f02571a1dd037e89e1a0000000000000000000000000000000000000000000000000000000000000000001a088a2f79b7966749c71aa7081d49c835e2e6d19e1f4973055983029a6783e14fea04aed191ed7e8550a1bbf0e0ce4f3c803ad70e2aa1e5ae245f99454ce7ba55c00'
        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])
       
        # EIP1559_TX = {
        #     value: 0,
        #     data: contractBytecode,
        #     gasLimit: 102_000,
        #     maxPriorityFeePerGas: 152_000_000_000, # 152 gwei
        #     maxFeePerGas: 150_000_000_000, # 150 gwei
        #     type: 2,
        # }
        rawtx = '0x02f901588080852363e7f0008522ecb25c0083018e708080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033c080a03e29322f77e70d98b925414c50574ab1981d066151cb793b5db86a74e6a3f5c8a005445f12244e15edffd96c46a8abebd4a48d72ef4d69d7fede08668b7fa0df26'
        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])
        
    def run_test(self):
        self.setup()
        
        self.test_send_raw_transaction()
        


if __name__ == '__main__':
    EVMTest().main()

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
        # TODO(canonbrother): debugging the value and chainId field error
        # LEGACY_TX = {
        #     value: 0, // must be zero for now https://github.com/rust-blockchain/evm/blob/a14b6b02452ebf8e8a039b92ab1191041f806794/src/executor/stack/memory.rs#L356
        #     data: contractBytecode,
        #     gasLimit: 21_000,
        #     gasPrice: 21_000_000_000,
        #     chainId: 1133, // must be set else error https://github.com/rust-blockchain/ethereum/blob/0ffbe47d1da71841be274442a3050da9c895e10a/src/transaction.rs#L74
        # }
        rawtx = '0xf90152808522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c634300080200338208fea0fa8bbf844bfaef999aa77c92785e491cd92699c95b72a4582be7b056e5576854a0574606ad9efa8685c3bcd8c8cba7783336a0831e8690d6faf326b7e3eeb34f8b'
        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])
        
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
        rawtx = '0x01f9060280808522ecb25c008307a1208080b9057460c0604052600760808190526621b7bab73a32b960c91b60a090815261002891600091906100ab565b50600060025534801561003a57600080fd5b50600180546001600160a01b031916331790556040517ff15da729ec5b36e9bda8b3f71979cdac5d0f3169f8590778ac0cd82cc5cc1d4a9061009e906020808252600e908201526d2432b63637961021b7bab73a32b960911b604082015260600190565b60405180910390a161017f565b8280546100b790610144565b90600052602060002090601f0160209004810192826100d9576000855561011f565b82601f106100f257805160ff191683800117855561011f565b8280016001018555821561011f579182015b8281111561011f578251825591602001919060010190610104565b5061012b92915061012f565b5090565b5b8082111561012b5760008155600101610130565b60028104600182168061015857607f821691505b6020821081141561017957634e487b7160e01b600052602260045260246000fd5b50919050565b6103e68061018e6000396000f3fe608060405234801561001057600080fd5b506004361061009e5760003560e01c80638da5cb5b116100665780638da5cb5b146100f4578063a87d942c1461011f578063c8a4ac9c14610127578063d14e62b81461013a578063ee82ac5e1461014d5761009e565b806306fdde03146100a3578063119fbbd4146100c15780631a93d1c3146100cb578063672d5d3b146100db5780638361ff9c146100e1575b600080fd5b6100ab61015f565b6040516100b891906102d5565b60405180910390f35b6100c96101ed565b005b455b6040519081526020016100b8565b436100cd565b6100cd6100ef36600461029c565b610207565b600154610107906001600160a01b031681565b6040516001600160a01b0390911681526020016100b8565b6002546100cd565b6100cd6101353660046102b4565b61026d565b6100c961014836600461029c565b610280565b6100cd61015b36600461029c565b4090565b6000805461016c9061035f565b80601f01602080910402602001604051908101604052809291908181526020018280546101989061035f565b80156101e55780601f106101ba576101008083540402835291602001916101e5565b820191906000526020600020905b8154815290600101906020018083116101c857829003601f168201915b505050505081565b6001600260008282546102009190610328565b9091555050565b6000600a8211156102695760405162461bcd60e51b815260206004820152602260248201527f56616c7565206d757374206e6f742062652067726561746572207468616e2031604482015261181760f11b606482015260840160405180910390fd5b5090565b60006102798284610340565b9392505050565b6001546001600160a01b0316331461029757600080fd5b600255565b6000602082840312156102ad578081fd5b5035919050565b600080604083850312156102c6578081fd5b50508035926020909101359150565b6000602080835283518082850152825b81811015610301578581018301518582016040015282016102e5565b818111156103125783604083870101525b50601f01601f1916929092016040019392505050565b6000821982111561033b5761033b61039a565b500190565b600081600019048311821515161561035a5761035a61039a565b500290565b60028104600182168061037357607f821691505b6020821081141561039457634e487b7160e01b600052602260045260246000fd5b50919050565b634e487b7160e01b600052601160045260246000fdfea2646970667358221220698216ef3f0a0eede3cc4f13017b1d1699d56b3aa4aa8491a3f47fc2d37ac22164736f6c63430008020033f838f7949b8a4af42140d8a4c153a822f02571a1dd037e89e1a0000000000000000000000000000000000000000000000000000000000000000080a0402e59fcb2e0f89a591dd96d2d2fd5b17b91d83766d4f5e821e441980e760dd0a04a863d9dc796bf01b74a046880a3c9e244c7e13200ff886074621cbc71593c67'
        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])
       
        # EIP1559_TX = {
        #     NOTE(canonbrother): take note on your own nonce value if sending raw tx
        #     nonce: 1, 
        #     value: 0,
        #     data: contractBytecode,
        #     gasLimit: 3_000_000_000_000_000,
        #     maxPriorityFeePerGas: 152_000_000_000, # 152 gwei
        #     maxFeePerGas: 150_000_000_000, # 150 gwei
        #     type: 2,
        # }
        rawtx = '0x02f905d38001852363e7f0008522ecb25c00870aa87bee5380008080b9057460c0604052600760808190526621b7bab73a32b960c91b60a090815261002891600091906100ab565b50600060025534801561003a57600080fd5b50600180546001600160a01b031916331790556040517ff15da729ec5b36e9bda8b3f71979cdac5d0f3169f8590778ac0cd82cc5cc1d4a9061009e906020808252600e908201526d2432b63637961021b7bab73a32b960911b604082015260600190565b60405180910390a161017f565b8280546100b790610144565b90600052602060002090601f0160209004810192826100d9576000855561011f565b82601f106100f257805160ff191683800117855561011f565b8280016001018555821561011f579182015b8281111561011f578251825591602001919060010190610104565b5061012b92915061012f565b5090565b5b8082111561012b5760008155600101610130565b60028104600182168061015857607f821691505b6020821081141561017957634e487b7160e01b600052602260045260246000fd5b50919050565b6103e68061018e6000396000f3fe608060405234801561001057600080fd5b506004361061009e5760003560e01c80638da5cb5b116100665780638da5cb5b146100f4578063a87d942c1461011f578063c8a4ac9c14610127578063d14e62b81461013a578063ee82ac5e1461014d5761009e565b806306fdde03146100a3578063119fbbd4146100c15780631a93d1c3146100cb578063672d5d3b146100db5780638361ff9c146100e1575b600080fd5b6100ab61015f565b6040516100b891906102d5565b60405180910390f35b6100c96101ed565b005b455b6040519081526020016100b8565b436100cd565b6100cd6100ef36600461029c565b610207565b600154610107906001600160a01b031681565b6040516001600160a01b0390911681526020016100b8565b6002546100cd565b6100cd6101353660046102b4565b61026d565b6100c961014836600461029c565b610280565b6100cd61015b36600461029c565b4090565b6000805461016c9061035f565b80601f01602080910402602001604051908101604052809291908181526020018280546101989061035f565b80156101e55780601f106101ba576101008083540402835291602001916101e5565b820191906000526020600020905b8154815290600101906020018083116101c857829003601f168201915b505050505081565b6001600260008282546102009190610328565b9091555050565b6000600a8211156102695760405162461bcd60e51b815260206004820152602260248201527f56616c7565206d757374206e6f742062652067726561746572207468616e2031604482015261181760f11b606482015260840160405180910390fd5b5090565b60006102798284610340565b9392505050565b6001546001600160a01b0316331461029757600080fd5b600255565b6000602082840312156102ad578081fd5b5035919050565b600080604083850312156102c6578081fd5b50508035926020909101359150565b6000602080835283518082850152825b81811015610301578581018301518582016040015282016102e5565b818111156103125783604083870101525b50601f01601f1916929092016040019392505050565b6000821982111561033b5761033b61039a565b500190565b600081600019048311821515161561035a5761035a61039a565b500290565b60028104600182168061037357607f821691505b6020821081141561039457634e487b7160e01b600052602260045260246000fd5b50919050565b634e487b7160e01b600052601160045260246000fdfea2646970667358221220698216ef3f0a0eede3cc4f13017b1d1699d56b3aa4aa8491a3f47fc2d37ac22164736f6c63430008020033c080a02e500ab5f7c0231e2b79e44255d5480de1742cbb5de99740eef8dbca1963658ca052f32553b3963a1957a7119d1a15b02e9a25446fcb0d70ed114943498b32543e'
        hash = self.nodes[0].eth_sendRawTransaction(rawtx)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])
        
    def test_send_transaction(self):
        # pragma solidity ^0.8.2;
        # contract Multiply {
        #     function multiply(uint a, uint b) public pure returns (uint) {
        #         return a * b;
        #     }
        # }
        contractBytecode = '0x608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033'
        
        txLegacy = {
            'value': '0x00',
            'data': contractBytecode,
            'gas': '0x7a120', # 500_000
            'gasPrice': '0x22ecb25c00', # 150_000_000_000,
        }
        hash = self.nodes[0].eth_sendTransaction(txLegacy)
        self.nodes[0].generate(1)
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_is_hex_string(receipt['contractAddress'])

        tx2930 = {
            'value': '0x00',
            'data': contractBytecode,
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
            'value': '0x0',
            'data': contractBytecode,
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
        
        self.test_send_raw_transaction()
        
        self.test_send_transaction()
        


if __name__ == '__main__':
    EVMTest().main()

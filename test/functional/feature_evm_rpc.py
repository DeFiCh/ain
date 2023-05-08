#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
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
        self.ethAddress = self.nodes[0].getnewaddress("","eth")
        self.to_address = self.nodes[0].getnewaddress("","eth")

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, self.ethAddress, 0, 21, 21000, self.to_address, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

    def test_node_params(self):
        is_miningA = self.nodes[0].eth_mining()
        assert_equal(is_miningA, False)

        hashrate = self.nodes[0].eth_hashrate()
        assert_equal(hashrate, "0x0")

        netversion = self.nodes[0].net_version()
        assert_equal(netversion, "1133")

        chainid = self.nodes[0].eth_chainId()
        assert_equal(chainid, "0x46d")

    def test_gas(self):
        estimate_gas = self.nodes[0].eth_estimateGas({
            'from': self.ethAddress,
            'to': self.to_address,
            'gas': "0x5208", # 21_000
            'value': "0x0",
        })
        assert_equal(estimate_gas, "0x5278")

        gas_price = self.nodes[0].eth_gasPrice()
        assert_equal(gas_price, "0x2540be400") # 10_000_000_000

    def test_accounts(self):
        eth_accounts = self.nodes[0].eth_accounts()
        assert_equal(eth_accounts.sort(), [self.ethAddress, self.to_address].sort())

    def test_address_state(self, address):
        assert_raises_rpc_error(-32602, "invalid length 7, expected a (both 0x-prefixed or not) hex string or byte array containing 20 bytes at line 1 column 9", self.nodes[0].eth_getBalance, "test123")

        balance = self.nodes[0].eth_getBalance(address)
        assert_equal(balance, int_to_eth_u256(100))

        code = self.nodes[0].eth_getCode(address)
        assert_equal(code, "0x")

        blockNumber = self.nodes[0].eth_blockNumber()

        self.nodes[0].transferbalance("evmin",{self.address:["50@DFI"]}, {self.ethAddress:["50@DFI"]})
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(address, "latest")
        assert_equal(balance, int_to_eth_u256(150))

        balance = self.nodes[0].eth_getBalance(address, blockNumber) # Test querying previous block
        assert_equal(balance, int_to_eth_u256(150))


    def test_block(self):
        latest_block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(latest_block['number'], "0x2")

    def test_send_raw_transation(self):
        # {
        #     // privkey 0664c2fe16c096ee83189679187983b8282a3c8db35b91325e4002e526db7a51
        #     from: '0x7e804c732c75592e17f598fe80c966daaf815e76',
        #     to: '', // for contract creation
        #     value: ethers.utils.parseEther('0.0001'),
        #     gasLimit: 72000, // for contract creation
        #     nonce: 1,
        #     chainId: META_CHAIN_ID,
        #     data: Counter.bytecode, // for contract creation
        #     gasPrice: ethers.utils.parseEther('0.0000001')
        # }
        hash = self.nodes[0].eth_sendRawTransaction('0xf90a8c0185174876e8008301194080865af3107a4000b90a3160806040526040518060400160405280600781526020017f436f756e746572000000000000000000000000000000000000000000000000008152506000908051906020019061004f9291906100dd565b50600060025534801561006157600080fd5b5033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055507ff15da729ec5b36e9bda8b3f71979cdac5d0f3169f8590778ac0cd82cc5cc1d4a6040516100d0906101a3565b60405180910390a161025e565b8280546100e9906101d4565b90600052602060002090601f01602090048101928261010b5760008555610152565b82601f1061012457805160ff1916838001178555610152565b82800160010185558215610152579182015b82811115610151578251825591602001919060010190610136565b5b50905061015f9190610163565b5090565b5b8082111561017c576000816000905550600101610164565b5090565b600061018d600e836101c3565b915061019882610235565b602082019050919050565b600060208201905081810360008301526101bc81610180565b9050919050565b600082825260208201905092915050565b600060028204905060018216806101ec57607f821691505b60208210811415610200576101ff610206565b5b50919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b7f48656c6c6f2c20436f756e746572000000000000000000000000000000000000600082015250565b6107c48061026d6000396000f3fe608060405234801561001057600080fd5b506004361061009e5760003560e01c80638da5cb5b116100665780638da5cb5b14610137578063a87d942c14610155578063c8a4ac9c14610173578063d14e62b8146101a3578063ee82ac5e146101bf5761009e565b806306fdde03146100a3578063119fbbd4146100c15780631a93d1c3146100cb578063672d5d3b146100e95780638361ff9c14610107575b600080fd5b6100ab6101ef565b6040516100b891906104e5565b60405180910390f35b6100c961027d565b005b6100d3610299565b6040516100e09190610527565b60405180910390f35b6100f16102a1565b6040516100fe9190610527565b60405180910390f35b610121600480360381019061011c91906103c1565b6102a9565b60405161012e9190610527565b60405180910390f35b61013f6102f7565b60405161014c91906104af565b60405180910390f35b61015d61031d565b60405161016a9190610527565b60405180910390f35b61018d600480360381019061018891906103ea565b610327565b60405161019a9190610527565b60405180910390f35b6101bd60048036038101906101b891906103c1565b61033d565b005b6101d960048036038101906101d491906103c1565b6103a1565b6040516101e691906104ca565b60405180910390f35b600080546101fc90610687565b80601f016020809104026020016040519081016040528092919081815260200182805461022890610687565b80156102755780601f1061024a57610100808354040283529160200191610275565b820191906000526020600020905b81548152906001019060200180831161025857829003601f168201915b505050505081565b600160026000828254610290919061055e565b92505081905550565b600045905090565b600043905090565b6000600a8211156102ef576040517f08c379a00000000000000000000000000000000000000000000000000000000081526004016102e690610507565b60405180910390fd5b819050919050565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b6000600254905090565b6000818361033591906105b4565b905092915050565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff163373ffffffffffffffffffffffffffffffffffffffff161461039757600080fd5b8060028190555050565b600081409050919050565b6000813590506103bb81610777565b92915050565b6000602082840312156103d357600080fd5b60006103e1848285016103ac565b91505092915050565b600080604083850312156103fd57600080fd5b600061040b858286016103ac565b925050602061041c858286016103ac565b9150509250929050565b61042f8161060e565b82525050565b61043e81610620565b82525050565b600061044f82610542565b610459818561054d565b9350610469818560208601610654565b61047281610717565b840191505092915050565b600061048a60228361054d565b915061049582610728565b604082019050919050565b6104a98161064a565b82525050565b60006020820190506104c46000830184610426565b92915050565b60006020820190506104df6000830184610435565b92915050565b600060208201905081810360008301526104ff8184610444565b905092915050565b600060208201905081810360008301526105208161047d565b9050919050565b600060208201905061053c60008301846104a0565b92915050565b600081519050919050565b600082825260208201905092915050565b60006105698261064a565b91506105748361064a565b9250827fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff038211156105a9576105a86106b9565b5b828201905092915050565b60006105bf8261064a565b91506105ca8361064a565b9250817fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0483118215151615610603576106026106b9565b5b828202905092915050565b60006106198261062a565b9050919050565b6000819050919050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000819050919050565b60005b83811015610672578082015181840152602081019050610657565b83811115610681576000848401525b50505050565b6000600282049050600182168061069f57607f821691505b602082108114156106b3576106b26106e8565b5b50919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b6000601f19601f8301169050919050565b7f56616c7565206d757374206e6f742062652067726561746572207468616e203160008201527f302e000000000000000000000000000000000000000000000000000000000000602082015250565b6107808161064a565b811461078b57600080fd5b5056fea2646970667358221220b3b1b820085eedbb4673c278d5cd7e3c838e644c398ca8caac8e0b05185772a464736f6c63430008020033820a95a0580d8b58e4eb977b4a097a6741155a2751f81a92972ba7b2852be1c57a9c02b7a073ea2a04821e7cfb2075c93c60a30d2b3ec9ca08f81e2d7d69f88f0d7e809b29')
        print('hash:', hash)
        
        self.nodes[0].generate(1)
        
        tx = self.nodes[0].eth_getTransactionByHash(hash)
        print('tx:', tx)
        
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        print('receipt:', receipt)

    def run_test(self):
        self.setup()

        self.test_node_params()

        self.test_gas()

        self.test_accounts()

        self.nodes[0].transferbalance("evmin",{self.address:["100@DFI"]}, {self.ethAddress:["100@DFI"]})
        self.nodes[0].generate(1)

        self.test_address_state(self.ethAddress) # TODO test smart contract

        self.test_block()

        self.test_send_raw_transation()


if __name__ == '__main__':
    EVMTest().main()

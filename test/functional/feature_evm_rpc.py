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
            'to': self.toAddress,
            'value': "0x0",
        })
        assert_equal(estimate_gas, "0x5208")

        gas_price = self.nodes[0].eth_gasPrice()
        assert_equal(gas_price, "0x2540be400") # 10_000_000_000

    def test_accounts(self):
        eth_accounts = self.nodes[0].eth_accounts()
        assert_equal(eth_accounts.sort(), [self.ethAddress, self.toAddress].sort())

    def test_address_state(self, address):
        assert_raises_rpc_error(-32602, "invalid length 7, expected a (both 0x-prefixed or not) hex string or byte array containing 20 bytes at line 1 column 9", self.nodes[0].eth_getBalance, "test123")

        balance = self.nodes[0].eth_getBalance(address)
        assert_equal(balance, int_to_eth_u256(100))

        code = self.nodes[0].eth_getCode(address)
        assert_equal(code, "0x")

        blockNumber = self.nodes[0].eth_blockNumber()

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"50@DFI", "domain": 2}, "dst":{"address":self.ethAddress, "amount":"50@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(address, "latest")
        assert_equal(balance, int_to_eth_u256(150))

        balance = self.nodes[0].eth_getBalance(address, blockNumber) # Test querying previous block
        assert_equal(balance, int_to_eth_u256(100))


    def test_block(self):
        latest_block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(latest_block['number'], "0x3")

        # Test full transaction block
        self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)

        latest_block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(latest_block['number'], "0x4")
        assert_equal(latest_block['transactions'][0], "0x8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497")

        latest_full_block = self.nodes[0].eth_getBlockByNumber("latest", True)
        assert_equal(latest_full_block['number'], "0x4")
        assert_equal(latest_full_block['transactions'][0]['blockHash'], latest_full_block["hash"])
        assert_equal(latest_full_block['transactions'][0]['blockNumber'], latest_full_block["number"])
        assert_equal(latest_full_block['transactions'][0]['from'], self.ethAddress)
        assert_equal(latest_full_block['transactions'][0]['gas'], '0x5208')
        assert_equal(latest_full_block['transactions'][0]['gasPrice'], '0x4e3b29200')
        assert_equal(latest_full_block['transactions'][0]['hash'], '0x8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497')
        assert_equal(latest_full_block['transactions'][0]['input'], '0x')
        assert_equal(latest_full_block['transactions'][0]['nonce'], '0x0')
        assert_equal(latest_full_block['transactions'][0]['to'], self.toAddress)
        assert_equal(latest_full_block['transactions'][0]['transactionIndex'], '0x0')
        assert_equal(latest_full_block['transactions'][0]['value'], '0xde0b6b3a7640000')
        assert_equal(latest_full_block['transactions'][0]['v'], '0x25')
        assert_equal(latest_full_block['transactions'][0]['r'], '0x37f41c543402c9b02b35b45ef43ac31a63dcbeba0c622249810ecdec00aee376')

        # state check
        block = self.nodes[0].eth_getBlockByHash(latest_block['hash'])
        assert_equal(block, latest_block)

    def run_test(self):
        self.setup()

        self.test_node_params()

        self.test_gas()

        self.test_accounts()

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.ethAddress, "amount":"100@DFI", "domain": 3}}])
        self.nodes[0].generate(2)

        self.test_address_state(self.ethAddress) # TODO test smart contract

        self.test_block()


if __name__ == '__main__':
    EVMTest().main()

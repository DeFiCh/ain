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

from decimal import Decimal

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
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1']
        ]

    def run_test(self):

        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        ethAddress = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        to_address = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'
        self.nodes[0].importprivkey('af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23') # ethAddress
        self.nodes[0].importprivkey('17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35') # to_address

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, ethAddress, 0, 21, 21000, to_address, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)
        self.sync_blocks()

        # activate EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_raises_rpc_error(-32600, "amount 0.00000000 is less than 100.00000000", self.nodes[0].transferdomain, 1, {address:["100@DFI"]}, {ethAddress:["100@DFI"]})

        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": address
        })
        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({address: "101@DFI"})
        self.nodes[0].generate(1)
        self.sync_blocks()

        DFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        ETHbalance = self.nodes[0].eth_getBalance(ethAddress)
        assert_equal(DFIbalance, Decimal('101'))
        assert_equal(ETHbalance, int_to_eth_u256(0))
        assert_equal(len(self.nodes[0].getaccount(ethAddress, {}, True)), 0)

        assert_raises_rpc_error(-3, "xpected type number, got string", self.nodes[0].transferdomain, "blabla", {address:["100@DFI"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-8, "Invalid parameters, argument \"type\" must be either 1 (DFI token to EVM) or 2 (EVM to DFI token)", self.nodes[0].transferdomain, 0, {address:["100@DFI"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-5, "recipient (blablabla) does not refer to any valid address", self.nodes[0].transferdomain, 1, {"blablabla":["100@DFI"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-5, "recipient (blablabla) does not refer to any valid address", self.nodes[0].transferdomain, 1, {address:["100@DFI"]}, {"blablabla":["100@DFI"]})

        assert_raises_rpc_error(-32600, "From address must not be an ETH address in case of \"evmin\" transfer type", self.nodes[0].transferdomain, 1, {ethAddress:["100@DFI"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-32600, "To address must be an ETH address in case of \"evmin\" transfer type", self.nodes[0].transferdomain, 1, {address:["100@DFI"]}, {address:["100@DFI"]})
        assert_raises_rpc_error(-32600, "sum of inputs (from) != sum of outputs (to)", self.nodes[0].transferdomain, 1, {address:["101@DFI"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-32600, "sum of inputs (from) != sum of outputs (to)", self.nodes[0].transferdomain, 1, {address:["100@BTC"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-32600, "sum of inputs (from) != sum of outputs (to)", self.nodes[0].transferdomain, 1, {address:["100@DFI"]}, {ethAddress:["100@BTC"]})
        assert_raises_rpc_error(-32600, "For \"evmin\" transfers, only DFI token is currently supported", self.nodes[0].transferdomain, 1, {address:["100@BTC"]}, {ethAddress:["100@BTC"]})
        assert_raises_rpc_error(-32600, "Not enough balance in " + ethAddress + " to cover \"evmout\" transfer", self.nodes[0].transferdomain, 2, {ethAddress:["100@DFI"]}, {address:["100@DFI"]})

        self.nodes[0].transferdomain(1,{address:["100@DFI"]}, {ethAddress:["100@DFI"]})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check that EVM balance shows in gettokenabalances
        assert_equal(self.nodes[0].gettokenbalances({}, False, False, True), ['101.00000000@0'])

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        newETHbalance = self.nodes[0].eth_getBalance(ethAddress)

        assert_equal(newDFIbalance, DFIbalance - Decimal('100'))
        assert_equal(newETHbalance, int_to_eth_u256(100))
        assert_equal(len(self.nodes[0].getaccount(ethAddress, {}, True)), 0)

        assert_raises_rpc_error(-32600, "From address must be an ETH address in case of \"evmout\" transfer type", self.nodes[0].transferdomain, 2, {address:["100@DFI"]}, {address:["100@DFI"]})
        assert_raises_rpc_error(-32600, "To address must not be an ETH address in case of \"evmout\" transfer type", self.nodes[0].transferdomain, 2, {ethAddress:["100@DFI"]}, {ethAddress:["100@DFI"]})
        assert_raises_rpc_error(-32600, "sum of inputs (from) != sum of outputs (to)", self.nodes[0].transferdomain, 2, {ethAddress:["101@DFI"]}, {address:["100@DFI"]})
        assert_raises_rpc_error(-32600, "sum of inputs (from) != sum of outputs (to)", self.nodes[0].transferdomain, 2, {ethAddress:["100@BTC"]}, {address:["100@DFI"]})
        assert_raises_rpc_error(-32600, "sum of inputs (from) != sum of outputs (to)", self.nodes[0].transferdomain, 2, {ethAddress:["100@DFI"]}, {address:["100@BTC"]})
        assert_raises_rpc_error(-32600, "For \"evmout\" transfers, only DFI token is currently supported", self.nodes[0].transferdomain, 2, {ethAddress:["100@BTC"]}, {address:["100@BTC"]})

        self.nodes[0].transferdomain(2, {ethAddress:["100@DFI"]}, {address:["100@DFI"]})
        self.nodes[0].generate(1)
        self.sync_blocks()

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        newETHbalance = self.nodes[0].eth_getBalance(ethAddress)

        assert_equal(newDFIbalance, DFIbalance)
        assert_equal(newETHbalance, ETHbalance)
        assert_equal(len(self.nodes[0].getaccount(ethAddress, {}, True)), 0)

        # Fund Eth address
        self.nodes[0].transferdomain(1,{address:["10@DFI"]}, {ethAddress:["10@DFI"]})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Try and send a TX with a high nonce
        assert_raises_rpc_error(-32600, "evm tx failed to validate", self.nodes[0].evmtx, ethAddress, 1, 21, 21000, to_address, 1)

        # Test EVM Tx
        tx = self.nodes[0].evmtx(ethAddress, 0, 21, 21000, to_address, 1)
        raw_tx = self.nodes[0].getrawtransaction(tx)
        self.sync_mempools()

        # Check the pending TXs
        result = self.nodes[0].eth_pendingTransactions()
        assert_equal(result[0]['blockHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(result[0]['blockNumber'], 'null')
        assert_equal(result[0]['from'], ethAddress)
        assert_equal(result[0]['gas'], '0x5208')
        assert_equal(result[0]['gasPrice'], '0x4e3b29200')
        assert_equal(result[0]['hash'], '0x8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497')
        assert_equal(result[0]['input'], '0x')
        assert_equal(result[0]['nonce'], '0x0')
        assert_equal(result[0]['to'], to_address)
        assert_equal(result[0]['transactionIndex'], '0x0')
        assert_equal(result[0]['value'], '0xde0b6b3a7640000')
        assert_equal(result[0]['v'], '0x25')
        assert_equal(result[0]['r'], '0x37f41c543402c9b02b35b45ef43ac31a63dcbeba0c622249810ecdec00aee376')
        assert_equal(result[0]['s'], '0x5eb2be77eb0c7a1875a53ba15fc6afe246fbffe869157edbde64270e41ba045e')

        # Check mempools for TX
        assert_equal(self.nodes[0].getrawmempool(), [tx])
        assert_equal(self.nodes[1].getrawmempool(), [tx])
        self.nodes[0].generate(1)

        # Check EVM Tx is in block
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        assert_equal(block['tx'][1], tx)

        # Check EVM Tx shows in block on EVM side
        block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(block['transactions'], ['0x8c99e9f053e033078e33c2756221f38fd529b914165090a615f27961de687497'])

        # Check pending TXs now empty
        assert_equal(self.nodes[0].eth_pendingTransactions(), [])

        # Try and send EVM TX a second time
        assert_raises_rpc_error(-26, "evm tx failed to validate", self.nodes[0].sendrawtransaction, raw_tx)

        # Check EVM blockhash and miner fee shown
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        raw_tx = self.nodes[0].getrawtransaction(block['tx'][0], 1)
        block_hash = raw_tx['vout'][1]['scriptPubKey']['hex'][4:68]
        fee_amount = raw_tx['vout'][1]['scriptPubKey']['hex'][68:]
        assert_equal(block_hash, '1111111111111111111111111111111111111111111111111111111111111111')
        assert_equal(fee_amount, '0852000000000000')

        # Test rollback of EVM related TXs
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(101))
        assert_equal(self.nodes[0].getblockcount(), 100)

if __name__ == '__main__':
    EVMTest().main()

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

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1']
        ]

    def run_test(self):

        miner_eth = '0xb36814fd26190b321aa985809293a41273cfe15e' # node0 miner reward will go to this address on EVM side
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

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({address: "101@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        DFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        ETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)

        assert_equal(DFIbalance, Decimal('101'))
        assert_equal(len(ETHbalance), 0)

        self.nodes[0].transferbalance("evmin",{address:["100@DFI"]}, {ethAddress:["100@DFI"]})
        self.nodes[0].generate(1)

        # Check that EVM balance shows in gettokenabalances
        assert_equal(self.nodes[0].gettokenbalances({}, False, False, True), ['101.00000000@0'])

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        newETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)

        assert_equal(newDFIbalance, DFIbalance - Decimal('100'))
        assert_equal(len(newETHbalance), 0)

        self.nodes[0].transferbalance("evmout", {ethAddress:["100@DFI"]}, {address:["100@DFI"]})
        self.nodes[0].generate(1)

        newDFIbalance = self.nodes[0].getaccount(address, {}, True)['0']
        # newETHbalance = self.nodes[0].getaccount(ethAddress, {}, True)['0']

        assert_equal(newDFIbalance, DFIbalance)
        # assert_equal(newETHbalance, ETHbalance)

        # Fund Eth address
        self.nodes[0].transferbalance("evmin",{address:["10@DFI"]}, {ethAddress:["10@DFI"]})
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
        assert_equal(result[0]['input'], '0x0')
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

        # Test rollback of EVM related TXs
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(101))
        assert_equal(self.nodes[0].getblockcount(), 100)

if __name__ == '__main__':
    EVMTest().main()

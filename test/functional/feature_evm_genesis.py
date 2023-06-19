#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
import os

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_is_hex_string,
    assert_raises_rpc_error,
)

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        ainPath = os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
        genesisPath = os.path.join(ainPath, 'lib/ain-evm/genesis.json')
        
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-ethstartstate={}'.format(genesisPath), '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
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

    def run_test(self):
        self.setup()
        
        ethBlock0 = self.nodes[0].eth_getBlockByNumber( 0)
        assert_equal(ethBlock0['difficulty'], '0x400000')
        assert_equal(ethBlock0['extraData'], '0x686f727365')
        assert_equal(ethBlock0['gasLimit'], '0x1388')
        assert_equal(ethBlock0['parentHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        # to_check(): the mixHash get updated somewhere
        # assert_equal(ethBlock0['mixHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(ethBlock0['nonce'], '0x123123123123123f')
        assert_equal(ethBlock0['timestamp'], '0x539')
        
        founderBalance = self.nodes[0].eth_getBalance('a94f5374fce5edbc8e2a8697c15331677e6ebf0b')
        assert_equal(founderBalance, '0x9184e72a000')
        
if __name__ == '__main__':
    EVMTest().main()

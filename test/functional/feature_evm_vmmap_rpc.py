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
        self.nodes[0].importprivkey('af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23')  # ethAddress
        self.nodes[0].importprivkey('17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35')  # toAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, self.ethAddress, 0, 21, 21000, self.toAddress, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

    def vmmap_should_exist(self):
        address = self.nodes[0].getnewaddress("", "legacy")
        eth_address = self.nodes[0].vmmap(address, 2)
        assert_equal(address, self.nodes[0].vmmap(eth_address, 2))

    def vmmap_valid_key_not_present_should_fail(self):
        # Give an address that is not own by the node. THis should fail since we don't have the public key of the address.
        eth_address = '0x3DA3eA35d64557864bbD0da7f6a19a2d2F69f19C'
        assert_raises_rpc_error(-5, "no full public key for address 0x3DA3eA35d64557864bbD0da7f6a19a2d2F69f19C", self.nodes[0].vmmap, eth_address, 2)
        assert_raises_rpc_error(-5, "no full public key for address 0x3DA3eA35d64557864bbD0da7f6a19a2d2F69f19C", self.nodes[0].vmmap, eth_address, 1)

    def vmmap_invalid_key_type_should_fail(self):
        address = self.nodes[0].getnewaddress()
        assert_raises_rpc_error(-8, "Invalid parameters, argument \"type\" must be between 0 and 7.", self.nodes[0].vmmap, address, 8)
        assert_raises_rpc_error(-8, "Invalid parameters, argument \"type\" must be between 0 and 7.", self.nodes[0].vmmap, address, -1)

    def vmmap_invalid_keys_should_fail(self):
        # Check that vmmap is failing on wrong input
        eth_address = '0x0000000000000000000000000000000000000000'
        assert_raises_rpc_error(-5, "0x0000000000000000000000000000000000000000 does not refer to a key", self.nodes[0].vmmap, eth_address, 2)
        assert_raises_rpc_error(-5, "0x0000000000000000000000000000000000000000 does not refer to a key", self.nodes[0].vmmap, eth_address, 1)
        assert_raises_rpc_error(-5, "Invalid address: test", self.nodes[0].vmmap, 'test', 1)
        assert_raises_rpc_error(-5, "Invalid address: test", self.nodes[0].vmmap, 'test', 2)

    def logvmmaps_tx_exist(self):
        list_tx = self.nodes[0].logvmmaps(1)
        eth_tx = self.nodes[0].eth_getBlockByNumber("latest", False)['transactions'][0]
        assert_equal(eth_tx[2:] in list(list_tx['indexes'].values()), True)
        dfi_tx = self.nodes[0].vmmap(eth_tx, 4)
        assert_equal(dfi_tx in list(list_tx['indexes'].keys()), True)

    def logvmmaps_invalid_tx_should_fail(self):
        list_tx = self.nodes[0].logvmmaps(1)
        assert_equal("invalid tx" not in list(list_tx['indexes'].values()), True)
        assert_equal("0x0000000000000000000000000000000000000000000000000000000000000000" not in list(list_tx['indexes'].values()), True)

    def logvmmaps_block_exist(self):
        list_blocks = self.nodes[0].logvmmaps(0)
        eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)['hash']
        assert_equal(eth_block[2:] in list(list_blocks['indexes'].values()), True)
        dfi_block = self.nodes[0].vmmap(eth_block, 6)
        assert_equal(dfi_block in list(list_blocks['indexes'].keys()), True)

    def logvmmaps_invalid_block_should_fail(self):
        list_tx = self.nodes[0].logvmmaps(1)
        assert_equal("invalid tx" not in list(list_tx['indexes'].values()), True)
        assert_equal("0x0000000000000000000000000000000000000000000000000000000000000000" not in list(list_tx['indexes'].values()), True)

    def vmmap_valid_tx_should_success(self):
        # Check if xvmmap is working for Txs
        list_tx = self.nodes[0].logvmmaps(1)
        self.dvm_tx = list(list_tx['indexes'].keys())[0]
        self.evm_tx = self.nodes[0].vmmap(self.dvm_tx, 3)
        assert_equal(self.dvm_tx, self.nodes[0].vmmap(self.evm_tx, 4))
        assert_equal("0x" + self.evm_tx, self.nodes[0].eth_getBlockByNumber("latest", False)['transactions'][0])

    def vmmap_invalid_tx_should_fail(self):
        # Check vmmap fail on wrong tx
        fake_evm_tx = '0x0000000000000000000000000000000000000000000000000000000000000000'
        assert_raises_rpc_error(-32600, "DB r/w failure: 0000000000000000000000000000000000000000000000000000000000000000", self.nodes[0].vmmap, fake_evm_tx, 4)

    def vmmap_valid_block_should_success(self):
        # Check if xvmmap is working for Blocks
        self.latest_block = self.nodes[0].eth_getBlockByNumber("latest", False)
        self.dvm_block = self.nodes[0].vmmap(self.latest_block['hash'], 6)
        assert_equal(self.latest_block['hash'], "0x" + self.nodes[0].vmmap(self.dvm_block, 5))

    def vmmap_invalid_block_should_fail(self):
        # Check vmmap fail on wrong block
        evm_block = '0x0000000000000000000000000000000000000000000000000000000000000000'
        assert_raises_rpc_error(-32600, "DB r/w failure: 0000000000000000000000000000000000000000000000000000000000000000", self.nodes[0].vmmap, evm_block, 6)

    def vmmap_rollback_should_success(self):
        # Check if invalidate block is working for mapping. After invalidating block, the transaction and block shouldn't be mapped anymore.
        self.nodes[0].invalidateblock(self.dvm_block)
        assert_raises_rpc_error(-32600, "DB r/w failure: " + self.dvm_block, self.nodes[0].vmmap, self.dvm_block, 5)
        assert_raises_rpc_error(-32600, "DB r/w failure: " + self.latest_block['hash'][2:], self.nodes[0].vmmap, self.latest_block['hash'], 6)
        assert_raises_rpc_error(-32600, "DB r/w failure: " + self.dvm_tx, self.nodes[0].vmmap, self.dvm_tx, 3)
        assert_raises_rpc_error(-32600, "DB r/w failure: " + self.evm_tx, self.nodes[0].vmmap, self.evm_tx, 4)
        assert_equal(self.nodes[0].logvmmaps(1), {"indexes": {}, "count": 0})

    def run_test(self):
        self.setup()
        self.nodes[0].transferdomain([{"src": {"address": self.address, "amount": "100@DFI", "domain": 2}, "dst": {"address": self.ethAddress, "amount": "100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)
        self.vmmap_should_exist()
        self.logvmmaps_tx_exist()
        self.logvmmaps_invalid_tx_should_fail()
        self.logvmmaps_block_exist()
        self.logvmmaps_invalid_block_should_fail()
        self.vmmap_invalid_key_type_should_fail()
        self.vmmap_valid_key_not_present_should_fail()
        self.vmmap_invalid_keys_should_fail()
        self.vmmap_valid_tx_should_success()
        self.vmmap_invalid_tx_should_fail()
        self.vmmap_valid_block_should_success()
        self.vmmap_invalid_block_should_fail()
        self.vmmap_rollback_should_success()


if __name__ == '__main__':
    EVMTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test vmmap behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)

class VMMapType:
    Auto = 0
    AddressDVMToEVM = 1
    AddressEVMToDVM = 2
    TxHashDVMToEVM = 3
    TxHashEVMToEVM = 4
    BlockHashDVMToEVM = 5
    BlockHashEVMToDVM = 6

class VMMapTests(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1'],
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
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true', 'v0/params/feature/transferdomain': 'true', 'v0/transferdomain/allowed/dvm-evm': 'true'}})
        self.nodes[0].generate(1)
        self.start_block_height = self.nodes[0].getblockcount()

    def vmmap_address_basics(self):
        self.rollback_to(self.start_block_height)
        # Same keys to import both formats.
        # Currently in discussion. Tests disabled.
        priv_keys = [
            "cNoUVyyacpVBpotBGxrnM5XXekdqV8qgnowVQfgCvDWVU9jn4gUz",
            "cPaTadxsWhzHNgi2hAiFXXnw7foEGXBME75s27CEGFeS8S3pYf8j",
            "cSu1eq6MKxZ2exooiXEwC7jA4W7Gd3YyfDL8BWQCm8abaDKrnDkr",
        ]
        addr_maps = [
            ["bcrt1qmhpq9hxgdglwja6uruc92yne8ekxljgykrfta5", "0xfD0766e7aBe123A25c73c95f6dc3eDe26D0b7263"],
            ["bcrt1qtqggfdte5jp8duffzmt54aqtqwlv3l8xsjdrhf", "0x4d07A76Db2a281a348d5A5a1833F4322D77799d5"],
            ["bcrt1qdw7fqrq9n2d530uh05vdm2yvpag2ydm0z67yc5", "0x816a4DDbC26B80602767B13Fb17B2e1785125BE7"],
        ]
        for x in priv_keys:
            self.nodes[0].importprivkey(x)
        for [dfi_addr, eth_addr] in addr_maps:
            assert_equal(self.nodes[0].vmmap(dfi_addr, 1), eth_addr)
            assert_equal(self.nodes[0].vmmap(eth_addr, 2), dfi_addr)

    def vmmap_address_basics_manual_import(self):
        self.rollback_to(self.start_block_height)
        # Import both keys for now.
        priv_keys = [
            ["cNoUVyyacpVBpotBGxrnM5XXekdqV8qgnowVQfgCvDWVU9jn4gUz", "2468918553ca24474efea1e6a3641a1302bd643d15c13a6dbe89b8da38c90b3c"],
            ["cPaTadxsWhzHNgi2hAiFXXnw7foEGXBME75s27CEGFeS8S3pYf8j", "3b8ccde96d9c78c6cf248ffcb9ed89ba8327b8c994600ca391b38f5deffa15ca"],
            ["cSu1eq6MKxZ2exooiXEwC7jA4W7Gd3YyfDL8BWQCm8abaDKrnDkr", "9e9b4756952999af30a62ebe4f8bcd12ed251d820e5d3c8cee550685693f2688"],
        ]
        addr_maps = [
            ["bcrt1qmhpq9hxgdglwja6uruc92yne8ekxljgykrfta5", "0xfD0766e7aBe123A25c73c95f6dc3eDe26D0b7263"],
            ["bcrt1qtqggfdte5jp8duffzmt54aqtqwlv3l8xsjdrhf", "0x4d07A76Db2a281a348d5A5a1833F4322D77799d5"],
            ["bcrt1qdw7fqrq9n2d530uh05vdm2yvpag2ydm0z67yc5", "0x816a4DDbC26B80602767B13Fb17B2e1785125BE7"],
        ]
        for [wif, rawkey] in priv_keys:
            # Adding this line will make the second rawkey import fail
            # due to a bug in importprivkey.
            #
            # Context: Since we have a different ID for each import (eth and non eth),
            # Have ID check fails resulting in https://github.com/defich/ain/blob/tests_vmmap/src/wallet/wallet.cpp/#L1872-L1875
            # failing when actually trying to insert the key
            # However, https://github.com/defich/ain/blob/tests_vmmap/src/wallet/wallet.cpp#L1862
            # still sets the keyid in the map, so further imports use that and succeed
            #
            # self.nodes[0].importprivkey(wif)
            self.nodes[0].importprivkey(rawkey)
        for [dfi_addr, eth_addr] in addr_maps:
            assert_equal(self.nodes[0].vmmap(dfi_addr, 1), eth_addr)
            assert_equal(self.nodes[0].vmmap(eth_addr, 2), dfi_addr)

    def vmmap_valid_address_not_present_should_fail(self):
        self.rollback_to(self.start_block_height)
        # Give an address that is not own by the node. THis should fail since we don't have the public key of the address.
        eth_address = self.nodes[1].getnewaddress("", "eth")
        assert_raises_rpc_error(-5, "no full public key for address " + eth_address, self.nodes[0].vmmap, eth_address, 2)

    def vmmap_valid_address_invalid_type_should_fail(self):
        self.rollback_to(self.start_block_height)
        address = self.nodes[0].getnewaddress("", "legacy")
        p2sh_address = self.nodes[0].getnewaddress("", "p2sh-segwit")
        eth_address = self.nodes[0].getnewaddress("", "eth")
        assert_invalid = lambda *args: assert_raises_rpc_error(-8, "Invalid type parameter", self.nodes[0].vmmap, *args)
        assert_invalid(address, 8)
        assert_invalid(address, -1)
        assert_invalid(eth_address, VMMapType.AddressDVMToEVM)
        assert_invalid(address, VMMapType.AddressEVMToDVM)
        assert_invalid(p2sh_address, VMMapType.AddressDVMToEVM)
        assert_invalid(p2sh_address, VMMapType.AddressDVMToEVM)

    def vmmap_invalid_address_should_fail(self):
        self.rollback_to(self.start_block_height)
        # Check that vmmap is failing on wrong input
        eth_address = '0x0000000000000000000000000000000000000000'
        assert_raises_rpc_error(-5, eth_address + " does not refer to a key", self.nodes[0].vmmap, eth_address, 2)
        assert_raises_rpc_error(-8, "Invalid type parameter", self.nodes[0].vmmap, eth_address, 1)
        assert_raises_rpc_error(-8, "Invalid type parameter", self.nodes[0].vmmap, 'test', 1)
        assert_raises_rpc_error(-8, "Invalid type parameter", self.nodes[0].vmmap, 'test', 2)

    def vmmap_valid_tx_should_succeed(self):
        self.rollback_to(self.start_block_height)
        self.nodes[0].transferdomain([{"src": {"address": self.address, "amount": "100@DFI", "domain": 2}, "dst": {"address": self.ethAddress, "amount": "100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        tx_maps = []
        nonce = 0
        for i in range(5):
            # generate 5 txs in the block
            num_txs = 5
            for j in range(num_txs):
                self.nodes[0].evmtx(self.ethAddress, nonce, 21, 21000, self.toAddress, 1)
                nonce += 1
            self.nodes[0].generate(1)
            dfi_block = self.nodes[0].getblock(self.nodes[0].getbestblockhash())
            eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)
            for j in range(num_txs):
                # note dfi block is j+1 since we ignore coinbase
                tx_maps.append([dfi_block['tx'][j+1], eth_block['transactions'][j]])
        for item in tx_maps:
            assert_equal(self.nodes[0].vmmap(item[0], VMMapType.TxHashDVMToEVM), item[1])
            assert_equal(self.nodes[0].vmmap(item[1], VMMapType.TxHashEVMToEVM), item[0])

    def vmmap_invalid_should_fail(self):
        self.rollback_to(self.start_block_height)
        latest_eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)['hash']
        fake_evm_tx = '0x0000000000000000000000000000000000000000000000000000000000000000'
        assert_err = lambda *args: assert_raises_rpc_error(-32600, None, self.nodes[0].vmmap, *args)
        for map_type in range(6):
            if map_type in [0, 1, 2]:
                continue # addr types and auto are ignored for this test
            assert_raises_rpc_error(-32600, "Key not found: " + fake_evm_tx[2:], self.nodes[0].vmmap, fake_evm_tx, map_type)
            assert_err("0x00", map_type)
            assert_err("garbage", map_type)
            assert_err(latest_eth_block + "0x00", map_type) # invalid suffix
            assert_err("0x00" + latest_eth_block, map_type) # invalid prefix

    def vmmap_valid_block_should_succeed(self):
        self.rollback_to(self.start_block_height)
        self.nodes[0].transferdomain([{"src": {"address": self.address, "amount": "100@DFI", "domain": 2}, "dst": {"address": self.ethAddress, "amount": "100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        block_maps = []
        for i in range(5):
            self.nodes[0].evmtx(self.ethAddress, i, 21, 21000, self.toAddress, 1)
            self.nodes[0].generate(1)
            dfi_block = self.nodes[0].getblock(self.nodes[0].getbestblockhash())
            eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)
            block_maps.append([dfi_block['hash'], eth_block['hash']])
        for item in block_maps:
            assert_equal(self.nodes[0].vmmap(item[0], VMMapType.BlockHashDVMToEVM), item[1])
            assert_equal(self.nodes[0].vmmap(item[1], VMMapType.BlockHashEVMToDVM), item[0])

    def vmmap_rollback_should_succeed(self):
        self.rollback_to(self.start_block_height)
        # Check if invalidate block is working for mapping. After invalidating block, the transaction and block shouldn't be mapped anymore.
        base_block = self.nodes[0].eth_getBlockByNumber("latest", False)['hash']
        self.nodes[0].transferdomain([{"src": {"address": self.address, "amount": "100@DFI", "domain": 2}, "dst": {"address": self.ethAddress, "amount": "100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        base_block_dvm = self.nodes[0].getbestblockhash()
        tx = self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)
        new_block = self.nodes[0].eth_getBlockByNumber("latest", False)['hash']
        list_blocks = self.nodes[0].logvmmaps(0)
        list_blocks = list(list_blocks['indexes'].values())
        list_tx = self.nodes[0].logvmmaps(1)
        list_tx = list(list_tx['indexes'].keys())
        assert_equal(base_block[2:] in list_blocks, True)
        assert_equal(new_block[2:] in list_blocks, True)
        assert_equal(tx in list_tx, True)
        self.nodes[0].invalidateblock(base_block_dvm)
        list_blocks = self.nodes[0].logvmmaps(0)
        list_blocks = list(list_blocks['indexes'].values())
        list_tx = self.nodes[0].logvmmaps(1)
        list_tx = list(list_tx['indexes'].keys())
        assert_equal(base_block[2:] in list_blocks, True)
        assert_equal(new_block[2:] in list_blocks, False)
        assert_equal(tx in list_tx, False)

    def logvmmaps_tx_exist(self):
        self.rollback_to(self.start_block_height)
        self.nodes[0].transferdomain([{"src": {"address": self.address, "amount": "100@DFI", "domain": 2}, "dst": {"address": self.ethAddress, "amount": "100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        tx = self.nodes[0].evmtx(self.ethAddress, 0, 21, 21000, self.toAddress, 1)
        self.nodes[0].generate(1)
        list_tx = self.nodes[0].logvmmaps(1)
        eth_tx = self.nodes[0].eth_getBlockByNumber("latest", False)['transactions'][0]
        assert_equal(eth_tx[2:] in list(list_tx['indexes'].values()), True)
        assert_equal(tx in list(list_tx['indexes'].keys()), True)

    def logvmmaps_invalid_tx_should_fail(self):
        list_tx = self.nodes[0].logvmmaps(1)
        assert_equal("invalid tx" not in list(list_tx['indexes'].values()), True)
        assert_equal("0x0000000000000000000000000000000000000000000000000000000000000000" not in list(list_tx['indexes'].values()), True)
        assert_equal("garbage" not in list(list_tx['indexes'].values()), True)
        assert_equal("0x" not in list(list_tx['indexes'].values()), True)

    def logvmmaps_block_exist(self):
        list_blocks = self.nodes[0].logvmmaps(0)
        eth_block = self.nodes[0].eth_getBlockByNumber("latest", False)['hash']
        assert_equal(eth_block[2:] in list(list_blocks['indexes'].values()), True)
        dfi_block = self.nodes[0].vmmap(eth_block, 6)
        assert_equal(dfi_block in list(list_blocks['indexes'].keys()), True)

    def logvmmaps_invalid_block_should_fail(self):
        list_block = self.nodes[0].logvmmaps(1)
        assert_equal("invalid tx" not in list(list_block['indexes'].values()), True)
        assert_equal("0x0000000000000000000000000000000000000000000000000000000000000000" not in list(list_block['indexes'].values()), True)
        assert_equal("garbage" not in list(list_block['indexes'].values()), True)
        assert_equal("0x" not in list(list_block['indexes'].values()), True)

    def run_test(self):
        self.setup()
        # vmmap tests
        # self.vmmap_address_basics()
        self.vmmap_address_basics_manual_import()
        self.vmmap_valid_address_not_present_should_fail()
        self.vmmap_valid_address_invalid_type_should_fail()
        self.vmmap_invalid_address_should_fail()
        self.vmmap_valid_tx_should_succeed()
        self.vmmap_valid_block_should_succeed()
        self.vmmap_invalid_should_fail()
        self.vmmap_rollback_should_succeed()
        # logvmmap tests
        self.logvmmaps_tx_exist()
        self.logvmmaps_invalid_tx_should_fail()
        self.logvmmaps_block_exist()
        self.logvmmaps_invalid_block_should_fail()


if __name__ == '__main__':
    VMMapTests().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test addressmap behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class AddressConversionType:
    Auto = 0
    DVMToEVMAddress = 1
    EVMToDVMAddress = 2


class addressmapTests(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=105",
                "-subsidytest=1",
            ],
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=105",
                "-subsidytest=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        addr_keys = [
            [
                "0x9b8a4af42140d8a4c153a822f02571a1dd037e89",
                "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23",
            ],
            [
                "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110",
                "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35",
            ],
        ]

        self.ethAddress = addr_keys[0][0]
        self.toAddress = addr_keys[1][0]

        for [addr, key] in addr_keys:
            self.nodes[0].importprivkey(key)
            assert_equal(self.nodes[0].dumpprivkey(addr), key)

        # Generate chain
        self.nodes[0].generate(101)
        assert_raises_rpc_error(
            -32600,
            "called before Metachain height",
            self.nodes[0].evmtx,
            self.ethAddress,
            0,
            21,
            21000,
            self.toAddress,
            0.1,
        )

        # Move to fork height
        self.nodes[0].generate(4)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                }
            }
        )
        self.nodes[0].generate(1)
        self.start_block_height = self.nodes[0].getblockcount()

    def addressmap_address_basics_manual_import(self):
        self.rollback_to(self.start_block_height)
        # Import both keys for now.
        priv_keys = [
            [
                "cNoUVyyacpVBpotBGxrnM5XXekdqV8qgnowVQfgCvDWVU9jn4gUz",
                "2468918553ca24474efea1e6a3641a1302bd643d15c13a6dbe89b8da38c90b3c",
            ],
            [
                "cPaTadxsWhzHNgi2hAiFXXnw7foEGXBME75s27CEGFeS8S3pYf8j",
                "3b8ccde96d9c78c6cf248ffcb9ed89ba8327b8c994600ca391b38f5deffa15ca",
            ],
            [
                "cSu1eq6MKxZ2exooiXEwC7jA4W7Gd3YyfDL8BWQCm8abaDKrnDkr",
                "9e9b4756952999af30a62ebe4f8bcd12ed251d820e5d3c8cee550685693f2688",
            ],
        ]
        addr_maps = [
            [
                "n32jT7A5sv6t9Hm2NAYK6juuwsCdsWe1SF",
                "bcrt1qmhpq9hxgdglwja6uruc92yne8ekxljgykrfta5",
                "0xfD0766e7aBe123A25c73c95f6dc3eDe26D0b7263",
            ],
            [
                "n4VvfBJBTQpDf8ooyjgehiq9mTjeKhcwEc",
                "bcrt1qtqggfdte5jp8duffzmt54aqtqwlv3l8xsjdrhf",
                "0x4d07A76Db2a281a348d5A5a1833F4322D77799d5",
            ],
            [
                "n1HUChzN3RR89jhtehNBNboLUnt4QNAJ8E",
                "bcrt1qdw7fqrq9n2d530uh05vdm2yvpag2ydm0z67yc5",
                "0x816a4DDbC26B80602767B13Fb17B2e1785125BE7",
            ],
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
        for [legacy_address, bech32_address, erc55_address] in addr_maps:
            # dvm -> evm
            res = self.nodes[0].addressmap(
                legacy_address, AddressConversionType.DVMToEVMAddress
            )
            assert_equal(res["input"], legacy_address)
            assert_equal(res["type"], 1)
            assert_equal(res["format"]["erc55"], erc55_address)

            res = self.nodes[0].addressmap(
                bech32_address, AddressConversionType.DVMToEVMAddress
            )
            assert_equal(res["input"], bech32_address)
            assert_equal(res["type"], 1)
            assert_equal(res["format"]["erc55"], erc55_address)

            # evm -> dvm
            res = self.nodes[0].addressmap(
                erc55_address, AddressConversionType.EVMToDVMAddress
            )
            assert_equal(res["input"], erc55_address)
            assert_equal(res["type"], 2)
            assert_equal(res["format"]["legacy"], legacy_address)
            assert_equal(res["format"]["bech32"], bech32_address)

            # auto (dvm -> evm)
            res = self.nodes[0].addressmap(legacy_address, AddressConversionType.Auto)
            assert_equal(res["input"], legacy_address)
            assert_equal(res["type"], 1)
            assert_equal(res["format"]["erc55"], erc55_address)

            res = self.nodes[0].addressmap(bech32_address, AddressConversionType.Auto)
            assert_equal(res["input"], bech32_address)
            assert_equal(res["type"], 1)
            assert_equal(res["format"]["erc55"], erc55_address)

            # auto (evm -> dvm)
            res = self.nodes[0].addressmap(erc55_address, AddressConversionType.Auto)
            assert_equal(res["input"], erc55_address)
            assert_equal(res["type"], 2)
            assert_equal(res["format"]["legacy"], legacy_address)
            assert_equal(res["format"]["bech32"], bech32_address)

    def addressmap_valid_address_not_present_should_fail(self):
        self.rollback_to(self.start_block_height)
        # Give an address that is not own by the node. THis should fail since we don't have the public key of the address.
        erc55_addressess = self.nodes[1].getnewaddress("", "erc55")
        assert_raises_rpc_error(
            -5,
            "no full public key for address " + erc55_addressess,
            self.nodes[0].addressmap,
            erc55_addressess,
            AddressConversionType.EVMToDVMAddress,
        )

    def addressmap_valid_address_invalid_type_should_fail(self):
        self.rollback_to(self.start_block_height)
        address = self.nodes[0].getnewaddress("", "legacy")
        p2sh_address = self.nodes[0].getnewaddress("", "p2sh-segwit")
        erc55_addressess = self.nodes[0].getnewaddress("", "erc55")
        assert_invalid_parameter = lambda *args: assert_raises_rpc_error(
            -8, "Invalid type parameter", self.nodes[0].addressmap, *args
        )
        assert_invalid_format = lambda *args: assert_raises_rpc_error(
            -8, "Invalid address format", self.nodes[0].addressmap, *args
        )
        assert_invalid_parameter(address, 9)
        assert_invalid_parameter(address, -1)
        assert_invalid_format(erc55_addressess, AddressConversionType.DVMToEVMAddress)
        assert_invalid_format(address, AddressConversionType.EVMToDVMAddress)
        assert_invalid_format(p2sh_address, AddressConversionType.DVMToEVMAddress)
        assert_invalid_format(p2sh_address, AddressConversionType.DVMToEVMAddress)

    def addressmap_invalid_address_should_fail(self):
        self.rollback_to(self.start_block_height)
        # Check that addressmap is failing on wrong input
        erc55_addressess = "0x0000000000000000000000000000000000000000"
        assert_raises_rpc_error(
            -5,
            erc55_addressess + " does not refer to a key",
            self.nodes[0].addressmap,
            erc55_addressess,
            AddressConversionType.EVMToDVMAddress,
        )
        assert_raises_rpc_error(
            -8,
            "Invalid address format",
            self.nodes[0].addressmap,
            erc55_addressess,
            AddressConversionType.DVMToEVMAddress,
        )
        assert_raises_rpc_error(
            -8,
            "Invalid address format",
            self.nodes[0].addressmap,
            "test",
            AddressConversionType.DVMToEVMAddress,
        )
        assert_raises_rpc_error(
            -8,
            "Invalid address format",
            self.nodes[0].addressmap,
            "test",
            AddressConversionType.EVMToDVMAddress,
        )

    def addressmap_auto_invalid_address_should_fail(self):
        self.rollback_to(self.start_block_height)
        assert_raises_rpc_error(
            -8,
            "Unsupported type or unable to determine conversion type automatically from the input",
            self.nodes[0].addressmap,
            "test",
            AddressConversionType.Auto,
        )

    def run_test(self):
        self.setup()
        # Address map tests
        self.addressmap_address_basics_manual_import()
        self.addressmap_valid_address_not_present_should_fail()
        self.addressmap_valid_address_invalid_type_should_fail()
        self.addressmap_invalid_address_should_fail()
        self.addressmap_auto_invalid_address_should_fail()


if __name__ == "__main__":
    addressmapTests().main()

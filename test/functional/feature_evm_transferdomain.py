#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256
)

from decimal import Decimal

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=107', '-changiintermediateheight=107', '-changiintermediate2height=107', '-changiintermediate3height=107', '-changiintermediate4height=107', '-subsidytest=1', '-txindex=1'],
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=107', '-changiintermediateheight=107', '-changiintermediate2height=107', '-changiintermediate3height=107', '-changiintermediate4height=107', '-subsidytest=1', '-txindex=1']
        ]
    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address_2nd = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        self.eth_address = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        self.eth_address_bech32 = 'bcrt1qta8meuczw0mhqupzjl5wplz47xajz0dn0wxxr8'
        self.eth_address_privkey = 'af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23'
        self.eth_address1 = self.nodes[0].getnewaddress("", "eth")
        self.no_auth_eth_address = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'


        # Import eth_address and validate Bech32 eqivilent is part of the wallet
        self.nodes[0].importprivkey(self.eth_address_privkey)
        result = self.nodes[0].getaddressinfo(self.eth_address_bech32)
        assert_equal(result['scriptPubKey'], '00145f4fbcf30273f770702297e8e0fc55f1bb213db3')
        assert_equal(result['pubkey'], '021286647f7440111ab928bdea4daa42533639c4567d81eca0fff622fb6438eae3')
        assert_equal(result['ismine'], True)
        assert_equal(result['iswitness'], True)

        # Generate chain
        self.nodes[0].generate(104)

        # Create additional token to be used in tests
        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.address
        })

        # Fund DFI address
        self.nodes[0].utxostoaccount({self.address: "101@DFI"})
        self.nodes[0].generate(1)

    def check_initial_balance(self):
        # Check initial balances
        self.dfi_balance = self.nodes[0].getaccount(self.address, {}, True)['0']
        self.eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(self.dfi_balance, Decimal('101'))
        assert_equal(self.eth_balance, int_to_eth_u256(0))
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 0)


    def invalid_before_fork_and_disabled(self):
        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])

        # Move to fork height
        self.nodes[0].generate(2)

        assert_raises_rpc_error(-32600, "Cannot create tx, EVM is not enabled", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])

        # Activate EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        # Check error before transferdomain enabled
        assert_raises_rpc_error(-32600, "Cannot create tx, transfer domain is not enabled", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])

        # Activate transferdomain
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/transferdomain': 'true'}})
        self.nodes[0].generate(1)

        # Check error before transferdomain DVM to EVM is enabled
        assert_raises_rpc_error(-32600, "DVM to EVM is not currently enabled", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])

        # Activate transferdomain DVM to EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/transferdomain/allowed/dvm-evm': 'true'}})
        self.nodes[0].generate(1)

        # Check error before transferdomain DVM to EVM is enabled
        assert_raises_rpc_error(-32600, "EVM to DVM is not currently enabled", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 2}}])

        # Activate transferdomain DVM to EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/transferdomain/allowed/evm-dvm': 'true'}})
        self.nodes[0].generate(1)

        self.start_height = self.nodes[0].getblockcount()

    def invalid_parameters(self):
        # Check for invalid parameters in transferdomain rpc
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"address\" must not be null", self.nodes[0].transferdomain, [{"src": {"amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"amount\" must not be null", self.nodes[0].transferdomain, [{"src": {"address":self.address, "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"domain\" must not be null", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI"}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "JSON value is not an integer as expected", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": "dvm"}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "JSON value is not an integer as expected", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": "evm"}}])
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"domain\" must be either 2 (DFI token to EVM) or 3 (EVM to DFI token)", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 0}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Unknown transfer domain aspect", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 4}}])
        assert_raises_rpc_error(-5, "recipient (blablabla) does not refer to any valid address", self.nodes[0].transferdomain, [{"src": {"address":"blablabla", "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-5, "recipient (blablabla) does not refer to any valid address", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":"blablabla", "amount":"100@DFI", "domain": 3}}])

    def invalid_values_dvm_evm(self):
        # Check for valid values DVM->EVM in transferdomain rpc
        assert_raises_rpc_error(-32600, "Src address must be a legacy or Bech32 address in case of \"DVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Dst address must be an ERC55 address in case of \"EVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Cannot transfer inside same domain", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Source amount must be equal to destination amount", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"101@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@BTC", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@BTC", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Excess data set, maximum allow is 0", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2, "data": "1"}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Excess data set, maximum allow is 0", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3, "data": "1"}}])

    def valid_transfer_dvm_evm(self):
        # Transfer 100 DFI from DVM to EVM
        tx1 = self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"100@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        # Check tx1 fields
        result = self.nodes[0].getcustomtx(tx1)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "DVM")
        assert_equal(result["dst"]["address"], self.eth_address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "EVM")

        # Check that EVM balance shows in gettokenbalances
        assert_equal(self.nodes[0].gettokenbalances({}, False, False, True), ['101.00000000@0'])

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(self.address, {}, True)['0']
        new_eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(new_dfi_balance, self.dfi_balance - Decimal('100'))
        assert_equal(new_eth_balance, int_to_eth_u256(100))
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 1)
        assert_equal(self.nodes[0].getaccount(self.eth_address)[0], "100.00000000@DFI")

    def invalid_values_evm_dvm(self):
        # Check for valid values EVM->DVM in transferdomain rpc
        assert_raises_rpc_error(-32600, "Src address must be an ERC55 address in case of \"EVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":self.address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Dst address must be a legacy or Bech32 address in case of \"DVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.eth_address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Cannot transfer inside same domain", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Source amount must be equal to destination amount", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"101@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address, "amount":"100@BTC", "domain": 3}, "dst":{"address":self.address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"100@BTC", "domain": 2}}])
        assert_raises_rpc_error(-32600, "TransferDomain currently only supports a single transfer per transaction", self.nodes[0].transferdomain, [{"src": {"address":self.eth_address1, "amount":"10@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"10@DFI", "domain": 2}},
                                           {"src": {"address":self.eth_address1, "amount":"10@DFI", "domain": 3}, "dst":{"address":self.address_2nd, "amount":"10@DFI", "domain": 2}}])

    def valid_transfer_evm_dvm(self):
        self.rollback_to(self.start_height)

        # Transfer 100 DFI from DVM to EVM
        self.valid_transfer_dvm_evm()

        # Transfer 100 DFI from EVM to DVM
        tx = self.nodes[0].transferdomain([{"src": {"address":self.eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"100@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        # Check tx fields
        result = self.nodes[0].getcustomtx(tx)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.eth_address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "EVM")
        assert_equal(result["dst"]["address"], self.address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "DVM")

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(self.address, {}, True)['0']
        new_eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(new_dfi_balance, self.dfi_balance)
        assert_equal(new_eth_balance, self.eth_balance)
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 0)

    def invalid_transfer_no_auth(self):
        assert_raises_rpc_error(-5, "Incorrect authorization for " + self.address1, self.nodes[0].transferdomain, [{"src": {"address":self.address1, "amount":"1@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"1@DFI", "domain": 3}}])
        assert_raises_rpc_error(-5, "no full public key for address", self.nodes[0].transferdomain, [{"src": {"address":self.no_auth_eth_address, "amount":"1@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"1@DFI", "domain": 2}}])

    def valid_transfer_to_evm_then_move_then_back_to_dvm(self):
        self.rollback_to(self.start_height)

        # Transfer 100 DFI from DVM to EVM
        tx1 = self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"101@DFI", "domain": 2}, "dst":{"address":self.eth_address, "amount":"101@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        # Check tx1 fields
        result = self.nodes[0].getcustomtx(tx1)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.address)
        assert_equal(result["src"]["amount"], "101.00000000@0")
        assert_equal(result["src"]["domain"], "DVM")
        assert_equal(result["dst"]["address"], self.eth_address)
        assert_equal(result["dst"]["amount"], "101.00000000@0")
        assert_equal(result["dst"]["domain"], "EVM")

        # Check that EVM balance shows in gettokenbalances
        assert_equal(self.nodes[0].gettokenbalances({}, False, False, True), ['101.00000000@0'])

        # Check new balances
        new_eth_balance = self.nodes[0].eth_getBalance(self.eth_address)
        assert_equal(new_eth_balance, int_to_eth_u256(101))
        assert_equal(len(self.nodes[0].getaccount(self.eth_address, {}, True)), 1)
        assert_equal(self.nodes[0].getaccount(self.eth_address)[0], "101.00000000@DFI")

        # Move from one EVM address to another
        self.nodes[0].evmtx(self.eth_address, 0, 21, 21001, self.eth_address1, 100)
        self.nodes[0].generate(1)

        new_eth1_balance = self.nodes[0].eth_getBalance(self.eth_address1)
        assert_equal(new_eth1_balance, int_to_eth_u256(100))

        dfi_balance = self.nodes[0].getaccount(self.address, {}, True)['0']

        # Transfer 100 DFI from EVM to DVM
        tx = self.nodes[0].transferdomain([{"src": {"address":self.eth_address1, "amount":"100@DFI", "domain": 3}, "dst":{"address":self.address, "amount":"100@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        # Check tx fields
        result = self.nodes[0].getcustomtx(tx)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], self.eth_address1)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "EVM")
        assert_equal(result["dst"]["address"], self.address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "DVM")

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(self.address, {}, True)['0']
        assert_equal(new_dfi_balance, dfi_balance + Decimal('100'))
        new_eth1_balance = self.nodes[0].eth_getBalance(self.eth_address1)
        assert_equal(new_eth1_balance, "0x0")
        assert_equal(len(self.nodes[0].getaccount(self.eth_address1, {}, True)), 0)

    def run_test(self):
        self.setup()
        self.invalid_before_fork_and_disabled()
        self.check_initial_balance()
        self.invalid_parameters()

        # Transfer DVM->EVM
        self.invalid_values_dvm_evm()
        self.valid_transfer_dvm_evm()

        # Transfer EVM->DVM
        self.invalid_values_evm_dvm()
        self.valid_transfer_evm_dvm()

        # Invalid authorisation
        self.invalid_transfer_no_auth()

        self.valid_transfer_to_evm_then_move_then_back_to_dvm()

if __name__ == '__main__':
    EVMTest().main()

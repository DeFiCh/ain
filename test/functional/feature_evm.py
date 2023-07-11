#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""
from test_framework.evm_key_pair import KeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256
)
from test_framework.evm_contract import EVMContract

from decimal import Decimal
from web3 import Web3

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1'],
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1']
        ]

    def test_tx_without_chainid(self, node, keypair, web3):

        nonce = web3.eth.get_transaction_count(keypair.address)

        node.transferdomain([{"src": {"address": node.get_genesis_keys().ownerAuthAddress, "amount": "50@DFI",
                                      "domain": 2},
                              "dst": {"address": keypair.address, "amount": "50@DFI", "domain": 3}}])
        node.generate(1)

        tx = {
            'nonce': nonce,
            'to': "0x0000000000000000000000000000000000000000",
            'value': web3.to_wei(0.1, 'ether'),
            'gas': 21000,
            'gasPrice': web3.to_wei(1, 'gwei')
        }

        signed_tx = web3.eth.account.sign_transaction(tx, keypair.pkey)
        web3.eth.send_raw_transaction(signed_tx.rawTransaction)
        node.generate(1)

    def run_test(self):
        # Addresses and keys
        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        eth_address = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        eth_address_privkey = 'af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23'
        eth_address1 = self.nodes[0].getnewaddress("", "eth")
        to_address = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'
        to_address_privkey = '17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35'

        # Import Bech32 compressed private key for:
        # Bech32: bcrt1qu7xc8kkpwzxzamw5236j2gpvtxmgp2zmfzmc32
        # Eth: 0x1286B92185a5d95eA7747F399e6cB1842851fAC3
        self.nodes[0].importprivkey("cNQ9fkAkHfWCPuyi5huZS6co3vND7tkNoWL7HiR2Jck3Jcb28SYW")
        bech32_info = self.nodes[0].getaddressinfo('bcrt1qu7xc8kkpwzxzamw5236j2gpvtxmgp2zmfzmc32')
        assert_equal(bech32_info['ismine'], True)
        assert_equal(bech32_info['solvable'], True)
        assert_equal(bech32_info['pubkey'], '03451d293bef258fa768bed74a5301ce4cfee2b1a8d9f87d20bb669668d9cb75b8')
        eth_info = self.nodes[0].getaddressinfo('0x1286B92185a5d95eA7747F399e6cB1842851fAC3')
        assert_equal(eth_info['ismine'], True)
        assert_equal(eth_info['solvable'], True)
        assert_equal(eth_info['pubkey'], '04451d293bef258fa768bed74a5301ce4cfee2b1a8d9f87d20bb669668d9cb75b86e90a39bdc9cf04e708ad0b3a8589ce3d1fab3b37a6e7651e7fa9e61e442abf1')

        # Import Eth private key for:
        # Bech32: bcrt1q25m0h24ef4njmjznwwe85w99cn78k04te6w3qt
        # Eth: 0xe5BBbf6eEDc1F217D72DD97E23049ab4B21AB84E
        self.nodes[0].importprivkey("56c679ab38001e7d427e3fbc4363fcd2100e74d8ac650a2d2ff3a69254d4dae4")
        bech32_info = self.nodes[0].getaddressinfo('bcrt1q25m0h24ef4njmjznwwe85w99cn78k04te6w3qt')
        assert_equal(bech32_info['ismine'], True)
        assert_equal(bech32_info['solvable'], True)
        assert_equal(bech32_info['pubkey'], '02ed3add70f9d3fde074bc74310d5684f5e5d2836106a8286aef1324f9791658da')
        eth_info = self.nodes[0].getaddressinfo('0xe5BBbf6eEDc1F217D72DD97E23049ab4B21AB84E')
        assert_equal(eth_info['ismine'], True)
        assert_equal(eth_info['solvable'], True)
        assert_equal(eth_info['pubkey'], '04ed3add70f9d3fde074bc74310d5684f5e5d2836106a8286aef1324f9791658da9034d75da80783a544da73d3bb809df9f8bd50309b51b8ee3fab240d5610511c')

        # Import Bech32 uncompressed private key for:
        # Bech32: bcrt1qzm54jxk82jp34jx49v5uaxk4ye2pv03e5aknl6
        # Eth: 0xd61Cd3F09E2C20376BFa34ed3a4FcF512341fA0E
        self.nodes[0].importprivkey('92e6XLo5jVAVwrQKPNTs93oQco8f8sDNBcpv73Dsrs397fQtFQn')
        bech32_info = self.nodes[0].getaddressinfo('bcrt1qzm54jxk82jp34jx49v5uaxk4ye2pv03e5aknl6')
        assert_equal(bech32_info['ismine'], True)
        assert_equal(bech32_info['iswitness'], True)
        assert_equal(bech32_info['pubkey'], '02087a947bbb87f5005d25c56a10a7660694b81bffe209a9e89a6e2683a6a900b6')
        eth_info = self.nodes[0].getaddressinfo('0xd61Cd3F09E2C20376BFa34ed3a4FcF512341fA0E')
        assert_equal(eth_info['ismine'], True)
        assert_equal(eth_info['solvable'], True)
        assert_equal(eth_info['pubkey'], '04087a947bbb87f5005d25c56a10a7660694b81bffe209a9e89a6e2683a6a900b6ff3a7732eb015021deda823f265ed7a5bbec7aa7e83eb395d4cb7d5dea63d144')

        # Import addresses
        self.nodes[0].importprivkey(eth_address_privkey) # eth_address
        self.nodes[0].importprivkey(to_address_privkey) # to_address

        # Check export of private key
        privkey = self.nodes[0].dumpprivkey(to_address)
        assert_equal(privkey, to_address_privkey)

        # Check creation and private key dump of new Eth key
        test_eth_dump = self.nodes[0].getnewaddress("", "eth")
        self.nodes[0].dumpprivkey(test_eth_dump)

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(-32600, "called before NextNetworkUpgrade height", self.nodes[0].evmtx, eth_address, 0, 21, 21000, to_address, 0.1)

        # Move to fork height
        self.nodes[0].generate(4)

        # Activate EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        # Check transferdomain without DFI balance before DFI address is funded
        assert_raises_rpc_error(-32600, "amount 0.00000000 is less than 100.00000000", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])

        # Create additional token to be used in tests
        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": address
        })
        self.nodes[0].getbalance()

        # Fund DFI address
        txid = self.nodes[0].utxostoaccount({address: "101@DFI"})
        self.nodes[0].generate(1)

        # transferdomain tests

        # Check initial balances
        dfi_balance = self.nodes[0].getaccount(address, {}, True)['0']
        eth_balance = self.nodes[0].eth_getBalance(eth_address)
        assert_equal(dfi_balance, Decimal('101'))
        assert_equal(eth_balance, int_to_eth_u256(0))
        assert_equal(len(self.nodes[0].getaccount(eth_address, {}, True)), 0)

        # Check for invalid parameters in transferdomain rpc
        assert_raises_rpc_error(-5, "Eth type addresses are not valid", self.nodes[0].createrawtransaction, [{'txid': txid, 'vout': 1}], [{eth_address: 1}])
        assert_raises_rpc_error(-5, "Eth type addresses are not valid", self.nodes[0].sendmany, "", {eth_address: 1})
        assert_raises_rpc_error(-5, "Eth type addresses are not valid", self.nodes[0].sendmany, "", {eth_address: 1})
        assert_raises_rpc_error(-5, "Eth type addresses are not valid", self.nodes[0].sendtoaddress, eth_address, 1)
        assert_raises_rpc_error(-5, "Eth type addresses are not valid", self.nodes[0].accounttoaccount, address, {eth_address: "1@DFI"})
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"address\" must not be null", self.nodes[0].transferdomain, [{"src": {"amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"amount\" must not be null", self.nodes[0].transferdomain, [{"src": {"address":address, "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"domain\" must not be null", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI"}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "JSON value is not an integer as expected", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": "dvm"}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-8, "JSON value is not an integer as expected", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": "evm"}}])
        assert_raises_rpc_error(-8, "Invalid parameters, src argument \"domain\" must be either 2 (DFI token to EVM) or 3 (EVM to DFI token)", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 0}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Unknown transfer domain aspect", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 4}}])
        assert_raises_rpc_error(-5, "recipient (blablabla) does not refer to any valid address", self.nodes[0].transferdomain, [{"src": {"address":"blablabla", "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-5, "recipient (blablabla) does not refer to any valid address", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":"blablabla", "amount":"100@DFI", "domain": 3}}])

        # Check for valid values DVM->EVM in transferdomain rpc
        assert_raises_rpc_error(-32600, "Src address must be a legacy or Bech32 address in case of \"DVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Dst address must be an ETH address in case of \"EVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Cannot transfer inside same domain", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Source amount must be equal to destination amount", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"101@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@BTC", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@BTC", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Excess data set, maximum allow is 0", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2, "data": "1"}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Excess data set, maximum allow is 0", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3, "data": "1"}}])
        assert_raises_rpc_error(-32600, "Not enough balance in " + eth_address + " to cover \"EVM\" domain transfer", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":address, "amount":"100@DFI", "domain": 2}}])

        # Transfer 100 DFI from DVM to EVM
        tx1 = self.nodes[0].transferdomain([{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        # Check tx1 fields
        result = self.nodes[0].getcustomtx(tx1)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "DVM")
        assert_equal(result["dst"]["address"], eth_address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "EVM")

        # Check that EVM balance shows in gettokenbalances
        assert_equal(self.nodes[0].gettokenbalances({}, False, False, True), ['101.00000000@0'])

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(address, {}, True)['0']
        new_eth_balance = self.nodes[0].eth_getBalance(eth_address)
        assert_equal(new_dfi_balance, dfi_balance - Decimal('100'))
        assert_equal(new_eth_balance, int_to_eth_u256(100))
        assert_equal(len(self.nodes[0].getaccount(eth_address, {}, True)), 1)
        assert_equal(self.nodes[0].getaccount(eth_address)[0], "100.00000000@DFI")

        # Check for valid values EVM->DVM in transferdomain rpc
        assert_raises_rpc_error(-32600, "Src address must be an ETH address in case of \"EVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 3}, "dst":{"address":address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Dst address must be a legacy or Bech32 address in case of \"DVM\" domain", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "Cannot transfer inside same domain", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":address, "amount":"100@DFI", "domain": 3}}])
        assert_raises_rpc_error(-32600, "Source amount must be equal to destination amount", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":address, "amount":"101@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@BTC", "domain": 3}, "dst":{"address":address, "amount":"100@DFI", "domain": 2}}])
        assert_raises_rpc_error(-32600, "For transferdomain, only DFI token is currently supported", self.nodes[0].transferdomain, [{"src": {"address":eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":address, "amount":"100@BTC", "domain": 2}}])

        # Transfer 100 DFI from DVM to EVM
        tx2 = self.nodes[0].transferdomain([{"src": {"address":eth_address, "amount":"100@DFI", "domain": 3}, "dst":{"address":address, "amount":"100@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        # Check tx2 fields
        result = self.nodes[0].getcustomtx(tx2)["results"]["transfers"][0]
        assert_equal(result["src"]["address"], eth_address)
        assert_equal(result["src"]["amount"], "100.00000000@0")
        assert_equal(result["src"]["domain"], "EVM")
        assert_equal(result["dst"]["address"], address)
        assert_equal(result["dst"]["amount"], "100.00000000@0")
        assert_equal(result["dst"]["domain"], "DVM")

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(address, {}, True)['0']
        new_eth_balance = self.nodes[0].eth_getBalance(eth_address)
        assert_equal(new_dfi_balance, dfi_balance)
        assert_equal(new_eth_balance, eth_balance)
        assert_equal(len(self.nodes[0].getaccount(eth_address, {}, True)), 0)

        # Check multiple transfers in one tx
        tx3 = self.nodes[0].transferdomain([{"src": {"address":address, "amount":"10@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"10@DFI", "domain": 3}},
                                      {"src": {"address":address, "amount":"20@DFI", "domain": 2}, "dst":{"address":eth_address1, "amount":"20@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

        # Check tx3 fields
        result = self.nodes[0].getcustomtx(tx3)["results"]["transfers"]
        assert_equal(result[0]["src"]["address"], address)
        assert_equal(result[0]["src"]["amount"], "10.00000000@0")
        assert_equal(result[0]["src"]["domain"], "DVM")
        assert_equal(result[0]["dst"]["address"], eth_address)
        assert_equal(result[0]["dst"]["amount"], "10.00000000@0")
        assert_equal(result[0]["dst"]["domain"], "EVM")
        assert_equal(result[1]["src"]["address"], address)
        assert_equal(result[1]["src"]["amount"], "20.00000000@0")
        assert_equal(result[1]["src"]["domain"], "DVM")
        assert_equal(result[1]["dst"]["address"], eth_address1)
        assert_equal(result[1]["dst"]["amount"], "20.00000000@0")
        assert_equal(result[1]["dst"]["domain"], "EVM")

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(address, {}, True)['0']
        new_eth_balance = self.nodes[0].eth_getBalance(eth_address)
        new_eth_balance1 = self.nodes[0].eth_getBalance(eth_address1)
        assert_equal(new_dfi_balance, dfi_balance - Decimal('30'))
        assert_equal(new_eth_balance, int_to_eth_u256(10))
        assert_equal(new_eth_balance1, int_to_eth_u256(20))

        # Create transferdomain DVM->EVM and in same tx do EVM->DVM transfer (mixed transfers)
        tx = self.nodes[0].transferdomain([{"src": {"address":address, "amount":"10@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"10@DFI", "domain": 3}},
                                               {"src": {"address":eth_address1, "amount":"20@DFI", "domain": 3}, "dst":{"address":address, "amount":"20@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        # Check fields
        result = self.nodes[0].getcustomtx(tx)["results"]["transfers"]
        assert_equal(result[0]["src"]["address"], address)
        assert_equal(result[0]["src"]["amount"], "10.00000000@0")
        assert_equal(result[0]["src"]["domain"], "DVM")
        assert_equal(result[0]["dst"]["address"], eth_address)
        assert_equal(result[0]["dst"]["amount"], "10.00000000@0")
        assert_equal(result[0]["dst"]["domain"], "EVM")
        assert_equal(result[1]["src"]["address"], eth_address1)
        assert_equal(result[1]["src"]["amount"], "20.00000000@0")
        assert_equal(result[1]["src"]["domain"], "EVM")
        assert_equal(result[1]["dst"]["address"], address)
        assert_equal(result[1]["dst"]["amount"], "20.00000000@0")
        assert_equal(result[1]["dst"]["domain"], "DVM")

        # Create transferdomain DVM->EVM
        self.nodes[0].transferdomain([{"src": {"address":eth_address, "amount":"20@DFI", "domain": 3}, "dst":{"address":address, "amount":"20@DFI", "domain": 2}}])
        self.nodes[0].generate(1)

        # Check new balances
        new_dfi_balance = self.nodes[0].getaccount(address, {}, True)['0']
        new_eth_balance = self.nodes[0].eth_getBalance(eth_address)
        new_eth_balance1 = self.nodes[0].eth_getBalance(eth_address1)
        assert_equal(new_dfi_balance, dfi_balance)
        assert_equal(new_eth_balance, eth_balance)
        assert_equal(len(self.nodes[0].getaccount(eth_address, {}, True)), 0)
        assert_equal(new_eth_balance1, int_to_eth_u256(0))

        # evmtx tests

        # Fund Eth address
        self.nodes[0].transferdomain([{"src": {"address":address, "amount":"10@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"10@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 10000000000000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 0)

        # Get miner DFI balance before transaction
        miner_before = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        before_blockheight = self.nodes[0].getblockcount()

        # Test EVM Tx added first in time ordering
        self.nodes[0].evmtx(eth_address, 0, 21, 21001, to_address, 1)
        self.sync_mempools()

        # Add more EVM Txs to test block ordering
        tx5 = self.nodes[0].evmtx(eth_address, 5, 21, 21001, to_address, 1)
        tx4 = self.nodes[0].evmtx(eth_address, 4, 21, 21001, to_address, 1)
        tx2 = self.nodes[0].evmtx(eth_address, 2, 21, 21001, to_address, 1)
        tx1 = self.nodes[0].evmtx(eth_address, 1, 21, 21001, to_address, 1)
        tx3 = self.nodes[0].evmtx(eth_address, 3, 21, 21001, to_address, 1)
        raw_tx = self.nodes[0].getrawtransaction(tx5)
        self.sync_mempools()

        # Check the pending TXs
        result = self.nodes[0].eth_pendingTransactions()
        assert_equal(result[0]['blockHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(result[0]['blockNumber'], 'null')
        assert_equal(result[0]['from'], eth_address)
        assert_equal(result[0]['gas'], '0x5209')
        assert_equal(result[0]['gasPrice'], '0x4e3b29200')
        assert_equal(result[0]['hash'], '0xadf0fbeb972cdc4a82916d12ffc6019f60005de6dde1bbc7cb4417fe5a7b1bcb')
        assert_equal(result[0]['input'], '0x')
        assert_equal(result[0]['nonce'], '0x0')
        assert_equal(result[0]['to'], to_address.lower())
        assert_equal(result[0]['transactionIndex'], '0x0')
        assert_equal(result[0]['value'], '0xde0b6b3a7640000')
        assert_equal(result[0]['v'], '0x26')
        assert_equal(result[0]['r'], '0x3a0587be1a14bd5e68bc883e627f3c0999cff9458e30ea8049f17bd7369d7d9c')
        assert_equal(result[0]['s'], '0x1876f296657bc56499cc6398617f97b2327fa87189c0a49fb671b4361876142a')

        # Create replacement for nonce 0 TX with higher fee
        web3 = Web3(Web3.HTTPProvider(self.nodes[0].get_evm_rpc()))
        abi, bytecode = EVMContract.from_file("SimpleStorage.sol", "Test").compile()
        tx = web3.eth.contract(abi=abi, bytecode=bytecode).constructor(None).build_transaction({
            'chainId': 1133,
            'nonce': 0,
            'gasPrice': Web3.to_wei(5, "gwei")
        })
        signed_tx = web3.eth.account.sign_transaction(tx, private_key=eth_address_privkey)
        web3.eth.send_raw_transaction(signed_tx.rawTransaction)
        self.sync_mempools()

        # Check mempools for TXs
        mempool0 = self.nodes[0].getrawmempool()
        mempool1 = self.nodes[1].getrawmempool()
        assert_equal(sorted(mempool0), sorted(mempool1))

        # Mint TXs
        self.nodes[0].generate(1)

        # Check TXs in block in correct order
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(block_txs[1], '10f25f6e0b6a818c25c876c1bef12788a05b29d2f254c9ff0439f8d7427f5e57')
        assert_equal(block_txs[2], tx1)
        assert_equal(block_txs[3], tx2)
        assert_equal(block_txs[4], tx3)
        assert_equal(block_txs[5], tx4)
        assert_equal(block_txs[6], tx5)

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 4997179955000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 5000000000000000000)

        # Check miner account balance after transfer
        miner_after = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        miner_fee = miner_after - miner_before

        # Check EVM Tx shows in block on EVM side
        block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(block['transactions'], [
            '0x21d9f328bd713e31431596c326ef8da189106ce45b3bf4b75bf705c0e14be7d1',
            '0x66c380af8f76295bab799d1228af75bd3c436b7bbeb9d93acd8baac9377a851a',
            '0x02b05a6646feb65bf9491f9551e02678263239dc2512d73c9ad6bc80dc1c13ff',
            '0x1d4c8a49ad46d9362c805d6cdf9a8937ba115eec9def17b3efe23a09ee694e5c',
            '0xa382aa9f70f15bd0bf70e838f5ac0163e2501dbff2712e9622275e655e42ec1c',
            '0x05d4cdabc4ad55fb7caf42a7fb6d4e8cea991e2331cd9d98a5eef10d84b5c994'
        ])

        # Check pending TXs contains lower fee nonce TX - Mempool should remove this!
        assert_equal(self.nodes[0].eth_pendingTransactions(), [
            {'blockHash': '0x0000000000000000000000000000000000000000000000000000000000000000',
             'blockNumber': 'null',
             'from': eth_address,
             'gas': '0x5209',
             'gasPrice': '0x4e3b29200',
             'hash': '0xadf0fbeb972cdc4a82916d12ffc6019f60005de6dde1bbc7cb4417fe5a7b1bcb',
             'input': '0x',
             'nonce': '0x0',
             'to': '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110',
             'transactionIndex': '0x0',
             'value': '0xde0b6b3a7640000',
             'v': '0x26',
             'r': '0x3a0587be1a14bd5e68bc883e627f3c0999cff9458e30ea8049f17bd7369d7d9c',
             's': '0x1876f296657bc56499cc6398617f97b2327fa87189c0a49fb671b4361876142a',
             'type': '0x0'}
        ])

        # Try and send EVM TX a second time
        assert_raises_rpc_error(-26, "evm tx failed to validate", self.nodes[0].sendrawtransaction, raw_tx)

        # Check EVM blockhash
        eth_block = self.nodes[0].eth_getBlockByNumber('latest')
        eth_hash = eth_block['hash'][2:]
        eth_fee = eth_block['gasUsed'][2:]
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        raw_tx = self.nodes[0].getrawtransaction(block['tx'][0], 1)
        block_hash = raw_tx['vout'][1]['scriptPubKey']['hex'][20:84]
        assert_equal(block_hash, eth_hash)

        # Check EVM miner fee
        opreturn_fee_amount = raw_tx['vout'][1]['scriptPubKey']['hex'][84:]
        opreturn_fee_sats = Decimal(int(opreturn_fee_amount[2:4] + opreturn_fee_amount[0:2], 16)) / 100000000
        eth_fee_sats = Decimal(int(Decimal(int(eth_fee, 16)) / 10)) / 100000000
        assert_equal(opreturn_fee_sats, eth_fee_sats)
        assert_equal(opreturn_fee_sats, miner_fee)

        # Test rollback of EVM TX
        self.rollback_to(before_blockheight, self.nodes)
        miner_rollback = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        assert_equal(miner_before, miner_rollback)

        # Test max limit of TX from a specific sender
        for i in range(63):
            self.nodes[0].evmtx(eth_address, i, 21, 21001, to_address, 1)

        # Test error at the 65th EVM TX
        assert_raises_rpc_error(-26, "too-many-eth-txs-by-sender", self.nodes[0].evmtx, eth_address, 64, 21, 21001, to_address, 1)

        # Miot a block
        self.nodes[0].generate(1)
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(len(block_txs), 64)

        # Try and send another TX to make sure mempool has removed entires
        self.nodes[0].evmtx(eth_address, 64, 21, 21001, to_address, 1)
        self.nodes[0].generate(1)

        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(len(block_txs), 1)

        # Test that node should not crash without chainId param
        key_pair = KeyPair.from_node(self.nodes[0])
        self.test_tx_without_chainid(self.nodes[0], key_pair, web3)

        # Test rollback of EVM related TXs
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(101))
        assert_equal(self.nodes[0].getblockcount(), 100)

if __name__ == '__main__':
    EVMTest().main()

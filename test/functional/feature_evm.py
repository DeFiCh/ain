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

from decimal import Decimal


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1'],
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate3height=105', '-subsidytest=1', '-txindex=1']
        ]

    def test_tx_without_chainid(self, node, keypair):
        from web3 import Web3

        web3 = Web3(Web3.HTTPProvider(node.get_evm_rpc()))
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

        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        eth_address = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        eth_address1 = self.nodes[0].getnewaddress("", "eth")
        to_address = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'
        to_address_privkey = '17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35'

        self.nodes[0].importprivkey('af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23') # eth_address
        self.nodes[0].importprivkey(to_address_privkey) # to_address

        # Check export of private key
        privkey = self.nodes[0].dumpprivkey(to_address)
        assert_equal(privkey, to_address_privkey)

        # Check creation and prikey dump of new Eth key
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

        # Test EVM Tx
        tx3 = self.nodes[0].evmtx(eth_address, 2, 21, 21001, to_address, 1)
        tx2 = self.nodes[0].evmtx(eth_address, 1, 21, 21001, to_address, 1)
        tx = self.nodes[0].evmtx(eth_address, 0, 21, 21001, to_address, 1)
        tx4 = self.nodes[0].evmtx(eth_address, 3, 21, 21001, to_address, 1)
        raw_tx = self.nodes[0].getrawtransaction(tx)
        self.sync_mempools()

        # Check the pending TXs
        result = self.nodes[0].eth_pendingTransactions()
        assert_equal(result[2]['blockHash'], '0x0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(result[2]['blockNumber'], 'null')
        assert_equal(result[2]['from'], eth_address)
        assert_equal(result[2]['gas'], '0x5209')
        assert_equal(result[2]['gasPrice'], '0x4e3b29200')
        assert_equal(result[2]['hash'], '0xadf0fbeb972cdc4a82916d12ffc6019f60005de6dde1bbc7cb4417fe5a7b1bcb')
        assert_equal(result[2]['input'], '0x')
        assert_equal(result[2]['nonce'], '0x0')
        assert_equal(result[2]['to'], to_address.lower())
        assert_equal(result[2]['transactionIndex'], '0x0')
        assert_equal(result[2]['value'], '0xde0b6b3a7640000')
        assert_equal(result[2]['v'], '0x26')
        assert_equal(result[2]['r'], '0x3a0587be1a14bd5e68bc883e627f3c0999cff9458e30ea8049f17bd7369d7d9c')
        assert_equal(result[2]['s'], '0x1876f296657bc56499cc6398617f97b2327fa87189c0a49fb671b4361876142a')

        # Check mempools for TXs
        mempool = self.nodes[0].getrawmempool()
        assert (mempool.count(tx))
        assert (mempool.count(tx2))
        assert (mempool.count(tx3))
        assert (mempool.count(tx4))
        mempool = self.nodes[1].getrawmempool()
        assert (mempool.count(tx))
        assert (mempool.count(tx2))
        assert (mempool.count(tx3))
        assert (mempool.count(tx4))
        self.nodes[0].generate(1)

        # Check TXs in block in correct order
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(block_txs[1], tx)
        assert_equal(block_txs[2], tx2)
        assert_equal(block_txs[3], tx3)
        assert_equal(block_txs[4], tx4)

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 5998236000000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 4000000000000000000)

        # Check miner account balance after transfer
        miner_after = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        miner_fee = miner_after - miner_before

        # Check EVM Tx is in block
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        assert_equal(block['tx'][1], tx)

        # Check EVM Tx shows in block on EVM side
        block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(block['transactions'], [
            '0xadf0fbeb972cdc4a82916d12ffc6019f60005de6dde1bbc7cb4417fe5a7b1bcb',
            '0x66c380af8f76295bab799d1228af75bd3c436b7bbeb9d93acd8baac9377a851a',
            '0x02b05a6646feb65bf9491f9551e02678263239dc2512d73c9ad6bc80dc1c13ff',
            '0x1d4c8a49ad46d9362c805d6cdf9a8937ba115eec9def17b3efe23a09ee694e5c'
        ])

        # Check pending TXs now empty
        assert_equal(self.nodes[0].eth_pendingTransactions(), [])

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
        eth_fee_sats = Decimal(int(eth_fee, 16)) / 1000000000
        assert_equal(opreturn_fee_sats, eth_fee_sats)
        assert_equal(opreturn_fee_sats, miner_fee)

        # Test rollback of EVM TX
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        miner_rollback = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        assert_equal(miner_before, miner_rollback)

        # Test that node should not crash without chainId param
        key_pair = KeyPair.from_node(self.nodes[0])
        self.test_tx_without_chainid(self.nodes[0], key_pair)

        # Test rollback of EVM related TXs
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(101))
        assert_equal(self.nodes[0].getblockcount(), 100)

if __name__ == '__main__':
    EVMTest().main()

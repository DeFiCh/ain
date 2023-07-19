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
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1'],
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1']
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
            'gasPrice': web3.to_wei(10, 'gwei')
        }

        signed_tx = web3.eth.account.sign_transaction(tx, keypair.pkey)
        web3.eth.send_raw_transaction(signed_tx.rawTransaction)
        node.generate(1)

    def run_test(self):

        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        eth_address = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        eth_address_bech32 = 'bcrt1qta8meuczw0mhqupzjl5wplz47xajz0dn0wxxr8'
        eth_address_privkey = 'af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23'
        to_address = '0x6c34cbb9219d8caa428835d2073e8ec88ba0a110'
        to_address_privkey = '17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35'

        # Import to_address
        self.nodes[0].importprivkey(to_address_privkey)

        # Import eth_address and validate Bech32 eqivilent is part of the wallet
        self.nodes[0].importprivkey('af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23')
        result = self.nodes[0].getaddressinfo(eth_address_bech32)
        assert_equal(result['scriptPubKey'], '00145f4fbcf30273f770702297e8e0fc55f1bb213db3')
        assert_equal(result['pubkey'], '021286647f7440111ab928bdea4daa42533639c4567d81eca0fff622fb6438eae3')
        assert_equal(result['ismine'], True)
        assert_equal(result['iswitness'], True)

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

        # Check error before EVM enabled
        assert_raises_rpc_error(-32600, "Cannot create tx, EVM is not enabled", self.nodes[0].evmtx, eth_address, 0, 21, 21000, to_address, 0.1)
        assert_raises_rpc_error(-32600, "Cannot create tx, EVM is not enabled", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])

        # Activate EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        # Check error before transferdomain enabled
        assert_raises_rpc_error(-32600, "Cannot create tx, transfer domain is not enabled", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])

        # Activate transferdomain
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/transferdomain': 'true'}})
        self.nodes[0].generate(1)

        # Check error before transferdomain DVM to EVM is enabled
        assert_raises_rpc_error(-32600, "DVM to EVM is not currently enabled", self.nodes[0].transferdomain, [{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])

        # Activate transferdomain DVM to EVM
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/transferdomain/allowed/dvm-evm': 'true'}})
        self.nodes[0].generate(1)

        # Fund DFI address
        txid = self.nodes[0].utxostoaccount({address: "101@DFI"})
        self.nodes[0].generate(1)

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

        # evmtx tests

        # Fund Eth address
        self.nodes[0].transferdomain([{"src": {"address":address, "amount":"10@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"10@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 10000000000000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 0)

        # Send tokens to burn address
        burn_address = "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"
        self.nodes[0].importprivkey("93ViFmLeJVgKSPxWGQHmSdT5RbeGDtGW4bsiwQM2qnQyucChMqQ")
        result = self.nodes[0].getburninfo()
        assert_equal(result['address'], burn_address)
        self.nodes[0].accounttoaccount(address, {burn_address: "1@DFI"})
        self.nodes[0].generate(1)

        # Get burn address and miner account balance before transaction
        burn_before = Decimal(self.nodes[0].getaccount(burn_address)[0].split('@')[0])
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
            'gasPrice': Web3.to_wei(10, "gwei")
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
        assert_equal(block_txs[1], 'fe0106aae2f1fd36535cf5e3d5007b5237e323633f5ee94dcc7b626c31425654')
        assert_equal(block_txs[2], tx1)
        assert_equal(block_txs[3], tx2)
        assert_equal(block_txs[4], tx3)
        assert_equal(block_txs[5], tx4)
        assert_equal(block_txs[6], tx5)

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 4996564910000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 5000000000000000000)

        # Get burn address and miner account balance after transfer
        burn_after = Decimal(self.nodes[0].getaccount(burn_address)[0].split('@')[0])
        miner_after = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        burnt_fee = burn_after - burn_before
        miner_fee = miner_after - miner_before

        # Check EVM Tx shows in block on EVM side
        block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(block['transactions'], [
            '0x8e0acf5ae13fccb364872d2852670f35fecdb55ed341ca9664e7cd80639bdb52',
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
             'from': '0x9b8a4af42140d8a4c153a822f02571a1dd037e89',
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
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        raw_tx = self.nodes[0].getrawtransaction(block['tx'][0], 1)
        block_hash = raw_tx['vout'][1]['scriptPubKey']['hex'][20:84]
        assert_equal(block_hash, eth_hash)

        # Check EVM burnt fee
        opreturn_burnt_fee_amount = raw_tx['vout'][1]['scriptPubKey']['hex'][84:]
        opreturn_burnt_fee_sats = Decimal(int(opreturn_burnt_fee_amount[4:6] + opreturn_burnt_fee_amount[2:4] + opreturn_burnt_fee_amount[0:2], 16)) / 100000000
        assert_equal(opreturn_burnt_fee_sats, burnt_fee)

        # Check EVM miner fee
        opreturn_priority_fee_amount = raw_tx['vout'][1]['scriptPubKey']['hex'][100:]
        opreturn_priority_fee_sats = Decimal(int(opreturn_priority_fee_amount[4:6] + opreturn_priority_fee_amount[2:4] + opreturn_priority_fee_amount[0:2], 16)) / 100000000
        assert_equal(opreturn_priority_fee_sats, miner_fee)

        # Test rollback of EVM TX
        self.rollback_to(before_blockheight, self.nodes)
        # self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        miner_rollback = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        assert_equal(miner_before, miner_rollback)

        # Test max limit of TX from a specific sender
        for i in range(63):
            self.nodes[0].evmtx(eth_address, i, 21, 21001, to_address, 1)

        # Test error at the 65th EVM TX
        assert_raises_rpc_error(-26, "too-many-eth-txs-by-sender", self.nodes[0].evmtx, eth_address, 64, 21, 21001, to_address, 1)

        # Mint a block
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

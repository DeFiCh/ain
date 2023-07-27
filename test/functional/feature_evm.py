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
from web3 import Web3

class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1'],
            ['-txordering=2', '-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-changiintermediateheight=105', '-changiintermediate2height=105', '-changiintermediate3height=105', '-changiintermediate4height=105', '-subsidytest=1', '-txindex=1']
        ]

    def test_tx_without_chainid(self, node, keypair):

        web3 = Web3(Web3.HTTPProvider(self.nodes[0].get_evm_rpc()))
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
        # Addresses and keys
        address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        eth_address = '0x9b8a4af42140d8a4c153a822f02571a1dd037e89'
        eth_address_privkey = 'af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23'
        eth_address_bech32 = 'bcrt1qta8meuczw0mhqupzjl5wplz47xajz0dn0wxxr8'
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
        txid = self.nodes[0].utxostoaccount({address: "200@DFI"})
        self.nodes[0].generate(1)

        # Check initial balances
        dfi_balance = self.nodes[0].getaccount(address, {}, True)['0']
        eth_balance = self.nodes[0].eth_getBalance(eth_address)
        assert_equal(dfi_balance, Decimal('200'))
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
        self.nodes[0].transferdomain([{"src": {"address":address, "amount":"100@DFI", "domain": 2}, "dst":{"address":eth_address, "amount":"100@DFI", "domain": 3}}])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 100000000000000000000)
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
        tx0 = self.nodes[0].evmtx(eth_address, 0, 22, 21001, to_address, 1)
        self.sync_mempools()

        # Check mempools for TXs
        mempool0 = self.nodes[0].getrawmempool()
        mempool1 = self.nodes[1].getrawmempool()
        assert_equal(sorted(mempool0), sorted(mempool1))

        # Mint TXs
        self.nodes[0].generate(1)

        # Check TXs in block in correct order
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(block_txs[1], tx0)
        assert_equal(block_txs[2], tx1)
        assert_equal(block_txs[3], tx2)
        assert_equal(block_txs[4], tx3)
        assert_equal(block_txs[5], tx4)
        assert_equal(block_txs[6], tx5)

        # Check Eth balances after transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 93997333000000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 6000000000000000000)

        # Get burn address and miner account balance after transfer
        burn_after = Decimal(self.nodes[0].getaccount(burn_address)[0].split('@')[0])
        miner_after = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        burnt_fee = burn_after - burn_before
        miner_fee = miner_after - miner_before

        # Check EVM Tx shows in block on EVM side
        block = self.nodes[0].eth_getBlockByNumber("latest", False)
        assert_equal(block['transactions'], [
            '0xcffc5526b42c0defa7d90cc806e50e582a0339a3336c7e32de237fbe4d62263b',
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
             'type': '0x0',
             'chainId': '0x1'}
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
        miner_rollback = Decimal(self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress)[0].split('@')[0])
        assert_equal(miner_before, miner_rollback)

        # Check Eth balances before transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 100000000000000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 0)

        # Test max limit of TX from a specific sender
        for i in range(63):
            self.nodes[0].evmtx(eth_address, i, 21, 21001, to_address, 1)

        # Test error at the 65th EVM TX
        assert_raises_rpc_error(-26, "too-many-eth-txs-by-sender", self.nodes[0].evmtx, eth_address, 64, 21, 21001, to_address, 1)

        # Mint a block
        self.nodes[0].generate(1)
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(len(block_txs), 64)

        # Check Eth balances after transfer
        assert_equal(int(self.nodes[0].eth_getBalance(eth_address)[2:], 16), 36972217000000000000)
        assert_equal(int(self.nodes[0].eth_getBalance(to_address)[2:], 16), 63000000000000000000)

        # Try and send another TX to make sure mempool has removed entires
        tx = self.nodes[0].evmtx(eth_address, 63, 21, 21001, to_address, 1)
        self.nodes[0].generate(1)

        # Check TX is in block
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(block_txs[1], tx)

        # Test setting of finalized block
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/evm/block/finality_count': '100'}})
        self.nodes[0].generate(1)

        # Check Gov var is present
        attrs = self.nodes[0].getgov("ATTRIBUTES")['ATTRIBUTES']
        assert_equal(attrs['v0/evm/block/finality_count'], '100')

        # Test multiple replacement TXs with differing fees
        self.nodes[0].evmtx(eth_address, 64, 22, 21001, to_address, 1)
        self.nodes[0].evmtx(eth_address, 64, 23, 21001, to_address, 1)
        tx0 = self.nodes[0].evmtx(eth_address, 64, 25, 21001, to_address, 1)
        self.nodes[0].evmtx(eth_address, 64, 21, 21001, to_address, 1)
        self.nodes[0].evmtx(eth_address, 64, 24, 21001, to_address, 1)
        self.nodes[0].evmtx(to_address, 0, 22, 21001, eth_address, 1)
        self.nodes[0].evmtx(to_address, 0, 23, 21001, eth_address, 1)
        tx1 = self.nodes[0].evmtx(to_address, 0, 25, 21001, eth_address, 1)
        self.nodes[0].evmtx(to_address, 0, 21, 21001, eth_address, 1)
        self.nodes[0].evmtx(to_address, 0, 24, 21001, eth_address, 1)
        self.nodes[0].generate(1)

        # Check highest paying fee TX in block
        block_txs = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))['tx']
        assert_equal(block_txs[1], tx0)
        assert_equal(block_txs[2], tx1)

        # Test that node should not crash without chainId param
        key_pair = KeyPair.from_node(self.nodes[0])
        self.test_tx_without_chainid(self.nodes[0], key_pair)

        # Test rollback of EVM related TXs
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(101))
        assert_equal(self.nodes[0].getblockcount(), 100)

if __name__ == '__main__':
    EVMTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test smart contracts"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time

class SmartContractTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1010', '-subsidytest=1', '-txindex=1', '-jellyfish_regtest=1']]

    def rollback(self, count):
        block = self.nodes[0].getblockhash(count)
        self.nodes[0].invalidateblock(block)
        self.nodes[0].clearmempool()

    def run_test(self):
        self.nodes[0].generate(1000)

        address = self.nodes[0].getnewaddress("", "legacy")
        invalid_address = 'mrV4kYRhyrfiCcpfXDSZuhe56kCQu84gqh'
        dfi_amount = 1000
        btc_amount = 1
        dfip = 'dbtcdfiswap'

        # Check invalid calls
        assert_raises_rpc_error(-5, 'Incorrect authorization for {}'.format(invalid_address), self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@2', invalid_address)
        assert_raises_rpc_error(-4, 'Insufficient funds', self.nodes[0].executesmartcontract, dfip, str(dfi_amount) + '@0')
        assert_raises_rpc_error(-8, 'Specified smart contract not found', self.nodes[0].executesmartcontract, 'DFIP9999', str(dfi_amount) + '@0')
        assert_raises_rpc_error(-8, 'BTC source address must be provided for DFIP2201', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@2')

        # Create tokens
        self.nodes[0].createtoken({
            "symbol": "ETH",
            "name": "Ether",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)
        self.nodes[0].createtoken({
            "symbol": "BTC",
            "name": "Bitcoin",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        # Create and fund address with BTC
        address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].minttokens("100000@BTC")
        self.nodes[0].generate(1)
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {address: "20000@BTC"})
        self.nodes[0].generate(1)

        # Check invalid calls
        assert_raises_rpc_error(-32600, 'called before FortCanningHill height', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@2', address)

        # Move to FortCanningHill
        self.nodes[0].generate(1010 - self.nodes[0].getblockcount())

        # Check invalid call
        assert_raises_rpc_error(-32600, 'DFIP2201 smart contract is not enabled', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@2', address)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/active':'false'}})
        self.nodes[0].generate(1)

        # Check invalid call
        assert_raises_rpc_error(-32600, 'DFIP2201 smart contract is not enabled', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@2', address)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/active':'true'}})
        self.nodes[0].generate(1)

        # Check invalid calls
        assert_raises_rpc_error(-32600, 'is less than', self.nodes[0].executesmartcontract, dfip, '20000.00000001@2', address)
        assert_raises_rpc_error(-3, 'Amount out of range', self.nodes[0].executesmartcontract, dfip, '0@2', address)
        assert_raises_rpc_error(-32600, 'Specified token not found', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@9999', address)
        assert_raises_rpc_error(-32600, 'Only Bitcoin can be swapped in DFIP2201', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@1', address)
        assert_raises_rpc_error(-32600, 'fixedIntervalPrice with id <BTC/USD> not found', self.nodes[0].executesmartcontract, dfip, str(btc_amount) + '@2', address)

        # Test min swap
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/minswap':'0.00001'}})
        self.nodes[0].generate(1)

        # Check invalid calls
        assert_raises_rpc_error(-32600, 'Below minimum swapable amount, must be at least 0.00001000 BTC', self.nodes[0].executesmartcontract, dfip, '0.00000999@2', address)

        # Set up oracles
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        feeds = [{"currency": "USD", "token": "DFI"},
                 {"currency": "USD", "token": "BTC"}]
        oracle = self.nodes[0].appointoracle(oracle_address, feeds, 10)
        self.nodes[0].generate(1)

        prices = [{'currency': 'USD', 'tokenAmount': '1@DFI'},
                  {'currency': 'USD', 'tokenAmount': '1@BTC'}]
        self.nodes[0].setoracledata(oracle, int(time.time()), prices)
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
                                    'token': 'DFI',
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})

        self.nodes[0].setcollateraltoken({
                                    'token': 'BTC',
                                    'factor': 1,
                                    'fixedIntervalPriceId': "BTC/USD"})

        self.nodes[0].generate(7)

        # Import community balance
        self.nodes[0].importprivkey('cMv1JaaZ9Mbb3M3oNmcFvko8p7EcHJ8XD7RCQjzNaMs7BWRVZTyR')
        balance = self.nodes[0].getbalance()

        # Try and fund more than is in community balance
        assert_raises_rpc_error(-4, 'Insufficient funds', self.nodes[0].executesmartcontract, dfip, '18336.22505381@0', address)

        # Check smart contract details and balance
        result = self.nodes[0].listsmartcontracts()
        assert_equal(result[0]['name'], 'DFIP2201')
        assert_equal(result[0]['call'], 'dbtcdfiswap')
        assert_equal(result[0]['address'], 'bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqdku202')
        assert('0' not in result[0])

        # Fund smart contract
        tx = self.nodes[0].executesmartcontract(dfip, '18336.225@0')
        self.nodes[0].generate(1)

        # Check smart contract details and balance
        result = self.nodes[0].listsmartcontracts()
        assert_equal(result[0]['0'], Decimal('18336.225'))

        # Check balance has changed as expected
        block = self.nodes[0].getblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount() - 100))
        rewards = self.nodes[0].getrawtransaction(block['tx'][0], 1)['vout']
        staker_reward = rewards[0]['value']
        community_reward = rewards[1]['value']
        fee = self.nodes[0].gettransaction(tx)['fee']
        assert_equal(balance + staker_reward + community_reward - Decimal('18336.225') + fee, self.nodes[0].getbalance())

        # Test swap for more than in community fund by 1 Sat
        block = self.nodes[0].getblockcount() + 1
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/premium':'0.00000000'}})
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-32600, 'amount 18336.22500000 is less than 18336.22500001', self.nodes[0].executesmartcontract, dfip, '18336.22500001@2', address)

        # Test again for full amount in community balance
        self.nodes[0].executesmartcontract(dfip, '18336.22500000@2', address)
        self.nodes[0].generate(1)
        assert_equal(18336.22500000, float(self.nodes[0].getaccount(address)[0].split('@')[0]))
        assert('0' not in self.nodes[0].listsmartcontracts())

        # Set "real world" prices
        self.rollback(block)
        prices = [{'currency': 'USD', 'tokenAmount': '2@DFI'},
                  {'currency': 'USD', 'tokenAmount': '40000@BTC'}]
        self.nodes[0].setoracledata(oracle, int(time.time()), prices)
        self.nodes[0].generate(10)

        # Test default 2.5% premium
        block = self.nodes[0].getblockcount() + 1
        self.nodes[0].executesmartcontract(dfip, '0.09999999@2', address)
        self.nodes[0].generate(1)
        assert_equal(2049.999795, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test 5% premium
        self.rollback(block)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/premium':'0.05'}})
        self.nodes[0].generate(1)
        self.nodes[0].executesmartcontract(dfip, '0.09999999@2', address)
        self.nodes[0].generate(1)
        assert_equal(2099.99979, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test 0.1% premium
        self.rollback(block)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/premium':'0.001'}})
        self.nodes[0].generate(1)
        self.nodes[0].executesmartcontract(dfip, '0.09999999@2', address)
        self.nodes[0].generate(1)
        assert_equal(2001.9997998, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test 0.000001% premium
        self.rollback(block)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/premium':'0.00000001'}})
        self.nodes[0].generate(1)
        self.nodes[0].executesmartcontract(dfip, '0.1@2', address)
        self.nodes[0].generate(1)
        assert_equal(2000.00002, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test 0% premium
        self.rollback(block)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/premium':'0.00000000'}})
        self.nodes[0].generate(1)
        self.nodes[0].executesmartcontract(dfip, '0.1@2', address)
        self.nodes[0].generate(1)
        assert_equal(2000, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Swap min amount
        self.rollback(block)
        self.nodes[0].executesmartcontract(dfip, '0.00001@2', address)
        self.nodes[0].generate(1)
        assert_equal(0.205, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test smallest min amount
        self.rollback(block)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/minswap':'0.00000001'}})
        self.nodes[0].generate(1)
        self.nodes[0].executesmartcontract(dfip, '0.00000001@2', address)
        self.nodes[0].generate(1)
        assert_equal(0.000205, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test no smallest min amount
        self.rollback(block)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/minswap':'0.00000001'}})
        self.nodes[0].generate(1)
        self.nodes[0].executesmartcontract(dfip, '0.00000001@2', address)
        self.nodes[0].generate(1)
        assert_equal(0.000205, float(self.nodes[0].getaccount(address)[0].split('@')[0]))

        # Test disabling DFIP201
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/active':'false'}})
        self.nodes[0].generate(1)
        assert_raises_rpc_error(-32600, 'DFIP2201 smart contract is not enabled', self.nodes[0].executesmartcontract, dfip, '1@2', address)

if __name__ == '__main__':
    SmartContractTest().main()


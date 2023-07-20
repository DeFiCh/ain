#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token fractional split"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    truncate,
)

from decimal import Decimal
import time
import random


class TokenFractionalSplitTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-vaultindex=1', '-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-eunosheight=1',
             '-fortcanningheight=1', '-fortcanningmuseumheight=1', '-fortcanninghillheight=1',
             '-fortcanningroadheight=1', f'-fortcanningcrunchheight=1', '-subsidytest=1']]

    def run_test(self):
        self.setup_test_tokens()
        self.token_split()

    def setup_test_tokens(self):
        self.nodes[0].generate(101)

        # Symbols
        self.symbolTSLA = 'TSLA'

        # Store address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": self.symbolTSLA},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": f"1@{self.symbolTSLA}"},
        ]
        self.nodes[0].setoracledata(oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(11)

        # Create tokens
        self.nodes[0].setloantoken({
            'symbol': self.symbolTSLA,
            'name': self.symbolTSLA,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD",
            'mintable': True,
            'interest': 0
        })
        self.nodes[0].generate(1)

        # Set collateral tokens
        self.nodes[0].setcollateraltoken({
            'token': self.symbolTSLA,
            'factor': 1,
            'fixedIntervalPriceId': f"{self.symbolTSLA}/USD"
        })
        self.nodes[0].generate(1)

        # Store token IDs
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Create funded addresses
        self.funded_addresses = []
        for _ in range(100):
            amount = round(random.uniform(1, 1000), 8)
            self.nodes[0].minttokens([f'{str(amount)}@{self.idTSLA}'])
            self.nodes[0].generate(1)
            address = self.nodes[0].getnewaddress()
            self.nodes[0].accounttoaccount(self.address, {address: f'{str(amount)}@{self.idTSLA}'})
            self.nodes[0].generate(1)
            self.funded_addresses.append([address, Decimal(str(amount))])

    def check_token_split(self, token_id, token_symbol, token_suffix, minted, loan, collateral):

        # Check old token
        result = self.nodes[0].gettoken(token_id)[token_id]
        assert_equal(result['symbol'], f'{token_symbol}{token_suffix}')
        assert_equal(result['minted'], Decimal('0.00000000'))
        assert_equal(result['mintable'], False)
        assert_equal(result['tradeable'], False)
        assert_equal(result['finalized'], True)
        assert_equal(result['isLoanToken'], False)
        assert_equal(result['destructionTx'], self.nodes[0].getbestblockhash())
        assert_equal(result['destructionHeight'], self.nodes[0].getblockcount())

        # Check old token in Gov vars
        result = self.nodes[0].listgovs("attrs")[0][0]['ATTRIBUTES']
        assert (f'v0/token/{token_id}/fixed_interval_price_id' not in result)
        if collateral:
            assert (f'v0/token/{token_id}/loan_collateral_enabled' not in result)
            assert (f'v0/token/{token_id}/loan_collateral_factor' not in result)
        if loan:
            assert (f'v0/token/{token_id}/loan_minting_enabled' not in result)
            assert (f'v0/token/{token_id}/loan_minting_interest' not in result)
        assert (f'v0/locks/token/{token_id}' not in result)

        # Save old ID and get new one
        token_idv1 = token_id
        token_id = list(self.nodes[0].gettoken(token_symbol).keys())[0]

        # Check new token in Gov vars
        assert_equal(result[f'v0/token/{token_id}/fixed_interval_price_id'], f'{token_symbol}/USD')
        if collateral:
            assert_equal(result[f'v0/token/{token_id}/loan_collateral_enabled'], 'true')
            assert_equal(result[f'v0/token/{token_id}/loan_collateral_factor'], '1')
        if loan:
            assert_equal(result[f'v0/token/{token_id}/loan_minting_enabled'], 'true')
            assert_equal(result[f'v0/token/{token_id}/loan_minting_interest'], '0')
        assert (f'v0/oracles/splits/{self.nodes[0].getblockcount()}' not in result)
        assert_equal(result[f'v0/token/{token_idv1}/descendant'], f'{token_id}/{self.nodes[0].getblockcount()}')
        assert_equal(result[f'v0/token/{token_id}/ascendant'], f'{token_idv1}/split')
        assert_equal(result[f'v0/locks/token/{token_id}'], 'true')

        # Check new token
        result = self.nodes[0].gettoken(token_id)[token_id]
        assert_equal(result['symbol'], f'{token_symbol}')
        assert_equal(str(result['minted']).split('.')[0], minted)
        assert_equal(result['mintable'], True)
        assert_equal(result['tradeable'], True)
        assert_equal(result['finalized'], False)
        assert_equal(result['isLoanToken'], True)
        assert_equal(result['creationTx'], self.nodes[0].getblock(self.nodes[0].getbestblockhash())['tx'][1])
        assert_equal(result['creationHeight'], self.nodes[0].getblockcount())
        assert_equal(result['destructionTx'], '0000000000000000000000000000000000000000000000000000000000000000')
        assert_equal(result['destructionHeight'], -1)

        # Make sure no old tokens remain in the account
        result = self.nodes[0].getaccount(self.address)
        for val in result:
            assert_equal(val.find(f'{token_symbol}{token_suffix}'), -1)

    def token_split(self):

        # Set expected minted amount
        minted = str(self.nodes[0].gettoken(self.idTSLA)[self.idTSLA]['minted'] / Decimal('2.5')).split('.')[0]

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES": {f'v0/locks/token/{self.idTSLA}': 'true'}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov(
            {"ATTRIBUTES": {f'v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}': f'{self.idTSLA}/-2.5'}})
        self.nodes[0].generate(2)

        # Check token split correctly
        self.check_token_split(self.idTSLA, self.symbolTSLA, '/v1', minted, True, True)

        # Swap old for new values
        self.idTSLA = list(self.nodes[0].gettoken(self.symbolTSLA).keys())[0]

        # Check new balances and history
        for [address, amount] in self.funded_addresses:
            account = self.nodes[0].getaccount(address)
            new_amount = account[0].split('@')[0]
            split_amount = truncate(str(amount / Decimal('2.5')), 8)
            assert_equal(new_amount, f'{Decimal(split_amount):.8f}')
            history = self.nodes[0].listaccounthistory(address, {'txtype': 'TokenSplit'})
            assert_equal(len(history), 2)
            assert_equal(history[0]['amounts'][0], f'{-amount:.8f}' + f'@{self.symbolTSLA}/v1')
            assert_equal(history[1]['amounts'][0], account[0])


if __name__ == '__main__':
    TokenFractionalSplitTest().main()

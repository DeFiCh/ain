#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test listaccounts RPC pagination"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

# Utils

def getTokenFromAmount(amount):
    assert(amount.find('@'))
    return amount.split('@')[1]

class AccountsPaginationTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-dakotaheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningspringheight=1', '-fortcanningcrunchheight=1', '-greatworldheight=1', '-jellyfish_regtest=1'],
                ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=1', '-bayfrontgardensheight=1', '-dakotaheight=1', '-fortcanningheight=1', '-fortcanninghillheight=1', '-fortcanningroadheight=1', '-fortcanningspringheight=1', '-fortcanningcrunchheight=1', '-greatworldheight=1', '-jellyfish_regtest=1']]

    def run_test(self):
        self.setup()
        self.test_token_account_filtering()
        self.test_pagination()

    def setup_accounts(self, nAccounts=60):
        # generate n random accounts
        self.accounts = []
        for _ in range(nAccounts):
            account = self.nodes[0].getnewaddress()
            self.nodes[0].utxostoaccount({account: '1@0'})
            self.nodes[0].generate(1)
            self.accounts.append(account)

        listAccounts = self.nodes[0].listaccounts()
        assert_equal(len(listAccounts), nAccounts)

    def create_tokens(self):
        self.symbolGOLD = "GOLD"
        self.symbolSILVER = "SILVER"
        self.symbolDOGE = "DOGE"

        self.account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.nodes[0].createtoken({
            "symbol": self.symbolGOLD,
            "name": "Gold token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.nodes[0].createtoken({
            "symbol": self.symbolSILVER,
            "name": "Silver token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.nodes[0].createtoken({
            "symbol": self.symbolDOGE,
            "name": "DOGE token",
            "collateralAddress": self.account0
        })
        self.nodes[0].generate(1)
        self.symbol_key_GOLD = "GOLD#" + str(self.get_id_token(self.symbolGOLD))
        self.symbol_key_SILVER = "SILVER#" + str(self.get_id_token(self.symbolSILVER))
        self.symbol_key_DOGE = "DOGE#" + str(self.get_id_token(self.symbolDOGE))

    def distribute_tokens(self, amount=1000):
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_GOLD)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_SILVER)
        self.nodes[0].minttokens(str(amount) + "@" + self.symbol_key_DOGE)
        self.nodes[0].generate(1)
        for i in range(20):
            self.nodes[0].accounttoaccount(self.account0, {self.accounts[i]: "1@" + self.symbol_key_GOLD})
            self.nodes[0].generate(1)

        for i in range(20, 30):
            self.nodes[0].accounttoaccount(self.account0, {self.accounts[i]: "1@" + self.symbol_key_SILVER})
            self.nodes[0].generate(1)

        for i in range(30, 50):
            self.nodes[0].accounttoaccount(self.account0, {self.accounts[i]: "1@" + self.symbol_key_DOGE})
            self.nodes[0].generate(1)

    # Check listaccounts with token filter returns only accounts that have specified token
    def test_token_account_filtering(self):
        # Must only contain accounts with GOLD
        pagination = {'token': self.symbol_key_GOLD}
        listAccounts_gold = self.nodes[0].listaccounts(pagination)
        assert(self.accountsHaveToken(listAccounts_gold, self.symbol_key_GOLD))
        assert(not self.accountsHaveToken(listAccounts_gold, self.symbol_key_SILVER))
        assert(not self.accountsHaveToken(listAccounts_gold, self.symbol_key_DOGE))

        # Must only contain accounts with SILVER
        pagination = {'token': self.symbol_key_SILVER}
        listAccounts_silver = self.nodes[0].listaccounts(pagination)
        assert(not self.accountsHaveToken(listAccounts_silver, self.symbol_key_GOLD))
        assert(self.accountsHaveToken(listAccounts_silver, self.symbol_key_SILVER))
        assert(not self.accountsHaveToken(listAccounts_silver, self.symbol_key_DOGE))

        # Must only contain accounts with DOGE
        pagination = {'token': self.symbol_key_DOGE}
        listAccounts_doge = self.nodes[0].listaccounts(pagination)
        assert(not self.accountsHaveToken(listAccounts_doge, self.symbol_key_GOLD))
        assert(not self.accountsHaveToken(listAccounts_doge, self.symbol_key_SILVER))
        assert(self.accountsHaveToken(listAccounts_doge, self.symbol_key_DOGE))

    # Test pagination works with start param
    def test_pagination(self):
        # Get all accounts
        accounts = self.nodes[0].listaccounts({'limit': 0})
        n_all_accounts = len(accounts)
        assert_equal(n_all_accounts, 113)

        accounts = self.nodes[0].listaccounts({'limit': 3})
        assert_equal(len(accounts), 3)

        pagination = {
                "limit": 10,
                'start': accounts[1]["key"],
                "including_start": True
        }
        accounts = self.nodes[0].listaccounts(pagination)
        assert_equal(len(accounts), 10)

        pagination = {
                "limit": 0,
                'start': accounts[1]["key"],
                "including_start": True
        }
        accounts = self.nodes[0].listaccounts(pagination)
        # This will fail need to fix pagination with limit = 0
        assert_equal(len(accounts), n_all_accounts-1)

    def setup(self):
        self.nodes[0].generate(100)
        self.sync_blocks()

        self.setup_accounts()
        self.create_tokens()
        self.distribute_tokens()

    # Given a list of accounts verify all contain a certain token in their balance
    def accountsHaveToken(self, account_list, token):
        for account in account_list:
            accountId = account["owner"]["addresses"][0]
            accountBalances = self.nodes[0].getaccount(accountId)
            found = False
            for balance in accountBalances:
                if getTokenFromAmount(balance) == token:
                    found = True
                    break
            if not found:
                return False
        return True

if __name__ == '__main__':
    AccountsPaginationTest().main ()



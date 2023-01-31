#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test listaccounts RPC pagination"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal

class AccountsValidatingTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101'],
            ['-txnotokens=0', '-amkheight=50', '-eunosheight=101'],
        ]
    def run_test(self):
        node = self.nodes[0]
        node1 = self.nodes[1]
        node.generate(101)
        self.sync_blocks()
        assert_equal(node.getblockcount(), 101) # eunos
        # Get addresses and set up account
        account = node.getnewaddress()
        node.utxostoaccount({account: "10@0"})
        node.generate(1)
        self.sync_blocks()
        addressInfo = node.getaddressinfo(account)
        accountkey1 = addressInfo["scriptPubKey"] + "@0" #key of the first account
        accounts = node1.listaccounts()
        #make sure one account in the system
        assert_equal(len(accounts), 1)
        assert_equal(accounts[0]["key"], accountkey1)
        pagination = {
                "start": accounts[len(accounts) - 1]["key"],
                "including_start": True
            }
        result2 = node1.listaccounts(pagination)
        #check the result has accountkey1
        assert_equal(len(result2), 1)
        assert_equal(result2[0]["key"], accountkey1)
        ###########################################################################################
        #test with two accounts
        #add another account
        account2 = node.getnewaddress()
        node.utxostoaccount({account2: "10@0"})
        node.generate(1)
        self.sync_blocks()
        addressInfo = node.getaddressinfo(account2)
        accountkey2 = addressInfo["scriptPubKey"] + "@0" #key of the second account
        accounts = node1.listaccounts()
        #make sure we have two account in the system
        assert_equal(len(accounts), 2)
        # results are in lexograpic order. perform the checks accordingly
        if accountkey1 < accountkey2 :
            assert_equal(accounts[0]["key"], accountkey1)
            assert_equal(accounts[1]["key"], accountkey2)
        else :
            assert_equal(accounts[0]["key"], accountkey2)
            assert_equal(accounts[1]["key"], accountkey1)
        pagination = {
                "start": accounts[len(accounts) - 1]["key"],#giving the last element of accounts[] as start
                "including_start": True
            }
        result2 = node1.listaccounts(pagination)
        #check for length
        assert_equal(len(result2), 1)
        # results are in lexograpic order. perform the checks accordingly
        if accountkey1 < accountkey2 :
            assert_equal(result2[0]["key"], accountkey2)
        else :
            assert_equal(result2[0]["key"], accountkey1)
        ###########################################################################################
        #Add another account from other node
        account3 = node1.getnewaddress()
        node.sendtoaddress(account3, 50)
        node.generate(1)
        self.sync_blocks()
        node1.utxostoaccount({account3: "10@0"})
        node1.generate(1)
        self.sync_blocks()
        addressInfo = node1.getaddressinfo(account3)
        accounts = node.listaccounts()
        #make sure we have three account in the system
        assert_equal(len(accounts), 3)
        pagination = {
                "start": accounts[0]["key"], #pass the first key in the accounts list
                "including_start": False
            }
        result2 = node1.listaccounts(pagination)
        #check for length, we should get 2 entries since listaccounts RPC should return all accounts even with pagination.
        assert_equal(len(result2), 2)

if __name__ == '__main__':
    AccountsValidatingTest().main ()

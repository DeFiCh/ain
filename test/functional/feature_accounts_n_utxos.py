#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic accounts operation
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

from decimal import Decimal

class AccountsAndUTXOsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # node0: main
        # node1: revert of destroy
        # node2: revert create (all)
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.setup_tokens()

        # Stop node #2 for future revert
        self.stop_node(2)

        idGold = list(self.nodes[0].gettoken("GOLD").keys())[0]
        idSilver = list(self.nodes[0].gettoken("SILVER").keys())[0]
        accountGold = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountSilver = self.nodes[1].get_genesis_keys().ownerAuthAddress
        initialGold = self.nodes[0].getaccount(accountGold, {}, True)[idGold]
        initialSilver = self.nodes[1].getaccount(accountSilver, {}, True)[idSilver]
        print("Initial GOLD:", initialGold, ", id", idGold)
        print("Initial SILVER:", initialSilver, ", id", idSilver)

        toGold = self.nodes[1].getnewaddress("", "legacy")

        # accounttoaccount
        #========================
        # missing from (account)
        try:
            self.nodes[0].accounttoaccount([], self.nodes[0].getnewaddress("", "legacy"), {toGold: "100@GOLD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO" in errorString)

        # missing from (account exist, but no tokens)
        try:
            self.nodes[0].accounttoaccount([], accountGold, {toGold: "100@SILVER"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not enough balance" in errorString)

        # missing amount
        try:
            self.nodes[0].accounttoaccount([], accountGold, {toGold: ""})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        #invalid UTXOs
        try:
            self.nodes[0].accounttoaccount([{"": 0}], accountGold, {toGold: "100@GOLD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("JSON value is not a string as expected" in errorString)

        # missing (account exists, but does not belong)
        try:
            self.nodes[0].accounttoaccount([], accountSilver, {accountGold: "100@SILVER"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO" in errorString)

        # transfer
        self.nodes[0].accounttoaccount([], accountGold, {toGold: "100@GOLD"})
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 100)
        assert_equal(self.nodes[0].getaccount(toGold, {}, True)[idGold], 100)

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], self.nodes[1].getaccount(accountGold, {}, True)[idGold])
        assert_equal(self.nodes[0].getaccount(toGold, {}, True)[idGold], self.nodes[1].getaccount(toGold, {}, True)[idGold])

        # transfer between nodes
        self.nodes[1].accounttoaccount([], accountSilver, {accountGold: "100@SILVER"})
        self.nodes[1].generate(1)

        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idSilver], initialSilver - 100)
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idSilver], 100)

        assert_equal(self.nodes[0].getaccount(accountSilver, {}, True)[idSilver], self.nodes[1].getaccount(accountSilver, {}, True)[idSilver])
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idSilver], self.nodes[1].getaccount(accountGold, {}, True)[idSilver])

        # utxostoaccount
        #========================
        try:
            self.nodes[0].utxostoaccount([], {toGold: "100@GOLD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Insufficient funds" in errorString)

        # missing amount
        try:
            self.nodes[0].utxostoaccount([], {toGold: ""})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        #invalid UTXOs
        try:
            self.nodes[0].utxostoaccount([{"": 0}], {accountGold: "100@DFI"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        # transfer
        initialBalance = self.nodes[0].getbalances()['mine']['trusted']
        self.nodes[0].utxostoaccount([], {accountGold: "100@DFI"})
        # # balance should not change before generation
        # assert_equal(initialBalance, self.nodes[0].getbalances()['mine']['trusted'])
        self.nodes[0].generate(1)
        assert(initialBalance != self.nodes[0].getbalances()['mine']['trusted'])

        # accounttoutxos
        #========================
        # missing from (account)
        try:
            self.nodes[0].accounttoutxos([], self.nodes[0].getnewaddress("", "legacy"), {toGold: "100@GOLD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO" in errorString)

        # missing amount
        try:
            self.nodes[0].accounttoutxos([], accountGold, {accountGold: ""})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        #invalid UTXOs
        try:
            self.nodes[0].accounttoutxos([{"": 0}], accountGold, {accountGold: "100@GOLD"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("JSON value is not a string as expected" in errorString)

        # missing (account exists, but does not belong)
        try:
            self.nodes[0].accounttoutxos([], accountSilver, {accountGold: "100@SILVER"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO" in errorString)

        # transfer
        self.nodes[0].accounttoutxos([], accountGold, {accountGold: "100@GOLD"})
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 200)
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], self.nodes[1].getaccount(accountGold, {}, True)[idGold])

        # REVERTING:
        #========================
        print ("Reverting...")
        self.start_node(2)
        self.nodes[2].generate(20)
        # # Check that collateral spending tx is still in the mempool
        # assert_equal(sendedTxHash, self.nodes[0].getrawmempool()[0])

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        assert_equal(len(self.nodes[0].listorders()), 0)
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold)
        assert_equal(self.nodes[0].getaccount(accountSilver, {}, True)[idSilver], initialSilver)

        # print ("Mempool: ", self.nodes[0].getrawmempool())
        assert_equal(len(self.nodes[0].getrawmempool()), 5) # 5 txs


if __name__ == '__main__':
    AccountsAndUTXOsTest ().main ()

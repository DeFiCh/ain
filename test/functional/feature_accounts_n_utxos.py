#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic accounts operation
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises_rpc_error, \
    connect_nodes_bi

class AccountsAndUTXOsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # node0: main
        # node1: secondary tester
        # node2: revert create (all)
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50'], ['-txnotokens=0', '-amkheight=50'], ['-txnotokens=0', '-amkheight=50']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.setup_tokens()

        # Stop node #2 for future revert
        self.stop_node(2)

        symbolGOLD = "GOLD#" + self.get_id_token("GOLD")
        symbolSILVER = "SILVER#" + self.get_id_token("SILVER")

        idGold = list(self.nodes[0].gettoken(symbolGOLD).keys())[0]
        idSilver = list(self.nodes[0].gettoken(symbolSILVER).keys())[0]
        accountGold = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountSilver = self.nodes[1].get_genesis_keys().ownerAuthAddress
        initialGold = self.nodes[0].getaccount(accountGold, {}, True)[idGold]
        initialSilver = self.nodes[1].getaccount(accountSilver, {}, True)[idSilver]
        print("Initial GOLD:", initialGold, ", id", idGold)
        print("Initial SILVER:", initialSilver, ", id", idSilver)

        toGold = self.nodes[1].getnewaddress("", "legacy")
        toSilver = self.nodes[0].getnewaddress("", "legacy")

        # accounttoaccount
        #========================
        # missing from (account)
        try:
            self.nodes[0].accounttoaccount(self.nodes[0].getnewaddress("", "legacy"),
                                           {toGold: "100@" + symbolGOLD}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not enough balance" in errorString)

        # missing from (account exist, but no tokens)
        try:
            self.nodes[0].accounttoaccount(accountGold,
                                           {toGold: "100@" + symbolSILVER}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not enough balance" in errorString)

        # missing amount
        try:
            self.nodes[0].accounttoaccount(accountGold,
                                           {toGold: ""}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        #invalid UTXOs
        try:
            self.nodes[0].accounttoaccount(accountGold,
                                           {toGold: "100@" + symbolGOLD}, [{"": 0}])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("JSON value is not a string as expected" in errorString)

        # missing (account exists, but does not belong) -
        try:
            self.nodes[0].accounttoaccount(accountSilver, {accountGold: "100@" + symbolSILVER}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect auth" in errorString)

        # transfer
        self.nodes[0].accounttoaccount(accountGold,
                                       {toGold: "100@" + symbolGOLD}, [])
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:1])

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 100)
        assert_equal(self.nodes[0].getaccount(toGold, {}, True)[idGold], 100)

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], self.nodes[1].getaccount(accountGold, {}, True)[idGold])
        assert_equal(self.nodes[0].getaccount(toGold, {}, True)[idGold], self.nodes[1].getaccount(toGold, {}, True)[idGold])

        # transfer between nodes
        self.nodes[1].accounttoaccount(accountSilver,
                                       {toSilver: "100@" + symbolSILVER}, [])
        self.nodes[1].generate(1)
        self.sync_blocks(self.nodes[0:1])

        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idSilver], initialSilver - 100)
        assert_equal(self.nodes[0].getaccount(toSilver, {}, True)[idSilver], 100)

        assert_equal(self.nodes[0].getaccount(accountSilver, {}, True)[idSilver], self.nodes[1].getaccount(accountSilver, {}, True)[idSilver])
        assert_equal(self.nodes[0].getaccount(toSilver, {}, True)[idSilver], self.nodes[1].getaccount(toSilver, {}, True)[idSilver])

        # utxostoaccount
        # Only technically supported for DFI as accounttoutxxos blocks non-DFI transfers
        #========================
        assert_raises_rpc_error(-4, "Unexpected token type set for recipient. Only native DFI token type expected.", self.nodes[0].utxostoaccount, {toGold: "100@" + symbolGOLD})

        # missing amount
        try:
            self.nodes[0].utxostoaccount({toGold: ""}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        #invalid UTXOs
        try:
            self.nodes[0].utxostoaccount({accountGold: "100@DFI"}, [{"": 0}])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        # transfer
        initialBalance = self.nodes[0].getbalances()['mine']['trusted']
        self.nodes[0].utxostoaccount({accountGold: "100@DFI"}, [])
        self.nodes[0].generate(1)
        assert(initialBalance != self.nodes[0].getbalances()['mine']['trusted'])

        # accounttoutxos
        #========================
        # missing from (account)
        try:
            self.nodes[0].accounttoutxos(self.nodes[0].getnewaddress("", "legacy"), {toGold: "100@DFI"}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not enough balance" in errorString)

        # missing amount
        try:
            self.nodes[0].accounttoutxos(accountGold, {accountGold: ""}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid amount" in errorString)

        #invalid UTXOs
        try:
            self.nodes[0].accounttoutxos(accountGold, {accountGold: "100@" + symbolGOLD}, [{"": 0}])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("JSON value is not a string as expected" in errorString)

        # missing (account exists, but does not belong)
        try:
            self.nodes[0].accounttoutxos(accountSilver, {accountGold: "100@" + symbolSILVER}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect auth" in errorString)

        # missing (account exists, there are tokens, but not token 0)
        try:
            self.nodes[0].accounttoutxos(toSilver, {accountGold: "100@DFI"}, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not enough balance" in errorString)

        # transfer
        try:
            self.nodes[0].accounttoutxos(accountGold, {accountGold: "100@" + symbolGOLD}, [])
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("only available for DFI transactions" in errorString)

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 100)
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], self.nodes[1].getaccount(accountGold, {}, True)[idGold])

        # gettokenbalances
        #========================
        # check balances about all accounts belonging to the wallet
        assert_equal(self.nodes[0].gettokenbalances({}, True)[idGold], initialGold - 100)
        assert_equal(self.nodes[0].gettokenbalances({}, True)[idSilver], 100)

        # chech work is_mine_only field
        list_all_acc = len(self.nodes[0].listaccounts({}, False, True, False))
        list_mine_acc = len(self.nodes[0].listaccounts({}, False, True, True))
        assert(list_all_acc != list_mine_acc)

        # REVERTING:
        #========================
        print ("Reverting...")
        self.start_node(2)
        self.nodes[2].generate(20)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold)
        assert_equal(self.nodes[0].getaccount(accountSilver, {}, True)[idSilver], initialSilver)

        assert_equal(len(self.nodes[0].getrawmempool()), 4) # 4 txs


if __name__ == '__main__':
    AccountsAndUTXOsTest ().main ()

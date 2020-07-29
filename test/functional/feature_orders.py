#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

from decimal import Decimal

class OrdersTest (DefiTestFramework):
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


        print("Check missed auth...")
        try:
            self.nodes[0].createorder([], {"give": "3@GOLD", "take": "500@SILVER", "premium":"1@GOLD", "owner": self.nodes[0].getnewaddress("", "legacy"), "timeinforce": 2})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Can't find any UTXO" in errorString)


        print("Check expired orders dismission, check accounts balances...")
        order0 = self.nodes[0].createorder([], {"give": "3@GOLD", "take": "500@SILVER", "premium":"1@GOLD", "owner": accountGold, "timeinforce": 2})
        order1 = self.nodes[1].createorder([], {"give": "20@SILVER", "take": "1@GOLD", "premium":"2@SILVER", "owner": accountSilver, "timeinforce": 3})
        self.sync_mempools(self.nodes[0:2])
        self.nodes[0].generate(1)
        # print("Orders:", self.nodes[0].listorders())
        assert_equal(len(self.nodes[0].listorders()), 2) # both orders active
        # check funds locked on orders:
        self.sync_blocks(self.nodes[0:2])
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 3 - 1)
        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idSilver], initialSilver - 20 - 2)

        self.nodes[0].generate(2)
        assert_equal(len(self.nodes[0].listorders()), 1) # first order expired
        self.nodes[0].generate(1)
        assert_equal(len(self.nodes[0].listorders()), 0) # second order expired

        # check that funds returned:
        self.sync_blocks(self.nodes[0:2])
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold)
        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idSilver], initialSilver)

#        print("Accounts (initial): ", self.nodes[0].listaccounts({}, False, True))
        print("Check simple order matching...")
        order0 = self.nodes[0].createorder([], {"give": "2@GOLD", "take": "10@SILVER", "premium":"0.1@GOLD", "owner": accountGold})
        order1 = self.nodes[1].createorder([], {"give": "20@SILVER", "take": "1@GOLD", "premium":"1@SILVER", "owner": accountSilver})
        self.sync_mempools(self.nodes[0:2])
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 2 - Decimal('0.1'))
        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idSilver], initialSilver - 20 - 1)

        # print("Accounts (orders created): ", self.nodes[0].listaccounts({}, False, True))
        # print("Orders:", self.nodes[0].listorders())

        matcherAddress = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].matchorders([], order0, order1, matcherAddress)
        self.nodes[0].generate(1)

        # print("Accounts (orders matched): ", self.nodes[0].listaccounts({}, False, True))
        self.sync_blocks(self.nodes[0:2])
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold - 2 - Decimal('0.1'))
        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idSilver], 10)
        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idSilver], initialSilver - 20 - 1)
        assert_equal(self.nodes[1].getaccount(accountSilver, {}, True)[idGold], 1)
        assert_equal(self.nodes[1].getaccount(matcherAddress, {}, True)[idGold], 1 + Decimal('0.1')) # surplus + premium
        assert_equal(self.nodes[1].getaccount(matcherAddress, {}, True)[idSilver], 10 + 1) # surplus + premium


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
        assert_equal(self.nodes[0].getaccount(matcherAddress, {}, True), {})

        # print ("Mempool: ", self.nodes[0].getrawmempool())
        assert_equal(len(self.nodes[0].getrawmempool()), 5) # 5 txs: 2 for expired orders, 2 for good orders, 1 matcher


if __name__ == '__main__':
    OrdersTest ().main ()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify send tokens operation with autoselection accounts balances
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

from decimal import Decimal

import random

class AnyAccountsToAccountsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        # We need to enlarge -datacarriersize for allowing for test big OP_RETURN scripts
        # resulting from building AnyAccountsToAccounts msg with many accounts balances
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontgardensheight=50', '-datacarriersize=1000']
        ]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        tokens = [
            {
                "wallet": self.nodes[0],
                "symbol": "GOLD",
                "name": "shiny gold",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 50
            },
            {
                "wallet": self.nodes[0],
                "symbol": "SILVER",
                "name": "just silver",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 50
            },
            {
                "wallet": self.nodes[0],
                "symbol": "COPPER",
                "name": "rusty copper",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 50
            },
            {
                "wallet": self.nodes[0],
                "symbol": "PLATINUM",
                "name": "star platinum",
                "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress,
                "amount": 50
            }
        ]
        # inside this function "tokenId" and "symbolId" will be assigned for each token obj
        self.setup_tokens(tokens)

        # list_tokens = self.nodes[0].listtokens()
        # pprint(list_tokens)

        # Split minters utxos
        for i in range(100):
            repeat = True
            while repeat:
                try:
                    self.nodes[0].sendtoaddress(self.nodes[0].get_genesis_keys().ownerAuthAddress, 1)
                    repeat = False
                except JSONRPCException as e:
                    if (e.error['message'] == "Insufficient funds"):
                        self.nodes[0].generate(1)
                    else:
                        print(e.error['message'])
                        break

        self.nodes[0].generate(1)
        self.sync_blocks()

        start_block_count = self.nodes[0].getblockcount()

        # Stop node #3 for future revert
        self.stop_node(3)
        enabled_nodes = [self.nodes[0], self.nodes[1], self.nodes[2]]

        # create node1 wallet from 12 addresses
        addr_types = ["legacy", "p2sh-segwit", "bech32"]
        node1_wallet = []

        minterAccountBalances = self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {}, True)

        wallet1_accs_count = 12

        for i in range(wallet1_accs_count):
            node1_wallet.append({"address": self.nodes[1].getnewaddress("", addr_types[i % len(addr_types)])})
            # send to new address 4 utxos
            for k in range(4):
                self.nodes[0].sendtoaddress(node1_wallet[i]["address"], 1)

            self.nodes[0].generate(1)

            for token in tokens:
                if i == wallet1_accs_count - 1: # last account
                    amount = minterAccountBalances[token["tokenId"]]
                else:
                    amount = random.randint(0, minterAccountBalances[token["tokenId"]] // 2)

                if amount == 0:
                    continue

                repeat = True
                while repeat:
                    try:
                        token["wallet"].accounttoaccount(
                            token["collateralAddress"], # from
                            {node1_wallet[i]["address"]: str(amount) + "@" + token["symbolId"]}) # to
                        repeat = False
                    except JSONRPCException as e:
                        if ("Can't find any UTXO's for owner." in e.error["message"]):
                            self.nodes[0].generate(1)
                        else:
                            repeat = False
                            raise e
                node1_wallet[i][token["tokenId"]] = Decimal(amount)
                minterAccountBalances[token["tokenId"]] = minterAccountBalances[token["tokenId"]] - amount

            self.nodes[0].generate(1)

        self.sync_blocks(enabled_nodes)

        # check that RPC getaccounts is equal of
        node1_wallet_rpc = []
        for node1_acc in node1_wallet:
            node1_wallet_rpc.append(self.nodes[0].getaccount(node1_acc["address"], {}, True))

        assert_equal(len(node1_wallet), len(node1_wallet_rpc))

        for i in range(len(node1_wallet)):
            _node1_acc = {**node1_wallet[i]}
            _node1_acc.pop("address")
            assert_equal(_node1_acc, node1_wallet_rpc[i])

        wallet2_addr1 = self.nodes[2].getnewaddress("", "legacy")
        wallet2_addr2 = self.nodes[2].getnewaddress("", "legacy")

        # sendtokenstoaddress

        # not enough balances for transfer
        to = {}

        to[wallet2_addr1] = ["20@" + tokens[0]["symbolId"], "20@" + tokens[1]["symbolId"]]
        to[wallet2_addr2] = ["51@" + tokens[3]["symbolId"]] # we have only 50

        try:
            self.nodes[1].sendtokenstoaddress({}, to, "forward")
        except JSONRPCException as e:
            errorString = e.error['message']

        assert("Not enough balance on wallet accounts" in errorString)

        # normal transfer from wallet1
        to = {}
        to[wallet2_addr1] = ["20@" + tokens[0]["symbolId"], "20@" + tokens[1]["symbolId"]]
        to[wallet2_addr2] = ["20@" + tokens[2]["symbolId"], "20@" + tokens[3]["symbolId"]]

        self.nodes[1].sendtokenstoaddress({}, to)

        self.sync_mempools(enabled_nodes)
        self.nodes[0].generate(1)
        self.sync_blocks(enabled_nodes)

        wallet2_addr1_balance = self.nodes[0].getaccount(wallet2_addr1, {}, True)
        wallet2_addr2_balance = self.nodes[0].getaccount(wallet2_addr2, {}, True)

        assert_equal(wallet2_addr1_balance[tokens[0]["tokenId"]], Decimal(20))
        assert_equal(wallet2_addr1_balance[tokens[1]["tokenId"]], Decimal(20))
        assert_equal(wallet2_addr2_balance[tokens[2]["tokenId"]], Decimal(20))
        assert_equal(wallet2_addr2_balance[tokens[3]["tokenId"]], Decimal(20))

        # send all remaining tokens to wallet1 change address
        wallet1_change_addr = self.nodes[1].getnewaddress("", "legacy")
        to = {}
        to[wallet1_change_addr] = ["30@" + tokens[0]["symbolId"], "30@" + tokens[1]["symbolId"], "30@" + tokens[2]["symbolId"], "30@" + tokens[3]["symbolId"]]

        self.nodes[1].sendtokenstoaddress({}, to)

        self.sync_mempools(enabled_nodes)
        self.nodes[0].generate(1)
        self.sync_blocks(enabled_nodes)

        wallet1_change_addr_balance = self.nodes[0].getaccount(wallet1_change_addr, {}, True)

        assert_equal(wallet1_change_addr_balance[tokens[0]["tokenId"]], Decimal(30))
        assert_equal(wallet1_change_addr_balance[tokens[1]["tokenId"]], Decimal(30))
        assert_equal(wallet1_change_addr_balance[tokens[2]["tokenId"]], Decimal(30))
        assert_equal(wallet1_change_addr_balance[tokens[3]["tokenId"]], Decimal(30))

        # send utxos to wallet2
        for k in range(4):
            self.nodes[0].sendtoaddress(wallet2_addr1, 1)
            self.nodes[0].sendtoaddress(wallet2_addr2, 1)

        self.nodes[0].generate(1)
        self.sync_blocks(enabled_nodes)

        # send tokens from wallet2 to wallet1 with manual "from" param
        accsFrom = {}
        accsFrom[wallet2_addr1] = ["20@" + tokens[0]["symbolId"], "20@" + tokens[1]["symbolId"]]
        accsFrom[wallet2_addr2] = ["20@" + tokens[2]["symbolId"], "20@" + tokens[3]["symbolId"]]
        to = {}
        to[wallet1_change_addr] = ["20@" + tokens[0]["symbolId"], "20@" + tokens[1]["symbolId"], "20@" + tokens[2]["symbolId"], "20@" + tokens[3]["symbolId"]]

        self.nodes[2].sendtokenstoaddress(accsFrom, to)

        self.sync_mempools(enabled_nodes)
        self.nodes[0].generate(1)
        self.sync_blocks(enabled_nodes)

        # check that wallet2 is empty
        wallet2_addr1_balance = self.nodes[0].getaccount(wallet2_addr1, {}, True)
        wallet2_addr2_balance = self.nodes[0].getaccount(wallet2_addr2, {}, True)
        assert(not wallet2_addr1_balance)
        assert(not wallet2_addr2_balance)

        # check that wallet1_change_addr has all tokens amount
        wallet1_change_addr_balance = self.nodes[0].getaccount(wallet1_change_addr, {}, True)
        assert_equal(wallet1_change_addr_balance[tokens[0]["tokenId"]], Decimal(50))
        assert_equal(wallet1_change_addr_balance[tokens[1]["tokenId"]], Decimal(50))
        assert_equal(wallet1_change_addr_balance[tokens[2]["tokenId"]], Decimal(50))
        assert_equal(wallet1_change_addr_balance[tokens[3]["tokenId"]], Decimal(50))

        # reverting
        print ("Reverting...")
        blocks = self.nodes[0].getblockcount() - start_block_count + 1
        self.start_node(3)
        self.nodes[3].generate(blocks)

        connect_nodes_bi(self.nodes, 2, 3)
        self.sync_blocks()

        # check that wallet1 and wallet 2 is empty
        for node1_acc in node1_wallet:
            node1_acc_ballance = self.nodes[1].getaccount(node1_acc["address"], {}, True)
            assert(not node1_acc_ballance)

        wallet1_change_addr_balance = self.nodes[1].getaccount(wallet1_change_addr, {}, True)
        wallet2_addr1_balance = self.nodes[2].getaccount(wallet2_addr1, {}, True)
        wallet2_addr2_balance = self.nodes[2].getaccount(wallet2_addr2, {}, True)
        assert(not wallet1_change_addr_balance)
        assert(not wallet2_addr1_balance)
        assert(not wallet2_addr2_balance)

        minterAccountBalances = self.nodes[0].getaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {}, True)

        assert_equal(minterAccountBalances[tokens[0]["tokenId"]], Decimal(50))
        assert_equal(minterAccountBalances[tokens[1]["tokenId"]], Decimal(50))
        assert_equal(minterAccountBalances[tokens[2]["tokenId"]], Decimal(50))
        assert_equal(minterAccountBalances[tokens[3]["tokenId"]], Decimal(50))

if __name__ == '__main__':
    AnyAccountsToAccountsTest().main()

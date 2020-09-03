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

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0']]


    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        #self.nodes[0].generate(100)
        #self.sync_all()
        print("Generating initial chain...")
        self.setup_tokens()
        # Stop node #3 for future revert
        self.stop_node(3)

        # CREATION:
        #========================
        # 1 Getting new addresses and checking coins
        idGold = list(self.nodes[0].gettoken("GOLD").keys())[0]
        idSilver = list(self.nodes[0].gettoken("SILVER").keys())[0]
        accountGN0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountSN1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        initialGold = self.nodes[0].getaccount(accountGN0, {}, True)[idGold]
        initialSilver = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]
        print("Initial GOLD AccN0:", initialGold, ", id", idGold)
        print("Initial SILVER AccN1:", initialSilver, ", id", idSilver)
        
        owner = self.nodes[0].getnewaddress("", "legacy")
        
        # 2 Transferring SILVER from N1 Account to N0 Account
        self.nodes[1].accounttoaccount([], accountSN1, {accountGN0: "900@SILVER"})
        self.nodes[1].generate(1)
        
        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)
        silverCheckN1 = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]
        print("Checking Silver on AccN1:", silverCheckN1, ", id", idSilver)
        
        # 3 Creating poolpair 
        self.nodes[0].createpoolpair({
            "tokenA": "GOLD",
            "tokenB": "SILVER",
            "commission": 0.1,
            "status": True,
            "ownerFeeAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].generate(1)

        # only 4 tokens = DFI, GOLD, SILVER, GS
        assert_equal(len(self.nodes[0].listtokens()), 4)

        # check tokens id
        pool = self.nodes[0].getpoolpair("GS")
        idGS = list(self.nodes[0].gettoken("GS").keys())[0]
        assert(pool[idGS]['idTokenA'] == idGold)
        assert(pool[idGS]['idTokenB'] == idSilver)
        
        print("PsStart")
        input("debug")
        # 4 Trying to poolswap
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": "SILVER",
            "amountFrom": 2,
            "to": accountSN1,
            "tokenTo": "GOLD",
        }, [])
        print("PsEnd")


        self.sync_blocks([self.nodes[0], self.nodes[2]])

        # REVERTING:
        #========================
        print ("Reverting...")
        # Reverting creation!
        self.start_node(3)
        self.nodes[3].generate(30)

        connect_nodes_bi(self.nodes, 0, 3)
        self.sync_blocks()
        assert_equal(len(self.nodes[0].listpoolpairs()), 0)

if __name__ == '__main__':
    PoolPairTest ().main ()

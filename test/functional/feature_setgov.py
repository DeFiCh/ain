#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- governance variables test
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import \
    connect_nodes, disconnect_nodes
from decimal import Decimal


class GovsetTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bishanheight=50'],
            ['-txnotokens=0', '-amkheight=50', '-bishanheight=50']]


    def run_test(self):
        # fast check for debug
        print (self.nodes[0].getgov("LP_SPLITS"))
        print (self.nodes[0].getgov("LP_DAILY_DFI_REWARD"))
        # return

        print("Generating initial chain...")
        self.setup_tokens()

        # Stop node #1 for future revert
        self.stop_node(1)

        # set|get not existent variable:
        try:
            self.nodes[0].setgov({"REWARD": "any"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not registered" in errorString)
        try:
            self.nodes[0].getgov("REWARD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not registered" in errorString)

        #
        # prepare the pools for LP_SPLITS
        #
        symbolGOLD = "GOLD#" + self.get_id_token("GOLD")
        symbolSILVER = "SILVER#" + self.get_id_token("SILVER")

        owner = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].createtoken({
            "symbol": "BRONZE",
            "name": "just bronze",
            "collateralAddress": owner # doesn't matter
        })
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolGOLD,
            "tokenB": symbolSILVER,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": symbolGOLD,
            "tokenB": "BRONZE#130",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "GB",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": symbolSILVER,
            "tokenB": "BRONZE#130",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "SB",
        }, [])
        self.nodes[0].generate(1)
        assert(len(self.nodes[0].listpoolpairs()) == 3)

        # set LP_SPLITS with absent pools id
        try:
            self.nodes[0].setgov({
                "LP_SPLITS": { "0": 0.5, "1": 0.4, "2": 0.2 }
                })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id=0 not found" in errorString)

        # set LP_SPLITS with total >100%
        try:
            self.nodes[0].setgov({
            "LP_SPLITS": { "1": 0.5, "2": 0.4, "3": 0.2 }
                })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("total" in errorString)


        self.nodes[0].setgov({
            "LP_SPLITS": { "1": 0.5, "2": 0.4, "3": 0.1 }
        })

        self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35.5})
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        # print(g1)
        assert (g1 == {'LP_SPLITS': {'1': Decimal('0.50000000'), '2': Decimal('0.40000000'), '3': Decimal('0.10000000')}} )

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        # print(g2)
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool1 = self.nodes[0].getpoolpair("1", True)['1']
        pool2 = self.nodes[0].getpoolpair("2", True)['2']
        pool3 = self.nodes[0].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == Decimal('0.50000000')
            and pool2['rewardPct'] == Decimal('0.40000000')
            and pool3['rewardPct'] == Decimal('0.10000000'))

        # start node 1 and sync for reverting to this chain point
        self.start_node(1)
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()

        # check sync between nodes 0 and 1
        g1 = self.nodes[1].getgov("LP_SPLITS")
        # print(g1)
        assert (g1 == {'LP_SPLITS': {'1': Decimal('0.50000000'), '2': Decimal('0.40000000'), '3': Decimal('0.10000000')}} )

        g2 = self.nodes[1].getgov("LP_DAILY_DFI_REWARD")
        # print(g2)
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool1 = self.nodes[1].getpoolpair("1", True)['1']
        pool2 = self.nodes[1].getpoolpair("2", True)['2']
        pool3 = self.nodes[1].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == Decimal('0.50000000')
            and pool2['rewardPct'] == Decimal('0.40000000')
            and pool3['rewardPct'] == Decimal('0.10000000'))

        # disconnect node #1
        disconnect_nodes(self.nodes[0], 1)

        # test set multuple:
        self.nodes[0].setgov({
            "LP_SPLITS": { "1": 1 },
            "LP_DAILY_DFI_REWARD": 45
        })
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        assert (g1 == {'LP_SPLITS': {'1': 1}} )

        # test that all previous pool's values was reset
        pool1 = self.nodes[0].getpoolpair("1", True)['1']
        pool2 = self.nodes[0].getpoolpair("2", True)['2']
        pool3 = self.nodes[0].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == 1)
        assert (pool2['rewardPct'] == 0)
        assert (pool3['rewardPct'] == 0)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': 45} )

        # REVERTING
        # mine blocks at node 1
        self.nodes[1].generate(20)

        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()

        # check that node 0 was synced to neccesary chain point
        g1 = self.nodes[0].getgov("LP_SPLITS")
        # print(g1)
        assert (g1 == {'LP_SPLITS': {'1': Decimal('0.50000000'), '2': Decimal('0.40000000'), '3': Decimal('0.10000000')}} )

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        # print(g2)
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool1 = self.nodes[0].getpoolpair("1", True)['1']
        pool2 = self.nodes[0].getpoolpair("2", True)['2']
        pool3 = self.nodes[0].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == Decimal('0.50000000')
            and pool2['rewardPct'] == Decimal('0.40000000')
            and pool3['rewardPct'] == Decimal('0.10000000'))


if __name__ == '__main__':
    GovsetTest ().main ()

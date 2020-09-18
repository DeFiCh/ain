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
from test_framework.util import assert_equal, \
    connect_nodes_bi
from decimal import Decimal


class GovsetTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0']]


    def run_test(self):
        # fast check for debug
        print (self.nodes[0].getgov("LP_SPLITS"))
        print (self.nodes[0].getgov("LP_DAILY_DFI_REWARD"))
        # return

        print("Generating initial chain...")
        self.setup_tokens()

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

        createTokenTx = self.nodes[0].createtoken([], {
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
            "ownerFeeAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": symbolGOLD,
            "tokenB": "BRONZE#130",
            "commission": 0.1,
            "status": True,
            "ownerFeeAddress": owner,
            "pairSymbol": "GB",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": symbolSILVER,
            "tokenB": "BRONZE#130",
            "commission": 0.1,
            "status": True,
            "ownerFeeAddress": owner,
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
            "LP_SPLITS": { "131": 0.5, "132": 0.4, "133": 0.2 }
                })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("total" in errorString)


        self.nodes[0].setgov({
            "LP_SPLITS": { "131": 0.5, "132": 0.4, "133": 0.1 }
        })

        self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35.5})
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        # print(g1)
        assert (g1 == {'LP_SPLITS': {'131': Decimal('0.50000000'), '132': Decimal('0.40000000'), '133': Decimal('0.10000000')}} )

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        # print(g2)
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool131 = self.nodes[0].getpoolpair("131", True)['131']
        pool132 = self.nodes[0].getpoolpair("132", True)['132']
        pool133 = self.nodes[0].getpoolpair("133", True)['133']
        assert (pool131['rewardPct'] == Decimal('0.50000000')
            and pool132['rewardPct'] == Decimal('0.40000000')
            and pool133['rewardPct'] == Decimal('0.10000000'))

        # test set multuple:
        self.nodes[0].setgov({
            "LP_SPLITS": { "131": 1 },
            "LP_DAILY_DFI_REWARD": 45
        })
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        assert (g1 == {'LP_SPLITS': {'131': 1}} )

        # test that all previous pool's values was reset
        pool131 = self.nodes[0].getpoolpair("131", True)['131']
        pool132 = self.nodes[0].getpoolpair("132", True)['132']
        pool133 = self.nodes[0].getpoolpair("133", True)['133']
        assert (pool131['rewardPct'] == 1)
        assert (pool132['rewardPct'] == 0)
        assert (pool133['rewardPct'] == 0)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': 45} )


if __name__ == '__main__':
    GovsetTest ().main ()

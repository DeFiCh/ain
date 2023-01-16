#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test listaccounthistory RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import (
    assert_equal,
    connect_nodes_bi
)


class TokensRPCListAccountHistory(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50',
             '-grandcentralheight=51'],
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50',
             '-grandcentralheight=51'],
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50',
             '-grandcentralheight=51'],
        ]

    def run_test(self):
        self.nodes[0].generate(101)
        num_tokens = len(self.nodes[0].listtokens())

        # collateral address
        collateral_a = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1)

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Stop node #2 for future revert
        self.stop_node(2)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_a = idx

        # Mint some tokens
        self.nodes[0].minttokens(["300@" + token_a])
        self.nodes[0].generate(1)

        # Get node 0 results
        results = self.nodes[0].listaccounthistory(collateral_a)

        # Expect two sends, two receives and one mint tokens
        assert_equal(len(results), 5)
        assert_equal(self.nodes[0].accounthistorycount(collateral_a), 5)

        # All TXs should be for collateral_a and contain a MintTokens TX
        found = False
        for txs in results:
            assert_equal(txs['owner'], collateral_a)
            self.log.info("test 0: block %d, txn is %d", txs['blockHeight'], txs['txn'])
            if txs['type'] == 'MintToken':
                found = True
        assert_equal(found, True)

        # check amounts field is type of array
        for txs in results:
            assert (hasattr(txs['amounts'], '__len__') and (not isinstance(txs['amounts'], str)))

        # list {"maxBlockHeight":103, "txn":1}, should list without blockheight = 103, txn=2. i.e without MintToken
        results = self.nodes[0].listaccounthistory(collateral_a, {"maxBlockHeight": 103, "txn": 1})
        for txs in results:
            self.log.info("test 1: block %d, txn is %d", txs['blockHeight'], txs['txn'])
            assert_equal(txs['owner'], collateral_a)
            assert_equal(txs['blockHeight'] <= 103, True)
            if txs['blockHeight'] == 103:
                assert_equal(txs['txn'] <= 1, True)  # for block 103 txn:1 applies.

        # list {"maxBlockHeight":103, "txn":0}, should list without blockheight = 103, txn=1,2. i.e without any txs from 103 block
        results = self.nodes[0].listaccounthistory(collateral_a, {"maxBlockHeight": 103, "txn": 0})

        for txs in results:
            self.log.info("test 2: block %d, txn is %d", txs['blockHeight'], txs['txn'])
            assert_equal(txs['owner'], collateral_a)
            assert_equal(txs['blockHeight'] <= 103, True)
            if txs['blockHeight'] == 103:
                assert_equal(txs['txn'] <= 0, True)
            else:
                assert_equal(txs['txn'] >= 0, True)  # means txn:0 only applicable to block 103 only

        # Get node 1 results
        results = self.nodes[1].listaccounthistory(collateral_a)

        # Expect one mint token TX
        assert_equal(len(results), 1)
        assert_equal(self.nodes[1].accounthistorycount(collateral_a), 1)

        # Check owner is collateral_a and type MintTokens
        assert_equal(results[0]['owner'], collateral_a)
        assert_equal(results[0]['type'], 'MintToken')

        # check amounts field is type of array
        assert (hasattr(results[0]['amounts'], '__len__') and (not isinstance(results[0]['amounts'], str)))

        result = self.nodes[0].listaccounthistory()
        assert 'blockReward' in [res['type'] for res in result]

        result = self.nodes[1].listaccounthistory()
        assert_equal(result, [])

        assert_equal(self.nodes[0].listaccounthistory('all', {"txtype": "MintToken"}),
                     self.nodes[0].listaccounthistory('all', {"txtype": "M"}))

        # test multiple transaction type filter
        self.nodes[0].burntokens({
            'amounts': "1@" + token_a,
            'from': collateral_a,
        })
        self.nodes[0].generate(1)

        self.nodes[0].burntokens({
            'amounts': "1@" + token_a,
            'from': collateral_a,
        })
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].listaccounthistory(collateral_a, {"txtypes": ["MintToken", "BurnToken"]}),
                     self.nodes[0].listaccounthistory(collateral_a, {"txtype": "BurnToken"}) +
                     self.nodes[0].listaccounthistory(collateral_a, {"txtype": "MintToken"}))

        assert_equal(len(self.nodes[0].listaccounthistory(collateral_a, {"txtypes": ["MintToken", "BurnToken"]})),
                     self.nodes[0].accounthistorycount(collateral_a, {"txtypes": ["MintToken", "BurnToken"]}))

        # txtype should be ignored if txtypes is passed
        assert_equal(self.nodes[0].listaccounthistory(collateral_a, {"txtypes": ["MintToken", "BurnToken"]}),
                     self.nodes[0].listaccounthistory(collateral_a, {"txtype": "BurnToken", "txtypes": ["MintToken",
                                                                                                        "BurnToken"]}))
        assert_equal(self.nodes[0].accounthistorycount(collateral_a, {"txtypes": ["MintToken", "BurnToken"]}),
                     self.nodes[0].accounthistorycount(collateral_a, {"txtype": "BurnToken", "txtypes": ["MintToken",
                                                                                                         "BurnToken"]}))

        # test pagination
        res0 = self.nodes[0].listaccounthistory(collateral_a, {"start": 0, "including_start": True})
        res1 = self.nodes[0].listaccounthistory(collateral_a, {"start": 1, "including_start": True})
        res2 = self.nodes[0].listaccounthistory(collateral_a, {"start": 1, "including_start": False})
        res3 = self.nodes[0].listaccounthistory(collateral_a, {"start": 2, "including_start": False})

        # check if entries line up
        assert_equal(res0[1], res1[0])
        assert_equal(res0[2], res2[0])
        assert_equal(res0[3], res3[0])

        # check if lengths add up
        assert_equal(len(res0), len(res1) + 1)
        assert_equal(len(res0), len(res2) + 2)
        assert_equal(len(res0), len(res3) + 3)

        # accounthistorycount should return total count
        assert_equal(self.nodes[0].accounthistorycount(), 112)

        # REVERTING:
        # ========================
        self.start_node(2)
        self.nodes[2].generate(4)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        # Get node 1 results
        results = self.nodes[1].listaccounthistory(collateral_a)

        # Expect mint token TX to be reverted
        assert_equal(len(results), 0)
        assert_equal(self.nodes[1].accounthistorycount(collateral_a), 0)


if __name__ == '__main__':
    TokensRPCListAccountHistory().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test ensures wallet locks unspend outputs"""

from test_framework.test_framework import DefiTestFramework

class TestLockUnspends(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50']]

    @DefiTestFramework.rollback
    def run_test(self):
        self.nodes[0].generate(105)
        account_address = self.nodes[0].getnewaddress("", "bech32")
        self.nodes[0].utxostoaccount({account_address: "10@0"})
        token_address = self.nodes[0].getnewaddress("", "bech32")
        self.nodes[0].createtoken({
            "symbol": "COL",
            "name": "COL",
            "isDAT": True,
            "mintable": True,
            "tradeable": True,
            "collateralAddress": token_address
        }, [])
        self.nodes[0].generate(1)

        token_a = 0
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "COL"):
                token_a = idx

        for i in range(0, 20):
            self.nodes[0].minttokens(["100@" + token_a])

        self.nodes[0].generate(1)

if __name__ == '__main__':
    TestLockUnspends().main()

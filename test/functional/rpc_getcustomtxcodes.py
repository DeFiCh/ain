#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getcustomtxcodes RPC."""

from test_framework.test_framework import DefiTestFramework
from test_framework.authproxy import JSONRPCException

from test_framework.util import (
    assert_equal,
)

class RPCgetCustomTxCodes(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-acindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50'],
        ]

    def run_test(self):
        self.nodes[0].generate(101)

        # collateral address
        collateral_a = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_a = idx

        # Mint some tokens
        self.nodes[0].minttokens(["300@" + token_a])
        self.nodes[0].generate(1)

        tx_list = self.nodes[0].getcustomtxcodes()
        for (key, value) in tx_list.items():
            if value == "MintToken":
                mint_token_tx_key = key
            if value == "CreateToken":
                create_token_tx_key = key

        list_history = self.nodes[0].listaccounthistory("mine", {"txtype": mint_token_tx_key})
        assert_equal(len(list_history), 1)
        assert_equal(list_history[0]["type"], "MintToken")

        burn_history = self.nodes[0].listburnhistory({"txtype": create_token_tx_key})
        assert_equal(len(burn_history), 1)
        assert_equal(burn_history[0]["type"], "CreateToken")

        list_history_count = self.nodes[0].accounthistorycount()
        list_history_count_mint = self.nodes[0].accounthistorycount("mine", {"txtype": mint_token_tx_key})
        assert(list_history_count_mint < list_history_count)
        assert_equal(list_history_count_mint, 1)

        invalid_tx_type = "wrong"
        assert not (invalid_tx_type in tx_list.keys())

        # should fail with invalid custom tx type
        try:
            self.nodes[0].listaccounthistory("mine", {"txtype": invalid_tx_type})
        except JSONRPCException as e:
            assert_equal(e.error['message'], "Invalid tx type (wrong)")

        try:
            self.nodes[0].listburnhistory({"txtype": invalid_tx_type})
        except JSONRPCException as e:
            assert_equal(e.error['message'], "Invalid tx type (wrong)")

        try:
            self.nodes[0].accounthistorycount("mine", {"txtype": invalid_tx_type})
        except JSONRPCException as e:
            assert_equal(e.error['message'], "Invalid tx type (wrong)")

if __name__ == '__main__':
    RPCgetCustomTxCodes().main ()

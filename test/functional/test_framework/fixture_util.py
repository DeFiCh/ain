#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Fixture utility functions for containing common fixtures for functional testing. The fixture helper functions will
by default always use nodes beginning from index 0 onwards."""

from .test_framework import DefiTestFramework
from .util import (
    assert_equal,
    get_id_token,
)


class CommonFixture:
    """Class for common utility fixture setup functions for function testing."""
    
    def setup_default_tokens(test : DefiTestFramework, my_tokens=None):
        assert (test.setup_clean_chain == True)
        assert ('-txnotokens=0' in test.extra_args[0])
        assert ('-txnotokens=0' in test.extra_args[1])
        test.nodes[0].generate(25)
        test.nodes[1].generate(25)
        test.sync_blocks()
        test.nodes[0].generate(98)
        test.sync_blocks()

        if my_tokens is not None:
            '''
                my_tokens list should contain objects with following structure:
                {
                    "wallet": node_obj,             # The test node obj with "collateralAddress" secret key in wallet
                    "symbol": "SYMBOL",             # The token symbol
                    "name": "token name",           # The token name
                    "collateralAddress": "address", # The token collateral address
                    "amount": amount,               # The token amount for minting
                }
            '''
            # create tokens
            for token in my_tokens:
                token["wallet"].createtoken({
                    "symbol": token["symbol"],
                    "name": token["name"],
                    "collateralAddress": token["collateralAddress"]
                })
            test.sync_mempools()
            test.nodes[0].generate(1)
            tokens = test.nodes[1].listtokens()
            assert_equal(len(tokens), len(my_tokens) + 1)

            # mint tokens
            for token in my_tokens:
                token["tokenId"] = get_id_token(test.nodes[0], token["symbol"])
                token["symbolId"] = token["symbol"] + "#" + token["tokenId"]
                token["wallet"].minttokens(str(token["amount"]) + "@" + token["symbolId"])

            test.sync_mempools()
            test.nodes[0].generate(1)
        else:
            # creates two tokens: GOLD for node#0 and SILVER for node1. Mint by 1000 for them
            test.nodes[0].createtoken({
                "symbol": "GOLD",
                "name": "shiny gold",
                "collateralAddress": test.nodes[0].get_genesis_keys().ownerAuthAddress  # collateralGold
            })
            test.nodes[0].generate(1)
            test.sync_blocks()
            test.nodes[1].createtoken({
                "symbol": "SILVER",
                "name": "just silver",
                "collateralAddress": test.nodes[1].get_genesis_keys().ownerAuthAddress  # collateralSilver
            })
            test.nodes[1].generate(1)
            test.sync_blocks()
            # At this point, tokens was created
            tokens = test.nodes[0].listtokens()
            assert_equal(len(tokens), 3)

            symbolGOLD = "GOLD#" + get_id_token(test.nodes[0], "GOLD")
            symbolSILVER = "SILVER#" + get_id_token(test.nodes[0], "SILVER")

            test.nodes[0].minttokens("1000@" + symbolGOLD)
            test.nodes[0].generate(1)
            test.sync_blocks()
            test.nodes[1].minttokens("2000@" + symbolSILVER)
            test.nodes[1].generate(1)
            test.sync_blocks()

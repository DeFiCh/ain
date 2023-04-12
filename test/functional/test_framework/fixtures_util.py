#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Utility functions for containing common fixtures for testing."""

from test_framework.test_framework import DefiTestFramework
from .nodes_util import NodeUtils
from .util import assert_equal

from decimal import Decimal

class Fixture:
    """
    Class for utility fixture setup functions. Encapsulates the following methods to customize test setup:
    - setup_default_tokens()
    - setup_merge_usd_value_tokens()
    - setup_split_usd_value_tokens()
    - setup_loan_low_interest_tokens()
    - setup_merge_tokens()
    - setup_split_mechanism_tokens()
    """
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
                token["tokenId"] = NodeUtils.get_id_token(test.nodes, token["symbol"])
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

            symbolGOLD = "GOLD#" + NodeUtils.get_id_token(test.nodes, "GOLD")
            symbolSILVER = "SILVER#" + NodeUtils.get_id_token(test.nodes, "SILVER")

            test.nodes[0].minttokens("1000@" + symbolGOLD)
            test.nodes[0].generate(1)
            test.sync_blocks()
            test.nodes[1].minttokens("2000@" + symbolSILVER)
            test.nodes[1].generate(1)
            test.sync_blocks()

    def setup_merge_usd_value_tokens(test : DefiTestFramework):
        # Set loan tokens
        test.nodes[0].setloantoken({
            'symbol': test.symbolDUSD,
            'name': test.symbolDUSD,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolT1,
            'name': test.symbolT1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolT1,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD"
        })
        test.nodes[0].generate(1)

        # Store token IDs
        test.idDUSD = list(test.nodes[0].gettoken(test.symbolDUSD).keys())[0]
        test.idT1 = list(test.nodes[0].gettoken(test.symbolT1).keys())[0]

    def setup_split_usd_value_tokens(test : DefiTestFramework):
        # Set loan tokens
        test.nodes[0].setloantoken({
            'symbol': test.symbolDUSD,
            'name': test.symbolDUSD,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolT1,
            'name': test.symbolT1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolT1,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD"
        })
        test.nodes[0].generate(1)

        # Store token IDs
        test.idDUSD = list(test.nodes[0].gettoken(test.symbolDUSD).keys())[0]
        test.idT1 = list(test.nodes[0].gettoken(test.symbolT1).keys())[0]

    def setup_loan_low_interest_tokens(test : DefiTestFramework):
        print('setting up loan and collateral tokens...')
        test.nodes[0].setloantoken({
            'symbol': test.symboldUSD,
            'name': "DUSD stable token",
            'fixedIntervalPriceId': "DUSD/USD",
            'mintable': True,
            'interest': 0})

        test.tokenInterest = Decimal(1)
        test.nodes[0].setloantoken({
            'symbol': test.symbolDOGE,
            'name': "DOGE token",
            'fixedIntervalPriceId': "DOGE/USD",
            'mintable': True,
            'interest': Decimal(test.tokenInterest * 100)})
        test.nodes[0].generate(1)

        # Set token ids
        test.iddUSD = list(test.nodes[0].gettoken(test.symboldUSD).keys())[0]
        test.idDFI = list(test.nodes[0].gettoken(test.symbolDFI).keys())[0]
        test.idDOGE = list(test.nodes[0].gettoken(test.symbolDOGE).keys())[0]

        # Mint tokens
        test.nodes[0].minttokens("1000000@DOGE")
        test.nodes[0].generate(1)
        test.nodes[0].minttokens("2000000@" + test.symboldUSD)  # necessary for pools
        test.nodes[0].generate(1)

        # Setup collateral tokens
        test.nodes[0].setcollateraltoken({
            'token': test.idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"})
        test.nodes[0].generate(300)

        assert_equal(len(test.nodes[0].listtokens()), 3)
        assert_equal(len(test.nodes[0].listloantokens()), 2)
        assert_equal(len(test.nodes[0].listcollateraltokens()), 1)

    def setup_merge_tokens(test : DefiTestFramework):
        # Set loan tokens
        test.nodes[0].setloantoken({
            'symbol': test.symbolT2,
            'name': test.symbolT2,
            'fixedIntervalPriceId': f"{test.symbolT2}/USD",
            "isDAT": True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolDUSD,
            'name': test.symbolDUSD,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolT1,
            'name': test.symbolT1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolT3,
            'name': test.symbolT3,
            'fixedIntervalPriceId': f"{test.symbolT3}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        # Set collateral tokens
        test.nodes[0].setcollateraltoken({
            'token': test.symbolDFI,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolDFI}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolT2,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolT2}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolT1,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD"
        })
        test.nodes[0].generate(1)

        # Store token IDs
        test.idDUSD = list(test.nodes[0].gettoken(test.symbolDUSD).keys())[0]
        test.idT1 = list(test.nodes[0].gettoken(test.symbolT1).keys())[0]
        test.idT2 = list(test.nodes[0].gettoken(test.symbolT2).keys())[0]
        test.idT3 = list(test.nodes[0].gettoken(test.symbolT3).keys())[0]

    def setup_split_mechanism_tokens(test : DefiTestFramework):
        # Set loan tokens
        test.nodes[0].setloantoken({
            'symbol': test.symbolT2,
            'name': test.symbolT2,
            'fixedIntervalPriceId': f"{test.symbolT2}/USD",
            "isDAT": True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolDUSD,
            'name': test.symbolDUSD,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolT1,
            'name': test.symbolT1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        test.nodes[0].setloantoken({
            'symbol': test.symbolT3,
            'name': test.symbolT3,
            'fixedIntervalPriceId': f"{test.symbolT3}/USD",
            'mintable': True,
            'interest': 0
        })
        test.nodes[0].generate(1)

        # Set collateral tokens
        test.nodes[0].setcollateraltoken({
            'token': test.symbolDFI,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolDFI}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolDUSD,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolDUSD}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolT2,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolT2}/USD"
        })
        test.nodes[0].generate(1)

        test.nodes[0].setcollateraltoken({
            'token': test.symbolT1,
            'factor': 1,
            'fixedIntervalPriceId': f"{test.symbolT1}/USD"
        })
        test.nodes[0].generate(1)

        # Store token IDs
        test.idDUSD = list(test.nodes[0].gettoken(test.symbolDUSD).keys())[0]
        test.idT1 = list(test.nodes[0].gettoken(test.symbolT1).keys())[0]
        test.idT2 = list(test.nodes[0].gettoken(test.symbolT2).keys())[0]
        test.idT3 = list(test.nodes[0].gettoken(test.symbolT3).keys())[0]

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Utility functions for testing node behaviour."""

from .test_node import TestNode

class NodeUtil:
    """
    Class for node's utility functions. Contains the following methods:
    - get_id_token()
    """
    def get_id_token(node : TestNode, symbol):
        list_tokens = node.listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == symbol):
                return str(idx)

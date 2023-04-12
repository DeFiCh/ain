#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Utility functions for testing node behaviour."""

from typing import List
from .test_node import TestNode

class NodeUtils:
    """
    Class for node's utility functions. Contains the following methods:
    - get_id_token()
    """
    def get_id_token(nodes : List[TestNode], symbol):
        list_tokens = nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == symbol):
                return str(idx)

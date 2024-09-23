#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test unfreezing of masternodes"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error


class UnfreezeMasternodesTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.df24height = 200
        self.extra_args = [
            [
                "-txnotokens=0",
                "-subsidytest=1",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-bayfrontmarinaheight=1",
                "-bayfrontgardensheight=1",
                "-clarkequayheight=1",
                "-dakotaheight=1",
                "-dakotacrescentheight=1",
                "-eunosheight=1",
                "-eunospayaheight=1",
                "-fortcanningheight=1",
                "-fortcanningmuseumheight=1",
                "-fortcanningparkheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-grandcentralheight=1",
                "-grandcentralepilogueheight=1",
                "-metachainheight=105",
                "-df23height=110",
                f"-df24height={self.df24height}",
            ],
        ]

    def run_test(self):

        # Set up
        self.setup()

        # Run pre-fork checks
        self.pre_fork_checks()

        # Set up Governance vars
        self.set_gov_vars()

        # Check masternodes unfrozen
        self.check_masternodes_unfrozen()

        # Check unfreeze height cannot be changed after activation
        self.test_change_unfreeze()

        # Check setting of new frozen masternodes
        self.test_new_frozen_masternodes()

    def setup(self):

        # Get masternode owner address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(110)

        # Create time locked masternodes
        self.node_5 = self.nodes[0].createmasternode(
            self.nodes[0].getnewaddress("", "legacy"), "", [], "FIVEYEARTIMELOCK"
        )
        self.node_10 = self.nodes[0].createmasternode(
            self.nodes[0].getnewaddress("", "legacy"), "", [], "TENYEARTIMELOCK"
        )
        self.nodes[0].generate(21)

        # Check masternodes
        result = self.nodes[0].getmasternode(self.node_5)[self.node_5]
        assert_equal(result["timelock"], "5 years")
        assert_equal(len(result["targetMultipliers"]), 3)
        result = self.nodes[0].getmasternode(self.node_10)[self.node_10]
        assert_equal(result["timelock"], "10 years")
        assert_equal(len(result["targetMultipliers"]), 4)

    def pre_fork_checks(self):

        # Unfreeze masternodes before fork
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF24Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/feature/unfreeze_masternodes": "210"}},
        )

    def set_gov_vars(self):

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

        # Try and set below current height
        assert_raises_rpc_error(
            -32600,
            "Cannot be set at or below current height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/feature/unfreeze_masternodes": "200"}},
        )

        # Set unfreeze height
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/feature/unfreeze_masternodes": "210"}}
        )
        self.nodes[0].generate(1)

        # Check unfreeze height
        assert_equal(
            self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                "v0/params/feature/unfreeze_masternodes"
            ],
            "210",
        )

    def check_masternodes_unfrozen(self):

        # Move to unfreezing height
        self.nodes[0].generate(210 - self.nodes[0].getblockcount())

        # Check time lock and target multipliers
        result = self.nodes[0].getmasternode(self.node_5)[self.node_5]
        assert "timelock" not in result
        assert_equal(len(result["targetMultipliers"]), 1)
        result = self.nodes[0].getmasternode(self.node_10)[self.node_10]
        assert "timelock" not in result
        assert_equal(len(result["targetMultipliers"]), 1)

        # Test resigning masternodes
        self.nodes[0].resignmasternode(self.node_5)
        self.nodes[0].resignmasternode(self.node_10)
        self.nodes[0].generate(41)

        # Check masternodes resigned
        result = self.nodes[0].getmasternode(self.node_5)[self.node_5]
        assert_equal(result["state"], "RESIGNED")
        result = self.nodes[0].getmasternode(self.node_10)[self.node_10]
        assert_equal(result["state"], "RESIGNED")

    def test_change_unfreeze(self):

        # Try and change unfreeze height after activation
        assert_raises_rpc_error(
            -32600,
            "Cannot change masternode unfreeze height after activation",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/feature/unfreeze_masternodes": "400"}},
        )

    def test_new_frozen_masternodes(self):

        # Try and create a masternode with a time lock
        assert_raises_rpc_error(
            -32600,
            "Masternode timelock disabled",
            self.nodes[0].createmasternode,
            self.nodes[0].getnewaddress("", "legacy"),
            "",
            [],
            "FIVEYEARTIMELOCK",
        )
        assert_raises_rpc_error(
            -32600,
            "Masternode timelock disabled",
            self.nodes[0].createmasternode,
            self.nodes[0].getnewaddress("", "legacy"),
            "",
            [],
            "TENYEARTIMELOCK",
        )


if __name__ == "__main__":
    UnfreezeMasternodesTest().main()

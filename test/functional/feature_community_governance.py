#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test community governance"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
import time
from decimal import Decimal


token_depr_gov_err_msg = "Only token deprecation toggle is allowed by governance"


class CommunityGovernanceTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.df24height = 250
        args = [
            "-txnotokens=0",
            "-subsidytest=1",
            "-regtest-minttoken-simulate-mainnet=1",
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
        ]
        self.extra_args = [args, args, args]

    def run_test(self):

        # Set up
        self.setup()

        # Run pre-fork checks
        self.pre_fork_checks()

        # Move to fork height
        self.move_to_df24()

        # Set up Governance
        self.test_setting_governance()

        # Test updating Governance
        self.govvar_checks()

        # Test member addition, removal and errors
        self.member_update_and_errors()

        # Test unsetgovheight
        self.unsetgovheight_checks()

        # Test cleargovheights
        self.cleargovheights_checks()

        # Test creating Governance DAT
        self.governanace_dat()

        # Test Foundation DAT access post-fork
        self.foundation_dat()

        # Test Governance pools
        self.governance_pools()

        # Test Governanace Oracles
        self.governance_oracles()

        # Test Governance loans
        self.governance_loans()

        # Test token deprecate token
        self.foundation_deprecate_token()

        # Test owner deprecate token
        self.governance_deprecate_token()

        # Test owner deprecate token
        self.owner_deprecate_token()

    def setup(self):

        # Get address for Governance
        self.governance_member = "bcrt1qrccu8ecz66vkglf4xcdhn3rhn6jg7lqk80w2ln"
        self.nodes[1].importprivkey(
            "cR4xaqiCqsVy1zJUAVsTBWJHYSPKfAMCHx7jaU3QZgYhB8ML7jmv"
        )

        # Generate chain
        self.nodes[0].generate(110)

        # Get address for node 2 token
        self.collateral_address = self.nodes[2].getnewaddress("", "bech32")

        # Fund nodes
        self.nodes[0].sendtoaddress(self.governance_member, 100)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress("", "bech32"), 100)
        self.nodes[0].generate(1)

        # Create DAT token for later tests
        self.nodes[0].createtoken(
            {
                "symbol": "ETH",
                "name": "Ethereum",
                "isDAT": True,
                "collateralAddress": self.collateral_address,
            }
        )
        self.nodes[0].generate(1)

        # Get address for Foundation
        foundation_member = self.nodes[0].getnewaddress()

        # Enable Foundation address in attributes
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/feature/gov-foundation": "true"}}
        )
        self.nodes[0].generate(1)

        # Get all Foundation addresses
        foundation_members = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
            "v0/params/foundation/members"
        ]

        # Add Foundation address
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/foundation/members": [foundation_member]}}
        )
        self.nodes[0].generate(1)

        # Remove previous addresses
        for member in foundation_members:
            self.nodes[0].setgov(
                {"ATTRIBUTES": {"v0/params/foundation/members": [f"-{member}"]}}
            )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check foundation addresses
        assert_equal(
            self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                "v0/params/foundation/members"
            ],
            [
                f"{foundation_member}",
            ],
        )

        # Check node one no longer has Foundation access
        assert_raises_rpc_error(
            -32600,
            "tx not from foundation member",
            self.nodes[1].setgov,
            {"ATTRIBUTES": {"v0/params/foundation/members": [self.governance_member]}},
        )

        # Enable unset Gov variables in attributes
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/feature/gov-unset": "true"}})
        self.nodes[0].generate(1)

    def rollback_to_start(self):

        # Rollback block
        self.rollback_to(self.start_block)

        # Update times across all nodes. Avoids sync issues.
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[1].generate(1)
        self.sync_blocks()
        self.nodes[2].generate(1)
        self.sync_blocks()

    def move_to_df24(self):

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())
        self.sync_blocks()

        # Store rollback block
        self.start_block = self.nodes[0].getblockcount()

    def pre_fork_checks(self):

        # Change address before fork
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF24Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/governance/members": [self.governance_member]}},
        )

        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF24Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/feature/governance": "true"}},
        )

        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF24Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/governance/govheight_min_blocks": "1000"}},
        )

        assert_raises_rpc_error(
            -32600,
            "Token cannot be deprecated below DF24Height",
            self.nodes[0].updatetoken,
            "ETH",
            {
                "deprecate": True,
            },
        )

    def test_setting_governance(self):

        # Rollback block
        self.rollback_to_start()

        # Set Governance address
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/governance/members": [self.governance_member]}}
        )
        self.nodes[0].generate(1)

        # Check foundation addresses
        assert_equal(
            self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                "v0/params/governance/members"
            ],
            [
                f"{self.governance_member}",
            ],
        )

        # Enable Governance
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/params/feature/governance": "true"}})
        self.nodes[0].generate(1)

    def enable_governance(self, address):

        # Set Governance address
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/governance/members": [address],
                    "v0/params/feature/governance": "true",
                }
            }
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

    def member_update_and_errors(self):

        # Rollback block
        self.rollback_to_start()

        # Set Governance address
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/governance/members": [self.governance_member]}}
        )
        self.nodes[0].generate(1)

        # Add already existant address
        assert_raises_rpc_error(
            -32600,
            "Member to add already present",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/governance/members": [self.governance_member]}},
        )

        # Remove an address that is not present
        assert_raises_rpc_error(
            -32600,
            "Member to remove not present",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/params/governance/members": [
                        f"-{self.nodes[0].getnewaddress()}"
                    ]
                }
            },
        )

        # Add nonsense address
        assert_raises_rpc_error(
            -5,
            "Invalid address provided",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {"v0/params/governance/members": ["NOTANADDRESS"]}},
        )

        # Get address for Governance
        new_member = self.nodes[1].getnewaddress()

        # Set Governance address
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/governance/members": [new_member]}}
        )
        self.nodes[0].generate(1)

        # Check two members present
        assert_equal(
            len(
                self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                    "v0/params/governance/members"
                ]
            ),
            2,
        )

        # Test member removal
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/governance/members": [f"-{new_member}"]}}
        )
        self.nodes[0].generate(1)

        # Check one member present
        assert_equal(
            len(
                self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                    "v0/params/governance/members"
                ]
            ),
            1,
        )

        # Set Governance address again
        self.nodes[0].setgov(
            {"ATTRIBUTES": {"v0/params/governance/members": [new_member]}}
        )
        self.nodes[0].generate(1)

        # Check two members present
        assert_equal(
            len(
                self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                    "v0/params/governance/members"
                ]
            ),
            2,
        )

        # Removing address again, add new address and set another variable
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/governance/members": [
                        f"-{new_member}",
                        "+2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs",
                    ],
                    "v0/params/dfip2201/active": "false",
                }
            }
        )
        self.nodes[0].generate(1)

        # Check address no longer present and new member added
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/params/governance/members"],
            [
                self.governance_member,
                "2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs",
            ],
        )

        # Check other key added
        assert_equal(attributes["v0/params/dfip2201/active"], "false")

        # Set stored Gov update
        activation_height = self.nodes[0].getblockcount() + 5
        self.nodes[0].setgovheight(
            {
                "ATTRIBUTES": {
                    "v0/params/governance/members": [
                        "-2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs",
                        "2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2",
                    ],
                    "v0/params/dfip2203/active": "true",
                }
            },
            activation_height,
        )
        self.nodes[0].generate(1)

        # Check pending changes show as expected
        attributes = self.nodes[0].listgovs()[8]
        assert_equal(
            attributes[1][str(activation_height)]["v0/params/governance/members"],
            [
                "-2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs",
                "2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2",
            ],
        )
        assert_equal(
            attributes[1][str(activation_height)]["v0/params/dfip2203/active"], "true"
        )

        # Move to stored height
        self.nodes[0].generate(5)

        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/params/governance/members"],
            [
                self.governance_member,
                "2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2",
            ],
        )

        # Check other key updated
        assert_equal(attributes["v0/params/dfip2203/active"], "true")

    def govvar_checks(self):

        # Try to update Foundation member by height
        activation_height = self.nodes[1].getblockcount() + 5
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].setgovheight,
            {"ATTRIBUTES": {"v0/params/foundation/members": [self.governance_member]}},
            activation_height,
        )

        # Try to deactivate foundation by height
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].setgovheight,
            {"ATTRIBUTES": {"v0/params/feature/gov-foundation": "false"}},
            activation_height,
        )

        # Try to unset Foundation members
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].unsetgovheight,
            {"ATTRIBUTES": ["v0/params/foundation/members"]},
            activation_height,
        )

        # Try to unset Foundation activation
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].unsetgovheight,
            {"ATTRIBUTES": ["v0/params/feature/gov-foundation"]},
            activation_height,
        )

    def cleargovheights_checks(self):

        # Rollback block
        self.rollback_to_start()

        # Set params to unset
        self.nodes[0].setgov(
            {
                "ORACLE_DEVIATION": "1",
                "ATTRIBUTES": {
                    "v0/params/dfip2206f/active": "false",
                },
            }
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Get current block
        current_block = self.nodes[0].getblockcount()

        # Create multiple set and unset heights
        self.nodes[0].setgovheight({"ORACLE_DEVIATION": "0.5"}, current_block + 100)
        self.nodes[0].setgovheight(
            {"ATTRIBUTES": {"v0/params/dfip2206f/active": "true"}}, current_block + 200
        )
        self.nodes[0].unsetgovheight(
            {"ORACLE_DEVIATION": ""},
            current_block + 101,
        )
        self.nodes[0].unsetgovheight(
            {"ATTRIBUTES": ["v0/params/dfip2206f/active"]},
            current_block + 201,
        )
        self.nodes[0].generate(1)

        # Check pending changes shown
        result = self.nodes[0].listgovs()
        assert_equal(len(result[7]), 3)
        assert_equal(len(result[8]), 3)
        assert_equal(result[7][1], {f"{current_block + 100}": Decimal("0.50000000")})
        assert_equal(result[7][2], {f"{current_block + 101}": []})
        assert_equal(
            result[8][1],
            {f"{current_block + 200}": {"v0/params/dfip2206f/active": "true"}},
        )
        assert_equal(
            result[8][2], {f"{current_block + 201}": ["v0/params/dfip2206f/active"]}
        )

        # Clear all pending changes
        self.nodes[0].cleargovheights()
        self.nodes[0].generate(1)

        # Check pending changes cleared
        result = self.nodes[0].listgovs()
        assert_equal(len(result[7]), 1)
        assert_equal(len(result[8]), 1)

    def unsetgovheight_checks(self):

        # Rollback block
        self.rollback_to_start()

        # Enable Governance
        self.enable_governance(self.collateral_address)

        # Set params to unset
        self.nodes[0].setgov(
            {
                "ORACLE_DEVIATION": "1",
                "ATTRIBUTES": {
                    "v0/params/dfip2206f/active": "false",
                },
            }
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check vars set
        result = self.nodes[0].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("1.00000000"))
        result = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(result["v0/params/dfip2206f/active"], "false")

        # Save block for rollback
        rollback_height = self.nodes[0].getblockcount()

        # Try to unset and next height
        assert_raises_rpc_error(
            -32600,
            "unsetHeight must be above the current block height",
            self.nodes[0].unsetgovheight,
            {"ORACLE_DEVIATION": ""},
            rollback_height + 1,
        )

        # Foundation unset
        self.nodes[0].unsetgovheight(
            {"ORACLE_DEVIATION": "", "ATTRIBUTES": ["v0/params/dfip2206f/active"]},
            rollback_height + 2,
        )
        self.nodes[0].generate(2)

        # Check keys no longer set
        result = self.nodes[0].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("0E-8"))
        assert (
            "v0/params/dfip2206f/active"
            not in self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        )

        # Rollback to before unset
        self.rollback_to(rollback_height)

        # Governance unset
        self.nodes[2].unsetgovheight(
            {"ORACLE_DEVIATION": "", "ATTRIBUTES": ["v0/params/dfip2206f/active"]},
            rollback_height + 2,
        )
        self.nodes[2].generate(2)

        # Check keys no longer set
        result = self.nodes[2].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("0E-8"))
        assert (
            "v0/params/dfip2206f/active"
            not in self.nodes[2].getgov("ATTRIBUTES")["ATTRIBUTES"]
        )

        # Rollback to before unset
        self.rollback_to(rollback_height)

        # Set minimum height for set and under gov
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/governance/govheight_min_blocks": "10",
                },
            }
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Get current block
        current_block = self.nodes[0].getblockcount()

        # Test setting with the min block limit
        assert_raises_rpc_error(
            -32600,
            "Height must be 10 blocks above the current height",
            self.nodes[0].unsetgovheight,  # Foundation
            {"ORACLE_DEVIATION": ""},
            current_block + 10,
        )

        assert_raises_rpc_error(
            -32600,
            "Height must be 10 blocks above the current height",
            self.nodes[2].unsetgovheight,  # Governance
            {"ORACLE_DEVIATION": ""},
            current_block + 10,
        )

        assert_raises_rpc_error(
            -32600,
            "Height must be 10 blocks above the current height",
            self.nodes[0].setgovheight,  # Foundation
            {"ORACLE_DEVIATION": "0.5"},
            current_block + 10,
        )

        assert_raises_rpc_error(
            -32600,
            "Height must be 10 blocks above the current height",
            self.nodes[2].setgovheight,  # Governance
            {"ORACLE_DEVIATION": "0.5"},
            current_block + 10,
        )

        # Test setting with the min block limit
        self.nodes[0].setgovheight({"ORACLE_DEVIATION": "0.5"}, current_block + 12)
        self.nodes[0].generate(12)

        # Check results
        result = self.nodes[0].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("0.50000000"))

        # Get current block
        current_block = self.nodes[0].getblockcount()

        # Test unsetting with min block limit
        self.nodes[0].unsetgovheight({"ORACLE_DEVIATION": ""}, current_block + 12)
        self.nodes[0].generate(1)

        # Check pending changes shown
        result = self.nodes[0].listgovs()
        assert_equal(result[7][1], {f"{current_block + 12}": []})

        # Move to unset height
        self.nodes[0].generate(11)
        self.sync_blocks()

        # Check results
        result = self.nodes[0].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("0E-8"))

        # Get current block
        current_block = self.nodes[0].getblockcount()

        # Test Governance setting with the min block limit
        self.nodes[2].setgovheight({"ORACLE_DEVIATION": "0.5"}, current_block + 12)
        self.nodes[2].generate(12)

        # Check results
        result = self.nodes[2].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("0.50000000"))

        # Get current block
        current_block = self.nodes[2].getblockcount()

        # Test unsetting with min block limit
        self.nodes[2].unsetgovheight({"ORACLE_DEVIATION": ""}, current_block + 12)
        self.nodes[2].generate(1)

        # Check pending changes shown
        result = self.nodes[2].listgovs()
        assert_equal(result[7][1], {f"{current_block + 12}": []})

        # Move to unset height
        self.nodes[2].generate(11)

        # Check results
        result = self.nodes[2].getgov("ORACLE_DEVIATION")
        assert_equal(result["ORACLE_DEVIATION"], Decimal("0E-8"))

    def governanace_dat(self):

        # Rollback block
        self.rollback_to_start()

        # Enable Governance
        self.enable_governance(self.collateral_address)

        # Create DAT token
        self.nodes[2].createtoken(
            {
                "symbol": "BTC",
                "name": "BTC",
                "isDAT": True,
                "collateralAddress": self.nodes[1].getnewaddress(),
            }
        )
        self.nodes[2].generate(1)
        self.sync_blocks()

        # Check DAT token created
        assert_equal(
            self.nodes[2].gettoken("BTC")["2"]["isDAT"],
            True,
        )

        # Update DAT token. Will use owner auth.
        self.nodes[1].updatetoken(
            "BTC",
            {
                "name": "Bitcoin",
            },
        )
        self.nodes[1].generate(1)

        # Check DAT token created
        assert_equal(
            self.nodes[1].gettoken("BTC")["2"]["name"],
            "Bitcoin",
        )

    def foundation_dat(self):

        # Rollback block
        self.rollback_to_start()

        # Create DAT token
        self.nodes[0].createtoken(
            {
                "symbol": "LTC",
                "name": "LTC",
                "isDAT": True,
                "collateralAddress": self.nodes[0].getnewaddress(),
            }
        )
        self.nodes[0].generate(1)

        # Check DAT token created
        assert_equal(
            self.nodes[0].gettoken("LTC")["2"]["isDAT"],
            True,
        )

        # Update DAT token. Will use owner auth.
        self.nodes[0].updatetoken(
            "LTC",
            {
                "name": "Litecoin",
            },
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Enable Governance
        self.enable_governance(self.governance_member)

        # Create Governance DAT token
        self.nodes[1].createtoken(
            {
                "symbol": "BTC",
                "name": "Bitcoin",
                "isDAT": True,
                "collateralAddress": self.nodes[1].getnewaddress(),
            }
        )
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Check DAT token created
        assert_equal(
            self.nodes[0].gettoken("BTC")["3"]["name"],
            "Bitcoin",
        )

        # Try and update DAT token with non-owner auth
        assert_raises_rpc_error(
            -5,
            "Incorrect authorization",
            self.nodes[0].updatetoken,
            "BTC",
            {
                "name": "BTC",
            },
        )

        # Try and mint DAT token with non-owner auth
        assert_raises_rpc_error(
            -32600,
            "tx must have at least one input from token owner",
            self.nodes[0].minttokens,
            ["2@BTC"],
        )

    def governance_pools(self):

        # Rollback block
        self.rollback_to_start()

        # Enable Governance
        self.enable_governance(self.governance_member)

        # Create DAT token
        self.nodes[1].createtoken(
            {
                "symbol": "LTC",
                "name": "Litecoin",
                "isDAT": True,
                "collateralAddress": self.nodes[1].getnewaddress(),
            }
        )
        self.nodes[1].generate(1)

        self.nodes[1].createtoken(
            {
                "symbol": "BTC",
                "name": "Bitcoin",
                "isDAT": True,
                "collateralAddress": self.nodes[1].getnewaddress(),
            }
        )
        self.nodes[1].generate(1)

        # Create pool pair
        self.nodes[1].createpoolpair(
            {
                "tokenA": "BTC",
                "tokenB": "LTC",
                "commission": 0.01,
                "status": True,
                "ownerAddress": self.nodes[1].getnewaddress(),
            }
        )
        self.nodes[1].generate(1)

        # Check pool pair created
        assert_equal(
            self.nodes[1].getpoolpair("BTC-LTC")["4"]["name"], "Bitcoin-Litecoin"
        )

        # Update pool pair
        self.nodes[1].updatepoolpair(
            {
                "pool": "BTC-LTC",
                "name": "BTC-LTC",
            }
        )
        self.nodes[1].generate(1)

        # Check pool pair updated
        assert_equal(self.nodes[1].getpoolpair("BTC-LTC")["4"]["name"], "BTC-LTC")

    def governance_oracles(self):

        # Rollback block
        self.rollback_to_start()

        # Enable Governance
        self.enable_governance(self.governance_member)

        # Define price feeds
        price_feeds = [
            {"currency": "USD", "token": "TSLA"},
        ]

        # Appoint Oracle
        oracle_address = self.nodes[1].getnewaddress()
        oracle_tx = self.nodes[1].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[1].generate(1)

        # Check Oracle appointed
        assert_equal(self.nodes[1].listoracles(), [oracle_tx])

        # Update price feeds
        price_feeds = [
            {"currency": "USD", "token": "META"},
            {"currency": "USD", "token": "DFI"},
        ]

        # Update Oracle
        self.nodes[1].updateoracle(oracle_tx, oracle_address, price_feeds, 100)
        self.nodes[1].generate(1)

        # Check Oracle updated
        oracle_data = self.nodes[1].getoracledata(oracle_tx)
        assert_equal(oracle_data["weightage"], 100)
        assert_equal(oracle_data["priceFeeds"][0]["token"], "DFI")
        assert_equal(oracle_data["priceFeeds"][1]["token"], "META")

        # Remove Oracle
        self.nodes[1].removeoracle(oracle_tx)
        self.nodes[1].generate(1)

        # Check Oracle removed
        assert_equal(self.nodes[1].listoracles(), [])

        # Appoint Oracle for loan testing
        oracle_tx = self.nodes[1].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[1].generate(1)

        # Feed oracle
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "10@META"},
            {"currency": "USD", "tokenAmount": "10@DFI"},
        ]

        # Set Oracle data
        self.nodes[1].setoracledata(oracle_tx, int(time.time()), oracle_prices)
        self.nodes[1].generate(1)

    def governance_loans(self):

        # Rollback block
        self.rollback_to_start()

        # Enable Governance
        self.enable_governance(self.governance_member)

        # Price feeds
        price_feeds = [
            {"currency": "USD", "token": "META"},
            {"currency": "USD", "token": "DFI"},
        ]

        # Appoint Oracle
        oracle_address = self.nodes[1].getnewaddress()
        oracle_tx = self.nodes[1].appointoracle(oracle_address, price_feeds, 10)
        self.nodes[1].generate(1)

        # Feed oracle
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "10@META"},
            {"currency": "USD", "tokenAmount": "10@DFI"},
        ]

        # Set Oracle data
        self.nodes[1].setoracledata(oracle_tx, int(time.time()), oracle_prices)
        self.nodes[1].generate(1)

        # Set collateral token
        self.nodes[1].setcollateraltoken(
            {"token": 0, "factor": 1, "fixedIntervalPriceId": "DFI/USD"}
        )
        self.nodes[1].generate(1)

        # Set loan token
        self.nodes[1].setloantoken(
            {
                "symbol": "META",
                "name": "Facebook",
                "fixedIntervalPriceId": "META/USD",
                "mintable": True,
                "interest": 0,
            }
        )
        self.nodes[1].generate(1)

        # Check loan token created
        assert_equal(self.nodes[1].gettoken("META")["2"]["name"], "Facebook")

        # Update loan token
        self.nodes[1].updateloantoken(
            "META",
            {
                "name": "META",
            },
        )
        self.nodes[1].generate(1)

        # Check loan token updated
        assert_equal(self.nodes[1].gettoken("META")["2"]["name"], "META")

        # Create loan scheme
        self.nodes[1].createloanscheme(150, 1, "LOAN1")
        self.nodes[1].generate(1)

        # Check loan scheme created
        results = self.nodes[1].listloanschemes()
        assert_equal(results[0]["id"], "LOAN1")
        assert_equal(results[0]["mincolratio"], 150)
        assert_equal(results[0]["default"], True)

        # Create loan scheme
        self.nodes[1].createloanscheme(200, 2, "LOAN2")
        self.nodes[1].generate(1)

        # Check loan scheme created
        results = self.nodes[1].listloanschemes()
        assert_equal(results[1]["id"], "LOAN2")
        assert_equal(results[1]["mincolratio"], 200)
        assert_equal(results[1]["default"], False)

        self.nodes[1].setdefaultloanscheme("LOAN2")
        self.nodes[1].generate(1)

        # Check loan scheme updated
        results = self.nodes[1].listloanschemes()
        assert_equal(results[1]["id"], "LOAN2")
        assert_equal(results[1]["default"], True)

        # Destroy a loan scheme
        self.nodes[1].destroyloanscheme("LOAN1")
        self.nodes[1].generate(1)

        # Check loan scheme destroyed
        results = self.nodes[1].listloanschemes()
        assert_equal(len(results), 1)
        assert_equal(results[0]["id"], "LOAN2")

    def foundation_deprecate_token(self):

        # Rollback block
        self.rollback_to_start()

        # Get address to test collateral change
        new_address = self.nodes[0].getnewaddress()

        # Foundation deprecate and set other values
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "name": "Litecoin",
                "deprecate": True,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "symbol": "LTC",
                "deprecate": True,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "tradeable": False,
                "deprecate": True,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "collateralAddress": new_address,
                "deprecate": True,
            },
        )

        # Test Foundation can deprecate token
        self.nodes[0].updatetoken(
            1,
            {
                "deprecate": True,
            },
        )
        self.nodes[0].generate(1)

        # Check token deprecated
        assert_equal(self.nodes[0].gettoken(1)["1"]["symbol"], "eol/ETH")

        # Foundation undeprecate and set other values
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "name": "Litecoin",
                "deprecate": False,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "symbol": "LTC",
                "deprecate": False,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "tradeable": False,
                "deprecate": False,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[0].updatetoken,
            1,
            {
                "collateralAddress": new_address,
                "deprecate": False,
            },
        )

        # Test Foundation can undeprecate token
        self.nodes[0].updatetoken(
            1,
            {
                "deprecate": False,
            },
        )
        self.nodes[0].generate(1)

        # Check token deprecated
        assert_equal(self.nodes[0].gettoken(1)["1"]["symbol"], "ETH")

    def governance_deprecate_token(self):

        # Rollback block
        self.rollback_to_start()

        # Enable Governance
        self.enable_governance(self.governance_member)

        # Get address to test collateral change
        new_address = self.nodes[1].getnewaddress()

        # Governance deprecate and set other values
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "name": "Litecoin",
                "deprecate": True,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "symbol": "LTC",
                "deprecate": True,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "tradeable": False,
                "deprecate": True,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "collateralAddress": new_address,
                "deprecate": True,
            },
        )

        # Test Governance can deprecate token
        self.nodes[1].updatetoken(
            1,
            {
                "deprecate": True,
            },
        )
        self.nodes[1].generate(1)

        # Check token deprecated
        assert_equal(self.nodes[1].gettoken(1)["1"]["symbol"], "eol/ETH")

        # Governance undeprecate and set other values
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "name": "Litecoin",
                "deprecate": False,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "symbol": "LTC",
                "deprecate": False,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "tradeable": False,
                "deprecate": False,
            },
        )
        assert_raises_rpc_error(
            -32600,
            token_depr_gov_err_msg,
            self.nodes[1].updatetoken,
            1,
            {
                "collateralAddress": new_address,
                "deprecate": False,
            },
        )

        # Test Governance can undeprecate token
        self.nodes[1].updatetoken(
            1,
            {
                "deprecate": False,
            },
        )
        self.nodes[1].generate(1)

        # Check token deprecated
        assert_equal(self.nodes[1].gettoken(1)["1"]["symbol"], "ETH")

    def owner_deprecate_token(self):

        # Rollback block
        self.rollback_to_start()

        # Test owner can deprecate token
        self.nodes[2].updatetoken(
            1,
            {
                "deprecate": True,
            },
        )
        self.nodes[2].generate(1)

        # Check token deprecated
        assert_equal(self.nodes[2].gettoken(1)["1"]["symbol"], "eol/ETH")

        # Test owner can undeprecate token
        self.nodes[2].updatetoken(
            1,
            {
                "deprecate": False,
            },
        )
        self.nodes[2].generate(1)

        # Check token deprecated
        assert_equal(self.nodes[2].gettoken(1)["1"]["symbol"], "ETH")

        # Test owner can deprecate token, rename and set falgs
        self.nodes[2].updatetoken(
            1,
            {
                "deprecate": True,
                "name": "Litecoin",
                "symbol": "LTC",
                "tradeable": False,
            },
        )
        self.nodes[2].generate(1)

        # Check token deprecated and renamed
        assert_equal(self.nodes[2].gettoken(1)["1"]["symbol"], "eol/LTC")
        assert_equal(self.nodes[2].gettoken(1)["1"]["name"], "Litecoin")
        assert_equal(self.nodes[2].gettoken(1)["1"]["tradeable"], False)

        # Test owner can deprecate token and rename
        self.nodes[2].updatetoken(
            1,
            {
                "deprecate": False,
                "name": "Ethereum",
                "symbol": "ETH",
                "tradeable": True,
            },
        )
        self.nodes[2].generate(1)

        # Check token deprecated and renamed
        assert_equal(self.nodes[2].gettoken(1)["1"]["symbol"], "ETH")
        assert_equal(self.nodes[2].gettoken(1)["1"]["name"], "Ethereum")
        assert_equal(self.nodes[2].gettoken(1)["1"]["tradeable"], True)


if __name__ == "__main__":
    CommunityGovernanceTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test community governance"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error
import time


class CommunityGovernanceTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.df24height = 250
        self.extra_args = [
            [
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
            ],
            [
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
            ],
        ]

    def run_test(self):

        # Set up
        self.setup()

        # Run pre-fork checks
        self.pre_fork_checks()

        # Set up Governance
        self.setup_governance()

        # Test member addition, removal and errors
        self.member_update_and_errors()

        # Test updating Governance
        self.govvar_checks()

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

    def setup(self):

        # Get address for Governance
        self.governance_member = "msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7"

        # Generate chain
        self.nodes[0].generate(110)

        # Fund node one
        self.nodes[0].sendtoaddress(self.governance_member, 100)

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
        self.sync_blocks()

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

    def setup_governance(self):

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())

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
        self.sync_blocks()

    def member_update_and_errors(self):

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
                "2MwHamtynMqvstggG5XsVPVoAKroa4CgwFs",
                self.governance_member,
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
        self.sync_blocks()

        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(
            attributes["v0/params/governance/members"],
            [
                "2N3kCSytvetmJybdZbqV5wK3Zzekg9SiSe2",
                self.governance_member,
            ],
        )

        # Check other key updated
        assert_equal(attributes["v0/params/dfip2203/active"], "true")

    def govvar_checks(self):

        # Test updating Governanace
        self.nodes[1].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/dfip2201/active": "false",
                }
            }
        )
        self.nodes[1].generate(1)

        # Check other key updated
        assert_equal(
            self.nodes[1].getgov("ATTRIBUTES")["ATTRIBUTES"][
                "v0/params/dfip2203/active"
            ],
            "true",
        )

        # Try to update Foundation member
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].setgov,
            {"ATTRIBUTES": {"v0/params/foundation/members": [self.governance_member]}},
        )

        # Try to deactivate foundation
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].setgov,
            {"ATTRIBUTES": {"v0/params/feature/gov-foundation": "false"}},
        )

        # Try to update Foundation member by height
        activation_height = self.nodes[1].getblockcount() + 5
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].setgovheight,
            {"ATTRIBUTES": {"v0/params/foundation/members": [self.governance_member]}},
            activation_height,
        )

        # Try to deactivate foundation bu height
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
            self.nodes[1].unsetgov,
            {"ATTRIBUTES": ["v0/params/foundation/members"]},
        )

        # Try to unset Foundation activation
        assert_raises_rpc_error(
            -32600,
            "Foundation cannot be modified by governance",
            self.nodes[1].unsetgov,
            {"ATTRIBUTES": ["v0/params/feature/gov-foundation"]},
        )

    def governanace_dat(self):

        # Create DAT token
        self.nodes[1].createtoken(
            {
                "symbol": "BTC",
                "name": "BTC",
                "isDAT": True,
                "collateralAddress": self.nodes[1].getnewaddress(),
            }
        )
        self.nodes[1].generate(1)

        # Check DAT token created
        assert_equal(
            self.nodes[1].gettoken("BTC")["1"]["isDAT"],
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
        self.sync_blocks()

        # Check DAT token created
        assert_equal(
            self.nodes[1].gettoken("BTC")["1"]["name"],
            "Bitcoin",
        )

    def foundation_dat(self):

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

        # Check DAT token created
        assert_equal(
            self.nodes[0].gettoken("BTC")["1"]["name"],
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
            self.nodes[1].getpoolpair("BTC-LTC")["3"]["name"], "Bitcoin-Litecoin"
        )

        # Update pool pair
        self.nodes[1].updatepoolpair(
            {
                "pool": "BTC-LTC",
                "name": "BTC-LTC",
            }
        )
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Check pool pair updated
        assert_equal(self.nodes[1].getpoolpair("BTC-LTC")["3"]["name"], "BTC-LTC")

    def governance_oracles(self):

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
        self.sync_blocks()

    def governance_loans(self):

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
        assert_equal(self.nodes[1].gettoken("META")["4"]["name"], "Facebook")

        # Update loan token
        self.nodes[1].updateloantoken(
            "META",
            {
                "name": "META",
            },
        )
        self.nodes[1].generate(1)

        # Check loan token updated
        assert_equal(self.nodes[1].gettoken("META")["4"]["name"], "META")

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
        self.sync_blocks()

        # Check loan scheme destroyed
        results = self.nodes[1].listloanschemes()
        assert_equal(len(results), 1)
        assert_equal(results[0]["id"], "LOAN2")


if __name__ == "__main__":
    CommunityGovernanceTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test vault creation fee"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

from decimal import Decimal


class VaultCreationFeeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txnotokens=0",
                "-txindex=1",
                "-subsidytest=1",
                "-amkheight=1",
                "-bayfrontheight=1",
                "-dakotaheight=1",
                "-eunosheight=1",
                "-fortcanningheight=1",
                "-fortcanninghillheight=1",
                "-fortcanningroadheight=1",
                "-fortcanningcrunchheight=1",
                "-fortcanningspringheight=1",
                "-fortcanninggreatworldheight=1",
                "-fortcanningepilogueheight=1",
                "-grandcentralheight=1",
                "-metachainheight=100",
                "-df23height=150",
            ],
        ]

    def run_test(self):
        # Run setup
        self.setup()

        # Test future swap limitation
        self.test_vault_creation_fee()

    def setup(self):
        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(105)

        # Create vault scheme
        self.nodes[0].createloanscheme(150, 1, "LOAN150")
        self.nodes[0].generate(1)

    def test_vault_creation_fee(self):

        # Try and set creation fee before fork
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF23Height",
            self.nodes[0].setgov,
            {
                "ATTRIBUTES": {
                    "v0/vaults/params/creation_fee": "100",
                }
            },
        )

        vault1 = self.nodes[0].createvault(self.address)
        self.nodes[0].generate(1)

        # Move to fork height
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Create vault with old fee
        vault1 = self.nodes[0].createvault(self.address)
        self.nodes[0].generate(1)

        # Verify vault
        assert_equal(self.nodes[0].getvault(vault1)["state"], "active")

        # Verify fee
        assert_equal(
            self.nodes[0].getrawtransaction(vault1, 1)["vout"][0]["value"],
            Decimal("1.00000000"),
        )

        # Set new fee
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/vaults/params/creation_fee": "100"}})
        self.nodes[0].generate(1)

        # Verify new fee
        assert_equal(
            self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                "v0/vaults/params/creation_fee"
            ],
            "100",
        )

        # Create vault with new fee
        vault2 = self.nodes[0].createvault(self.address)
        self.nodes[0].generate(1)

        # Verify vault
        assert_equal(self.nodes[0].getvault(vault2)["state"], "active")

        # Verify fee
        assert_equal(
            self.nodes[0].getrawtransaction(vault2, 1)["vout"][0]["value"],
            Decimal("100.00000000"),
        )

        # Set new fee
        self.nodes[0].setgov({"ATTRIBUTES": {"v0/vaults/params/creation_fee": "200"}})
        self.nodes[0].generate(1)

        # Verify new fee
        assert_equal(
            self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"][
                "v0/vaults/params/creation_fee"
            ],
            "200",
        )

        # Create vault with new fee
        vault3 = self.nodes[0].createvault(self.address)
        self.nodes[0].generate(1)

        # Verify vault
        assert_equal(self.nodes[0].getvault(vault3)["state"], "active")

        # Verify fee
        assert_equal(
            self.nodes[0].getrawtransaction(vault3, 1)["vout"][0]["value"],
            Decimal("200.00000000"),
        )

        # Close old fee vault and check refund
        self.nodes[0].closevault(vault1, self.address)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].gettokenbalances(), ["0.50000000@0"])

        # Close first new fee vault and check refund
        self.nodes[0].closevault(vault2, self.address)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].gettokenbalances(), ["50.50000000@0"])

        # Close second new fee vault and check refund
        self.nodes[0].closevault(vault3, self.address)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].gettokenbalances(), ["150.50000000@0"])


if __name__ == "__main__":
    VaultCreationFeeTest().main()

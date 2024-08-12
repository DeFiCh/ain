#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token transfer"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal, assert_raises_rpc_error


class TokenTransferTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.df24height = 150
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

        # Transfer token to new owner
        self.transfer_token()

    def setup(self):

        # Get address for node one
        address_node1 = self.nodes[1].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(110)

        # Fund nodes
        self.nodes[0].sendtoaddress(address_node1, 100)
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Create DAT token for later tests
        self.original_creation_tx = self.nodes[1].createtoken(
            {
                "symbol": "BTC",
                "name": "Bitcoin",
                "isDAT": True,
                "collateralAddress": address_node1,
            }
        )
        self.nodes[1].generate(1)
        self.sync_blocks()

    def pre_fork_checks(self):

        # Try and change address before fork
        assert_raises_rpc_error(
            -8,
            "Collateral address update is not allowed before DF24Height",
            self.nodes[1].updatetoken,
            "BTC",
            {
                "collateralAddress": self.nodes[1].getnewaddress(),
            },
        )

    def transfer_token(self):

        # Move to fork height
        self.nodes[0].generate(self.df24height - self.nodes[0].getblockcount())
        self.sync_blocks()

        # Get address for node zero
        address_node0 = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Change address after fork
        transfer_tx = self.nodes[1].updatetoken(
            "BTC",
            {
                "collateralAddress": address_node0,
            },
        )
        self.sync_mempools()

        # Create TX spending collateral
        raw_tx = self.nodes[0].createrawtransaction(
            [{"txid": transfer_tx, "vout": 1}], [{address_node0: 9.9999}]
        )
        signed_raw_tx = self.nodes[0].signrawtransactionwithwallet(raw_tx)
        assert_equal(signed_raw_tx["complete"], True)

        # Try and spend new collateral from mempool
        assert_raises_rpc_error(
            -26,
            "collateral-locked-in-mempool, tried to spend collateral of non-created mn or token",
            self.nodes[0].sendrawtransaction,
            signed_raw_tx["hex"],
        )

        # Mint transaction
        self.nodes[0].generate(1)

        # Check token collateral address
        result = self.nodes[0].gettoken("BTC")["1"]
        assert_equal(result["collateralAddress"], address_node0)

        # Try and spend collateral
        assert_raises_rpc_error(
            -26,
            "bad-txns-collateral-locked, tried to spend locked collateral",
            self.nodes[0].sendrawtransaction,
            signed_raw_tx["hex"],
        )

        # Transfer back to node one
        new_address = self.nodes[1].getnewaddress()
        transfer_tx = self.nodes[0].updatetoken(
            "BTC",
            {
                "collateralAddress": new_address,
            },
        )
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check token collateral address
        result = self.nodes[0].gettoken("BTC")["1"]
        assert_equal(result["collateralAddress"], new_address)

        # Now spend freed up collateral
        self.nodes[0].sendrawtransaction(signed_raw_tx["hex"])

        # Create TX spending new collateral
        raw_tx = self.nodes[1].createrawtransaction(
            [{"txid": transfer_tx, "vout": 1}], [{address_node0: 9.9999}]
        )
        signed_raw_tx = self.nodes[1].signrawtransactionwithwallet(raw_tx)
        assert_equal(signed_raw_tx["complete"], True)

        # Test spending of new collateral
        assert_raises_rpc_error(
            -26,
            "bad-txns-collateral-locked, tried to spend locked collateral",
            self.nodes[0].sendrawtransaction,
            signed_raw_tx["hex"],
        )


if __name__ == "__main__":
    TokenTransferTest().main()

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test custom pool reward."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from test_framework.authproxy import JSONRPCException

class TokensCustomPoolReward(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-clarkequayheight=50'],
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-clarkequayheight=50']]

    def run_test(self):
        self.nodes[0].generate(102)
        self.sync_blocks()
        num_tokens = len(self.nodes[0].listtokens())

        # collateral addresses
        collateral_a = self.nodes[0].getnewaddress("", "legacy")
        collateral_b = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Create token
        self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "silver",
            "collateralAddress": collateral_b
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_a = idx
            if (token["symbol"] == "SILVER"):
                token_b = idx

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 2)

        # Mint some tokens
        self.nodes[0].minttokens(["100000@" + token_a])
        self.nodes[0].minttokens(["100000@" + token_b])
        self.nodes[0].generate(1)

        # Create pool collateral address
        pool_collateral = self.nodes[0].getnewaddress("", "legacy")

        # Fail on zero amount token reward
        try:
            self.nodes[0].createpoolpair({
            "tokenA": "GOLD#" + token_a,
            "tokenB": "SILVER#" + token_b,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOLD",
            "customRewards": ["0@" + token_a]
        })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)
        errorString = ""

        # Fail on token that does not exist
        try:
            self.nodes[0].createpoolpair({
            "tokenA": "GOLD#" + token_a,
            "tokenB": "SILVER#" + token_b,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOLD",
            "customRewards": ["1@100"]
        })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("reward token 100 does not exist" in errorString)
        errorString = ""

        # Create pool with token rewards
        poolpair_tx = self.nodes[0].createpoolpair({
            "tokenA": "GOLD#" + token_a,
            "tokenB": "SILVER#" + token_b,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOLD",
            "customRewards": ["1@" + token_a, "1@" + token_b]
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check that customReards shows up in getcustomtx
        result = self.nodes[0].getcustomtx(poolpair_tx)
        assert_equal(result['results']['customRewards'], ["1.00000000@" + token_a, "1.00000000@" + token_b])

        # custom rewards should not show up yet without balance
        assert('customRewards' not in self.nodes[0].getpoolpair(1)['1'])

        # Create and fund liquidity provider
        provider = self.nodes[1].getnewaddress("", "legacy")

        # Add pool liqudity
        self.nodes[0].addpoolliquidity({
            "*": ["1@" + token_a, "1@" + token_b]
        }, provider)
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Provide pool reward for token a
        self.nodes[0].accounttoaccount(collateral_a, {pool_collateral: "1@" + token_a})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # token a reward should be added to provider
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '0.99999000@GOLD#128'])

        # Make sure amount reduced from token a
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['0.00001000@GOLD#128'])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # token a amount should be static as not enough funds to pay
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '0.99999000@GOLD#128'])

        # Provide pool reward for token b
        self.nodes[0].accounttoaccount(collateral_b, {pool_collateral: "1@" + token_b})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # token b reward should be added to provider but token a same as before
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '0.99999000@GOLD#128', '0.99999000@SILVER#129'])

        # Make sure amount reduced from token a
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['0.00001000@GOLD#128', '0.00001000@SILVER#129'])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # token a and b amount should be static as not enough funds to pay
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '0.99999000@GOLD#128', '0.99999000@SILVER#129'])

        # Fund addresses to avoid auto auth to send raw via node 1 easily
        self.nodes[0].sendtoaddress(collateral_a, 1)
        self.nodes[0].sendtoaddress(collateral_b, 1)
        self.nodes[0].generate(1)

        # Provide pool reward for token a and b
        tx1 = self.nodes[0].accounttoaccount(collateral_a, {pool_collateral: "10@" + token_a})
        tx2 = self.nodes[0].accounttoaccount(collateral_b, {pool_collateral: "10@" + token_b})

        # Send raw on second node to make sure that it is present in mempool
        rawtx1 = self.nodes[0].getrawtransaction(tx1)
        rawtx2 = self.nodes[0].getrawtransaction(tx2)
        self.nodes[1].sendrawtransaction(rawtx1)
        self.nodes[1].sendrawtransaction(rawtx2)

        # Generate block and trigger potential payouts
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Both token a and b should be present
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@128', '1.00000000@129'])
        assert_equal(self.nodes[0].getpoolpair(1)['1']['customRewards'], ['1.00000000@128', '1.00000000@129'])

        # Generate 9 more blocks and trigger potential payouts
        self.nodes[1].generate(9)
        self.sync_blocks()

        # token a and b rewards should be added to provider
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '10.99989000@GOLD#128', '10.99989000@SILVER#129'])

        # Make sure amount reduced from token a
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['0.00011000@GOLD#128', '0.00011000@SILVER#129'])

        # custom rewards should not show up yet without balance
        assert('customRewards' not in self.nodes[0].getpoolpair(1)['1'])

        # Top up of token a to test poolpair output
        self.nodes[0].accounttoaccount(collateral_a, {pool_collateral: "10@" + token_a})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check only token a reward shows
        assert_equal(self.nodes[0].getpoolpair(1)['1']['customRewards'], ['1.00000000@128'])

        # Generate another block and trigger potential payout
        self.nodes[0].generate(9)
        self.sync_blocks()

        # custom rewards should not show up yet without balance
        assert('customRewards' not in self.nodes[0].getpoolpair(1)['1'])

        # Top up of token b to test poolpair output
        self.nodes[0].accounttoaccount(collateral_b, {pool_collateral: "10@" + token_b})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check only token a reward shows
        assert_equal(self.nodes[0].getpoolpair(1)['1']['customRewards'], ['1.00000000@129'])

        # Generate another block and trigger potential payout
        self.nodes[0].generate(9)
        self.sync_blocks()

        # custom rewards should not show up yet without balance
        assert('customRewards' not in self.nodes[0].getpoolpair(1)['1'])

        # Create new token for reward
        num_tokens = len(self.nodes[0].listtokens())
        collateral_c = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createtoken({
            "symbol": "BRONZE",
            "name": "bronze",
            "collateralAddress": collateral_c
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "BRONZE"):
                token_c = idx

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Mint some tokens
        self.nodes[0].minttokens(["100000@" + token_c])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Top up of token c to test poolpair output before adding it to he pool
        self.nodes[0].accounttoaccount(collateral_c, {pool_collateral: "10@" + token_c})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Fail on zero amount token reward
        try:
            self.nodes[0].updatepoolpair({"pool": "1", "customRewards": ["0@" + token_c]})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)
        errorString = ""

        # Update poolpair with invalid reward token
        try:
            self.nodes[0].updatepoolpair({"pool": "1", "customRewards": ["1@100"]})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("reward token 100 does not exist" in errorString)
        errorString = ""

        # Replace tokens with token c
        updatepoolpair_tx = self.nodes[0].updatepoolpair({"pool": "1", "customRewards": ["1@" + token_c]})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check that customReards shows up in getcustomtx
        result = self.nodes[0].getcustomtx(updatepoolpair_tx)
        assert_equal(result['results']['customRewards'], ["1.00000000@" + token_c])

        # Check for new block reward and payout
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@130'])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '20.99979000@GOLD#128', '20.99979000@SILVER#129', '0.99999000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['0.00021000@GOLD#128', '0.00021000@SILVER#129', '9.00001000@BRONZE#130'])

        # Provide pool reward for token a, should not show as it wasd removed
        self.nodes[0].accounttoaccount(collateral_a, {pool_collateral: "10@" + token_a})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check for new block reward and payout
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@130'])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '20.99979000@GOLD#128', '20.99979000@SILVER#129', '1.99998000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['10.00021000@GOLD#128', '0.00021000@SILVER#129', '8.00002000@BRONZE#130'])

        # Add back token a
        updatepoolpair_tx = self.nodes[0].updatepoolpair({"pool": "1", "customRewards": ["1@" + token_a, "1@" + token_c]})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check that customReards shows up in getcustomtx
        result = self.nodes[0].getcustomtx(updatepoolpair_tx)
        assert_equal(result['results']['customRewards'], ["1.00000000@" + token_a, "1.00000000@" + token_c])

        # Check for new block reward and payout for token a and c
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@128', '1.00000000@130'])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '21.99978000@GOLD#128', '20.99979000@SILVER#129', '2.99997000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['9.00022000@GOLD#128', '0.00021000@SILVER#129', '7.00003000@BRONZE#130'])

        # Provide pool reward for token b, should not show as it was removed
        self.nodes[0].accounttoaccount(collateral_b, {pool_collateral: "10@" + token_b})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check for new block reward and payout
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@128', '1.00000000@130'])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '22.99977000@GOLD#128', '20.99979000@SILVER#129', '3.99996000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['8.00023000@GOLD#128', '10.00021000@SILVER#129', '6.00004000@BRONZE#130'])

        # Add back token b
        updatepoolpair_tx = self.nodes[0].updatepoolpair({"pool": "1", "customRewards": ["1@" + token_a, "1@" + token_b, "1@" + token_c]})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check that customReards shows up in getcustomtx
        result = self.nodes[0].getcustomtx(updatepoolpair_tx)
        assert_equal(result['results']['customRewards'], ["1.00000000@" + token_a, "1.00000000@" + token_b, "1.00000000@" + token_c])

        # Check for new block reward and payout for token a, b and c
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@128', '1.00000000@129', '1.00000000@130'])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '23.99976000@GOLD#128', '21.99978000@SILVER#129', '4.99995000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['7.00024000@GOLD#128', '9.00022000@SILVER#129', '5.00005000@BRONZE#130'])

        # Do nothing
        updatepoolpair_tx = self.nodes[0].updatepoolpair({"pool": "1"})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check that customReards shows up in getcustomtx
        result = self.nodes[0].getcustomtx(updatepoolpair_tx)
        assert('customRewards' not in result['results'])

        # Check for new block reward and payout for token a, b and c
        assert_equal(self.nodes[1].getpoolpair(1)['1']['customRewards'], ['1.00000000@128', '1.00000000@129', '1.00000000@130'])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '24.99975000@GOLD#128', '22.99977000@SILVER#129', '5.99994000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['6.00025000@GOLD#128', '8.00023000@SILVER#129', '4.00006000@BRONZE#130'])

        # Wipe all rewards
        updatepoolpair_tx = self.nodes[0].updatepoolpair({"pool": "1", "customRewards": []})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check TX shows rewards present and empty and balances should be unchanged
        result = self.nodes[0].getcustomtx(updatepoolpair_tx)
        assert_equal(result['results']['customRewards'], [])
        assert_equal(self.nodes[1].getaccount(provider), ['0.99999000@SILVGOLD', '24.99975000@GOLD#128', '22.99977000@SILVER#129', '5.99994000@BRONZE#130'])
        assert_equal(self.nodes[1].getaccount(pool_collateral), ['6.00025000@GOLD#128', '8.00023000@SILVER#129', '4.00006000@BRONZE#130'])

if __name__ == '__main__':
    TokensCustomPoolReward().main()

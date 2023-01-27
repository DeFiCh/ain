#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test auth single input and return change to auth sender"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal

class TokensAuthChange(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txindex=1', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50']]

    # Move all coins to new address and change address to test auto auth
    def clear_auth_utxos(self):
        non_auth_address = self.nodes[0].getnewaddress("", "legacy")
        balance = self.nodes[0].getbalance()
        self.nodes[0].sendtoaddress(non_auth_address, balance - Decimal("0.1")) # 0.1 to cover fee
        self.nodes[0].generate(1, 1000000, non_auth_address)

    # Check output/input count and addresses are expected
    def check_auto_auth_txs(self, tx, owner, outputs = 2):
        # Get auto auth TXs
        final_rawtx = self.nodes[0].getrawtransaction(tx, 1)
        auth_tx = self.nodes[0].getrawtransaction(final_rawtx['vin'][0]['txid'], 1)

        # Auth TX outputs all belong to auth address
        assert_equal(auth_tx['vout'][1]['scriptPubKey']['addresses'][0], owner)
        decTx = self.nodes[0].getrawtransaction(tx)
        customTx = self.nodes[0].decodecustomtx(decTx)
        vouts = 2
        if customTx['type'] == 'ResignMasternode':
            vouts = 3
        assert_equal(len(auth_tx['vout']), vouts)

        # Two outputs, single input and change to auth address on final TX
        assert_equal(final_rawtx['vout'][1]['scriptPubKey']['addresses'][0], owner)
        assert_equal(len(final_rawtx['vout']), outputs)
        assert_equal(len(final_rawtx['vin']), 1)

    @DefiTestFramework.rollback
    def run_test(self):
        coinbase = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].generate(101, 1000000, coinbase)
        num_tokens = len(self.nodes[0].listtokens())

        # collateral address
        collateral_a = self.nodes[0].getnewaddress("", "legacy")

        # Create foundation token
        create_tx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "isDAT": False,
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_a = idx

        # Make sure token updated as expected
        result = self.nodes[0].gettoken(token_a)[token_a]
        assert_equal(result['symbol'], "GOLD")
        assert_equal(result['isDAT'], False)
        assert_equal(token_a, "128")

        # Mint some tokens
        mint_tx = self.nodes[0].minttokens(["300@" + token_a])
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure 300 tokens were minted
        assert_equal(self.nodes[0].gettoken(token_a)[token_a]['minted'], 300)

        # Check auto auth TX
        self.check_auto_auth_txs(mint_tx, collateral_a)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Update token
        updatetx = self.nodes[0].updatetoken(token_a, {"symbol":"SILVER"})
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure token updated as expected
        result = self.nodes[0].gettoken(token_a)[token_a]
        assert_equal(result['symbol'], "SILVER")

        # Check auto auth TX
        self.check_auto_auth_txs(updatetx, collateral_a)

        # Create masternode
        num_mns = len(self.nodes[0].listmasternodes())
        collateral_mn = self.nodes[0].getnewaddress("", "legacy")
        mn_tx = self.nodes[0].createmasternode(collateral_mn)
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure new MN was successfully created
        assert_equal(len(self.nodes[0].listmasternodes()), num_mns + 1)

        # Test resign MN call
        resign_tx = self.nodes[0].resignmasternode(mn_tx)
        self.nodes[0].generate(1, 1000000, coinbase)

        # Check MN in PRE_RESIGNED state
        assert_equal(self.nodes[0].listmasternodes()[mn_tx]['state'], "PRE_RESIGNED")

        # Check auto auth TX
        self.check_auto_auth_txs(resign_tx, collateral_mn)

        # Test pair calls
        num_tokens = len(self.nodes[0].listtokens())

        # collateral address
        collateral_b = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_b
        })
        self.nodes[0].generate(1, 1000000, coinbase)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_b = idx

        # Mint some tokens for use later
        self.nodes[0].minttokens(["300@" + token_b])
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Create pool pair
        pool_collateral = self.nodes[0].getnewaddress("", "legacy")
        poolpair_tx = self.nodes[0].createpoolpair({
            "tokenA": "SILVER#" + token_a,
            "tokenB": "GOLD#" + token_b,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOLD"
        })
        self.nodes[0].generate(1, 1000000, coinbase)

        # Change to pool collateral address
        final_rawtx = self.nodes[0].getrawtransaction(poolpair_tx, 1)
        assert_equal(final_rawtx['vout'][1]['scriptPubKey']['addresses'][0], self.nodes[0].PRIV_KEYS[0].ownerAuthAddress)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Test account to account TX
        accounttoaccount_tx = self.nodes[0].accounttoaccount(collateral_b, {collateral_a: "100@" + token_b})
        self.nodes[0].generate(1, 1000000, coinbase)

        # Check auto auth TX
        self.check_auto_auth_txs(accounttoaccount_tx, collateral_b)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Test add pool liquidity TX
        pool_share = self.nodes[0].getnewaddress("", "legacy")
        liquidity_tx = self.nodes[0].addpoolliquidity({
            collateral_a: ['100@' + token_a, '100@' + token_b]
            }, pool_share)
        self.nodes[0].generate(1, 1000000, coinbase)

        # Check auto auth TX
        self.check_auto_auth_txs(liquidity_tx, collateral_a)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Test pool swap TX
        poolswap_tx = self.nodes[0].poolswap({
            "from": collateral_a,
            "tokenFrom": token_a,
            "amountFrom": 1,
            "to": collateral_b,
            "tokenTo": token_b,
            "maxPrice": 2
        })
        self.nodes[0].generate(1)

        # Check auto auth TX
        self.check_auto_auth_txs(poolswap_tx, collateral_a)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Test remove liquidity TX
        remove_liquidity_tx = self.nodes[0].removepoolliquidity(pool_share, "25@1")
        self.nodes[0].generate(1, 1000000, coinbase)

        # Check auto auth TX
        self.check_auto_auth_txs(remove_liquidity_tx, pool_share)

        # Test pool update TX
        poolpair_update_tx = self.nodes[0].updatepoolpair({
            "pool": poolpair_tx,
            "commission": 0.1
        })
        self.nodes[0].generate(1, 1000000, coinbase)

        # Check auto auth TX
        self.check_auto_auth_txs(poolpair_update_tx, self.nodes[0].PRIV_KEYS[0].ownerAuthAddress)

        # Test account to UTXOs TX
        self.nodes[0].utxostoaccount({collateral_a: "1@0"})
        self.nodes[0].generate(1, 1000000, coinbase)
        accountoutxos_tx = self.nodes[0].accounttoutxos(collateral_a, {collateral_b: "1@0"})
        self.nodes[0].generate(1, 1000000, coinbase)

        # Check auto auth TX
        self.check_auto_auth_txs(accountoutxos_tx, collateral_a, 3)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Test setgox TX
        setgov_tx = self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35})
        self.nodes[0].generate(1)

        # Check auto auth TX
        self.check_auto_auth_txs(setgov_tx, self.nodes[0].PRIV_KEYS[0].ownerAuthAddress)

        # Test send tokens to address TX
        self.nodes[0].utxostoaccount({collateral_a: "1@0"})
        self.nodes[0].generate(1)

        tokenstoaddress_tx = self.nodes[0].sendtokenstoaddress({collateral_a:"1@0"}, {collateral_b:"1@0"})
        self.nodes[0].generate(1)

        # Check auto auth TX
        self.check_auto_auth_txs(tokenstoaddress_tx, collateral_a)

        # Test pair calls
        num_tokens = len(self.nodes[0].listtokens())

        # Create foundation token
        create_tx = self.nodes[0].createtoken({
            "symbol": "BRONZE",
            "isDAT": True,
            "collateralAddress": self.nodes[0].PRIV_KEYS[0].ownerAuthAddress
        })
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "BRONZE"):
                token_c = idx

        # Make sure token updated as expected
        result = self.nodes[0].gettoken(token_c)[token_c]
        assert_equal(result['symbol'], "BRONZE")
        assert_equal(result['isDAT'], True)
        assert_equal(token_c, "2")

        # Check auto auth TX
        final_rawtx = self.nodes[0].getrawtransaction(create_tx, 1)
        assert_equal(final_rawtx['vout'][2]['scriptPubKey']['addresses'][0], self.nodes[0].PRIV_KEYS[0].ownerAuthAddress)

        # Clear auth UTXOs
        self.clear_auth_utxos()

        # Update DAT token
        updatetx = self.nodes[0].updatetoken(token_c, {"symbol":"COPPER"})
        self.nodes[0].generate(1, 1000000, coinbase)

        # Make sure token updated as expected
        result = self.nodes[0].gettoken(token_c)[token_c]
        assert_equal(result['symbol'], "COPPER")

        # Check auto auth TX
        self.check_auto_auth_txs(updatetx, self.nodes[0].PRIV_KEYS[0].ownerAuthAddress)

if __name__ == '__main__':
    TokensAuthChange().main()

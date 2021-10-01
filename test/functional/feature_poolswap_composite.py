#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test poolpair composite swap RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    disconnect_nodes,
)

from decimal import Decimal

class PoolPairCompositeTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=106', '-bayfrontgardensheight=107', '-dakotaheight=108', '-eunosheight=109', '-fortcanningheight=110'],
            ['-txnotokens=0', '-amkheight=1', '-bayfrontheight=106', '-bayfrontgardensheight=107', '-dakotaheight=108', '-eunosheight=109', '-fortcanningheight=110']]

    def run_test(self):

        # Create tokens
        collateral = self.nodes[0].get_genesis_keys().ownerAuthAddress
        tokens = [
            {
                "wallet": self.nodes[0],
                "symbol": "dUSD",
                "name": "DFI USD",
                "collateralAddress": collateral,
                "amount": 1000000
            },
            {
                "wallet": self.nodes[0],
                "symbol": "DOGE",
                "name": "Dogecoin",
                "collateralAddress": collateral,
                "amount": 1000000
            },
            {
                "wallet": self.nodes[0],
                "symbol": "TSLA",
                "name": "Tesla",
                "collateralAddress": collateral,
                "amount": 1000000
            },
            {
                "wallet": self.nodes[0],
                "symbol": "LTC",
                "name": "Litecoin",
                "collateralAddress": collateral,
                "amount": 1000000
            },
            {
                "wallet": self.nodes[0],
                "symbol": "USDC",
                "name": "USD Coin",
                "collateralAddress": collateral,
                "amount": 1000000
            },
        ]
        self.setup_tokens(tokens)
        disconnect_nodes(self.nodes[0], 1)

        symbolDOGE = "DOGE#" + self.get_id_token("DOGE")
        symbolTSLA = "TSLA#" + self.get_id_token("TSLA")
        symbolDUSD = "dUSD#" + self.get_id_token("dUSD")
        symbolLTC = "LTC#" + self.get_id_token("LTC")
        symbolUSDC = "USDC#" + self.get_id_token("USDC")

        idDOGE = list(self.nodes[0].gettoken(symbolDOGE).keys())[0]
        idTSLA = list(self.nodes[0].gettoken(symbolTSLA).keys())[0]
        idLTC = list(self.nodes[0].gettoken(symbolLTC).keys())[0]

        coin = 100000000

        # Creating poolpairs
        owner = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].createpoolpair({
            "tokenA": symbolDOGE,
            "tokenB": "DFI",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolTSLA,
            "tokenB": symbolDUSD,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolLTC,
            "tokenB": "DFI",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolDOGE,
            "tokenB": symbolUSDC,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolLTC,
            "tokenB": symbolUSDC,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        # Tokenise DFI
        self.nodes[0].utxostoaccount({collateral: "900@0"})
        self.nodes[0].generate(1)

        # Set up addresses for swapping
        source = self.nodes[0].getnewaddress("", "legacy")
        destination = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(collateral, {source: "100@" + symbolLTC})
        self.nodes[0].generate(1)

        # Try a swap before liquidity added
        ltc_to_doge_from = 10
        try:
            self.nodes[0].compositeswap({
                "from": source,
                "tokenFrom": symbolLTC,
                "amountFrom": ltc_to_doge_from,
                "to": destination,
                "tokenTo": symbolDOGE,
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('"LTC-DFI":"Lack of liquidity."' in errorString)
        assert('"LTC-USDC":"Lack of liquidity."' in errorString)

        # Add pool liquidity
        self.nodes[0].addpoolliquidity({
            collateral: ["1000@" + symbolDOGE, "200@DFI"]
        }, collateral, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            collateral: ["100@" + symbolTSLA, "30000@" + symbolDUSD]
        }, collateral, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            collateral: ["100@" + symbolLTC, "500@DFI"]
        }, collateral, [])
        self.nodes[0].generate(1)

        self.nodes[0].compositeswap({
            "from": source,
            "tokenFrom": symbolLTC,
            "amountFrom": ltc_to_doge_from,
            "to": destination,
            "tokenTo": symbolDOGE,
        }, [])
        self.nodes[0].generate(1)

        # Check source
        source_balance = self.nodes[0].getaccount(source, {}, True)
        assert_equal(source_balance[idLTC], Decimal('90.00000000'))
        assert_equal(len(source_balance), 1)

        # Check destination
        dest_balance = self.nodes[0].getaccount(destination, {}, True)
        doge_received = dest_balance[idDOGE]
        ltc_per_doge = round((ltc_to_doge_from * coin) / (doge_received * coin), 8)
        assert_equal(dest_balance[idDOGE], doge_received)
        assert_equal(len(dest_balance), 1)

        # Reset swap and try again with max price as expected
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        self.nodes[0].compositeswap({
            "from": source,
            "tokenFrom": symbolLTC,
            "amountFrom": ltc_to_doge_from,
            "to": destination,
            "tokenTo": symbolDOGE,
            "maxPrice": ltc_per_doge
        }, [])
        self.nodes[0].generate(1)

        # Check source
        source_balance = self.nodes[0].getaccount(source, {}, True)
        assert_equal(source_balance[idLTC], Decimal('90.00000000'))
        assert_equal(len(source_balance), 1)

        # Check destination
        dest_balance = self.nodes[0].getaccount(destination, {}, True)
        assert_equal(dest_balance[idDOGE], doge_received)
        assert_equal(len(dest_balance), 1)

        # Reset swap and try again with max price as expected less one Satoshi
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()

        try:
            self.nodes[0].compositeswap({
                "from": source,
                "tokenFrom": symbolLTC,
                "amountFrom": ltc_to_doge_from,
                "to": destination,
                "tokenTo": symbolDOGE,
                "maxPrice": ltc_per_doge - Decimal('0.00000001'),
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('"DOGE-DFI":"Price is higher than indicated."' in errorString)
        assert('"LTC-USDC":"Lack of liquidity."' in errorString)

        # Add better route for swap with double amount
        self.nodes[0].addpoolliquidity({
            collateral: ["100@" + symbolLTC, "500@" + symbolUSDC]
        }, collateral, [])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            collateral: ["2000@" + symbolDOGE, "200@" + symbolUSDC]
        }, collateral, [])
        self.nodes[0].generate(1)

        self.nodes[0].compositeswap({
            "from": source,
            "tokenFrom": symbolLTC,
            "amountFrom": ltc_to_doge_from,
            "to": destination,
            "tokenTo": symbolDOGE,
            "maxPrice": ltc_per_doge
        }, [])
        self.nodes[0].generate(1)

        # Check source
        source_balance = self.nodes[0].getaccount(source, {}, True)
        assert_equal(source_balance[idLTC], Decimal('90.00000000'))
        assert_equal(len(source_balance), 1)

        # Check destination
        dest_balance = self.nodes[0].getaccount(destination, {}, True)
        assert_equal(dest_balance[idDOGE], doge_received * 2)
        assert_equal(len(dest_balance), 1)

        # Set up addresses for swapping
        source = self.nodes[0].getnewaddress("", "legacy")
        destination = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(collateral, {source: "10@" + symbolTSLA})
        self.nodes[0].generate(1)

        # Let's move from TSLA to LTC
        tsla_to_ltc_from = 1
        errorString = ""
        try:
            self.nodes[0].compositeswap({
                "from": source,
                "tokenFrom": symbolTSLA,
                "amountFrom": tsla_to_ltc_from,
                "to": destination,
                "tokenTo": symbolLTC
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('Cannot find usable pool pair.' in errorString)

        # Let's add a pool to bridge TSLA-DUSD and LTC-DFI
        self.nodes[0].createpoolpair({
            "tokenA": symbolDUSD,
            "tokenB": "DFI",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        # Now swap TSLA to
        errorString = ""
        try:
            self.nodes[0].compositeswap({
                "from": source,
                "tokenFrom": symbolTSLA,
                "amountFrom": tsla_to_ltc_from,
                "to": destination,
                "tokenTo": symbolLTC
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('"dUSD-DFI":"Lack of liquidity."' in errorString)

        # Add some liquidity
        self.nodes[0].addpoolliquidity({
            collateral: ["1000@" + symbolDUSD, "200@" + "DFI"]
        }, collateral, [])
        self.nodes[0].generate(1)

        # Test max price
        try:
            self.nodes[0].compositeswap({
                "from": source,
                "tokenFrom": symbolTSLA,
                "amountFrom": tsla_to_ltc_from,
                "to": destination,
                "tokenTo": symbolLTC,
                "maxPrice": "0.15311841"
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('"LTC-DFI":"Price is higher than indicated."' in errorString)

        self.nodes[0].compositeswap({
            "from": source,
            "tokenFrom": symbolTSLA,
            "amountFrom": tsla_to_ltc_from,
            "to": destination,
            "tokenTo": symbolLTC,
            "maxPrice": "0.15311842"
        }, [])
        self.nodes[0].generate(1)

        # Check source
        source_balance = self.nodes[0].getaccount(source, {}, True)
        assert_equal(source_balance[idTSLA], Decimal('9.00000000'))
        assert_equal(len(source_balance), 1)

        # Check destination
        dest_balance = self.nodes[0].getaccount(destination, {}, True)
        assert_equal(dest_balance[idLTC], Decimal('6.53089259'))
        assert_equal(len(dest_balance), 1)

        # Add another route to TSLA
        self.nodes[0].createpoolpair({
            "tokenA": symbolDUSD,
            "tokenB": symbolUSDC,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner
        }, [])
        self.nodes[0].generate(1)

        # Add some liquidity
        self.nodes[0].addpoolliquidity({
            collateral: ["1000@" + symbolDUSD, "1000@" + symbolUSDC]
        }, collateral, [])
        self.nodes[0].generate(1)

        # Set up addresses for swapping
        source = self.nodes[0].getnewaddress("", "legacy")
        destination = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(collateral, {source: "10@" + symbolTSLA})
        self.nodes[0].generate(1)

        # Test max price
        try:
            self.nodes[0].compositeswap({
                "from": source,
                "tokenFrom": symbolTSLA,
                "amountFrom": tsla_to_ltc_from,
                "to": destination,
                "tokenTo": symbolLTC,
                "maxPrice": "0.03361577"
            }, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert('"LTC-DFI":"Price is higher than indicated."' in errorString)
        assert('"LTC-USDC":"Price is higher than indicated."' in errorString)

        self.nodes[0].compositeswap({
            "from": source,
            "tokenFrom": symbolTSLA,
            "amountFrom": tsla_to_ltc_from,
            "to": destination,
            "tokenTo": symbolLTC,
            "maxPrice": "0.03361578"
            }, [])
        self.nodes[0].generate(1)

        # Check source
        source_balance = self.nodes[0].getaccount(source, {}, True)
        assert_equal(source_balance[idTSLA], Decimal('9.00000000'))
        assert_equal(len(source_balance), 1)

        # Check destination
        dest_balance = self.nodes[0].getaccount(destination, {}, True)
        assert_equal(dest_balance[idLTC], Decimal('29.74793123'))
        assert_equal(len(dest_balance), 1)

if __name__ == '__main__':
    PoolPairCompositeTest().main()

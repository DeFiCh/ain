#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test ICX Orderbook."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

from decimal import Decimal


class ICXOrderbookErrorTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-txindex=1']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        # Burn address
        burn_address = "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"

        print("Generating initial chain...")
        self.nodes[0].generate(25)
        self.sync_blocks()
        self.nodes[1].generate(101)
        self.sync_blocks()

        self.nodes[1].createtoken({
            "symbol": "BTC",
            "name": "BTC DFI token",
            "collateralAddress": self.nodes[1].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[1].generate(1)
        self.sync_blocks()

        symbolDFI = "DFI"
        symbolBTC = "BTC#" + self.get_id_token("BTC")

        self.nodes[1].minttokens("2@" + symbolBTC)

        self.nodes[1].generate(1)
        self.sync_blocks()

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        accountDFI = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountBTC = self.nodes[1].get_genesis_keys().ownerAuthAddress

        self.nodes[0].utxostoaccount({accountDFI: "101@" + symbolDFI})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # initialDFI = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]
        # initialBTC = self.nodes[1].getaccount(accountBTC, {}, True)[idBTC]
        # print("Initial DFI:", initialDFI, ", id", idDFI)
        # print("Initial BTC:", initialBTC, ", id", idBTC)

        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        # transfer DFI
        self.nodes[1].accounttoaccount(accountBTC, {accountDFI: "1@" + symbolBTC})
        self.nodes[1].generate(1)
        self.sync_blocks()

        # create pool
        self.nodes[0].createpoolpair({
            "tokenA": symbolDFI,
            "tokenB": symbolBTC,
            "commission": 1,
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DFIBTC",
        }, [])
        self.nodes[0].generate(1)

        # check tokens id
        pool = self.nodes[0].getpoolpair("DFIBTC")
        idDFIBTC = list(self.nodes[0].gettoken("DFIBTC").keys())[0]
        assert(pool[idDFIBTC]['idTokenA'] == idDFI)
        assert(pool[idDFIBTC]['idTokenB'] == idBTC)

        # transfer
        self.nodes[0].addpoolliquidity({
            accountDFI: ["100@" + symbolDFI, "1@" + symbolBTC]
        }, accountDFI, [])
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ICX_TAKERFEE_PER_BTC":Decimal('0.001')})

        self.nodes[0].generate(1)

        result = self.nodes[0].getgov("ICX_TAKERFEE_PER_BTC")

        assert_equal(result["ICX_TAKERFEE_PER_BTC"], Decimal('0.001'))

        # unexistant token for create order
        try:
            self.nodes[0].icx_createorder({
                                    'tokenFrom': "DOGE",
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 1,
                                    'orderPrice':0.01})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Token DOGE does not exist!" in errorString)

        # wrong chain for create order
        try:
            self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "LTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 1,
                                    'orderPrice':0.01})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid parameters, argument \"chainTo\" must be \"BTC\" if \"tokenFrom\" specified" in errorString)

        # wrong address for DFI for create order
        try:
            self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountBTC,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 1,
                                    'orderPrice':0.01})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Address ("+accountBTC+") is not owned by the wallet" in errorString)

        # invalid receivePubkey for create order
        try:
            self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '000000000000000000000000000000000000000000000000000000000000000000',
                                    'amountFrom': 1,
                                    'orderPrice':0.01})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid public key:" in errorString)

        # not enough balance od DFI for create order
        try:
            self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 1500,
                                    'orderPrice':0.01})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Not enough balance for Token DFI on address "+accountDFI in errorString)

        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 1,
                                    'orderPrice':0.01})["result"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        # unexistent orderTx
        try:
            self.nodes[1].icx_makeoffer({
                                    'orderTx': "76432beb2a667efe4858b4e1ec93979b621c51c76abaab2434892655dd152e3d",
                                    'amount': 0.10,
                                    'ownerAddress': accountBTC})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("orderTx (76432beb2a667efe4858b4e1ec93979b621c51c76abaab2434892655dd152e3d) does not exist" in errorString)

        # invalid ownerAddress
        try:
            self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.01,
                                    'ownerAddress': accountDFI})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Address ("+accountDFI+") is not owned by the wallet" in errorString)

        # not enough DFI for takerFee
        try:
            self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.01,
                                    'ownerAddress': accountBTC})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("amount 0.00000000 is less than 0.00100000" in errorString)

        self.nodes[1].utxostoaccount({accountBTC: "0.001@" + symbolDFI})
        self.nodes[1].generate(1)

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.01,
                                    'ownerAddress': accountBTC})["result"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        # ext htlc cannot be first htlc
        try:
            self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.01,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("offer ("+ offerTx + ") needs to have dfc htlc submitted first, but no dfc htlc found!" in errorString)

        # not enough DFI for takerFee
        try:
            self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 1,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'timeout': 500})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("amount 0.00000000 is less than 0.00100000" in errorString)

        self.nodes[0].utxostoaccount({accountDFI: "0.001@" + symbolDFI})
        self.nodes[0].generate(1)

        # wrong amount
        try:
            self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 2,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'timeout': 500})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("cannot make dfc htlc with that amount, amount must be lower or equal the amount in offer" in errorString)

        # wrong timeout
        try:
            self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 1,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'timeout': 499})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid parameters, argument \"timeout\" must be greater than 499" in errorString)

        dfchtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 1,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'timeout': 500})["result"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        # less amount
        try:
            self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.001,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("cannot make ext htlc with that amount, amount must be equal to calculated dfchtlc amount - 0.00100000 != 0.01000000" in errorString)

        # more amount
        try:
            self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.1,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("cannot make ext htlc with that amount, amount must be equal to calculated dfchtlc amount - 0.10000000 != 0.01000000" in errorString)

        # different hash
        try:
            self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.01,
                                    'hash': '957fc0fd643f605b2938e0000000029fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid hash, external htlc hash is different than dfc htlc hash" in errorString)

        # timeout less than minimum
        try:
            self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.01,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 10})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid parameters, argument \"timeout\" must be greater than 14" in errorString)

        # timeout more than expiration of dfc htlc
        try:
            self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.01,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 30})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid parameters, argument \"timeout\" must be less than expiration period of 1st htlc" in errorString)

        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.01,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})["result"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        # wrong dfchtlcTx
        try:
            self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': "76432beb2a667efe4858b4e1ec93979b621c51c76abaab2434892655dd152e3d",
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("dfchtlcTx (76432beb2a667efe4858b4e1ec93979b621c51c76abaab2434892655dd152e3d) does not exist" in errorString)

        # wrong seed
        try:
            self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfchtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab7000000000d4523a9b534571e4e92e0c4610c6a6784ccef'})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("hash generated from given seed is different than in dfc htlc" in errorString)

        claimTx = self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfchtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})["result"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        # Make sure offer and order are now closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders()
        assert_equal(len(offer), 1)

if __name__ == '__main__':
    ICXOrderbookErrorTest().main()
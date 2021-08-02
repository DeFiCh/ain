#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test ICX Orderbook."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

from decimal import Decimal

class ICXOrderbookTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-eunospayaheight=50', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=50', '-eunospayaheight=50', '-txindex=1']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        # Burn address
        burn_address = "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"

        print("Generating initial chain...")
        self.nodes[0].generate(25)
        self.sync_blocks()
        self.nodes[1].generate(300)
        self.sync_blocks()

        self.nodes[1].createtoken({
            "symbol": "BTC",
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": self.nodes[1].get_genesis_keys().ownerAuthAddress
        })

        self.nodes[1].generate(1)
        self.sync_blocks()

        symbolDFI = "DFI"
        symbolBTC = "BTC"

        self.nodes[1].minttokens("100@" + symbolBTC)

        self.nodes[1].generate(1)
        self.sync_blocks()

        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        accountDFI = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountBTC = self.nodes[1].get_genesis_keys().ownerAuthAddress

        self.nodes[0].utxostoaccount({accountDFI: "500@" + symbolDFI})
        self.nodes[1].utxostoaccount({accountBTC: "10@" + symbolDFI})
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
            "tokenA": idBTC,
            "tokenB": idDFI,
            "commission": 1,
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "BTC-DFI",
        }, [])
        self.nodes[0].generate(1)

        # check tokens id
        pool = self.nodes[0].getpoolpair("BTC-DFI")
        idDFIBTC = list(self.nodes[0].gettoken("BTC-DFI").keys())[0]
        assert(pool[idDFIBTC]['idTokenA'] == idBTC)
        assert(pool[idDFIBTC]['idTokenB'] == idDFI)

        # transfer
        self.nodes[0].addpoolliquidity({
            accountDFI: ["1@" + symbolBTC, "100@" + symbolDFI]
        }, accountDFI, [])
        self.nodes[0].generate(1)

        assert_equal(len(self.nodes[0].getaccount(accountDFI, {}, True)), 2)

        self.nodes[0].setgov({"ICX_TAKERFEE_PER_BTC":Decimal('0.001')})

        self.nodes[0].generate(1)

        result = self.nodes[0].getgov("ICX_TAKERFEE_PER_BTC")

        assert_equal(result["ICX_TAKERFEE_PER_BTC"], Decimal('0.001'))

        # DFI/BTC Open and close an order
        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        order = self.nodes[0].icx_getorder(orderTx)

        assert_equal(order[orderTx]["status"], "OPEN")
        assert_equal(order[orderTx]["type"], "INTERNAL")
        assert_equal(order[orderTx]["tokenFrom"], symbolDFI)
        assert_equal(order[orderTx]["chainTo"], "BTC")
        assert_equal(order[orderTx]["ownerAddress"], accountDFI)
        assert_equal(order[orderTx]["receivePubkey"], '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941')
        assert_equal(order[orderTx]["amountFrom"], Decimal('15'))
        assert_equal(order[orderTx]["amountToFill"], Decimal('15'))
        assert_equal(order[orderTx]["orderPrice"], Decimal('0.01000000'))
        assert_equal(order[orderTx]["amountToFillInToAsset"], Decimal('0.1500000'))
        assert_equal(order[orderTx]["expireHeight"], self.nodes[0].getblockchaininfo()["blocks"] + 2880)

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.10,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.01000000'))

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 2)

        offer = self.nodes[0].icx_getorder(offerTx)

        assert_equal(offer[offerTx]["status"], "OPEN")

        # Close offer
        closeOrder = self.nodes[1].icx_closeoffer(offerTx)["txid"]
        rawCloseOrder = self.nodes[1].getrawtransaction(closeOrder, 1)
        authTx = self.nodes[1].getrawtransaction(rawCloseOrder['vin'][0]['txid'], 1)
        found = False
        for vout in authTx['vout']:
            if 'addresses' in vout['scriptPubKey'] and vout['scriptPubKey']['addresses'][0] == accountBTC:
                found = True
        assert(found)

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer)

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 1)

        offer = self.nodes[0].icx_getorder(offerTx)

        assert_equal(offer[offerTx]["status"], "CLOSED")

        # Check order exist
        order = self.nodes[0].icx_listorders()
        assert_equal(len(order), 2)

        # Close order
        closeOrder = self.nodes[0].icx_closeorder(orderTx)["txid"]
        rawCloseOrder = self.nodes[0].getrawtransaction(closeOrder, 1)
        authTx = self.nodes[0].getrawtransaction(rawCloseOrder['vin'][0]['txid'], 1)
        found = False
        for vout in authTx['vout']:
            if 'addresses' in vout['scriptPubKey'] and vout['scriptPubKey']['addresses'][0] == accountDFI:
                found = True
        assert(found)

        self.nodes[0].generate(1)
        self.sync_blocks()

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 1)

        order = self.nodes[0].icx_getorder(orderTx)

        assert_equal(order[orderTx]["status"], "CLOSED")
        assert_equal(order[orderTx]["type"], "INTERNAL")

        # BTC/DFI Open and close an order
        orderTx = self.nodes[0].icx_createorder({
                                    'chainFrom': "BTC",
                                    'tokenTo': idDFI,
                                    'ownerAddress': accountDFI,
                                    'amountFrom': 2,
                                    'orderPrice':100})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 10,
                                    'ownerAddress': accountBTC,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.01000000'))

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 2)

        # Close offer
        closeOrder = self.nodes[1].icx_closeoffer(offerTx)["txid"]
        rawCloseOrder = self.nodes[1].getrawtransaction(closeOrder, 1)
        authTx = self.nodes[1].getrawtransaction(rawCloseOrder['vin'][0]['txid'], 1)
        found = False
        for vout in authTx['vout']:
            if 'addresses' in vout['scriptPubKey'] and vout['scriptPubKey']['addresses'][0] == accountBTC:
                found = True
        assert(found)

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer)

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 1)

        # Check order exist
        order = self.nodes[0].icx_listorders()
        assert_equal(len(order), 2)

        # Close order
        closeOrder = self.nodes[0].icx_closeorder(orderTx)["txid"]
        rawCloseOrder = self.nodes[0].getrawtransaction(closeOrder, 1)
        authTx = self.nodes[0].getrawtransaction(rawCloseOrder['vin'][0]['txid'], 1)
        found = False
        for vout in authTx['vout']:
            if 'addresses' in vout['scriptPubKey'] and vout['scriptPubKey']['addresses'][0] == accountDFI:
                found = True
        assert(found)

        self.nodes[0].generate(1)
        self.sync_blocks()

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 1)


        # DFI/BTC scenario
        # Open an order

        beforeOrder = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]

        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeOrder - Decimal('15.00000000'))

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 2)
        assert_equal(order[orderTx]["tokenFrom"], symbolDFI)
        assert_equal(order[orderTx]["chainTo"], "BTC")
        assert_equal(order[orderTx]["ownerAddress"], accountDFI)
        assert_equal(order[orderTx]["receivePubkey"], '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941')
        assert_equal(order[orderTx]["amountFrom"], Decimal('15'))
        assert_equal(order[orderTx]["amountToFill"], Decimal('15'))
        assert_equal(order[orderTx]["orderPrice"], Decimal('0.01000000'))
        assert_equal(order[orderTx]["amountToFillInToAsset"], Decimal('0.1500000'))
        assert_equal(order[orderTx]["expireHeight"], self.nodes[0].getblockchaininfo()["blocks"] + 2880)

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.10,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.01000000'))

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 2)
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.10000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.01000000'))
        assert_equal(offer[offerTx]["expireHeight"], self.nodes[0].getblockchaininfo()["blocks"] + 20)

        beforeDFCHTLC = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]

        dfchtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 10,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220'})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeDFCHTLC - Decimal('0.01000000'))

        # Check burn
        assert_equal(self.nodes[0].getburninfo()['tokens'][0], "0.02000000@DFI")
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['type'], 'ICXSubmitDFCHTLC')
        assert_equal(result[0]['amounts'][0], '0.02000000@DFI')

        htlcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(htlcs), 2)
        assert_equal(htlcs[dfchtlcTx]["type"], 'DFC')
        assert_equal(htlcs[dfchtlcTx]["status"], 'OPEN')
        assert_equal(htlcs[dfchtlcTx]["offerTx"], offerTx)
        assert_equal(htlcs[dfchtlcTx]["amount"], Decimal('10.00000000'))
        assert_equal(htlcs[dfchtlcTx]["amountInEXTAsset"], Decimal('0.10000000'))
        assert_equal(htlcs[dfchtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(htlcs[dfchtlcTx]["timeout"], 1440)

        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.10,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 24})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        htlcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(htlcs), 3)
        assert_equal(htlcs[exthtlcTx]["type"], 'EXTERNAL')
        assert_equal(htlcs[exthtlcTx]["status"], 'OPEN')
        assert_equal(htlcs[exthtlcTx]["offerTx"], offerTx)
        assert_equal(htlcs[exthtlcTx]["amount"], Decimal('0.10000000'))
        assert_equal(htlcs[exthtlcTx]["amountInDFCAsset"], Decimal('10.00000000'))
        assert_equal(htlcs[exthtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(htlcs[exthtlcTx]["htlcScriptAddress"], '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N')
        assert_equal(htlcs[exthtlcTx]["ownerPubkey"], '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252')
        assert_equal(htlcs[exthtlcTx]["timeout"], 24)

        beforeClaim0 = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]
        beforeClaim1 = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        claimTx = self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfchtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})

        assert_equal(len(htlcs), 4)
        assert_equal(htlcs[claimTx]["type"], 'CLAIM DFC')
        assert_equal(htlcs[claimTx]["dfchtlcTx"], dfchtlcTx)
        assert_equal(htlcs[claimTx]["seed"], 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef')

        assert_equal(htlcs[dfchtlcTx]["status"], 'CLAIMED')

        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeClaim0 + Decimal('0.01250000'))
        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeClaim1 + Decimal('10.00000000'))

        # Make sure offer is closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 1)

        # Verify closed offer
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx, "closed": True})
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.10000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.01000000'))

        # Check partial order remaining
        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 2)
        assert_equal(order[orderTx]["tokenFrom"], symbolDFI)
        assert_equal(order[orderTx]["chainTo"], "BTC")
        assert_equal(order[orderTx]["ownerAddress"], accountDFI)
        assert_equal(order[orderTx]["receivePubkey"], '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941')
        assert_equal(order[orderTx]["amountFrom"], Decimal('15'))
        assert_equal(order[orderTx]["amountToFill"], Decimal('5'))
        assert_equal(order[orderTx]["orderPrice"], Decimal('0.01000000'))
        assert_equal(order[orderTx]["amountToFillInToAsset"], Decimal('0.0500000'))


        # Make offer for more than is available to test partial fill
        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.10,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 2)
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.10000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.01000000'))
        assert_equal(offer[offerTx]["expireHeight"], self.nodes[0].getblockchaininfo()["blocks"] + 20)


        dfchtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 5,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220'})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check burn
        assert_equal(self.nodes[0].getburninfo()['tokens'][0], "0.03000000@DFI")
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['type'], 'ICXSubmitDFCHTLC')
        assert_equal(result[0]['amounts'][0], '0.01000000@DFI')

        htlcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(htlcs), 2)
        assert_equal(htlcs[dfchtlcTx]["type"], 'DFC')
        assert_equal(htlcs[dfchtlcTx]["status"], 'OPEN')
        assert_equal(htlcs[dfchtlcTx]["offerTx"], offerTx)
        assert_equal(htlcs[dfchtlcTx]["amount"], Decimal('5.00000000'))
        assert_equal(htlcs[dfchtlcTx]["amountInEXTAsset"], Decimal('0.05000000'))
        assert_equal(htlcs[dfchtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(htlcs[dfchtlcTx]["timeout"], 1440)

        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.05,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 24})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        htlcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(htlcs), 3)
        assert_equal(htlcs[exthtlcTx]["type"], 'EXTERNAL')
        assert_equal(htlcs[exthtlcTx]["status"], 'OPEN')
        assert_equal(htlcs[exthtlcTx]["offerTx"], offerTx)
        assert_equal(htlcs[exthtlcTx]["amount"], Decimal('0.05000000'))
        assert_equal(htlcs[exthtlcTx]["amountInDFCAsset"], Decimal('5.00000000'))
        assert_equal(htlcs[exthtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(htlcs[exthtlcTx]["htlcScriptAddress"], '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N')
        assert_equal(htlcs[exthtlcTx]["ownerPubkey"], '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252')
        assert_equal(htlcs[exthtlcTx]["timeout"], 24)

        beforeClaim0 = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]
        beforeClaim1 = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        claimTx = self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfchtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})

        assert_equal(len(htlcs), 4)
        assert_equal(htlcs[claimTx]["type"], 'CLAIM DFC')
        assert_equal(htlcs[claimTx]["dfchtlcTx"], dfchtlcTx)
        assert_equal(htlcs[claimTx]["seed"], 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef')

        assert_equal(htlcs[dfchtlcTx]["status"], 'CLAIMED')

        # makerIncentive
        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeClaim0 + Decimal('0.00625000'))
        # claimed DFI on taker address
        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeClaim1 + Decimal('5.00000000'))

        # Make sure offer and order are now closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders()
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders({"closed": True})
        assert_equal(order[orderTx]["status"], 'FILLED')

        # --------------------------------------------------------------------------------
        # BTC/DFI scenario
        self.nodes[1].utxostoaccount({accountBTC: "3001@" + symbolDFI})
        self.nodes[1].generate(1)
        self.sync_blocks()

        # Open an order
        orderTx = self.nodes[0].icx_createorder({
                                    'chainFrom': "BTC",
                                    'tokenTo': idDFI,
                                    'ownerAddress': accountDFI,
                                    'amountFrom': 2,
                                    'orderPrice':1000})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 2)
        assert_equal(order[orderTx]["chainFrom"], "BTC")
        assert_equal(order[orderTx]["tokenTo"], symbolDFI)
        assert_equal(order[orderTx]["ownerAddress"], accountDFI)
        assert_equal(order[orderTx]["amountFrom"], Decimal('2'))
        assert_equal(order[orderTx]["amountToFill"], Decimal('2'))
        assert_equal(order[orderTx]["orderPrice"], Decimal('1000.00000000'))
        assert_equal(order[orderTx]["amountToFillInToAsset"], Decimal('2000.00000000'))
        assert_equal(order[orderTx]["expireHeight"], self.nodes[0].getblockchaininfo()["blocks"] + 2880)

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 3000,
                                    'ownerAddress': accountBTC,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.30000000'))

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 2)
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('3000.00000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["receivePubkey"], '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941')
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.30000000'))
        assert_equal(offer[offerTx]["expireHeight"], self.nodes[0].getblockchaininfo()["blocks"] + 20)


        exthtlcTx = self.nodes[0].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 2,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 72})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.20000000'))

        # Check burn
        assert_equal(self.nodes[0].getburninfo()['tokens'][0], "0.43000000@DFI")
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['type'], 'ICXSubmitEXTHTLC')
        assert_equal(result[0]['amounts'][0], '0.40000000@DFI')

        htlcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(htlcs), 2)
        assert_equal(htlcs[exthtlcTx]["type"], 'EXTERNAL')
        assert_equal(htlcs[exthtlcTx]["status"], 'OPEN')
        assert_equal(htlcs[exthtlcTx]["offerTx"], offerTx)
        assert_equal(htlcs[exthtlcTx]["amount"], Decimal('2.00000000'))
        assert_equal(htlcs[exthtlcTx]["amountInDFCAsset"], Decimal('2000.00000000'))
        assert_equal(htlcs[exthtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(htlcs[exthtlcTx]["htlcScriptAddress"], '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N')
        assert_equal(htlcs[exthtlcTx]["ownerPubkey"], '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252')
        assert_equal(htlcs[exthtlcTx]["timeout"], 72)

        dfchtlcTx = self.nodes[1].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 2000,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        htlcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(htlcs), 3)
        assert_equal(htlcs[dfchtlcTx]["type"], 'DFC')
        assert_equal(htlcs[dfchtlcTx]["status"], 'OPEN')
        assert_equal(htlcs[dfchtlcTx]["offerTx"], offerTx)
        assert_equal(htlcs[dfchtlcTx]["amount"], Decimal('2000.00000000'))
        assert_equal(htlcs[dfchtlcTx]["amountInEXTAsset"], Decimal('2.00000000'))
        assert_equal(htlcs[dfchtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(htlcs[dfchtlcTx]["timeout"], 480)


        beforeClaim = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]
        claimTx = self.nodes[0].icx_claimdfchtlc({
                                    'dfchtlcTx': dfchtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})

        assert_equal(len(htlcs), 4)
        assert_equal(htlcs[claimTx]["type"], 'CLAIM DFC')
        assert_equal(htlcs[claimTx]["dfchtlcTx"], dfchtlcTx)
        assert_equal(htlcs[claimTx]["seed"], 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef')

        assert_equal(htlcs[dfchtlcTx]["status"], 'CLAIMED')

        # claimed DFI + refunded makerDeposit + makerIncentive on maker address
        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeClaim + Decimal('2000.00000000') + Decimal('0.25000000'))

        # Make sure offer and order are now closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders()
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders({"closed": True})
        assert_equal(order[orderTx]["status"], 'FILLED')


        # DFI/BTC partial offer acceptance
        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 1,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.10000000'))

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('1.00000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.10000000'))

        dfchtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 15,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220'})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.01500000'))

        # Check burn
        assert_equal(self.nodes[0].getburninfo()['tokens'][0], "0.46000000@DFI")
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['type'], 'ICXSubmitDFCHTLC')
        assert_equal(result[0]['amounts'][0], '0.03000000@DFI')

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.01500000'))


        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.15,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 24})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        claimTx = self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfchtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        # Make sure offer and order are now closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders()
        assert_equal(len(offer), 1)
        order = self.nodes[0].icx_listorders({"closed": True})
        assert_equal(order[orderTx]["status"], 'FILLED')

        # DFI/BTC scenario expiration test
        # Open an order

        beforeOrderDFI0 = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]
        beforeOrderDFI1 = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        orderTxDFI = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTxDFI,
                                    'amount': 0.1,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.01000000'))

        offer = self.nodes[0].icx_listorders({"orderTx": orderTxDFI})
        assert_equal(len(offer), 2)

        self.nodes[1].generate(20)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer)

        offer = self.nodes[0].icx_listorders({"orderTx": orderTxDFI})
        assert_equal(len(offer), 1)

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTxDFI,
                                    'amount': 0.1,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        beforeDFCHTLC = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]

        dfchtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 10,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220'})["txid"]
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeDFCHTLC - Decimal('0.01000000'))


        self.nodes[0].generate(100)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeDFCHTLC)

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})
        offer = self.nodes[0].icx_listorders({
                                    "orderTx": orderTxDFI,
                                    "closed": True})

        assert_equal(len(htlcs), 2)
        assert_equal(htlcs[dfchtlcTx]["status"], 'EXPIRED')
        assert_equal(offer[offerTx]["status"], 'EXPIRED')

        # BTC/DFI scenario expiration test
        # Open an order
        orderTxBTC = self.nodes[0].icx_createorder({
                                    'chainFrom': "BTC",
                                    'tokenTo': idDFI,
                                    'ownerAddress': accountDFI,
                                    'amountFrom': 1,
                                    'orderPrice':10})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        beforeOffer = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTxBTC,
                                    'amount': 10,
                                    'ownerAddress': accountBTC,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941'})["txid"]


        self.nodes[1].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer - Decimal('0.10000000'))


        offer = self.nodes[0].icx_listorders({"orderTx": orderTxBTC})
        assert_equal(len(offer), 2)

        self.nodes[1].generate(20)
        self.sync_blocks()

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOffer)


        offer = self.nodes[0].icx_listorders({"orderTx": orderTxBTC})
        assert_equal(len(offer), 1)


        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTxBTC,
                                    'amount': 10,
                                    'ownerAddress': accountBTC,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941'})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        beforeEXTHTLC = self.nodes[0].getaccount(accountDFI, {}, True)[idDFI]

        exthtlcTx = self.nodes[0].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 1,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 72})["txid"]

        self.nodes[0].generate(1)

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx})
        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeEXTHTLC - Decimal('0.10000000'))


        self.nodes[0].generate(100)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeEXTHTLC)

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})
        offer = self.nodes[0].icx_listorders({
                                    "orderTx": orderTxBTC,
                                    "closed": True})

        assert_equal(len(htlcs), 2)
        assert_equal(htlcs[exthtlcTx]["status"], 'EXPIRED')
        assert_equal(offer[offerTx]["status"], 'EXPIRED')


        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})["txid"]

        self.nodes[0].generate(1)
        self.sync_blocks()

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.1,
                                    'ownerAddress': accountBTC})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        dfchtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 10,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220'})["txid"]
        self.nodes[0].generate(1)
        self.sync_blocks()

        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.1,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 24})["txid"]

        self.nodes[1].generate(1)
        self.sync_blocks()

        self.nodes[0].generate(2880)
        self.sync_blocks()

        # everything back to beforeOrderDFI0 balance minus one makerDeposit (0.01) for last order
        assert_equal(self.nodes[0].getaccount(accountDFI, {}, True)[idDFI], beforeOrderDFI0 - Decimal('0.01000000'))
        # everything back to beforeOrderDFI1 balance minus one 3x takerFees (0.01 - 0.1 - 0.01)
        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeOrderDFI1 - Decimal('0.12000000'))

        order = self.nodes[0].icx_listorders({"closed": True})

        assert_equal(order[orderTxDFI]["status"], 'EXPIRED')
        assert_equal(order[orderTxBTC]["status"], 'EXPIRED')
        assert_equal(order[orderTx]["status"], 'EXPIRED')

        htlcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})
        assert_equal(htlcs[dfchtlcTx]["status"], 'REFUNDED')

if __name__ == '__main__':
    ICXOrderbookTest().main()

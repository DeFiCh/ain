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

        assert_equal(len(self.nodes[0].getaccount(accountDFI, {}, True)), 2)


        self.nodes[0].generate(1)

        result = self.nodes[0].setgov({"ICX_TAKERFEE_PER_BTC":Decimal('0.001')})

        self.nodes[0].generate(1)


        # Open and close an order
        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})

        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check order exist
        order = self.nodes[0].icx_listorders()
        assert_equal(len(order), 1)

        # Close order
        closeOrder = self.nodes[0].icx_closeorder(orderTx)
        rawCloseOrder = self.nodes[0].getrawtransaction(closeOrder, 1)
        authTx = self.nodes[0].getrawtransaction(rawCloseOrder['vin'][0]['txid'], 1)
        found = False
        for vout in authTx['vout']:
            if 'addresses' in vout['scriptPubKey'] and vout['scriptPubKey']['addresses'][0] == accountDFI:
                found = True

        self.nodes[0].generate(1)
        self.sync_blocks()

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 0)

        # Open and close an order
        orderTx = self.nodes[0].icx_createorder({
                                    'tokenFrom': idDFI,
                                    'chainTo': "BTC",
                                    'ownerAddress': accountDFI,
                                    'receivePubkey': '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941',
                                    'amountFrom': 15,
                                    'orderPrice':0.01})

        self.nodes[0].generate(1)
        self.sync_blocks()

        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 1)
        assert_equal(order[orderTx]["tokenFrom"], symbolDFI)
        assert_equal(order[orderTx]["chainTo"], "BTC")
        assert_equal(order[orderTx]["ownerAddress"], accountDFI)
        assert_equal(order[orderTx]["receivePubkey"], '037f9563f30c609b19fd435a19b8bde7d6db703012ba1aba72e9f42a87366d1941')
        assert_equal(order[orderTx]["amountFrom"], Decimal('15'))
        assert_equal(order[orderTx]["amountToFill"], Decimal('15'))
        assert_equal(order[orderTx]["orderPrice"], Decimal('0.01000000'))
        assert_equal(order[orderTx]["amountToFillInToAsset"], Decimal('0.1500000'))

        offerTx = self.nodes[1].icx_makeoffer({
                                    'orderTx': orderTx,
                                    'amount': 0.10,
                                    'ownerAddress': accountBTC})

        self.nodes[1].generate(1)
        self.sync_blocks()

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})

        assert_equal(len(offer), 1)
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.10000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.01000000'))

        dfhtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 10,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'timeout': 500})

        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check burn
        assert_equal(self.nodes[0].getburninfo()['tokens'][0], "0.02000000@DFI")
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['type'], 'ICXSubmitDFCHTLC')
        assert_equal(result[0]['amounts'][0], '0.02000000@DFI')

        hltcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(hltcs), 1)
        assert_equal(hltcs[dfhtlcTx]["type"], 'DFC')
        assert_equal(hltcs[dfhtlcTx]["status"], 'OPEN')
        assert_equal(hltcs[dfhtlcTx]["offerTx"], offerTx)
        assert_equal(hltcs[dfhtlcTx]["amount"], Decimal('10.00000000'))
        assert_equal(hltcs[dfhtlcTx]["amountInEXTAsset"], Decimal('0.10000000'))
        assert_equal(hltcs[dfhtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(hltcs[dfhtlcTx]["timeout"], 500)

        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.10,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})

        self.nodes[1].generate(1)
        self.sync_blocks()

        hltcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(hltcs), 2)
        assert_equal(hltcs[exthtlcTx]["type"], 'EXTERNAL')
        assert_equal(hltcs[exthtlcTx]["status"], 'OPEN')
        assert_equal(hltcs[exthtlcTx]["offerTx"], offerTx)
        assert_equal(hltcs[exthtlcTx]["amount"], Decimal('0.10000000'))
        assert_equal(hltcs[exthtlcTx]["amountInDFCAsset"], Decimal('10.00000000'))
        assert_equal(hltcs[exthtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(hltcs[exthtlcTx]["htlcScriptAddress"], '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N')
        assert_equal(hltcs[exthtlcTx]["ownerPubkey"], '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252')
        assert_equal(hltcs[exthtlcTx]["timeout"], 15)

        beforeClaim = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        claimTx = self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfhtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})

        self.nodes[1].generate(1)
        self.sync_blocks()

        hltcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})

        assert_equal(len(hltcs), 3)
        assert_equal(hltcs[claimTx]["type"], 'CLAIM DFC')
        assert_equal(hltcs[claimTx]["dfchtlcTx"], dfhtlcTx)
        assert_equal(hltcs[claimTx]["seed"], 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef')

        assert_equal(hltcs[dfhtlcTx]["status"], 'CLAIMED')

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeClaim + Decimal(10.00000000))

        # Make sure offer is closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 0)

        # Verify closed offer
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx, "closed": True})
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.10000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.01000000'))

        # Check partial order remaining
        order = self.nodes[0].icx_listorders()

        assert_equal(len(order), 1)
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
                                    'ownerAddress': accountBTC})

        self.nodes[1].generate(1)
        self.sync_blocks()

        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 1)
        assert_equal(offer[offerTx]["orderTx"], orderTx)
        assert_equal(offer[offerTx]["amount"], Decimal('0.05000000'))
        assert_equal(offer[offerTx]["ownerAddress"], accountBTC)
        assert_equal(offer[offerTx]["takerFee"], Decimal('0.00500000'))

        dfhtlcTx = self.nodes[0].icx_submitdfchtlc({
                                    'offerTx': offerTx,
                                    'amount': 5,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'timeout': 500})

        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check burn
        assert_equal(self.nodes[0].getburninfo()['tokens'][0], "0.03000000@DFI")
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['type'], 'ICXSubmitDFCHTLC')
        assert_equal(result[0]['amounts'][0], '0.01000000@DFI')

        hltcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(hltcs), 1)
        assert_equal(hltcs[dfhtlcTx]["type"], 'DFC')
        assert_equal(hltcs[dfhtlcTx]["status"], 'OPEN')
        assert_equal(hltcs[dfhtlcTx]["offerTx"], offerTx)
        assert_equal(hltcs[dfhtlcTx]["amount"], Decimal('5.00000000'))
        assert_equal(hltcs[dfhtlcTx]["amountInEXTAsset"], Decimal('0.05000000'))
        assert_equal(hltcs[dfhtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(hltcs[dfhtlcTx]["timeout"], 500)

        exthtlcTx = self.nodes[1].icx_submitexthtlc({
                                    'offerTx': offerTx,
                                    'amount': 0.05,
                                    'hash': '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220',
                                    'htlcScriptAddress': '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N',
                                    'ownerPubkey': '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252',
                                    'timeout': 15})

        self.nodes[1].generate(1)
        self.sync_blocks()

        hltcs = self.nodes[0].icx_listhtlcs({"offerTx": offerTx})

        assert_equal(len(hltcs), 2)
        assert_equal(hltcs[exthtlcTx]["type"], 'EXTERNAL')
        assert_equal(hltcs[exthtlcTx]["status"], 'OPEN')
        assert_equal(hltcs[exthtlcTx]["offerTx"], offerTx)
        assert_equal(hltcs[exthtlcTx]["amount"], Decimal('0.05000000'))
        assert_equal(hltcs[exthtlcTx]["amountInDFCAsset"], Decimal('5.00000000'))
        assert_equal(hltcs[exthtlcTx]["hash"], '957fc0fd643f605b2938e0631a61529fd70bd35b2162a21d978c41e5241a5220')
        assert_equal(hltcs[exthtlcTx]["htlcScriptAddress"], '13sJQ9wBWh8ssihHUgAaCmNWJbBAG5Hr9N')
        assert_equal(hltcs[exthtlcTx]["ownerPubkey"], '036494e7c9467c8c7ff3bf29e841907fb0fa24241866569944ea422479ec0e6252')
        assert_equal(hltcs[exthtlcTx]["timeout"], 15)

        beforeClaim = self.nodes[1].getaccount(accountBTC, {}, True)[idDFI]

        claimTx = self.nodes[1].icx_claimdfchtlc({
                                    'dfchtlcTx': dfhtlcTx,
                                    'seed': 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef'})

        self.nodes[1].generate(1)
        self.sync_blocks()

        hltcs = self.nodes[0].icx_listhtlcs({
                                    "offerTx": offerTx,
                                    "closed": True})

        assert_equal(len(hltcs), 3)
        assert_equal(hltcs[claimTx]["type"], 'CLAIM DFC')
        assert_equal(hltcs[claimTx]["dfchtlcTx"], dfhtlcTx)
        assert_equal(hltcs[claimTx]["seed"], 'f75a61ad8f7a6e0ab701d5be1f5d4523a9b534571e4e92e0c4610c6a6784ccef')

        assert_equal(hltcs[dfhtlcTx]["status"], 'CLAIMED')

        assert_equal(self.nodes[1].getaccount(accountBTC, {}, True)[idDFI], beforeClaim + Decimal(5.00000000))

        # Make sure offer and order are now closed
        offer = self.nodes[0].icx_listorders({"orderTx": orderTx})
        assert_equal(len(offer), 0)
        order = self.nodes[0].icx_listorders()
        assert_equal(len(offer), 0)

if __name__ == '__main__':
    ICXOrderbookTest().main()
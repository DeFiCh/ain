#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Loan - loan basics."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from decimal import Decimal

class ConsortiumTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=1', '-fortcanningheight=50', '-eunosheight=50', '-fortcanninghillheight=50', '-fortcanningparkheight=50', '-fortcanningroadheight=50', '-fortcanningcrunchheight=50', '-fortcanningspringheight=50', '-fortcanninggreatworldheight=250', '-grandcentralheight=254', '-regtest-minttoken-simulate-mainnet=1', '-txindex=1']]

    def run_test(self):

        print("Generating initial chain...")
        self.nodes[0].generate(100)
        self.sync_blocks()

        account0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        account1 = self.nodes[1].getnewaddress("", "legacy")
        account2 = self.nodes[2].get_genesis_keys().ownerAuthAddress
        account3 = self.nodes[3].get_genesis_keys().ownerAuthAddress

        self.nodes[1].generate(20)
        self.sync_blocks()

        self.nodes[0].sendtoaddress(account1, 10)
        self.nodes[0].sendtoaddress(account2, 10)
        self.nodes[0].sendtoaddress(account3, 10)

        self.nodes[0].generate(1)
        self.sync_blocks()

        symbolBTC = "BTC"
        symbolDOGE = "DOGE"

        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": account0
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].createtoken({
            "symbol": symbolDOGE,
            "name": "DOGE token",
            "isDAT": True,
            "collateralAddress": account3
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        idDOGE = list(self.nodes[0].gettoken(symbolDOGE).keys())[0]

        assert_raises_rpc_error(-32600, "called before GrandCentral height", self.nodes[0].burntokens, {
            'amounts': "1@" + symbolBTC,
            'from': account0,
        })

        self.nodes[0].generate(254 - self.nodes[0].getblockcount())
        self.sync_blocks()

        # DAT owner can mint
        self.nodes[3].minttokens(["1@" + symbolDOGE])
        self.nodes[3].generate(1)
        self.sync_blocks()

        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolBTC])
        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolDOGE])
        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[3].minttokens, ["1@" + symbolBTC])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/consortium' : 'true'}})

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolBTC])
        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolDOGE])
        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[3].minttokens, ["1@" + symbolBTC])

        # Set global mint limits
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/consortium/' + idBTC + '/mint_limit': '0',
                                             'v0/consortium/' + idBTC + '/mint_limit_daily': '0'}})
        self.nodes[0].generate(1)

        # Verify mint_limit set
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes['v0/consortium/' + idBTC + '/mint_limit'], '0')
        assert_equal(attributes['v0/consortium/' + idBTC + '/mint_limit_daily'], '0')

        # Set global mint limits
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idBTC + '/mint_limit' : '10',
                                            'v0/consortium/' + idBTC + '/mint_limit_daily' : '10'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Verify mint_limit set
        attributes = self.nodes[0].getgov("ATTRIBUTES")["ATTRIBUTES"]
        assert_equal(attributes['v0/consortium/' + idBTC + '/mint_limit'], '10')
        assert_equal(attributes['v0/consortium/' + idBTC + '/mint_limit_daily'], '10')

        # Test setting member mint limit hight than global mint
        assert_raises_rpc_error(-32600, "Mint limit higher than global mint limit", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/consortium/' + idBTC + '/members' : {"01":{"name":"account2BTC",
                                                                                            "ownerAddress": account2,
                                                                                            "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                            "mintLimitDaily":10.00000000,
                                                                                            "mintLimit":11.00000000}}}})

        # Test setting member mint limit hight than global mint
        assert_raises_rpc_error(-32600, "Daily mint limit higher than daily global mint limit", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/consortium/' + idBTC + '/members' : {"01":{"name":"account2BTC",
                                                                                            "ownerAddress": account2,
                                                                                            "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                            "mintLimitDaily":11.00000000,
                                                                                            "mintLimit":10.00000000}}}})

        # Set consortium members
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idBTC + '/members' : {"01":{"name":"account2BTC",
                                                                                            "ownerAddress": account2,
                                                                                            "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                            "mintLimitDaily":10.00000000,
                                                                                            "mintLimit":10.00000000},
                                                                                      "02":{"name":"account3BTC",
                                                                                            "ownerAddress": account3,
                                                                                            "backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5",
                                                                                            "mintLimitDaily":4.00000000,
                                                                                            "mintLimit":4.00000000}}}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_raises_rpc_error(-32600, "Cannot add a member with an owner address of a existing consortium member", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/consortium/' + idBTC + '/members' : {"03":{"name":"test",
                                                                                                                                                                "ownerAddress": account2,
                                                                                                                                                                "backingId":"7cb2f6954291d81d2270c9a6a52442b3f8c637b1ec793c731cb5f5a8f7fb9b9d",
                                                                                                                                                                "mintLimitDaily":10.00000000,
                                                                                                                                                                "mintLimit":10.00000000}}}})

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/consortium/' + idBTC + '/members'], {"01":{"name":"account2BTC","ownerAddress": account2,"backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf","mintLimit": Decimal('10.00000000'),"mintLimitDaily":Decimal('10.00000000'),"status":0},"02":{"name":"account3BTC","ownerAddress": account3,"backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5","mintLimit":Decimal('4.00000000'),"mintLimitDaily":Decimal('4.00000000'),"status":0}})
        assert_equal(attribs['v0/consortium/' + idBTC + '/mint_limit'], '10')

        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        self.nodes[2].minttokens(["1@" + symbolBTC])
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2)[0], '1.00000000@' + symbolBTC)
        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('1.00000000'))

        # Set global mint limits
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idDOGE + '/mint_limit' : '6', 'v0/consortium/' + idDOGE + '/mint_limit_daily' : '6'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Create consortium members
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idDOGE + '/members' : {"01":{"name":"account2DOGE",
                                                                                                   "ownerAddress": account2,
                                                                                                   "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                                   "mintLimitDaily":5.00000000,
                                                                                                   "mintLimit":5.00000000}}}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idDOGE + '/members' : {"02":{"name":"account1DOGE",
                                                                                                   "ownerAddress": account1,
                                                                                                   "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                                   "mintLimitDaily":5.00000000,
                                                                                                   "mintLimit":5.00000000}}}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idDOGE + '/mint_limit' : '6', 'v0/consortium/' + idDOGE + '/mint_limit_daily' : '6'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/consortium/' + idBTC + '/members'], {"01":{"name":"account2BTC","ownerAddress": account2,"backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf","mintLimit":Decimal('10.00000000'),"mintLimitDaily":Decimal('10.00000000'),"status":0},"02":{"name":"account3BTC","ownerAddress": account3,"backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5","mintLimit":Decimal('4.00000000'),"mintLimitDaily":Decimal('4.00000000'),"status":0}})
        assert_equal(attribs['v0/consortium/' + idBTC + '/mint_limit'], '10')

        assert_equal(attribs['v0/consortium/' + idDOGE + '/members'], {"01":{"name":"account2DOGE","ownerAddress": account2,"backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf","mintLimit":Decimal('5.00000000'),"mintLimitDaily":Decimal('5.00000000'),"status":0},"02":{"name":"account1DOGE","ownerAddress": account1,"backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf","mintLimit":Decimal('5.00000000'),"mintLimitDaily":Decimal('5.00000000'),"status":0}})
        assert_equal(attribs['v0/consortium/' + idDOGE + '/mint_limit'], '6')
        assert_equal(attribs['v0/consortium/' + idDOGE + '/mint_limit_daily'], '6')

        assert_raises_rpc_error(-32600, "You are not a foundation or consortium member and cannot mint this token", self.nodes[3].minttokens, ["1@" + symbolDOGE])

        self.nodes[2].minttokens(["2@" + symbolDOGE])
        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2), ['1.00000000@' + symbolBTC, '2.00000000@' + symbolDOGE])

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/1.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('2.00000000'))

        assert_raises_rpc_error(-32600, "You will exceed your maximum mint limit for " + symbolDOGE + " token by minting this amount!", self.nodes[2].minttokens, ["3.00000001@" + symbolDOGE])

        self.nodes[2].burntokens({
            'amounts': "1@" + symbolDOGE,
            'from': account2,
        })

        self.nodes[2].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[2].getaccount(account2), ['1.00000000@' + symbolBTC, '1.00000000@' + symbolDOGE])

        attribs = self.nodes[2].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/1.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('1.00000000'))

        self.nodes[2].accounttoaccount(account2, {account0: "1@" + symbolDOGE})
        self.nodes[2].accounttoaccount(account2, {account1: "0.2@" + symbolBTC})
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[0].burntokens({
            'amounts': "0.5@" + symbolDOGE,
            'from': account0,
            'context': account2
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(account2), ['0.80000000@' + symbolBTC])
        assert_equal(self.nodes[0].getaccount(account0), ['0.50000000@' + symbolDOGE])

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/1.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('0.50000000'))

        self.nodes[0].burntokens({
            'amounts': "0.5@" + symbolDOGE,
            'from': account0,
        })

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(account0), [])

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/1.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('0.50000000'))

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idDOGE + '/members' : {"01":{"name":"account2DOGE",
                                                                                                   "ownerAddress": account2,
                                                                                                   "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                                   "mintLimit":5.00000000,
                                                                                                   "mintLimitDaily":5.00000000,
                                                                                                   "status":1}}}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/consortium/' + idDOGE + '/members'], {"01":{"name":"account2DOGE","ownerAddress": account2,"backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf","mintLimit":Decimal('5.00000000'),"mintLimitDaily":Decimal('5.00000000'),"status":1},"02":{"name":"account1DOGE","ownerAddress": account1,"backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf","mintLimit":Decimal('5.00000000'),"mintLimitDaily":Decimal('5.00000000'),"status":0}})
        assert_equal(self.nodes[0].getburninfo(), {'address': 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG', 'amount': Decimal('0E-8'), 'tokens': [], 'consortiumtokens': ['2.00000000@DOGE'], 'feeburn': Decimal('2.00000000'), 'auctionburn': Decimal('0E-8'), 'paybackburn': [], 'dexfeetokens': [], 'dfipaybackfee': Decimal('0E-8'), 'dfipaybacktokens': [], 'paybackfees': [], 'paybacktokens': [], 'emissionburn': Decimal('4923.76500000'), 'dfip2203': [], 'dfip2206f': []})

        assert_raises_rpc_error(-32600, "Cannot mint token, not an active member of consortium for DOGE!", self.nodes[2].minttokens, ["1@" + symbolDOGE])

        # can mint because it is a founder
        self.nodes[0].minttokens(["1@" + symbolBTC])
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[2].minttokens(["6@" + symbolBTC])
        self.nodes[2].generate(1)
        self.sync_blocks()

        # burn to check that total minted is checked against max limit
        self.nodes[2].burntokens({
            'amounts': "6@" + symbolBTC,
            'from': account2,
        })
        self.nodes[2].generate(1)
        self.sync_blocks()

        self.nodes[3].minttokens(["2@" + symbolBTC])
        self.nodes[3].generate(1)
        self.sync_blocks()

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('9.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('3.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('7.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/7.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/supply'], Decimal('2.00000000'))

        assert_raises_rpc_error(-32600, "You will exceed your maximum mint limit for " + symbolBTC + " token by minting this amount!", self.nodes[3].minttokens, ["2.00000001@" + symbolBTC])
        assert_raises_rpc_error(-32600, "You will exceed global maximum consortium mint limit for " + symbolBTC + " token by minting this amount!", self.nodes[3].minttokens, ["1.00000001@" + symbolBTC])

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idBTC + '/members' : {"02":{"name":"account3BTC",
                                                                                                  "ownerAddress": account3,
                                                                                                  "backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5",
                                                                                                  "mintLimitDaily":4.00000000,
                                                                                                  "mintLimit":6.00000000}}}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idBTC + '/mint_limit' : '20'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Test global daily limit
        assert_raises_rpc_error(-32600, "You will exceed global daily maximum consortium mint limit for " + symbolBTC + " token by minting this amount.", self.nodes[3].minttokens, ["2@" + symbolBTC])

        # Increase global daily limit
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idBTC + '/mint_limit_daily' : '12'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[3].minttokens(["2@" + symbolBTC])
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check minted and daily minted increased
        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/minted'], Decimal('4.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/daily_minted'], '144/4.00000000')

        # Test daily limit
        assert_raises_rpc_error(-32600, "You will exceed your daily mint limit for " + symbolBTC + " token by minting this amount", self.nodes[3].minttokens, ["2@" + symbolBTC])

        # Move to next day, day is 144 blocks on regtest.
        self.nodes[0].generate(288 - self.nodes[0].getblockcount())
        self.sync_blocks()

        # Mint more tokens
        self.nodes[3].minttokens(["2@" + symbolBTC])
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Check minted increased and daily minted reset
        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/minted'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/daily_minted'], '288/2.00000000')

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/consortium/' + idBTC + '/members' : {"02":{"name":"account3BTC",
                                                                                            "ownerAddress": account3,
                                                                                            "backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5",
                                                                                            "mintLimitDaily":2.00000000,
                                                                                            "mintLimit":8.00000000}}}})

        self.nodes[0].generate(1)
        self.sync_blocks()

        # Test new daily limit
        assert_raises_rpc_error(-32600, "You will exceed your daily mint limit for " + symbolBTC + " token by minting this amount", self.nodes[3].minttokens, ["1@" + symbolBTC])

        # burn to check that burning from address of a member for different token does not get counted when burning other tokens
        self.nodes[1].burntokens({
            'amounts': "0.1@" + symbolBTC,
            'from': account1,
        })
        self.nodes[1].generate(1)
        self.sync_blocks()

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('13.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('7.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('7.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/7.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('1.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/minted'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/daily_minted'], '288/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/supply'], Decimal('6.00000000'))

        # burn to check context for a member from another member that is on different token
        self.nodes[1].burntokens({
            'amounts': "0.1@" + symbolBTC,
            'from': account1,
            'context': account2
        })
        self.nodes[1].generate(1)
        self.sync_blocks()

        attribs = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attribs['v0/live/economy/consortium/1/minted'], Decimal('13.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/burnt'], Decimal('6.10000000'))
        assert_equal(attribs['v0/live/economy/consortium/1/supply'], Decimal('6.90000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium/2/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/minted'], Decimal('7.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/daily_minted'], '144/7.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/burnt'], Decimal('6.10000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/01/supply'], Decimal('0.90000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/minted'], Decimal('2.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/daily_minted'], '144/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/burnt'], Decimal('1.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/2/01/supply'], Decimal('0.50000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/minted'], Decimal('6.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/daily_minted'], '288/2.00000000')
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/burnt'], Decimal('0.00000000'))
        assert_equal(attribs['v0/live/economy/consortium_members/1/02/supply'], Decimal('6.00000000'))

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/feature/consortium' : 'false'}})

        self.nodes[0].generate(1)
        self.sync_blocks()

        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolBTC])
        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[2].minttokens, ["1@" + symbolDOGE])
        assert_raises_rpc_error(-32600, "You are not a foundation member or token owner and cannot mint this token", self.nodes[3].minttokens, ["1@" + symbolBTC])

        # DAT owner can mint again
        self.nodes[3].minttokens(["1@" + symbolDOGE])
        self.nodes[3].generate(1)
        self.sync_blocks()

        # Test unlimited limit
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/consortium/' + idBTC + '/mint_limit': '-1', 'v0/consortium/' + idBTC + '/mint_limit_daily': '-1'}})
        self.nodes[0].generate(1)
        self.sync_blocks()

        self.nodes[0].minttokens(["100000000@" + symbolBTC])
        self.nodes[0].generate(1)

        # Increase limit
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/consortium/' + idBTC + '/members': {"01":{"name":"account2BTC",
                                                                                            "ownerAddress": account2,
                                                                                            "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                            "mintLimitDaily":100000.00000000,
                                                                                            "mintLimit":100000.00000000},
                                                                                      "02":{"name":"account3BTC",
                                                                                            "ownerAddress": account3,
                                                                                            "backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5",
                                                                                            "mintLimitDaily":400000.00000000,
                                                                                            "mintLimit":400000.00000000}}}})
        self.nodes[0].generate(1)

        # Decrease limit
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/consortium/' + idBTC + '/members': {"01":{"name":"account2BTC",
                                                                                            "ownerAddress": account2,
                                                                                            "backingId":"ebf634ef7143bc5466995a385b842649b2037ea89d04d469bfa5ec29daf7d1cf",
                                                                                            "mintLimitDaily":1.00000000,
                                                                                            "mintLimit":1.00000000},
                                                                                      "02":{"name":"account3BTC",
                                                                                            "ownerAddress": account3,
                                                                                            "backingId":"6c67fe93cad3d6a4982469a9b6708cdde2364f183d3698d3745f86eeb8ba99d5",
                                                                                            "mintLimitDaily":1.00000000,
                                                                                            "mintLimit":1.00000000}}}})
        self.nodes[0].generate(1)

        # Throw error for invalid values
        assert_raises_rpc_error(-5, "Amount must be positive or -1", self.nodes[0].setgov, {
            "ATTRIBUTES": {'v0/consortium/' + idBTC + '/mint_limit': '-2'}})

        # Mint to an address
        newAddress = self.nodes[0].getnewaddress("", "bech32")
        self.nodes[0].minttokens(["2@" + symbolBTC], newAddress)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getaccount(newAddress), ['2.00000000@BTC'])


if __name__ == '__main__':
    ConsortiumTest().main()

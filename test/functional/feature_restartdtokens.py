#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- restart dtokens test
"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, get_solc_artifact_path

from decimal import Decimal
import math
import time
from web3 import Web3


class RestartdTokensTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txnotokens=0",
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
                "-metachainheight=105",
                "-df23height=150",  # must have 50 diff to metachain start, no idea why
                "-df24height=1000",
            ],
        ]

    def run_test(self):

        # setup tokens, vaults, pools etc.
        self.setup()

        self.check_initial_state()
        # ensure expected initial state

        # do restart and check funds
        self.nodes[0].generate(1000 - self.nodes[0].getblockcount())

        # check split funds
        # looped vault must be unlooped and 90% of DUSD collateral locked
        # SPY payed back with balance,
        #     swap all DUSD from collateral to pay back,
        #     swap parts of DFI to pay back
        # 90% of LM positions locked as tokens
        # 90% of DUSD-DFI position back to address
        # 90% of balances locked
        # check old pools, check new pools with 10% of old liquidity in new tokens

        #'''
        print(self.nodes[0].getblockcount())
        print(self.nodes[0].getaccount(self.address))
        print("---")
        print(self.nodes[0].getloantoken("USDD"))
        print("---")

        print()
        print(self.nodes[0].getvault(self.loop_vault_id))
        print()
        print(self.nodes[0].getvault(self.vault_id1))
        print()
        print(self.nodes[0].getvault(self.vault_id2))
        print()
        print(self.nodes[0].getvault(self.vault_id2_1))
        print()
        print(self.nodes[0].getvault(self.vault_id3))
        print()
        #'''

        assert_equal(
            [
                {
                    id: [
                        token["symbol"],
                        token["isLoanToken"],
                        token["mintable"],
                        token["minted"],
                    ]
                }
                for (id, token) in self.nodes[0].listtokens().items()
            ],
            [
                {'0': ['DFI', False, False, Decimal('0E-8')]}, {'1': ['BTC', False, True, Decimal('2.00000000')]}, {'2': ['USDT', False, True, Decimal('1000.00000000')]}, {'3': ['SPY/lock1', False, False, Decimal('0E-8')]}, {'4': ['DUSD/lock1', False, False, Decimal('0E-8')]}, {'5': ['SPY-DUSD/lock1', False, False, Decimal('0E-8')]}, {'6': ['DUSD-DFI/lock1', False, False, Decimal('0E-8')]}, {'7': ['BTC-DFI', False, False, Decimal('0E-8')]}, {'8': ['USDT-DFI', False, False, Decimal('0E-8')]}, {'9': ['USDT-DUSD/lock1', False, False, Decimal('0E-8')]}, {'10': ['SPY', True, True, Decimal('7.50000091')]}, {'11': ['USDD', True, True, Decimal('4803.87432603')]}, {'12': ['SPY-USDD', False, False, Decimal('0E-8')]}, {'13': ['USDD-DFI', False, False, Decimal('0E-8')]}, {'14': ['USDT-USDD', False, False, Decimal('0E-8')]}
            ],
        )

        assert_equal(
            [
                {
                    id: [
                        pool["symbol"],
                        pool["idTokenA"],
                        pool["idTokenB"],
                        pool["reserveA"],
                        pool["reserveB"],
                    ]
                }
                for (id, pool) in self.nodes[0].listpoolpairs().items()
            ],
            [
                {"5": ["SPY-DUSD/lock1", "3", "4", 0, 0]},
                {"6": ["DUSD-DFI/lock1", "4", "0", 0, 0]},
                {"7": ["BTC-DFI", "1", "0", 1, 10000]},
                {"8": ["USDT-DFI", "2", "0", 50, 10]},
                {"9": ["USDT-DUSD/lock1", "2", "4", Decimal("0E-8"), Decimal("0E-8")]},
                {
                    "12": [
                        "SPY-USDD",
                        "10",
                        "11",
                        Decimal("0.20000117"),
                        Decimal("20.00006397"),
                    ]
                },
                {
                    "13": [
                        "USDD-DFI",
                        "11",
                        "0",
                        Decimal("276.47228143"),
                        Decimal("58.59079946"),
                    ]
                },
                {
                    "14": [
                        "USDT-USDD",
                        "2",
                        "11",
                        Decimal("91.99600874"),
                        Decimal("97.82823880"),
                    ]
                },
            ],
        )

        assert_equal(
            self.nodes[0].listlockedtokens(),
            [
                {
                    "owner": self.address,
                    "values": ["1.80000756@SPY", "819.05037985@USDD"],
                }
            ],
        )

        assert_equal(
            self.nodes[0].getlockedtokens(self.address),
            ["1.80000756@SPY", "819.05037985@USDD"],
        )

        # TODO: check address with no locked tokens
        # TODO: add address with vault and multiple loans and collaterals (also one without direct DUSD pool)

        # TODO: currently best guess numbers, need to check with final results
        assert_equal(
            self.nodes[0].getvault(self.loop_vault_id),
            {
                "vaultId": self.loop_vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address,
                "state": "active",
                "collateralAmounts": ["15.00938965@USDD"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": -1,  # why? cause USDD is locked?
                "loanValue": -1,
                "interestValue": -1,
                "informativeRatio": -1,
                "collateralRatio": -1,
            },
        )
        assert_equal(
            self.nodes[0].getvault(self.vault_id),
            {
                "vaultId": self.vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address,
                "state": "active",
                "collateralAmounts": ["99.12867349@DFI"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("495.64336745"),
                "loanValue": 0,
                "interestValue": 0,
                "informativeRatio": -1,
                "collateralRatio": -1,
            },
        )

        assert_equal(
            self.nodes[0].getaccount(self.address),
            [
                "399.78260718@DFI",
                "10.00000000@USDD",
                "1.99999900@SPY-USDD",
                "7.07078382@USDD-DFI",
            ],
        )

        # TODO: check in locks USDD and SPY
        # check release ratio of lock
        # check total locked funds (gov economy?)
        # check correct behaviour on TD of old SPY + DUSD
        # check case with DFI collateral and DUSD loan

        # TODO: add second address with less SPY loan than balance

        # do normal tokensplit on SPY
        # check correct balances in SC
        # check correct behaviour on TD of old SPY and new SPY

        # release tranche

        # check balances
        # check TD (updated releaseRatio)
        #'''

    def check_initial_state(self):

        assert_equal(
            self.nodes[0].getvault(self.loop_vault_id),
            {
                "vaultId": self.loop_vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address1,
                "state": "active",
                "collateralAmounts": ["250.00000000@DUSD"],
                "loanAmounts": ["99.99848555@DUSD"],
                "interestAmounts": ["-0.00151445@DUSD"],
                "collateralValue": Decimal("250.00000000"),
                "loanValue": Decimal("99.99848555"),
                "interestValue": Decimal("-0.00151445"),
                "informativeRatio": Decimal("250.00378618"),
                "collateralRatio": 250,
            },
        )

        assert_equal(
            self.nodes[0].getvault(self.vault_id1),
            {
                "vaultId": self.vault_id1,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address1,
                "state": "active",
                "collateralAmounts": [
                    "30.00000000@DFI",
                    "0.01000000@BTC",
                    "20.00000000@USDT",
                    "1.00000000@DUSD",
                ],
                "loanAmounts": ["1.00000007@SPY", "99.99867485@DUSD"],
                "interestAmounts": ["0.00000007@SPY", "-0.00132515@DUSD"],
                "collateralValue": Decimal("671.00000000"),
                "loanValue": Decimal("199.99868185"),
                "interestValue": Decimal("-0.00131815"),
                "informativeRatio": Decimal("335.50221121"),
                "collateralRatio": 336,
            },
        )

        assert_equal(
            self.nodes[0].getvault(self.vault_id2),
            {
                "vaultId": self.vault_id2,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address2,
                "state": "active",
                "collateralAmounts": [
                    "16.00000000@DFI",
                    "0.01000000@BTC",
                    "10.00000000@DUSD",
                ],
                "loanAmounts": ["1.00000004@SPY", "0.99999243@DUSD"],
                "interestAmounts": ["0.00000004@SPY", "-0.00000757@DUSD"],
                "collateralValue": Decimal("590.00000000"),
                "loanValue": Decimal("100.99999643"),
                "interestValue": Decimal("-0.00000357"),
                "informativeRatio": Decimal("584.15843648"),
                "collateralRatio": 584,
            },
        )

        assert_equal(
            self.nodes[0].getvault(self.vault_id2_1),
            {
                "vaultId": self.vault_id2_1,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address2,
                "state": "active",
                "collateralAmounts": ["40.00000000@DUSD"],
                "loanAmounts": ["0.10000001@SPY", "9.99992428@DUSD"],
                "interestAmounts": ["0.00000001@SPY", "-0.00007572@DUSD"],
                "collateralValue": Decimal("40.00000000"),
                "loanValue": Decimal("19.99992528"),
                "interestValue": Decimal("-0.00007472"),
                "informativeRatio": Decimal("200.00074720"),
                "collateralRatio": 200,
            },
        )

        assert_equal(
            self.nodes[0].getvault(self.vault_id3),
            {
                "vaultId": self.vault_id3,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address3,
                "state": "active",
                "collateralAmounts": ["80.00000000@DFI", "0.01000000@BTC"],
                "loanAmounts": ["2.00000002@SPY", "199.99962139@DUSD"],
                "interestAmounts": ["0.00000002@SPY", "-0.00037861@DUSD"],
                "collateralValue": Decimal("900.00000000"),
                "loanValue": Decimal("399.99962339"),
                "interestValue": Decimal("-0.00037661"),
                "informativeRatio": Decimal("225.00021184"),
                "collateralRatio": 225,
            },
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.97000000@BTC",
                "1272.79219613@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "30.00000000@USDT",
                "39324.00000000@DFI",
                "4.50000000@SPY",
                "570.00000000@DUSD",
                "9.99999000@SPY-DUSD",
                "948.68328805@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            [
                "0.10000000@SPY",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address2)),
            [],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            [
                "10.00000000@SPY-DUSD",
                "3.00000000@SPY",
                "300.00000000@DUSD",
            ],
        )

        assert_equal(
            self.spy_contract.functions.balanceOf(self.evmaddress).call() / (10**18),
            Decimal(0.5),
        )

        assert_equal(
            self.dusd_contract.functions.balanceOf(self.evmaddress).call() / (10**18),
            Decimal(40),
        )

        assert_equal(
            [
                {
                    id: [
                        pool["symbol"],
                        pool["idTokenA"],
                        pool["idTokenB"],
                        pool["reserveA"],
                        pool["reserveB"],
                        pool["reserveB/reserveA"],
                    ]
                }
                for (id, pool) in self.nodes[0].listpoolpairs().items()
            ],
            [
                {"5": ["SPY-DUSD", "3", "4", 2, 200, 100]},
                {"6": ["DUSD-DFI", "4", "0", 3000, 540, Decimal("0.18")]},
                {"7": ["BTC-DFI", "1", "0", 1, 10000, 10000]},
                # 5$ per DFI
                {"8": ["USDT-DFI", "2", "0", 50, 10, Decimal("0.20000000")]},
                # 1.1 DUSD per USDT = 0.9 USDT per DUSD
                {"9": ["USDT-DUSD", "2", "4", 900, 1000, Decimal("1.11111111")]},
            ],
        )
        assert_equal(
            [
                {
                    id: [
                        token["symbol"],
                        token["isLoanToken"],
                        token["mintable"],
                        token["minted"],
                    ]
                }
                for (id, token) in self.nodes[0].listtokens().items()
            ],
            [
                {"0": ["DFI", False, False, Decimal("0E-8")]},
                {"1": ["BTC", False, True, Decimal("2.00000000")]},
                {"2": ["USDT", False, True, Decimal("1000.00000000")]},
                {"3": ["SPY", True, True, Decimal("10.10000000")]},
                {"4": ["DUSD", True, True, Decimal("5411.00000000")]},
                {"5": ["SPY-DUSD", False, False, Decimal("0E-8")]},
                {"6": ["DUSD-DFI", False, False, Decimal("0E-8")]},
                {"7": ["BTC-DFI", False, False, Decimal("0E-8")]},
                {"8": ["USDT-DFI", False, False, Decimal("0E-8")]},
                {"9": ["USDT-DUSD", False, False, Decimal("0E-8")]},
            ],
        )

    ######## SETUP ##########

    def setup(self):

        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address1 = self.nodes[0].getnewaddress("", "bech32")
        self.address2 = self.nodes[0].getnewaddress()
        self.address3 = self.nodes[0].getnewaddress()

        # Generate chain
        self.nodes[0].generate(105)

        # Setup oracles
        self.setup_oracles()

        # Setup tokens
        self.setup_tokens()

        # Setup Gov vars
        self.setup_govvars()

        # Move to df23 height (for dst v2) and further blocks for more DFI)
        self.nodes[0].generate(500 - self.nodes[0].getblockcount())

        self.distribute_balances()

        # Setup variables
        self.setup_variables()

        self.prepare_evm_funds()

        self.setup_test_pools()

        # to have reference height for interest in vaults
        self.nodes[0].generate(900 - self.nodes[0].getblockcount())

        self.setup_test_vaults()

    def prepare_evm_funds(self):
        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "0.5@SPY", "domain": 2},
                    "dst": {
                        "address": self.evmaddress,
                        "amount": "0.5@SPY",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "40@DUSD", "domain": 2},
                    "dst": {
                        "address": self.evmaddress,
                        "amount": "40@DUSD",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

    def setup_variables(self):

        self.evmaddress = self.nodes[0].getnewaddress("", "erc55")
        self.evm_privkey = self.nodes[0].dumpprivkey(self.evmaddress)

        self.burn_address = self.nodes[0].w3.to_checksum_address(
            "0x0000000000000000000000000000000000000000"
        )

        self.contract_address_spyv1 = self.nodes[0].w3.to_checksum_address(
            f"0xff00000000000000000000000000000000000003"
        )

        self.contract_address_dusdv1 = self.nodes[0].w3.to_checksum_address(
            f"0xff00000000000000000000000000000000000004"
        )

        # DST20 ABI
        self.dst20_abi = open(
            get_solc_artifact_path("dst20_v1", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        self.dst20_v2_abi = open(
            get_solc_artifact_path("dst20_v2", "abi.json"),
            "r",
            encoding="utf8",
        ).read()

        # Check DUSD variables
        self.dusd_contract = self.nodes[0].w3.eth.contract(
            address=self.contract_address_dusdv1, abi=self.dst20_v2_abi
        )
        assert_equal(self.dusd_contract.functions.symbol().call(), "DUSD")
        assert_equal(self.dusd_contract.functions.name().call(), "dUSD")

        # Check SPY variables
        self.spy_contract = self.nodes[0].w3.eth.contract(
            address=self.contract_address_spyv1, abi=self.dst20_v2_abi
        )
        assert_equal(self.spy_contract.functions.symbol().call(), "SPY")
        assert_equal(self.spy_contract.functions.name().call(), "SP500")

    def setup_oracles(self):

        # Price feeds
        price_feed = [
            {"currency": "USD", "token": "DFI"},
            {"currency": "USD", "token": "SPY"},
            {"currency": "USD", "token": "DUSD"},
            {"currency": "USD", "token": "USDT"},
            {"currency": "USD", "token": "BTC"},
        ]

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        self.oracle = self.nodes[0].appointoracle(oracle_address, price_feed, 10)
        self.nodes[0].generate(1)

        # Set Oracle prices
        oracle_prices = [
            {"currency": "USD", "tokenAmount": "5@DFI"},  # $5 per DFI
            {"currency": "USD", "tokenAmount": "100@SPY"},  # $100 per SPY
            {"currency": "USD", "tokenAmount": "1@DUSD"},
            {"currency": "USD", "tokenAmount": "1@USDT"},
            {"currency": "USD", "tokenAmount": "50000@BTC"},
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def setup_tokens(self):

        # DATs
        self.nodes[0].createtoken(
            {
                "symbol": "BTC",
                "name": "BTC token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )

        self.nodes[0].createtoken(
            {
                "symbol": "USDT",
                "name": "USDT token",
                "isDAT": True,
                "collateralAddress": self.address,
            }
        )
        self.nodes[0].generate(1)

        # Set loan tokens
        self.nodes[0].setloantoken(
            {
                "symbol": "SPY",
                "name": "SP500",
                "fixedIntervalPriceId": "SPY/USD",
                "isDAT": True,
                "interest": 0,
            }
        )
        self.nodes[0].generate(1)
        self.idSPY = list(self.nodes[0].gettoken("SPY").keys())[0]

        self.nodes[0].setloantoken(
            {
                "symbol": "DUSD",
                "name": "dUSD",
                "fixedIntervalPriceId": "DUSD/USD",
                "isDAT": True,
                "interest": 0,
            }
        )

        self.nodes[0].generate(1)
        self.idDUSD = list(self.nodes[0].gettoken("DUSD").keys())[0]

        # collaterals
        self.nodes[0].setcollateraltoken(
            {
                "token": "DFI",
                "factor": 1,
                "fixedIntervalPriceId": "DFI/USD",
            }
        )
        self.nodes[0].setcollateraltoken(
            {"token": "BTC", "factor": 1, "fixedIntervalPriceId": "BTC/USD"}
        )
        self.nodes[0].setcollateraltoken(
            {"token": "USDT", "factor": 1, "fixedIntervalPriceId": "USDT/USD"}
        )

        self.nodes[0].generate(1)
        self.nodes[0].setcollateraltoken(
            {
                "token": "DUSD",
                "factor": 1,
                "fixedIntervalPriceId": "DUSD/USD",
            }
        )
        self.nodes[0].generate(1)

    def distribute_balances(self):
        # Mint tokens
        self.nodes[0].minttokens(["6@SPY", "5000@DUSD", "1000@USDT", "2@BTC"])
        self.nodes[0].generate(1)

        # Create account DFI
        self.nodes[0].utxostoaccount({self.address: "50000@DFI"})
        self.nodes[0].sendutxosfrom(self.address, self.address1, 1)
        self.nodes[0].sendutxosfrom(self.address, self.address2, 1)
        self.nodes[0].sendutxosfrom(self.address, self.address3, 1)

        self.nodes[0].accounttoaccount(
            self.address,
            {self.address1: ["0.1@SPY"], self.address3: ["3@SPY", "300@DUSD"]},
        )
        self.nodes[0].generate(1)

    def setup_govvars(self):

        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/evm-dvm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
                    f"v0/token/{self.idDUSD}/loan_minting_interest": "-10",
                    "v0/vaults/dusd-vault/enabled": "true",
                    f"v0/token/{self.idDUSD}/loan_payback_collateral": "true",
                }
            }
        )
        self.nodes[0].generate(10)

    def setup_test_pools(self):
        # Create pool pair
        self.nodes[0].createpoolpair(
            {
                "tokenA": "SPY",
                "tokenB": "DUSD",
                "commission": 0.002,
                "status": True,
                "ownerAddress": self.address,
                "symbol": "SPY-DUSD",
            }
        )
        self.nodes[0].createpoolpair(
            {
                "tokenA": "DUSD",
                "tokenB": "DFI",
                "commission": 0.002,
                "status": True,
                "ownerAddress": self.address,
                "symbol": "DUSD-DFI",
            }
        )
        self.nodes[0].createpoolpair(
            {
                "tokenA": "BTC",
                "tokenB": "DFI",
                "commission": 0.002,
                "status": True,
                "ownerAddress": self.address,
                "symbol": "BTC-DFI",
            }
        )
        self.nodes[0].createpoolpair(
            {
                "tokenA": "USDT",
                "tokenB": "DFI",
                "commission": 0.002,
                "status": True,
                "ownerAddress": self.address,
                "symbol": "USDT-DFI",
            }
        )
        self.nodes[0].createpoolpair(
            {
                "tokenA": "USDT",
                "tokenB": "DUSD",
                "commission": 0.002,
                "status": True,
                "ownerAddress": self.address,
                "symbol": "USDT-DUSD",
            }
        )
        self.nodes[0].generate(1)

        self.spyPoolId = list(self.nodes[0].gettoken("SPY-DUSD").keys())[0]
        self.dusdPoolId = list(self.nodes[0].gettoken("DUSD-DFI").keys())[0]

        # Fund pools
        self.nodes[0].addpoolliquidity(
            {self.address: ["3000@DUSD", "540@DFI"]},  # $5 per DFI -> $0.9 per DUSD
            self.address,
        )
        self.nodes[0].addpoolliquidity(
            {self.address: ["50@USDT", "10@DFI"]},
            self.address,
        )
        self.nodes[0].addpoolliquidity(
            {self.address: ["1@BTC", "10000@DFI"]},
            self.address,
        )
        self.nodes[0].addpoolliquidity(
            {self.address: ["900@USDT", "1000@DUSD"]},
            self.address,
        )
        self.nodes[0].addpoolliquidity(
            {self.address: ["1@SPY", "100@DUSD"]},
            self.address,
        )
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity(
            {self.address: ["1@SPY", "100@DUSD"]},
            self.address3,
        )
        self.nodes[0].generate(1)

    def setup_test_vaults(self):
        # Create loan scheme
        self.nodes[0].createloanscheme(150, 0.05, "LOAN0001")
        self.nodes[0].generate(1)

        # loans go to main address, defined balances for adresses where set in creation of tokens

        # address1
        self.vault_id1 = self.nodes[0].createvault(self.address1, "")
        self.loop_vault_id = self.nodes[0].createvault(self.address1, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id1, self.address, f"30@DFI")
        self.nodes[0].deposittovault(self.vault_id1, self.address, f"1@DUSD")
        self.nodes[0].deposittovault(self.vault_id1, self.address, f"0.01@BTC")
        self.nodes[0].deposittovault(self.vault_id1, self.address, f"20@USDT")

        self.nodes[0].deposittovault(self.loop_vault_id, self.address, f"150@DUSD")
        self.nodes[0].generate(1)

        # create loop
        self.nodes[0].takeloan(
            {"vaultId": self.loop_vault_id, "to": self.address, "amounts": "100@DUSD"}
        )
        self.nodes[0].deposittovault(self.loop_vault_id, self.address, f"100@DUSD")
        self.nodes[0].generate(1)

        # add normal loan
        self.nodes[0].takeloan(
            {
                "vaultId": self.vault_id1,
                "to": self.address,
                "amounts": ["100@DUSD", "1@SPY"],
            }
        )
        self.nodes[0].generate(1)

        ## address2

        self.vault_id2 = self.nodes[0].createvault(self.address2, "")
        self.vault_id2_1 = self.nodes[0].createvault(self.address2, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id2, self.address, f"16@DFI")
        self.nodes[0].deposittovault(self.vault_id2, self.address, f"10@DUSD")
        self.nodes[0].deposittovault(self.vault_id2, self.address, f"0.01@BTC")

        self.nodes[0].deposittovault(self.vault_id2_1, self.address, f"40@DUSD")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan(
            {
                "vaultId": self.vault_id2,
                "to": self.address,
                "amounts": ["1@DUSD", "1@SPY"],
            }
        )

        self.nodes[0].takeloan(
            {
                "vaultId": self.vault_id2_1,
                "to": self.address,
                "amounts": ["10@DUSD", "0.1@SPY"],
            }
        )
        self.nodes[0].generate(1)

        ## address3

        self.vault_id3 = self.nodes[0].createvault(self.address3, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id3, self.address, f"80@DFI")
        self.nodes[0].deposittovault(self.vault_id3, self.address, f"0.01@BTC")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan(
            {
                "vaultId": self.vault_id3,
                "to": self.address,
                "amounts": ["200@DUSD", "2@SPY"],
            }
        )
        self.nodes[0].generate(1)


if __name__ == "__main__":
    RestartdTokensTest().main()

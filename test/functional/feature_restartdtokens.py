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
        print(self.nodes[0].getvault(self.loop_vault_id))
        print(self.nodes[0].getvault(self.vault_id))
        print(self.nodes[0].getaccount(self.address))
        print(self.nodes[0].listloantokens())
        #'''


        assert_equal([{id:[token['symbol'],token['isLoanToken'],token['mintable']]} for (id,token) in self.nodes[0].listtokens().items()], 
                     [{'0': ['DFI', False, False]}, 
                      {'1': ['SPY', False, False]}, 
                      {'2': ['DUSD', False, False]},
                      {'3': ['SPY-DUSD', False, False]}, 
                      {'4': ['DUSD-DFI', False, False]}, 
                      {'5': ['SPY', True, True]}, 
                      {'6': ['USDD', True, True]},
                      {'7': ['SPY-USDD', False, False]}, 
                      {'8': ['USDD-DFI', False, False]}])
        
        assert_equal([{id:pool['symbol']} for (id,pool) in self.nodes[0].listpoolpairs().items()],
                     [{'3': 'SPY-DUSD'}, {'4': 'DUSD-DFI'},
                      {'7': 'SPY-USDD'}, {'8': 'USDD-DFI'}])
        
        # TODO: currently best guess numbers, need to check with final results
        assert_equal(
            self.nodes[0].getvault(self.loop_vault_id),
            {
                "vaultId": self.loop_vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU",
                "state": "active",
                "collateralAmounts": ["15.00938964@USDD"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("15.00938964"),
                "loanValue": Decimal("0"),
                "interestValue": Decimal("0"),
                "informativeRatio": Decimal("-1"),
                "collateralRatio": -1,
            },
        )
        assert_equal(
            self.nodes[0].getvault(self.vault_id),
            {
                "vaultId": self.vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU",
                "state": "active",
                "collateralAmounts": ["98.70000000@DFI"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("493.50000000"),
                "loanValue": Decimal("0"),
                "interestValue": Decimal("0"),
                "informativeRatio": Decimal("-1"),
                "collateralRatio": -1,
            },
        )

        # check old pool pairs (should be empty)

        pool = self.nodes[0].getpoolpair("USDD-DFI")[self.dusdPoolId]
        assert_equal(pool["reserveA"], Decimal("44.25564014"))
        assert_equal(pool["reserveB"], Decimal("1.12967269"))

        # converting DUSD->dTokens via pools to payback loans would likely massively shift the pools.
        # since its all in the system: better use "Futureswap"
        pool = self.nodes[0].getpoolpair("SPY-USDD")[self.spyPoolId]
        assert_equal(pool["reserveA"], Decimal("0.2"))
        assert_equal(pool["reserveB"], Decimal("20"))

        assert_equal(
            self.nodes[0].getaccount(self.address),
            [
                "399.00000000@DFI", # add comission from payback swap, added DFI from pool
                "10.00000000@USDD",
                "1.999999000@SPY-USDD",
                "7.07106681@USDD-DFI",
            ],
        )

        # TODO: check in locks USDD and SPY
        # check release ratio of lock
        # check total locked funds (gov economy?)
        # check correct behaviour on TD of old SPY + DUSD

        # TODO: add second address with less SPY loan than balance

        # do normal tokensplit on SPY
        # check correct balances in SC
        # check correct behaviour on TD of old SPY and new SPY

        # release tranche

        # check balances
        # check TD (updated releaseRatio)
        #'''

    def check_initial_state(self):

        print(self.nodes[0].listloantokens())

        assert_equal(
            self.nodes[0].getvault(self.loop_vault_id),
            {
                "vaultId": self.loop_vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU",
                "state": "active",
                "collateralAmounts": ["250.00000000@DUSD"],
                "loanAmounts": ["99.99924278@DUSD"],
                "interestAmounts": ["-0.00075722@DUSD"],
                "collateralValue": Decimal("250.00000000"),
                "loanValue": Decimal("99.99924278"),
                "interestValue": Decimal("-0.00075722"),
                "informativeRatio": Decimal("250.00189306"),
                "collateralRatio": 250,
            },
        )
        assert_equal(
            self.nodes[0].getvault(self.vault_id),
            {
                "vaultId": self.vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU",
                "state": "active",
                "collateralAmounts": ["100.00000000@DFI", "10.00000000@DUSD"],
                "loanAmounts": ["2.00000006@SPY"],
                "interestAmounts": ["0.00000006@SPY"],
                "collateralValue": Decimal("510.00000000"),
                "loanValue": Decimal("200.00000600"),
                "interestValue": Decimal("0.00000600"),
                "informativeRatio": Decimal("254.99999235"),
                "collateralRatio": 255,
            },
        )

        assert_equal(
            self.nodes[0].getaccount(self.address),
            [
                "390.00000000@DFI",
                "1.50000000@SPY",
                "100.00000000@DUSD",
                "19.99999000@SPY-DUSD",
                "70.71066811@DUSD-DFI",
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

        pool = self.nodes[0].getpoolpair("DUSD-DFI")[self.dusdPoolId]
        assert_equal(pool["reserveA"], Decimal("500"))
        assert_equal(pool["reserveB"], Decimal("10"))

        pool = self.nodes[0].getpoolpair("SPY-DUSD")[self.spyPoolId]
        assert_equal(pool["reserveA"], Decimal("2"))
        assert_equal(pool["reserveB"], Decimal("200"))

        assert_equal([{id:pool['symbol']} for (id,pool) in self.nodes[0].listpoolpairs().items()],
                     [{'3': 'SPY-DUSD'}, {'4': 'DUSD-DFI'}])
        assert_equal([{id:[token['symbol'],token['isLoanToken'],token['mintable']]} for (id,token) in self.nodes[0].listtokens().items()], 
                     [{'0': ['DFI', False, False]}, 
                      {'1': ['SPY', True, True]}, 
                      {'2': ['DUSD', True, True]},
                      {'3': ['SPY-DUSD', False, False]}, 
                      {'4': ['DUSD-DFI', False, False]}])


    ######## SETUP ##########

    def setup(self):

        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress

        # Generate chain
        self.nodes[0].generate(105)

        # Setup oracles
        self.setup_oracles()

        # Setup tokens
        self.setup_tokens()

        # Setup Gov vars
        self.setup_govvars()

        # Move to df23 height (for dst v2)
        self.nodes[0].generate(150 - self.nodes[0].getblockcount())

        # Setup variables
        self.setup_variables()

        self.prepare_evm_funds()

        # to have reference height for interest in vaults
        self.nodes[0].generate(500 - self.nodes[0].getblockcount())

        self.setup_test_vaults()

        self.setup_test_pools()

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
            f"0xff00000000000000000000000000000000000001"
        )

        self.contract_address_dusdv1 = self.nodes[0].w3.to_checksum_address(
            f"0xff00000000000000000000000000000000000002"
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
        ]
        self.nodes[0].setoracledata(self.oracle, int(time.time()), oracle_prices)
        self.nodes[0].generate(10)

    def setup_tokens(self):

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
        self.nodes[0].generate(1)
        self.nodes[0].setcollateraltoken(
            {
                "token": "DUSD",
                "factor": 1,
                "fixedIntervalPriceId": "DUSD/USD",
            }
        )
        self.nodes[0].generate(1)

        # Mint tokens
        self.nodes[0].minttokens(["2@SPY", "1000@DUSD"])
        self.nodes[0].generate(1)

        # Create account DFI
        self.nodes[0].utxostoaccount({self.address: "500@DFI"})
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
        self.nodes[0].generate(1)

        self.spyPoolId = list(self.nodes[0].gettoken("SPY-DUSD").keys())[0]
        self.dusdPoolId = list(self.nodes[0].gettoken("DUSD-DFI").keys())[0]

        # Fund pools
        self.nodes[0].addpoolliquidity(
            {self.address: ["500@DUSD", "10@DFI"]},  # $5 per DFI -> $0.1 per DUSD
            self.address,
        )
        self.nodes[0].addpoolliquidity(
            {self.address: ["2@SPY", "200@DUSD"]},
            self.address,
        )
        self.nodes[0].generate(1)

    def setup_test_vaults(self):
        # Create loan scheme
        self.nodes[0].createloanscheme(150, 0.05, "LOAN0001")
        self.nodes[0].generate(1)

        self.vault_id = self.nodes[0].createvault(self.address, "")
        self.loop_vault_id = self.nodes[0].createvault(self.address, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id, self.address, f"100@DFI")
        self.nodes[0].deposittovault(self.vault_id, self.address, f"10@DUSD")
        self.nodes[0].deposittovault(self.loop_vault_id, self.address, f"150@DUSD")
        self.nodes[0].generate(1)

        # create loop
        self.nodes[0].takeloan({"vaultId": self.loop_vault_id, "amounts": "100@DUSD"})
        self.nodes[0].deposittovault(self.loop_vault_id, self.address, f"100@DUSD")
        self.nodes[0].generate(1)

        # add normal loan
        self.nodes[0].takeloan({"vaultId": self.vault_id, "amounts": "2@SPY"})
        self.nodes[0].generate(1)


if __name__ == "__main__":
    RestartdTokensTest().main()

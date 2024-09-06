#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- restart dtokens test
"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    get_solc_artifact_path,
)

from decimal import Decimal
import time
from web3 import Web3


class RestartdTokensTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            DefiTestFramework.fork_params_till(21)
            + [
                "-txnotokens=0",
                "-subsidytest=1",
                "-metachainheight=105",
                "-df23height=150",  # must have 50 diff to metachain start, no idea why
                "-df24height=990",
            ],
        ]

    def run_test(self):

        # setup tokens, vaults, pools etc.
        self.setup()

        pre_check_height = self.nodes[0].getblockcount()
        # ensure expected initial state
        self.check_initial_state()

        # Try and set dToken restart before fork height
        assert_raises_rpc_error(
            -32600,
            "Cannot be set before DF24Height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/1010": "0.9"}},
        )

        # Move to fork height
        self.nodes[0].generate(990 - self.nodes[0].getblockcount())

        self.check_failing_restart()

        self.check_second_vault_case()

        # rollback and check if state is correct again
        self.rollback_to(pre_check_height)
        self.check_initial_state()
        # Move to fork height
        self.nodes[0].generate(990 - self.nodes[0].getblockcount())

        self.check_only_lm_case()

        # rollback and check if state is correct again
        self.rollback_to(pre_check_height)
        self.check_initial_state()
        # Move to fork height
        self.nodes[0].generate(990 - self.nodes[0].getblockcount())

        self.check_full_test()

    def check_full_test(self):

        self.do_and_check_restart()

        self.check_token_lock()

        # TODO: check history entries

        self.check_upgrade_fail()

        self.check_td()

        self.release_first_1()

        # release all but 1%
        self.release_88()

        self.check_token_split()

        # TD with lock again (check correct lock ratio)

        self.check_td_99()

        # last tranche
        self.release_final_1()

    def check_only_lm_case(self):
        lmaddress = self.nodes[0].getnewaddress()
        self.nodes[0].accounttoaccount(
            self.address,
            {lmaddress: ["1@SPY", "10@USDT-DUSD", "10@DUSD-DFI", "1@SPY-DUSD"]},
        )
        self.nodes[0].generate(1)

        self.nodes[0].poolswap(
            {
                "from": self.address,
                "tokenFrom": "DUSD",
                "amountFrom": 500,
                "to": self.address,
                "tokenTo": "DFI",
            }
        )
        self.nodes[0].generate(1)

        """
        # get account before restart triggers consolidate rewards, but we want to check that this works correctly without consolidation before.
        # keeping the numbers here for reference
        assert_equal(
            sorted(self.nodes[0].getaccount(lmaddress)),
            [
                "0.00785674@DUSD",
                "1.00000000@SPY",
                "1.00000000@SPY-DUSD",
                "10.00000000@DUSD-DFI",
                "10.00000000@USDT-DUSD",
            ],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.96000000@BTC",
                "10.00000000@USDT",
                "120.00000000@DUSD",
                "1262.79219613@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "39373.68546796@DFI",
                "4.00000000@SPY",
                "8.99999000@SPY-DUSD",
                "938.68328805@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        """

        # Set dToken restart and move to execution block
        self.nodes[0].setgov({"ATTRIBUTES": {f"v0/params/dtoken_restart/1000": "0.5"}})
        self.nodes[0].generate(10)

        sort = [lmaddress, self.address, self.address1, self.address2, self.address3]

        assert_equal(
            sorted(
                self.nodes[0].listlockedtokens(),
                key=lambda a: sort.index(a["owner"]),
            ),
            [
                {
                    "owner": lmaddress,
                    "values": ["0.55000056@SPY", "22.42913302@DUSD"],
                },
                {
                    "owner": self.address,
                    "values": ["2.45000009@SPY", "2135.10910595@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["75.00908676@DUSD"],
                },
                {
                    "owner": self.address2,
                    "values": ["10.00086631@DUSD"],
                },
                {
                    "owner": self.address3,
                    "values": ["0.99999930@SPY", "100.01673319@DUSD"],
                },
            ],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(lmaddress)),
            [
                "0.00392837@DUSD",
                "0.50000000@SPY",
                "0.50000499@SPY-DUSD",
                "2.05772446@DFI",
                "4.74342123@USDT",
                "5.00000500@DUSD-DFI",
                "5.00000500@USDT-DUSD",
            ],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.96000000@BTC",
                "2.00000000@SPY",
                "22.36066977@USDT-DFI",
                "39633.53304560@DFI",
                "4.49999498@SPY-DUSD",
                "455.25657402@USDT",
                "469.34163902@USDT-DUSD",
                "60.49607162@DUSD",
                "631.39609307@DUSD-DFI",
                "99.99999000@BTC-DFI",
            ],
        )

    def check_second_vault_case(self):

        # add second vault with SPY and DUSD loan
        self.vault_id3_2 = self.nodes[0].createvault(self.address3, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id3_2, self.address, f"1000@DFI")
        self.nodes[0].generate(1)

        self.nodes[0].takeloan(
            {
                "vaultId": self.vault_id3_2,
                "to": self.address,
                "amounts": ["200@DUSD", "2@SPY"],
            }
        )
        self.nodes[0].generate(1)

        # check initial

        assert_equal(
            self.nodes[0].getvault(self.vault_id3),
            {
                "vaultId": self.vault_id3,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address3,
                "state": "active",
                "collateralAmounts": ["80.00000000@DFI", "0.01000000@BTC"],
                "loanAmounts": ["2.00000158@SPY", "199.96857497@DUSD"],
                "interestAmounts": ["0.00000158@SPY", "-0.03142503@DUSD"],
                "collateralValue": Decimal("900.00000000"),
                "loanValue": Decimal("399.96873297"),
                "interestValue": Decimal("-0.03126703"),
                "informativeRatio": Decimal("225.01758907"),
                "collateralRatio": 225,
            },
        )

        assert_equal(
            self.nodes[0].getvault(self.vault_id3_2),
            {
                "vaultId": self.vault_id3_2,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address3,
                "state": "active",
                "collateralAmounts": ["1000.00000000@DFI"],
                "loanAmounts": ["2.00000002@SPY", "199.99962139@DUSD"],
                "interestAmounts": ["0.00000002@SPY", "-0.00037861@DUSD"],
                "collateralValue": Decimal("5000.00000000"),
                "loanValue": Decimal("399.99962339"),
                "interestValue": Decimal("-0.00037661"),
                "informativeRatio": Decimal("1250.00117690"),
                "collateralRatio": 1250,
            },
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            ["10.00000000@SPY-DUSD", "3.00000000@SPY", "300.00000000@DUSD"],
        )

        # Set dToken restart and move to execution block
        self.nodes[0].setgov({"ATTRIBUTES": {f"v0/params/dtoken_restart/1000": "0.5"}})
        self.nodes[0].generate(10)

        # Check economy keys have been set
        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/token_lock_ratio"),
            [[{"ATTRIBUTES": {"v0/live/economy/token_lock_ratio": "0.5"}}]],
        )

        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/locked_tokens"),
            [
                [
                    {
                        "ATTRIBUTES": {
                            "v0/live/economy/locked_tokens": [
                                "1.00000000@SPY/v1",
                                "1.00000000@DUSD/v1",
                            ]
                        }
                    }
                ]
            ],
        )

        # check token lock and vaults

        sort = [self.address, self.address1, self.address2, self.address3]

        assert_equal(
            sorted(
                self.nodes[0].listlockedtokens(), key=lambda a: sort.index(a["owner"])
            ),
            [
                {
                    "owner": self.address,
                    "values": ["4.00000069@SPY", "2170.02855473@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["75.00908676@DUSD"],
                },
                {
                    "owner": self.address2,
                    "values": ["10.00086631@DUSD"],
                },
                {
                    "owner": self.address3,
                    "values": ["0.50000018@SPY", "49.99988130@DUSD"],
                },
            ],
        )

        # can't know how its sorted internally, and depending on which vault is used first,
        # one gets the full payback with balance, other needs swap of collateral
        v = self.nodes[0].getvault(self.vault_id3_2)
        if v["collateralAmounts"] != ["1000.00000000@DFI"]:
            assert_equal(
                self.nodes[0].getvault(self.vault_id3),
                {
                    "vaultId": self.vault_id3,
                    "loanSchemeId": "LOAN0001",
                    "ownerAddress": self.address3,
                    "state": "active",
                    "collateralAmounts": ["80.00000000@DFI", "0.01000000@BTC"],
                    "loanAmounts": [],
                    "interestAmounts": [],
                    "collateralValue": Decimal("900.00000000"),
                    "loanValue": Decimal("0E-8"),
                    "interestValue": 0,
                    "informativeRatio": Decimal("-1.00000000"),
                    "collateralRatio": -1,
                },
            )

            assert_equal(
                self.nodes[0].getvault(self.vault_id3_2),
                {
                    "vaultId": self.vault_id3_2,
                    "loanSchemeId": "LOAN0001",
                    "ownerAddress": self.address3,
                    "state": "active",
                    "collateralAmounts": ["956.36930468@DFI"],
                    "loanAmounts": [],
                    "interestAmounts": [],
                    "collateralValue": Decimal("4781.84652340"),
                    "loanValue": Decimal("0E-8"),
                    "interestValue": 0,
                    "informativeRatio": Decimal("-1.00000000"),
                    "collateralRatio": -1,
                },
            )
        else:
            assert_equal(
                self.nodes[0].getvault(self.vault_id3),
                {
                    "vaultId": self.vault_id3,
                    "loanSchemeId": "LOAN0001",
                    "ownerAddress": self.address3,
                    "state": "active",
                    "collateralAmounts": ["36.36930468@DFI", "0.01000000@BTC"],
                    "loanAmounts": [],
                    "interestAmounts": [],
                    "collateralValue": Decimal("681.84652340"),
                    "loanValue": Decimal("0E-8"),
                    "interestValue": 0,
                    "informativeRatio": Decimal("-1.00000000"),
                    "collateralRatio": -1,
                },
            )

            assert_equal(
                self.nodes[0].getvault(self.vault_id3_2),
                {
                    "vaultId": self.vault_id3_2,
                    "loanSchemeId": "LOAN0001",
                    "ownerAddress": self.address3,
                    "state": "active",
                    "collateralAmounts": ["1000.00000000@DFI"],
                    "loanAmounts": [],
                    "interestAmounts": [],
                    "collateralValue": Decimal("5000.00000000"),
                    "loanValue": Decimal("0E-8"),
                    "interestValue": 0,
                    "informativeRatio": Decimal("-1.00000000"),
                    "collateralRatio": -1,
                },
            )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            [
                "4.99999498@SPY-DUSD",
            ],
        )

    def do_and_check_restart(self):

        # Set dToken restart and move to execution block
        self.nodes[0].setgov({"ATTRIBUTES": {f"v0/params/dtoken_restart/1000": "0.9"}})
        self.nodes[0].generate(10)

        # Check economy keys have been set
        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/token_lock_ratio"),
            [[{"ATTRIBUTES": {"v0/live/economy/token_lock_ratio": "0.9"}}]],
        )

        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/locked_tokens"),
            [
                [
                    {
                        "ATTRIBUTES": {
                            "v0/live/economy/locked_tokens": [
                                "1.00000000@SPY/v1",
                                "1.00000000@DUSD/v1",
                            ]
                        }
                    }
                ]
            ],
        )

        # Try and set dToken restart after it has already been executed
        assert_raises_rpc_error(
            -32600,
            "dToken restart has already been executed and cannot be set again",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/1020": "0.9"}},
        )

    def check_failing_restart(self):

        # Try and set dToken restart on current height
        assert_raises_rpc_error(
            -32600,
            "Block height must be more than current height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/990": "0.9"}},
        )
        # Try and set dToken restart below current height
        assert_raises_rpc_error(
            -32600,
            "Block height must be more than current height",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/900": "0.9"}},
        )

        # Try and set dToken restart above 1
        assert_raises_rpc_error(
            -5,
            "Percentage exceeds 100%",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/1000": "1.01"}},
        )

        # Try and set dToken restart above 1
        assert_raises_rpc_error(
            -5,
            "Can't lock none nor all dTokens",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/1000": "1"}},
        )

        # no lock, no valid
        assert_raises_rpc_error(
            -5,
            "Can't lock none nor all dTokens",
            self.nodes[0].setgov,
            {"ATTRIBUTES": {f"v0/params/dtoken_restart/1000": "0.0"}},
        )

    def check_td_99(self):

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evmaddress,
                        "amount": "10.00000001@DUSD/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address1,
                        "amount": "10.00000001@DUSD/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evmaddress,
                        "amount": "0.2@SPY/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.address1,
                        "amount": "0.2@SPY/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        # 99% directly transfered, only 1% still locked
        assert_equal(
            sorted(self.nodes[0].getaccount(self.newaddress)),
            ["0.99000000@SPY", "9.90000000@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            ["1.98000000@SPY", "143.41617442@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].listlockedtokens(), key=lambda a: a["values"][0]),
            [
                {
                    "owner": self.newaddress,
                    "values": ["0.10000000@DUSD", "0.01000000@SPY"],
                },
                {
                    "owner": self.address2,
                    "values": ["0.20001733@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["1.60018175@DUSD", "0.02000000@SPY"],
                },
                {
                    "owner": self.address3,
                    "values": ["2.00033467@DUSD", "0.19999990@SPY"],
                },
                {
                    "owner": self.address,
                    "values": ["43.40025190@DUSD", "0.60000020@SPY"],
                },
            ],
        )

    def release_final_1(self):
        self.nodes[0].releaselockedtokens(1)
        self.nodes[0].generate(1)

        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/token_lock_ratio"),
            [[{"ATTRIBUTES": {"v0/live/economy/token_lock_ratio": "0"}}]],
        )

        assert_equal(
            self.nodes[0].listlockedtokens(),
            [],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.newaddress)),
            ["1.00000000@SPY", "10.00000000@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.96000006@BTC",
                "127.27921961@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "3.16226764@SPY-DUSD",
                "3968.02267070@DUSD",
                "39847.97727529@DFI",
                "59.00001170@SPY",
                "855.00507780@USDT",
                "94.86832880@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            ["145.01635617@DUSD", "2.00000000@SPY"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address2)),
            ["18.00155936@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            ["18.99998570@SPY", "190.03348934@DUSD", "3.16230610@SPY-DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.tokenlockaddress)),
            [],
        )

    def check_token_split(self):
        # updated SPY
        self.idSPY = list(self.nodes[0].gettoken("SPY").keys())[0]

        # Lock token
        self.nodes[0].setgov({"ATTRIBUTES": {f"v0/locks/token/{self.idSPY}": "true"}})
        self.nodes[0].generate(1)

        # Token split
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/oracles/splits/{str(self.nodes[0].getblockcount() + 2)}": f"{self.idSPY}/10"
                }
            }
        )
        self.nodes[0].generate(2)

        assert_equal(
            [
                {
                    id: [
                        token["symbol"],
                        token["isLoanToken"],
                        token["mintable"],
                        round(
                            float(token["minted"]), 7
                        ),  # got some flipping errors on last digit
                    ]
                }
                for (id, token) in self.nodes[0].listtokens().items()
            ],
            [
                {"0": ["DFI", False, False, 0.0]},
                {"1": ["BTC", False, True, 2.0]},
                {"2": ["USDT", False, True, 1010.0]},
                {"3": ["SPY/v1", False, False, 0.0]},
                {"4": ["DUSD/v1", False, False, 0.0]},
                {"5": ["SPY-DUSD/v1", False, False, 0.0]},
                {"6": ["DUSD-DFI/v1", False, False, 0.0]},
                {"7": ["BTC-DFI", False, False, 0.0]},
                {"8": ["USDT-DFI", False, False, 0.0]},
                {"9": ["USDT-DUSD/v1", False, False, 0.0]},
                {"10": ["SPY/v2", False, False, 0.0]},
                {"11": ["DUSD", True, True, 4970.0804783]},
                {"12": ["SPY-DUSD/v2", False, False, 0.0]},
                {"13": ["DUSD-DFI", False, False, 0.0]},
                {"14": ["USDT-DUSD", False, False, 0.0]},
                {"15": ["SPY", True, True, 81.0000091]},
                {"16": ["SPY-DUSD", False, False, 0.0]},
            ],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.newaddress)),
            ["0.99000000@SPY", "9.90000000@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.96000006@BTC",
                "127.27921961@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "3.16226764@SPY-DUSD",
                "3924.62241880@DUSD",
                "39847.97727529@DFI",
                "58.40001150@SPY",
                "855.00507780@USDT",
                "94.86832880@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            ["133.51617442@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address2)),
            ["17.80154203@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            ["18.79998580@SPY", "188.03315467@DUSD", "3.16230610@SPY-DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.tokenlockaddress)),
            ["0.81000010@SPY", "47.20078564@DUSD"],
        )

        assert_equal(
            sorted(self.nodes[0].listlockedtokens(), key=lambda a: a["values"][0]),
            [
                {
                    "owner": self.newaddress,
                    "values": ["0.10000000@DUSD", "0.01000000@SPY"],
                },
                {
                    "owner": self.address2,
                    "values": ["0.20001733@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["1.50018174@DUSD"],
                },
                {
                    "owner": self.address3,
                    "values": ["2.00033467@DUSD", "0.19999990@SPY"],
                },
                {
                    "owner": self.address,
                    "values": ["43.40025190@DUSD", "0.60000020@SPY"],
                },
            ],
        )

    def release_88(self):
        self.nodes[0].releaselockedtokens(88)
        self.nodes[0].generate(1)

        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/token_lock_ratio"),
            [[{"ATTRIBUTES": {"v0/live/economy/token_lock_ratio": "0.01"}}]],
        )

        assert_equal(
            sorted(self.nodes[0].listlockedtokens(), key=lambda a: a["values"][0]),
            [
                {
                    "owner": self.newaddress,
                    "values": ["0.00100000@SPY", "0.10000000@DUSD"],
                },
                {
                    "owner": self.address3,
                    "values": ["0.01999999@SPY", "2.00033467@DUSD"],
                },
                {
                    "owner": self.address,
                    "values": ["0.06000002@SPY", "43.40025190@DUSD"],
                },
                {
                    "owner": self.address2,
                    "values": ["0.20001733@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["1.50018174@DUSD"],
                },
            ],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.newaddress)),
            ["0.09900000@SPY", "9.90000000@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.96000006@BTC",
                "0.99999999@SPY-DUSD",
                "127.27921961@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "3924.62241880@DUSD",
                "39847.97727529@DFI",
                "5.84000115@SPY",
                "855.00507780@USDT",
                "94.86832880@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            ["133.51617442@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address2)),
            ["17.80154203@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            ["0.99999899@SPY-DUSD", "1.87999858@SPY", "188.03315467@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.tokenlockaddress)),
            ["0.08100001@SPY", "47.20078564@DUSD"],
        )

    def check_td(self):

        self.newaddress = self.nodes[0].getnewaddress("", "bech32")
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evmaddress,
                        "amount": "10@DUSD/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.newaddress,
                        "amount": "10@DUSD/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evmaddress,
                        "amount": "0.1@SPY/v1",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.newaddress,
                        "amount": "0.1@SPY/v1",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        assert_equal(
            self.dusd_contract.functions.balanceOf(self.evmaddress).call() / (10**18),
            Decimal(30),
        )
        assert_equal(
            self.spy_contract.functions.balanceOf(self.evmaddress).call() / (10**18),
            Decimal(0.4),
        )
        assert_equal(
            self.usdd_contract.functions.balanceOf(self.evmaddress).call(),
            Decimal(0),
        )

        # TD of new token must not lock it

        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.newaddress,
                        "amount": "1@DUSD",
                        "domain": 2,
                    },
                    "dst": {
                        "address": self.evmaddress,
                        "amount": "1@DUSD",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        assert_equal(
            self.usdd_contract.functions.balanceOf(self.evmaddress).call() / 1e18,
            Decimal(1),
        )
        self.nodes[0].transferdomain(
            [
                {
                    "src": {
                        "address": self.evmaddress,
                        "amount": "1@DUSD",
                        "domain": 3,
                    },
                    "dst": {
                        "address": self.newaddress,
                        "amount": "1@DUSD",
                        "domain": 2,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        assert_equal(
            sorted(self.nodes[0].getaccount(self.newaddress)),
            ["0.01000000@SPY", "1.00000000@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].listlockedtokens(), key=lambda a: a["values"][0]),
            [
                {
                    "owner": self.newaddress,
                    "values": ["0.09000000@SPY", "9.00000000@DUSD"],
                },
                {
                    "owner": self.address3,
                    "values": ["1.79999874@SPY", "180.03011967@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["135.01635616@DUSD"],
                },
                {
                    "owner": self.address2,
                    "values": ["18.00155936@DUSD"],
                },
                {
                    "owner": self.address,
                    "values": ["5.40000117@SPY", "3906.02267070@DUSD"],
                },
            ],
        )
        lockHistory = self.nodes[0].listaccounthistory(
            self.newaddress, {"txtypes": ["?"]}
        )
        assert_equal(
            len(lockHistory), 0
        )  # no change in balance of address -> no history

    def release_first_1(self):

        self.nodes[0].releaselockedtokens(1)
        self.nodes[0].generate(1)

        assert_equal(
            self.nodes[0].listgovs("v0/live/economy/token_lock_ratio"),
            [[{"ATTRIBUTES": {"v0/live/economy/token_lock_ratio": "0.89"}}]],
        )

        assert_equal(
            sorted(self.nodes[0].listlockedtokens(), key=lambda a: a["values"][0]),
            [
                {
                    "owner": self.newaddress,
                    "values": ["0.08900000@SPY", "8.90000000@DUSD"],
                },
                {
                    "owner": self.address3,
                    "values": ["1.77999876@SPY", "178.02978501@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["133.51617443@DUSD"],
                },
                {
                    "owner": self.address2,
                    "values": ["17.80154204@DUSD"],
                },
                {
                    "owner": self.address,
                    "values": ["5.34000116@SPY", "3862.62241881@DUSD"],
                },
            ],
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.newaddress)),
            ["0.01100000@SPY", "1.10000000@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.56000001@SPY",
                "0.96000006@BTC",
                "0.99999999@SPY-DUSD",
                "105.40025189@DUSD",
                "127.27921961@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "39847.97727529@DFI",
                "855.00507780@USDT",
                "94.86832880@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            ["1.50018173@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address2)),
            ["0.20001732@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            ["0.11999981@SPY", "0.99999899@SPY-DUSD", "12.00370433@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.tokenlockaddress)),
            ["4200.86992029@DUSD", "7.20899992@SPY"],
        )

        # check history

        lockHistory = self.nodes[0].listaccounthistory(self.address, {"txtypes": ["?"]})
        assert_equal(
            len(lockHistory), 4
        )  # commission tx got added on consolidate rewards later
        releaseHistory = self.nodes[0].listaccounthistory(
            self.address, {"txtypes": ["!"]}
        )
        assert_equal(len(releaseHistory), 1)
        assert_equal(
            releaseHistory[0]["amounts"], ["0.06000001@SPY", "43.40025189@DUSD"]
        )

        lockHistory = self.nodes[0].listaccounthistory(
            self.address1, {"txtypes": ["?"]}
        )
        assert_equal(len(lockHistory), 0)  # must not have additional entry
        releaseHistory = self.nodes[0].listaccounthistory(
            self.address1, {"txtypes": ["!"]}
        )
        assert_equal(len(releaseHistory), 1)
        assert_equal(releaseHistory[0]["amounts"], ["1.50018173@DUSD"])

        lockHistory = self.nodes[0].listaccounthistory(
            self.tokenlockaddress, {"txtypes": ["?"]}
        )
        assert_equal(len(lockHistory), 3)  # TD added locks
        releaseHistory = self.nodes[0].listaccounthistory(
            self.tokenlockaddress, {"txtypes": ["!"]}
        )
        assert_equal(len(releaseHistory), 1)
        assert_equal(
            releaseHistory[0]["amounts"], ["-0.08099999@SPY", "-47.20078560@DUSD"]
        )

    def check_token_lock(self):
        self.usddId = int(list(self.nodes[0].gettoken("DUSD").keys())[0])

        self.usdd_contract = self.nodes[0].w3.eth.contract(
            address=self.nodes[0].w3.to_checksum_address(
                f"0xff0000000000000000000000000000000000{self.usddId:0{4}x}"
            ),
            abi=self.dst20_v2_abi,
        )
        assert_equal(
            [
                {
                    id: [
                        token["symbol"],
                        token["isLoanToken"],
                        token["mintable"],
                        round(
                            float(token["minted"]), 7
                        ),  # got some flipping errors on last digit
                    ]
                }
                for (id, token) in self.nodes[0].listtokens().items()
            ],
            [
                {"0": ["DFI", False, False, 0.0]},
                {"1": ["BTC", False, True, 2.0]},
                {"2": ["USDT", False, True, 1010.0]},
                {"3": ["SPY/v1", False, False, 0.0]},
                {"4": ["DUSD/v1", False, False, 0.0]},
                {"5": ["SPY-DUSD/v1", False, False, 0.0]},
                {"6": ["DUSD-DFI/v1", False, False, 0.0]},
                {"7": ["BTC-DFI", False, False, 0.0]},
                {"8": ["USDT-DFI", False, False, 0.0]},
                {"9": ["USDT-DUSD/v1", False, False, 0.0]},
                {"10": ["SPY", True, True, 8.0000009]},
                {"11": ["DUSD", True, True, 4960.0804783]},
                {"12": ["SPY-DUSD", False, False, 0.0]},
                {"13": ["DUSD-DFI", False, False, 0.0]},
                {"14": ["USDT-DUSD", False, False, 0.0]},
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
                        pool["totalLiquidity"],
                    ]
                }
                for (id, pool) in self.nodes[0].listpoolpairs().items()
            ],
            [
                {"5": ["SPY-DUSD/v1", "3", "4", 0, 0, 0]},
                {"6": ["DUSD-DFI/v1", "4", "0", 0, 0, 0]},
                {
                    "7": [
                        "BTC-DFI",
                        "1",
                        "0",
                        Decimal("1.00003580"),
                        Decimal("9999.64201282"),
                        100.00000000,
                    ]
                },
                {"8": ["USDT-DFI", "2", "0", 50, 10, Decimal("22.36067977")]},
                {"9": ["USDT-DUSD/v1", "2", "4", 0, 0, 0]},
                {  # why is DUSD reserve not lower? interest payment swapped SPY->DUSD
                    "12": [
                        "SPY-DUSD",
                        "10",
                        "11",
                        Decimal("0.20000117"),
                        Decimal("20.00006377"),
                        Decimal("2.00000898"),
                    ]
                },
                {
                    "13": [
                        "DUSD-DFI",
                        "11",
                        "0",
                        Decimal("266.13631955"),
                        Decimal("60.87106908"),
                        Decimal("127.27922961"),
                    ]
                },
                {
                    "14": [
                        "USDT-DUSD",
                        "2",
                        "11",
                        Decimal("93.88082168"),
                        Decimal("95.86624347"),
                        Decimal("94.86833880"),
                    ]
                },
            ],
        )

        assert_equal(
            self.nodes[0].getlockedtokens(self.address),
            ["5.40000117@SPY", "3906.02267070@DUSD"],
        )

        assert_equal(
            sorted(self.nodes[0].listlockedtokens(), key=lambda a: a["values"][0]),
            [
                {
                    "owner": self.address3,
                    "values": ["1.79999874@SPY", "180.03011967@DUSD"],
                },
                {
                    "owner": self.address1,
                    "values": ["135.01635616@DUSD"],
                },
                {
                    "owner": self.address2,
                    "values": ["18.00155936@DUSD"],
                },
                {
                    "owner": self.address,
                    "values": ["5.40000117@SPY", "3906.02267070@DUSD"],
                },
            ],
        )

        assert_equal(
            self.nodes[0].getvault(self.loop_vault_id),
            {
                "vaultId": self.loop_vault_id,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address1,
                "state": "active",
                "collateralAmounts": ["15.00181735@DUSD"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("15.00181735"),
                "loanValue": 0,
                "interestValue": 0,
                "informativeRatio": -1,
                "collateralRatio": -1,
            },
        )

        # DFI and DUSD used completely to payback, parts of USDT used too (shows that higher liq pool DUSD-DFI was used first)
        assert_equal(
            self.nodes[0].getvault(self.vault_id1),
            {
                "vaultId": self.vault_id1,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address1,
                "state": "active",
                "collateralAmounts": ["0.01000000@BTC", "11.11410051@USDT"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("511.11410051"),
                "loanValue": Decimal("0E-8"),
                "interestValue": 0,
                "informativeRatio": Decimal("-1.00000000"),
                "collateralRatio": -1,
            },
        )
        assert_equal(
            self.nodes[0].getvault(self.vault_id2),
            {
                "vaultId": self.vault_id2,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address2,
                "state": "active",
                "collateralAmounts": ["1.50964279@DFI", "0.01000000@BTC"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("507.54821395"),
                "loanValue": Decimal("0E-8"),
                "interestValue": 0,
                "informativeRatio": Decimal("-1.00000000"),
                "collateralRatio": -1,
            },
        )
        assert_equal(
            self.nodes[0].getvault(self.vault_id2_1),
            {
                "vaultId": self.vault_id2_1,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address2,
                "state": "active",
                "collateralAmounts": ["2.00017326@DUSD"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("2.00017326"),
                "loanValue": 0,
                "interestValue": 0,
                "informativeRatio": -1,
                "collateralRatio": -1,
            },
        )

        assert_equal(
            self.nodes[0].getvault(self.vault_id2_2),
            {
                "vaultId": self.vault_id2_2,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address2,
                "state": "active",
                "collateralAmounts": ["0.00996410@BTC"],
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("498.20500000"),
                "loanValue": Decimal("0E-8"),
                "interestValue": 0,
                "informativeRatio": Decimal("-1.00000000"),
                "collateralRatio": -1,
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
                "loanAmounts": [],
                "interestAmounts": [],
                "collateralValue": Decimal("900.00000000"),
                "loanValue": Decimal("0E-8"),
                "interestValue": 0,
                "informativeRatio": Decimal("-1.00000000"),
                "collateralRatio": -1,
            },
        )

        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.50000000@SPY",
                "0.96000006@BTC",
                "0.99999999@SPY-DUSD",
                "127.27921961@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "39847.97727529@DFI",
                "62.00000000@DUSD",
                "855.00507780@USDT",
                "94.86832880@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address1)),
            [],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address2)),
            [],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address3)),
            ["0.09999983@SPY", "0.99999899@SPY-DUSD", "10.00336967@DUSD"],
        )
        assert_equal(
            sorted(self.nodes[0].getaccount(self.tokenlockaddress)),
            ["4239.07070589@DUSD", "7.19999991@SPY"],
        )

        # Check history entries

        # Generate block to populate the history
        self.nodes[0].generate(1)

        # split
        assert_equal(
            len(self.nodes[0].listaccounthistory(self.address, {"txtypes": ["P"]})),
            13,  # = 5 split "tokens" (2 token, 3 pools 3 commission)
        )

        # Check balances
        assert_equal(
            sorted(self.nodes[0].getaccount(self.address)),
            [
                "0.50000000@SPY",
                "0.96000006@BTC",
                "0.99999999@SPY-DUSD",
                "127.27921961@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "39847.97727529@DFI",
                "62.00000000@DUSD",
                "855.00507780@USDT",
                "94.86832880@USDT-DUSD",
                "99.99999000@BTC-DFI",
            ],
        )

        assert_equal(
            [
                {"h": entry["blockHeight"], "t": entry["type"], "a": entry["amounts"]}
                for entry in self.nodes[0].listaccounthistory(
                    self.address, {"depth": 1}
                )
            ],
            [
                {"h": 1001, "t": "blockReward", "a": ["122.11455925@DFI"]},
                {"h": 1000, "t": "TokenSplit", "a": ["-5.00000000@SPY/v1"]},
                {"h": 1000, "t": "Commission", "a": ["0.00000006@BTC"]},
                {"h": 1000, "t": "Commission", "a": ["0.13769666@DFI"]},
                {"h": 1000, "t": "Commission", "a": ["0.07777178@USDT"]},
                {"h": 1000, "t": "TokenSplit", "a": ["5.00000000@SPY"]},
                {"h": 1000, "t": "TokenSplit", "a": ["-620.00000000@DUSD/v1"]},
                {"h": 1000, "t": "TokenSplit", "a": ["620.00000000@DUSD"]},
                {"h": 1000, "t": "TokenSplit", "a": ["-9.99999000@SPY-DUSD/v1"]},
                {"h": 1000, "t": "TokenSplit", "a": ["9.99999997@SPY-DUSD"]},
                {"h": 1000, "t": "TokenSplit", "a": ["-1272.79219613@DUSD-DFI/v1"]},
                {"h": 1000, "t": "TokenSplit", "a": ["1272.79219613@DUSD-DFI"]},
                {"h": 1000, "t": "TokenSplit", "a": ["-948.68328805@USDT-DUSD/v1"]},
                {"h": 1000, "t": "TokenSplit", "a": ["948.68328805@USDT-DUSD"]},
                {
                    "h": 1000,
                    "t": "TokenLock",
                    "a": [
                        "547.83957863@DFI",
                        "844.92730602@USDT",
                        "-4.50000000@SPY",
                        "-558.00000000@DUSD",
                        "-8.99999998@SPY-DUSD",
                        "-1145.51297652@DUSD-DFI",
                        "-853.81495925@USDT-DUSD",
                    ],
                },
                {"h": 1000, "t": "blockReward", "a": ["122.11455925@DFI"]},
            ],
        )

        lockHistory = self.nodes[0].listaccounthistory(self.address, {"txtypes": ["?"]})
        assert_equal(len(lockHistory), 4)
        assert_equal(
            lockHistory[0]["amounts"],
            [
                "547.83957863@DFI",
                "844.92730602@USDT",
                "-4.50000000@SPY",
                "-558.00000000@DUSD",
                "-8.99999998@SPY-DUSD",
                "-1145.51297652@DUSD-DFI",
                "-853.81495925@USDT-DUSD",
            ],
        )

        assert_equal(
            len(
                self.nodes[0].listaccounthistory(self.address1, {"txtypes": ["P", "?"]})
            ),
            0,
        )

        assert_equal(
            len(
                self.nodes[0].listaccounthistory(self.address2, {"txtypes": ["P", "?"]})
            ),
            0,
        )

        assert_equal(
            len(self.nodes[0].listaccounthistory(self.address3, {"txtypes": ["P"]})), 6
        )
        lockHistory = self.nodes[0].listaccounthistory(
            self.address3, {"txtypes": ["?"]}
        )
        assert_equal(len(lockHistory), 1)
        assert_equal(
            lockHistory[0]["amounts"],
            ["-0.89999847@SPY", "-90.03032705@DUSD", "-8.99999098@SPY-DUSD"],
        )

        assert_equal(
            len(
                self.nodes[0].listaccounthistory(
                    self.tokenlockaddress, {"txtypes": ["P"]}
                )
            ),
            0,
        )
        lockHistory = self.nodes[0].listaccounthistory(
            self.tokenlockaddress, {"txtypes": ["?"]}
        )
        assert_equal(
            len(lockHistory), 1
        )  # on lockaddress, every lock is a seperate entry
        assert_equal(
            lockHistory[0]["amounts"], ["7.19999991@SPY", "4239.07070589@DUSD"]
        )

    def check_upgrade_fail(self):
        # Call upgradeToken on pre-lock must fail
        amount = Web3.to_wei(10, "ether")

        upgrade_txn = self.dusd_contract.functions.upgradeToken(
            amount
        ).build_transaction(
            {
                "from": self.evmaddress,
                "nonce": self.nodes[0].eth_getTransactionCount(self.evmaddress),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 5_000_000,
            }
        )

        # Sign the transaction
        signed_txn = self.nodes[0].w3.eth.account.sign_transaction(
            upgrade_txn, self.evm_privkey
        )

        self.nodes[0].w3.eth.send_raw_transaction(signed_txn.rawTransaction)
        self.nodes[0].generate(1)

        tx_receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(signed_txn.hash)

        assert_equal(
            self.dusd_contract.functions.balanceOf(self.evmaddress).call() / (10**18),
            Decimal(40),
        )
        assert_equal(
            self.usdd_contract.functions.balanceOf(self.evmaddress).call(),
            Decimal(0),
        )
        assert_equal(tx_receipt["status"], 0)

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
                    "50.00000000@USDT",
                    "1.00000000@DUSD",
                ],
                "loanAmounts": ["1.00000007@SPY", "99.99867485@DUSD"],
                "interestAmounts": ["0.00000007@SPY", "-0.00132515@DUSD"],
                "collateralValue": Decimal("701.00000000"),
                "loanValue": Decimal("199.99868185"),
                "interestValue": Decimal("-0.00131815"),
                "informativeRatio": Decimal("350.50231007"),
                "collateralRatio": 351,
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
                    "20.00000000@DFI",
                    "0.01000000@BTC",
                    "10.00000000@DUSD",
                ],
                "loanAmounts": ["1.00000004@SPY", "0.99999243@DUSD"],
                "interestAmounts": ["0.00000004@SPY", "-0.00000757@DUSD"],
                "collateralValue": Decimal("610.00000000"),
                "loanValue": Decimal("100.99999643"),
                "interestValue": Decimal("-0.00000357"),
                "informativeRatio": Decimal("603.96041738"),
                "collateralRatio": 604,
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
            self.nodes[0].getvault(self.vault_id2_2),
            {
                "vaultId": self.vault_id2_2,
                "loanSchemeId": "LOAN0001",
                "ownerAddress": self.address2,
                "state": "active",
                "collateralAmounts": ["20.00000000@DFI", "0.01000000@BTC"],
                "loanAmounts": ["0.50000002@SPY", "49.99962139@DUSD"],
                "interestAmounts": ["0.00000002@SPY", "-0.00037861@DUSD"],
                "collateralValue": Decimal("600.00000000"),
                "loanValue": Decimal("99.99962339"),
                "interestValue": Decimal("-0.00037661"),
                "informativeRatio": Decimal("600.00225966"),
                "collateralRatio": 600,
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
                "0.96000000@BTC",
                "10.00000000@USDT",
                "1272.79219613@DUSD-DFI",
                "22.36066977@USDT-DFI",
                "39300.00000000@DFI",
                "5.00000000@SPY",
                "620.00000000@DUSD",
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
            sorted(self.nodes[0].getaccount(self.tokenlockaddress)),
            [],
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
                        pool["totalLiquidity"],
                        pool["reserveB/reserveA"],
                    ]
                }
                for (id, pool) in self.nodes[0].listpoolpairs().items()
            ],
            [
                {"5": ["SPY-DUSD", "3", "4", 2, 200, 20, 100]},
                {
                    "6": [
                        "DUSD-DFI",
                        "4",
                        "0",
                        3000,
                        540,
                        Decimal("1272.79220613"),
                        Decimal("0.18"),
                    ]
                },
                {"7": ["BTC-DFI", "1", "0", 1, 10000, 100, 10000]},
                # 5$ per DFI
                {
                    "8": [
                        "USDT-DFI",
                        "2",
                        "0",
                        50,
                        10,
                        Decimal("22.36067977"),
                        Decimal("0.20000000"),
                    ]
                },
                # 1.1 DUSD per USDT = 0.9 USDT per DUSD
                {
                    "9": [
                        "USDT-DUSD",
                        "2",
                        "4",
                        900,
                        1000,
                        Decimal("948.68329805"),
                        Decimal("1.11111111"),
                    ]
                },
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
                {"2": ["USDT", False, True, Decimal("1010.00000000")]},
                {"3": ["SPY", True, True, Decimal("10.60000000")]},
                {"4": ["DUSD", True, True, Decimal("5461.00000000")]},
                {"5": ["SPY-DUSD", False, False, Decimal("0E-8")]},
                {"6": ["DUSD-DFI", False, False, Decimal("0E-8")]},
                {"7": ["BTC-DFI", False, False, Decimal("0E-8")]},
                {"8": ["USDT-DFI", False, False, Decimal("0E-8")]},
                {"9": ["USDT-DUSD", False, False, Decimal("0E-8")]},
            ],
        )

    """      SETUP    """

    def setup(self):

        # Define address
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address1 = self.nodes[0].getnewaddress("", "bech32")
        self.address2 = self.nodes[0].getnewaddress()
        self.address3 = self.nodes[0].getnewaddress()
        scs = self.nodes[0].listsmartcontracts()
        for sc in scs:
            if sc["name"] == "TokenLock":
                self.tokenlockaddress = sc["address"]

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
                    "src": {"address": self.address, "amount": "1@DFI", "domain": 2},
                    "dst": {
                        "address": self.evmaddress,
                        "amount": "1@DFI",
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
        self.nodes[0].generate(1)

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
        self.nodes[0].minttokens(["6@SPY", "5000@DUSD", "1010@USDT", "2@BTC"])
        self.nodes[0].generate(1)

        # Create account DFI
        self.nodes[0].utxostoaccount({self.address: "50001@DFI"})
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
                    "v0/params/dfip2206a/dusd_interest_burn": "true",
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
        self.nodes[0].generate(1)
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
        self.nodes[0].generate(1)
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
        self.nodes[0].generate(1)
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
        self.dusdUsdtPoolId = list(self.nodes[0].gettoken("USDT-DUSD").keys())[0]
        self.btcPoolId = list(self.nodes[0].gettoken("BTC-DFI").keys())[0]

        # add pool fees to see that we have them correctly in the estimation of pool results
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    f"v0/poolpairs/{self.btcPoolId}/token_a_fee_pct": "0.001",
                    f"v0/poolpairs/{self.dusdPoolId}/token_a_fee_pct": "0.05",
                    f"v0/poolpairs/{self.dusdPoolId}/token_a_fee_direction": "in",
                    f"v0/poolpairs/{self.dusdUsdtPoolId}/token_b_fee_pct": "0.05",
                    f"v0/poolpairs/{self.dusdUsdtPoolId}/token_b_fee_direction": "in",
                }
            }
        )
        self.nodes[0].generate(1)

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

        # vault1: DFI, USDT, DUSD, BTC collateral -> will use all DUSD and DFI, parts of USDT
        # vault loop: pure DUSD loop
        # vault2: DFI, DUSD, BTC coll -> DFI remaining (to have the collective swap correctly checked)
        # vault2_1: DUSD loop with additional dToken loan -> all payback from DUSD
        # vault2_2: DFI, BTC coll -> need BTC for composite swapp payback
        # vault3: all payback from address

        # address1
        self.vault_id1 = self.nodes[0].createvault(self.address1, "")
        self.loop_vault_id = self.nodes[0].createvault(self.address1, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id1, self.address, f"30@DFI")
        self.nodes[0].deposittovault(self.vault_id1, self.address, f"1@DUSD")
        self.nodes[0].deposittovault(self.vault_id1, self.address, f"0.01@BTC")
        self.nodes[0].deposittovault(self.vault_id1, self.address, f"50@USDT")

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

        # address2

        self.vault_id2 = self.nodes[0].createvault(self.address2, "")
        self.vault_id2_1 = self.nodes[0].createvault(self.address2, "")
        self.vault_id2_2 = self.nodes[0].createvault(self.address2, "")
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(self.vault_id2, self.address, f"20@DFI")
        self.nodes[0].deposittovault(self.vault_id2, self.address, f"10@DUSD")
        self.nodes[0].deposittovault(self.vault_id2, self.address, f"0.01@BTC")

        self.nodes[0].deposittovault(self.vault_id2_1, self.address, f"40@DUSD")

        self.nodes[0].deposittovault(self.vault_id2_2, self.address, f"20@DFI")
        self.nodes[0].deposittovault(self.vault_id2_2, self.address, f"0.01@BTC")
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

        self.nodes[0].takeloan(
            {
                "vaultId": self.vault_id2_2,
                "to": self.address,
                "amounts": ["50@DUSD", "0.5@SPY"],
            }
        )
        self.nodes[0].generate(1)

        # address3

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

#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- governance variables test
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import \
    connect_nodes, disconnect_nodes, assert_equal, assert_raises_rpc_error
from decimal import Decimal
import time

class GovsetTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=200', '-fortcanningheight=400', '-fortcanninghillheight=1110', '-fortcanningroadheight=1150', '-fortcanningcrunchheight=1200', '-fortcanninggardensheight=1250', '-subsidytest=1'],
            ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-eunosheight=200', '-fortcanningheight=400', '-fortcanninghillheight=1110', '-fortcanningroadheight=1150', '-fortcanningcrunchheight=1200', '-fortcanninggardensheight=1250', '-subsidytest=1']]


    def run_test(self):
        self.setup_tokens()

        # Stop node #1 for future revert
        self.stop_node(1)

        # set|get not existent variable:
        try:
            self.nodes[0].setgov({"REWARD": "any"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not registered" in errorString)
        try:
            self.nodes[0].getgov("REWARD")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("not registered" in errorString)

        #
        # prepare the pools for LP_SPLITS
        #
        symbolGOLD = "GOLD#" + self.get_id_token("GOLD")
        symbolSILVER = "SILVER#" + self.get_id_token("SILVER")

        owner = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].createtoken({
            "symbol": "BRONZE",
            "name": "just bronze",
            "collateralAddress": owner # doesn't matter
        })
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": symbolGOLD,
            "tokenB": symbolSILVER,
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": symbolGOLD,
            "tokenB": "BRONZE#130",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "GB",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": symbolSILVER,
            "tokenB": "BRONZE#130",
            "commission": 0.1,
            "status": True,
            "ownerAddress": owner,
            "pairSymbol": "SB",
        }, [])
        self.nodes[0].generate(1)
        assert(len(self.nodes[0].listpoolpairs()) == 3)

        # set LP_SPLITS with absent pools id
        try:
            self.nodes[0].setgov({
                "LP_SPLITS": { "0": 0.5, "1": 0.4, "2": 0.2 }
                })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("id=0 not found" in errorString)

        # set LP_SPLITS with total >100%
        try:
            self.nodes[0].setgov({
            "LP_SPLITS": { "1": 0.5, "2": 0.4, "3": 0.2 }
                })
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("total" in errorString)


        self.nodes[0].setgov({
            "LP_SPLITS": { "1": 0.5, "2": 0.4, "3": 0.1 }
        })

        self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35.5})
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        assert (g1 == {'LP_SPLITS': {'1': Decimal('0.50000000'), '2': Decimal('0.40000000'), '3': Decimal('0.10000000')}} )

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool1 = self.nodes[0].getpoolpair("1", True)['1']
        pool2 = self.nodes[0].getpoolpair("2", True)['2']
        pool3 = self.nodes[0].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == Decimal('0.50000000')
            and pool2['rewardPct'] == Decimal('0.40000000')
            and pool3['rewardPct'] == Decimal('0.10000000'))

        # start node 1 and sync for reverting to this chain point
        self.start_node(1)
        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()

        # check sync between nodes 0 and 1
        g1 = self.nodes[1].getgov("LP_SPLITS")
        assert (g1 == {'LP_SPLITS': {'1': Decimal('0.50000000'), '2': Decimal('0.40000000'), '3': Decimal('0.10000000')}} )

        g2 = self.nodes[1].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool1 = self.nodes[1].getpoolpair("1", True)['1']
        pool2 = self.nodes[1].getpoolpair("2", True)['2']
        pool3 = self.nodes[1].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == Decimal('0.50000000')
            and pool2['rewardPct'] == Decimal('0.40000000')
            and pool3['rewardPct'] == Decimal('0.10000000'))

        # disconnect node #1
        disconnect_nodes(self.nodes[0], 1)

        # test set multuple:
        self.nodes[0].setgov({
            "LP_SPLITS": { "1": 1 },
            "LP_DAILY_DFI_REWARD": 45
        })
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        assert (g1 == {'LP_SPLITS': {'1': 1}} )

        # test that all previous pool's values was reset
        pool1 = self.nodes[0].getpoolpair("1", True)['1']
        pool2 = self.nodes[0].getpoolpair("2", True)['2']
        pool3 = self.nodes[0].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == 1)
        assert (pool2['rewardPct'] == 0)
        assert (pool3['rewardPct'] == 0)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': 45} )

        # REVERTING
        # mine blocks at node 1
        self.nodes[1].generate(20)

        connect_nodes(self.nodes[0], 1)
        self.sync_blocks()

        # check that node 0 was synced to neccesary chain point
        g1 = self.nodes[0].getgov("LP_SPLITS")
        assert (g1 == {'LP_SPLITS': {'1': Decimal('0.50000000'), '2': Decimal('0.40000000'), '3': Decimal('0.10000000')}} )

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )

        pool1 = self.nodes[0].getpoolpair("1", True)['1']
        pool2 = self.nodes[0].getpoolpair("2", True)['2']
        pool3 = self.nodes[0].getpoolpair("3", True)['3']
        assert (pool1['rewardPct'] == Decimal('0.50000000')
            and pool2['rewardPct'] == Decimal('0.40000000')
            and pool3['rewardPct'] == Decimal('0.10000000'))
        self.nodes[0].clearmempool()

        # Generate to Eunos hard fork
        self.nodes[0].generate(200 - self.nodes[0].getblockcount())

        # Try and set LP_DAILY_DFI_REWARD manually
        try:
            self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 100})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot be set manually after Eunos hard fork" in errorString)

        # Check new subsidy
        assert_equal(self.nodes[0].getgov('LP_DAILY_DFI_REWARD')['LP_DAILY_DFI_REWARD'], Decimal('14843.90592000')) # 144 blocks a day times 103.08268000

        # Roll back
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        # Check subsidy restored
        assert_equal(self.nodes[0].getgov('LP_DAILY_DFI_REWARD')['LP_DAILY_DFI_REWARD'], Decimal('35.50000000'))

        # Move to second reduction and check reward
        self.nodes[0].generate(350 - self.nodes[0].getblockcount())
        assert_equal(self.nodes[0].getgov('LP_DAILY_DFI_REWARD')['LP_DAILY_DFI_REWARD'], Decimal('14597.79395904')) # 144 blocks a day times 101.37356916

        # Rollback from second reduction
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        # Check subsidy restored
        assert_equal(self.nodes[0].getgov('LP_DAILY_DFI_REWARD')['LP_DAILY_DFI_REWARD'], Decimal('14843.90592000'))

        # Check LP_DAILY_LOAN_TOKEN_REWARD before FortCanning
        assert_equal(self.nodes[0].getgov('LP_DAILY_LOAN_TOKEN_REWARD')['LP_DAILY_LOAN_TOKEN_REWARD'], Decimal('0.00000000'))

        # Try and use setgovheight start height before FortCanning
        try:
            self.nodes[0].setgov({ "ORACLE_BLOCK_INTERVAL": 200})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot be set before FortCanning" in errorString)

        # Try and set LP_LOAN_TOKEN_SPLITS before FortCanning
        try:
            self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": { "1": 0.1, "2": 0.2, "3": 0.7 }})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot be set before FortCanning" in errorString)

        # Generate to FortCanning
        self.nodes[0].generate(400 - self.nodes[0].getblockcount())

        # Try and use setgovheight with ORACLE_BLOCK_INTERVAL
        try:
            self.nodes[0].setgovheight({ "ORACLE_BLOCK_INTERVAL": 200}, 600)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Cannot set via setgovheight." in errorString)

        # Test ORACLE_BLOCK_INTERVAL
        try:
            self.nodes[0].setgov({ "ORACLE_BLOCK_INTERVAL": 0})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Block interval cannot be less than 1" in errorString)

        try:
            self.nodes[0].setgov({ "ORACLE_BLOCK_INTERVAL": "120"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Block interval amount is not a number" in errorString)

        # Empty variable
        try:
            self.nodes[0].setgovheight({}, 600)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No Governance variable provided" in errorString)

        # Test changing Gov var by height. Check equal to next block failure.
        try:
            self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.01000000')}, self.nodes[0].getblockcount() + 1)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("startHeight must be above the current block height" in errorString)

        # Make sure erronous values still get picked up
        try:
            self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.00100000')}, self.nodes[0].getblockcount() + 10)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Deviation cannot be less than 1 percent" in errorString)

        # Check new subsidy
        assert_equal(self.nodes[0].getgov('LP_DAILY_LOAN_TOKEN_REWARD')['LP_DAILY_LOAN_TOKEN_REWARD'], Decimal('14156.13182400')) # 144 blocks a day times 98.30647100

        # Roll back
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        # Check subsidy restored
        assert_equal(self.nodes[0].getgov('LP_DAILY_LOAN_TOKEN_REWARD')['LP_DAILY_LOAN_TOKEN_REWARD'], Decimal('0.00000000'))

        # Move to next reduction and check reward
        self.nodes[0].generate(500 - self.nodes[0].getblockcount())
        assert_equal(self.nodes[0].getgov('LP_DAILY_LOAN_TOKEN_REWARD')['LP_DAILY_LOAN_TOKEN_REWARD'], Decimal('13921.42315824')) # 144 blocks a day times 96.67654971

        # Rollback from second reduction
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))

        # Check subsidy restored
        assert_equal(self.nodes[0].getgov('LP_DAILY_LOAN_TOKEN_REWARD')['LP_DAILY_LOAN_TOKEN_REWARD'], Decimal('14156.13182400'))

        # Test ORACLE_DEVIATION
        try:
            self.nodes[0].setgov({ "ORACLE_DEVIATION": Decimal('0.00100000')})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Deviation cannot be less than 1 percent" in errorString)

        self.nodes[0].setgov({ "ORACLE_DEVIATION": Decimal('0.01000000')})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov("ORACLE_DEVIATION")["ORACLE_DEVIATION"], Decimal('0.01000000'))

        # Test LOAN_LIQUIDATION_PENALTY
        try:
            self.nodes[0].setgov({ "LOAN_LIQUIDATION_PENALTY": Decimal('0.00100000')})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Penalty cannot be less than 0.01 DFI" in errorString)

        self.nodes[0].setgov({ "LOAN_LIQUIDATION_PENALTY": Decimal('0.01000000')})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov("LOAN_LIQUIDATION_PENALTY")["LOAN_LIQUIDATION_PENALTY"], Decimal('0.01000000'))

        # Set Gov var change 10 blocks ahead.
        self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.02000000')}, self.nodes[0].getblockcount() + 10)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ORACLE_DEVIATION')['ORACLE_DEVIATION'], Decimal('0.01000000'))
        self.nodes[0].generate(9)
        assert_equal(self.nodes[0].getgov('ORACLE_DEVIATION')['ORACLE_DEVIATION'], Decimal('0.02000000'))

        # Rollback change and make sure it is restored.
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        assert_equal(self.nodes[0].getgov('ORACLE_DEVIATION')['ORACLE_DEVIATION'], Decimal('0.01000000'))

        # Move forward again
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ORACLE_DEVIATION')['ORACLE_DEVIATION'], Decimal('0.02000000'))

        # Test multiple queued changes and make sure the last one is that one that take effect
        activate = self.nodes[0].getblockcount() + 10
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.1, "2": 0.2, "3": 0.7 }}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.2, "2": 0.2, "3": 0.6 }}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.3, "2": 0.2, "3": 0.5 }}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.4, "2": 0.2, "3": 0.4 }}, activate)
        self.nodes[0].generate(7)
        assert_equal(self.nodes[0].getgov('LP_SPLITS')['LP_SPLITS'], {'1': Decimal('0.40000000'), '2': Decimal('0.20000000'), '3': Decimal('0.40000000')})

        # Test multiple updates on future height with multiple changes.
        activate = self.nodes[0].getblockcount() + 10
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.5, "2": 0.2, "3": 0.3 }}, activate)
        self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.03000000')}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.04000000')}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.6, "2": 0.2, "3": 0.2 }}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.05000000')}, activate)
        self.nodes[0].generate(1)
        self.nodes[0].setgovheight({"LP_SPLITS": { "1": 0.7, "2": 0.2, "3": 0.1 }}, activate)
        self.nodes[0].generate(6)
        assert_equal(self.nodes[0].getgov('LP_SPLITS')['LP_SPLITS'], {'1': Decimal('0.70000000'), '2': Decimal('0.20000000'), '3': Decimal('0.10000000')})
        assert_equal(self.nodes[0].getgov('ORACLE_DEVIATION')['ORACLE_DEVIATION'], Decimal('0.05000000'))

        # Try and set less than 100%
        try:
            self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": { "1": 0.1, "2": 0.2, "3": 0.3 }})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("60000000 vs expected 100000000" in errorString)

        # Now set LP_LOAN_TOKEN_SPLITS
        self.nodes[0].setgov({"LP_LOAN_TOKEN_SPLITS": { "1": 0.1, "2": 0.2, "3": 0.7 }})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('LP_LOAN_TOKEN_SPLITS')['LP_LOAN_TOKEN_SPLITS'], {'1': Decimal('0.10000000'), '2': Decimal('0.20000000'), '3': Decimal('0.70000000')})

        # Check reward set on pool pairs
        assert_equal(self.nodes[0].getpoolpair("1", True)['1']['rewardLoanPct'], Decimal('0.10000000'))
        assert_equal(self.nodes[0].getpoolpair("2", True)['2']['rewardLoanPct'], Decimal('0.20000000'))
        assert_equal(self.nodes[0].getpoolpair("3", True)['3']['rewardLoanPct'], Decimal('0.70000000'))

        # Test listgovs
        result = self.nodes[0].listgovs()
        assert_equal(result[0][0]['ICX_TAKERFEE_PER_BTC'], Decimal('0E-8'))
        assert_equal(result[1][0]['LP_DAILY_LOAN_TOKEN_REWARD'], Decimal('13921.42315824'))
        assert_equal(result[2][0]['LP_LOAN_TOKEN_SPLITS'], {'1': Decimal('0.10000000'), '2': Decimal('0.20000000'), '3': Decimal('0.70000000')})
        assert_equal(result[3][0]['LP_DAILY_DFI_REWARD'], Decimal('14355.76253472'))
        assert_equal(result[4][0]['LOAN_LIQUIDATION_PENALTY'], Decimal('0.01000000'))
        assert_equal(result[5][0]['LP_SPLITS'], {'1': Decimal('0.70000000'), '2': Decimal('0.20000000'), '3': Decimal('0.10000000')} )
        assert_equal(result[6][0]['ORACLE_BLOCK_INTERVAL'], 0)
        assert_equal(result[7][0]['ORACLE_DEVIATION'], Decimal('0.05000000'))

        # Test visibility of pending changes in setgov
        activate = self.nodes[0].getblockcount() + 100
        self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.06000000')}, activate)
        self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.07000000')}, activate + 1)
        self.nodes[0].setgovheight({ "ICX_TAKERFEE_PER_BTC": Decimal('0.00100000')}, activate)
        self.nodes[0].setgovheight({ "ICX_TAKERFEE_PER_BTC": Decimal('0.00200000')}, activate + 1)
        self.nodes[0].generate(1)

        # Test visibility of pending changes in listgovs
        result = self.nodes[0].listgovs()
        assert_equal(result[0][0]['ICX_TAKERFEE_PER_BTC'], Decimal('0E-8'))
        assert_equal(result[0][1][str(activate)], Decimal('0.00100000'))
        assert_equal(result[0][2][str(activate + 1)], Decimal('0.00200000'))
        assert_equal(result[7][0]['ORACLE_DEVIATION'], Decimal('0.05000000'))
        assert_equal(result[7][1][str(activate)], Decimal('0.06000000'))
        assert_equal(result[7][2][str(activate + 1)], Decimal('0.07000000'))

        # Test setting on next interval
        interval = 6 # regtest default to 60 * 60 / 600
        self.nodes[0].generate((self.nodes[0].getblockcount() % interval) + 1)
        self.nodes[0].setgov({ "ORACLE_BLOCK_INTERVAL": 20})
        self.nodes[0].generate(1)
        interval = self.nodes[0].getgov("ORACLE_BLOCK_INTERVAL")["ORACLE_BLOCK_INTERVAL"]
        assert_equal(self.nodes[0].getgov("ORACLE_BLOCK_INTERVAL")["ORACLE_BLOCK_INTERVAL"], 20)

        # Set and view change pending for next interval
        self.nodes[0].setgov({ "ORACLE_BLOCK_INTERVAL": 30})
        self.nodes[0].generate(1)
        result = self.nodes[0].listgovs()
        assert_equal(result[6][0]['ORACLE_BLOCK_INTERVAL'], 20)
        count = self.nodes[0].getblockcount()
        nextInterval = count + (interval - (count % interval))
        assert_equal(result[6][1][str(nextInterval)], 30)

        # Move to next interval and make sure it applies
        self.nodes[0].generate(nextInterval)
        result = self.nodes[0].listgovs()
        assert_equal(len(result[6]), 1)
        assert_equal(result[6][0]['ORACLE_BLOCK_INTERVAL'], 30)

        # Test ATTRIBUTE before FCH
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningHill", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/payback_dfi':'true'}})

        # Move to FCH fork
        self.nodes[0].generate(1110 - self.nodes[0].getblockcount())

        # Set ATTRIBUTE with invalid settings
        assert_raises_rpc_error(-5, "Object of values expected", self.nodes[0].setgov, {"ATTRIBUTES":'error'})
        assert_raises_rpc_error(-5, "Object of values expected", self.nodes[0].setgov, {"ATTRIBUTES":'{}'})
        assert_raises_rpc_error(-5, "Empty version", self.nodes[0].setgov, {"ATTRIBUTES":{'':'true'}})
        assert_raises_rpc_error(-5, "Unsupported version", self.nodes[0].setgov, {"ATTRIBUTES":{'1/token/15/payback_dfi':'true'}})
        assert_raises_rpc_error(-5, "Empty value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/15/payback_dfi':''}})
        assert_raises_rpc_error(-5, "Incorrect key for <type>. Object of ['<version>/<type>/ID/<key>','value'] expected", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/payback_dfi':'true'}})
        assert_raises_rpc_error(-5, "Unrecognised type argument provided, valid types are: locks, oracles, params, poolpairs, token,", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/unrecognised/5/payback_dfi':'true'}})
        assert_raises_rpc_error(-5, "Unrecognised key argument provided, valid keys are: dex_in_fee_pct, dex_out_fee_pct, dfip2203, fixed_interval_price_id, loan_collateral_enabled, loan_collateral_factor, loan_minting_enabled, loan_minting_interest, loan_payback, loan_payback_fee_pct, payback_dfi, payback_dfi_fee_pct,", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/unrecognised':'true'}})
        assert_raises_rpc_error(-5, "Value must be an integer", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/not_a_number/payback_dfi':'true'}})
        assert_raises_rpc_error(-5, 'Boolean value must be either "true" or "false"', self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/payback_dfi':'not_a_number'}})
        assert_raises_rpc_error(-5, 'Boolean value must be either "true" or "false"', self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/payback_dfi':'unrecognised'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/payback_dfi_fee_pct':'not_a_number'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/payback_dfi_fee_pct':'-1'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: No such loan token (5)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/payback_dfi':'true'}})
        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/active':'not_a_bool'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/minswap':'-1'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/minswap':'not_a_number'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/premium': 'not_a_number'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/premium': '-1'}})
        assert_raises_rpc_error(-5, "Unsupported type for DFIP2201", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/reward_pct': '0.1'}})
        assert_raises_rpc_error(-5, "Unsupported type for DFIP2201", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/block_period': '2880'}})
        assert_raises_rpc_error(-5, "Percentage exceeds 100%", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2201/premium': '1.00000001'}})

        # Check DFIP2203 attributes cannot be set before FCR
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningRoad", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/active':'true'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningRoad", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/reward_pct':'0.05'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningRoad", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/block_period':'2880'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningRoad", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/dfip2203':'true'}})

        # Setup for loan related tests
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        appoint_oracle_tx = self.nodes[0].appointoracle(oracle_address, [{"currency": "USD", "token": "DFI"},{"currency": "USD", "token": "TSLA"},{"currency": "USD", "token": "AMZN"}], 1)
        self.nodes[0].generate(1)

        oracle_prices = [{"currency": "USD", "tokenAmount": "100.00000000@DFI"},{"currency": "USD", "tokenAmount": "100.00000000@TSLA"},{"currency": "USD", "tokenAmount": "100.00000000@AMZN"}]
        self.nodes[0].setoracledata(appoint_oracle_tx, int(time.time()), oracle_prices)
        self.nodes[0].generate(12)

        self.nodes[0].createloanscheme(150, 5, 'LOAN0001')
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            "symbol": "DUSD",
            "name": "DUSD",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        self.nodes[0].setloantoken({
                                    'symbol': 'TSLA',
                                    'name': "TSLA",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': False,
                                    'interest': 5})
        self.nodes[0].generate(1)

        self.nodes[0].setcollateraltoken({
            'token': "DUSD",
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)

        # Test setting of new Gov var
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/5/payback_dfi':'true'}})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'true'})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/5/payback_dfi_fee_pct':'0.05'}})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.05'})
        assert_equal(self.nodes[0].listgovs()[8][0]['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.05'})

        # Test setting multiple ATTRIBUTES
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/5/payback_dfi':'false','v0/token/5/payback_dfi_fee_pct':'0.02378'}})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'false', 'v0/token/5/payback_dfi_fee_pct': '0.02378'})

        # Test pending change
        activate = self.nodes[0].getblockcount() + 10
        self.nodes[0].setgovheight({"ATTRIBUTES":{'v0/token/5/payback_dfi':'true','v0/token/5/payback_dfi_fee_pct':'0.01'}}, activate)
        self.nodes[0].generate(9)

        # No change yet
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'false', 'v0/token/5/payback_dfi_fee_pct': '0.02378'})

        # Pending change present
        assert_equal(self.nodes[0].listgovs()[8][1][str(activate)], {'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.01'})

        # Check pending change applied
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.01'})
        assert_equal(self.nodes[0].listgovs()[8][0]['ATTRIBUTES'], {'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.01'})

        # Test params
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/active':'true','v0/params/dfip2201/minswap':'0.001','v0/params/dfip2201/premium':'0.025'}})
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES'], {'v0/params/dfip2201/active': 'true', 'v0/params/dfip2201/premium': '0.025', 'v0/params/dfip2201/minswap': '0.001', 'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.01'})
        assert_equal(self.nodes[0].listgovs()[8][0]['ATTRIBUTES'], {'v0/params/dfip2201/active': 'true', 'v0/params/dfip2201/premium': '0.025', 'v0/params/dfip2201/minswap': '0.001', 'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.01'})

        # Move to FCR fork
        self.nodes[0].generate(1150 - self.nodes[0].getblockcount())

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/5/loan_payback/1': 'true', 'v0/token/5/loan_payback/2': 'true', 'v0/token/5/loan_payback_fee_pct/1': '0.25'}})
        self.nodes[0].generate(1)
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/token/5/loan_payback/1'], 'true')
        assert_equal(attributes['v0/token/5/loan_payback/2'], 'true')
        assert_equal(attributes['v0/token/5/loan_payback_fee_pct/1'], '0.25')

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/5/loan_payback/0': 'true', 'v0/token/5/loan_payback_fee_pct/0': '0.33'}})
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        # dfi keys are set
        assert_equal(attributes['v0/token/5/payback_dfi'], 'true')
        assert_equal(attributes['v0/token/5/payback_dfi_fee_pct'], '0.33')
        # no new keys for DFI
        assert('v0/token/5/loan_payback/0' not in attributes)
        assert('v0/token/5/loan_payback_fee_pct/0' not in attributes)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/token/5/dex_in_fee_pct':'0.6','v0/token/5/dex_out_fee_pct':'0.12'}})
        self.nodes[0].generate(1)

        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/token/5/dex_in_fee_pct'], '0.6')
        assert_equal(attributes['v0/token/5/dex_out_fee_pct'], '0.12')

        # Set FCR ATTRIBUTE with invalid settings
        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/active':'not_a_bool'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/reward_pct':'not_a_number'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/reward_pct':'-1'}})
        assert_raises_rpc_error(-5, "Percentage exceeds 100%", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/reward_pct':'2'}})
        assert_raises_rpc_error(-5, "Value must be a positive integer", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/block_period':'not_a_number'}})
        assert_raises_rpc_error(-5, "Value must be a positive integer", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/block_period':'-1'}})
        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/dfip2203':'not_a_bool'}})
        assert_raises_rpc_error(-32600, "No such loan token", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/4/dfip2203':'true'}})
        assert_raises_rpc_error(-5, "Unsupported type for DFIP2203", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/premium': '0.025'}})
        assert_raises_rpc_error(-5, "Unsupported type for DFIP2203", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2203/minswap': '0.025'}})

        # Test setting FCR ATTRBIUTES
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/reward_pct':'0.05','v0/params/dfip2203/block_period':'20160','v0/token/5/dfip2203':'true'}})
        self.nodes[0].generate(1)
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2203/active':'true'}})
        self.nodes[0].generate(1)

        # Verify FCR results
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2203/active'], 'true')
        assert_equal(result['v0/params/dfip2203/reward_pct'], '0.05')
        assert_equal(result['v0/params/dfip2203/block_period'], '20160')
        assert_equal(result['v0/token/5/dfip2203'], 'true')

        # Test listgovs additional attributes

        # prefix filter
        result = self.nodes[0].listgovs("token")
        # Should only have attributes
        assert_equal(len(result[0]), 1)
        # Verify attributes
        assert_equal(result[0][0], {'ATTRIBUTES': {'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.33', 'v0/token/5/loan_payback/1': 'true', 'v0/token/5/loan_payback/2': 'true', 'v0/token/5/loan_payback_fee_pct/1': '0.25', 'v0/token/5/dex_in_fee_pct': '0.6', 'v0/token/5/dex_out_fee_pct': '0.12', 'v0/token/5/dfip2203': 'true'}})

        result = self.nodes[0].listgovs("token/5/loan")
        # Should only have attributes
        assert_equal(len(result[0]), 1)
        # Verify attributes
        assert_equal(result[0][0], {'ATTRIBUTES': {'v0/token/5/loan_payback/1': 'true', 'v0/token/5/loan_payback/2': 'true', 'v0/token/5/loan_payback_fee_pct/1': '0.25'}})

        # test for manual v0 prefix
        result = self.nodes[0].listgovs("v0/token/5/loan")
        # Should only have attributes
        assert_equal(len(result[0]), 1)
        # Verify attributes
        assert_equal(result[0][0], {'ATTRIBUTES': {'v0/token/5/loan_payback/1': 'true', 'v0/token/5/loan_payback/2': 'true', 'v0/token/5/loan_payback_fee_pct/1': '0.25'}})

        # gov only test
        result = self.nodes[0].listgovs("gov")
        assert_equal(result, [[{'ICX_TAKERFEE_PER_BTC': Decimal('0.00200000')}], [{'LP_DAILY_LOAN_TOKEN_REWARD': Decimal('13020.86331792')}], [{'LP_LOAN_TOKEN_SPLITS': {'1': Decimal('0.10000000'), '2': Decimal('0.20000000'), '3': Decimal('0.70000000')}}], [{'LP_DAILY_DFI_REWARD': Decimal('13427.10581184')}], [{'LOAN_LIQUIDATION_PENALTY': Decimal('0.01000000')}], [{'LP_SPLITS': {'1': Decimal('0.70000000'), '2': Decimal('0.20000000'), '3': Decimal('0.10000000')}}], [{'ORACLE_BLOCK_INTERVAL': 30}], [{'ORACLE_DEVIATION': Decimal('0.07000000')}], []])

        # live test. No live on this. For now, just ensure, no other attrs are listed
        assert_equal(self.nodes[0].listgovs("live"), [[{'ATTRIBUTES': {}}]])

        result_all = self.nodes[0].listgovs("all")
        result_27 = self.nodes[0].listgovs("v/2.7")
        result = self.nodes[0].listgovs()

        # In this particular test, in the fork period, it's the same.
        # TODO: Need to add more tests
        assert_equal(result, result_27)
        assert_equal(result, result_all)

        assert_equal(result, [[{'ICX_TAKERFEE_PER_BTC': Decimal('0.00200000')}], [{'LP_DAILY_LOAN_TOKEN_REWARD': Decimal('13020.86331792')}], [{'LP_LOAN_TOKEN_SPLITS': {'1': Decimal('0.10000000'), '2': Decimal('0.20000000'), '3': Decimal('0.70000000')}}], [{'LP_DAILY_DFI_REWARD': Decimal('13427.10581184')}], [{'LOAN_LIQUIDATION_PENALTY': Decimal('0.01000000')}], [{'LP_SPLITS': {'1': Decimal('0.70000000'), '2': Decimal('0.20000000'), '3': Decimal('0.10000000')}}], [{'ORACLE_BLOCK_INTERVAL': 30}], [{'ORACLE_DEVIATION': Decimal('0.07000000')}], [{'ATTRIBUTES': {'v0/params/dfip2201/active': 'true', 'v0/params/dfip2201/premium': '0.025', 'v0/params/dfip2201/minswap': '0.001', 'v0/params/dfip2203/active': 'true', 'v0/params/dfip2203/reward_pct': '0.05', 'v0/params/dfip2203/block_period': '20160', 'v0/token/5/payback_dfi': 'true', 'v0/token/5/payback_dfi_fee_pct': '0.33', 'v0/token/5/loan_payback/1': 'true', 'v0/token/5/loan_payback/2': 'true', 'v0/token/5/loan_payback_fee_pct/1': '0.25', 'v0/token/5/dex_in_fee_pct': '0.6', 'v0/token/5/dex_out_fee_pct': '0.12', 'v0/token/5/dfip2203': 'true'}}]])

        # Check errors
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/locks/token/5':'true'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/4000': '1/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':'TSLA/USD'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_enabled':'true'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_factor':'1.00000000'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_enabled':'true'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set before FortCanningCrunch", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_interest':'5.00000000'}})

        # Move to FCC fork
        self.nodes[0].generate(1200 - self.nodes[0].getblockcount())

        # Check errors
        assert_raises_rpc_error(-5, "Unrecognised locks argument provided, valid lockss are: token,", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/locks/oracle/5':'true'}})
        assert_raises_rpc_error(-32600, "No loan token with id (4)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/locks/token/4':'true'}})
        assert_raises_rpc_error(-5, "Unrecognised key argument provided", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/ascendant': '1'}})
        assert_raises_rpc_error(-5, "Unrecognised key argument provided", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/descendant': '1'}})
        assert_raises_rpc_error(-5, "Unrecognised key argument provided", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/epitaph': '1'}})
        assert_raises_rpc_error(-5, "Value must be an integer", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1200': '1/50,600'}})
        assert_raises_rpc_error(-5, "Two int values expected for split in id/mutliplier", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1200':'1/50,5/10'}})
        assert_raises_rpc_error(-5, "Mutliplier cannot be zero", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1201': '1/0'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Token (127) does not exist", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1201': '127/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Only DATs can be split", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1201': '128/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Tokenised DFI cannot be split", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1201': '0/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Pool tokens cannot be split", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/1201': '1/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set at or below current height", self.nodes[0].setgov, {"ATTRIBUTES":{f'v0/oracles/splits/{self.nodes[0].getblockcount()}': '5/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cannot be set at or below current height", self.nodes[0].setgov, {"ATTRIBUTES":{f'v0/oracles/splits/{self.nodes[0].getblockcount() + 1}': '5/50'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Cumulative application of Gov vars failed: Cannot be set at or below current height", self.nodes[0].setgovheight, {"ATTRIBUTES":{f'v0/oracles/splits/2000': '5/50'}}, 2000)
        assert_raises_rpc_error(-32600, "No loan token with id (4)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/oracles/splits/4000':'4/2'}})
        assert_raises_rpc_error(-32600, "ATTRIBUTES: Price feed DUFF/USD does not belong to any oracle", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':'DUFF/USD'}})
        assert_raises_rpc_error(-5, "Empty token / currency", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':' /USD'}})
        assert_raises_rpc_error(-5, "Empty token / currency", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':'DUFF/ '}})
        assert_raises_rpc_error(-5, "Empty token / currency", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':' / '}})
        assert_raises_rpc_error(-32600, "No such token (127)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/127/fixed_interval_price_id':'TSLA/USD'}})
        assert_raises_rpc_error(-5, "Exactly two entires expected for currency pair", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':'5'}})
        assert_raises_rpc_error(-5, "Exactly two entires expected for currency pair", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/fixed_interval_price_id':'5/10/20'}})
        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_enabled':'not_a_bool'}})
        assert_raises_rpc_error(-32600, "No such token (127)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/127/loan_collateral_enabled':'true'}})
        assert_raises_rpc_error(-32600, "Fixed interval price currency pair must be set first", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_enabled':'true'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_factor':'-1'}})
        assert_raises_rpc_error(-5, "Percentage exceeds 100%", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_factor':'1.00000001'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_factor':'not_a_number'}})
        assert_raises_rpc_error(-32600, "No such token (127)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/127/loan_collateral_factor':'1'}})
        assert_raises_rpc_error(-32600, "Fixed interval price currency pair must be set first", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_collateral_factor':'1'}})
        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_enabled':'not_a_bool'}})
        assert_raises_rpc_error(-32600, "No such token (127)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/127/loan_minting_enabled':'true'}})
        assert_raises_rpc_error(-32600, "Fixed interval price currency pair must be set first", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_enabled':'true'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_interest':'-1'}})
        assert_raises_rpc_error(-5, "Amount must be a positive value", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_interest':'not_a_number'}})
        assert_raises_rpc_error(-32600, "No such token (127)", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/127/loan_minting_interest':'1'}})
        assert_raises_rpc_error(-32600, "Fixed interval price currency pair must be set first", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/token/5/loan_minting_interest':'1'}})


        # Set locks
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/locks/token/5':'true'}})
        self.nodes[0].generate(1)

        # Check locks
        attriutes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attriutes['v0/locks/token/5'], 'true')

        # Set loan token for 4
        self.nodes[0].setgov({"ATTRIBUTES":{f'v0/token/4/fixed_interval_price_id':'TSLA/USD', f'v0/token/4/loan_minting_enabled':'true', f'v0/token/4/loan_minting_interest':'1'}})
        self.nodes[0].generate(1)

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/oracles/splits/4000':'4/50'}})
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/oracles/splits/4001':'5/5'}})
        self.nodes[0].generate(1)

        # Check auto lock
        assert_equal(self.nodes[0].listgovs()[8][1]['3928'], {'v0/locks/token/4': 'true'})

        # Disable auto lock
        self.nodes[0].setgovheight({"ATTRIBUTES":{'v0/locks/token/4':'false'}}, 3928)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listgovs()[8][1]['3928'], {'v0/locks/token/4': 'false'})

        # Re-enable auto lock
        self.nodes[0].setgovheight({"ATTRIBUTES":{'v0/locks/token/4':'true'}}, 3928)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].listgovs()[8][1]['3928'], {'v0/locks/token/4': 'true'})

        # Set pending split
        activation_height = self.nodes[0].getblockcount() + 5
        self.nodes[0].setgovheight({"ATTRIBUTES":{'v0/oracles/splits/4001':'5/10'}}, activation_height)
        self.nodes[0].generate(1)

        # Check value unchanged
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/oracles/splits/4001'], '5/5')

        # Move to pending height
        self.nodes[0].generate(activation_height - self.nodes[0].getblockcount())

        # Check change is applied
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/oracles/splits/4001'], '5/10')

        # Set stored lock
        activation_height = self.nodes[0].getblockcount() + 5
        self.nodes[0].setgovheight({"ATTRIBUTES":{'v0/locks/token/4':'true'}}, activation_height)
        self.nodes[0].generate(1)

        # Make sure lock pending but not active
        attributes = self.nodes[0].listgovs()[8]
        assert('v0/locks/token/4' not in attributes[0]['ATTRIBUTES'])
        assert_equal(attributes[1][str(activation_height)]['v0/locks/token/4'], 'true')

        # Move to pending height
        self.nodes[0].generate(activation_height - self.nodes[0].getblockcount())

        # Check lock now present
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/locks/token/4'], 'true')

        # Disable lock
        self.nodes[0].setgov({"ATTRIBUTES":{'v0/locks/token/5':'false'}})
        self.nodes[0].generate(1)

        # Set split
        activation_height = self.nodes[0].getblockcount() + 5
        self.nodes[0].setgovheight({"ATTRIBUTES":{f'v0/oracles/splits/{activation_height + 10}':'5/10'}}, activation_height)
        self.nodes[0].generate(1)

        # Move to pending height
        self.nodes[0].generate(activation_height - self.nodes[0].getblockcount())

        # Check auto lock
        attributes = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(attributes['v0/locks/token/5'], 'true')

        # Move to FCG fork
        self.nodes[0].generate(1250 - self.nodes[0].getblockcount())

        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2206/direct_interest_dusd_burn':'not_a_bool'}})
        assert_raises_rpc_error(-5, "Boolean value must be either \"true\" or \"false\"", self.nodes[0].setgov, {"ATTRIBUTES":{'v0/params/dfip2206/direct_loan_dusd_burn':'not_a_bool'}})

        self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2206/direct_interest_dusd_burn':'true', 'v0/params/dfip2206/direct_loan_dusd_burn':'true'}})
        self.nodes[0].generate(1)

        # Verify FCR results
        result = self.nodes[0].getgov('ATTRIBUTES')['ATTRIBUTES']
        assert_equal(result['v0/params/dfip2206/direct_interest_dusd_burn'], 'true')
        assert_equal(result['v0/params/dfip2206/direct_loan_dusd_burn'], 'true')

if __name__ == '__main__':
    GovsetTest ().main ()

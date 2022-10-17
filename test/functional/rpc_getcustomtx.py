#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getcustomtx RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, disconnect_nodes
from decimal import Decimal
import calendar
import time

class TokensRPCGetCustomTX(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120', '-eunosheight=120', '-eunospayaheight=120', '-fortcanningheight=120', '-fortcanninghillheight=122'], # Wallet TXs
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120', '-eunosheight=120', '-eunospayaheight=120', '-fortcanningheight=120', '-fortcanninghillheight=122', '-txindex=1'], # Transaction index
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120', '-eunosheight=120', '-fortcanningheight=120', '-fortcanninghillheight=122']] # Will not find historical TXs

    def check_result(self, result):
        # Get block hash and height
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Check the constant keys in getcustomtx
        assert_equal(result['valid'], True)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_blocks()
        num_tokens = len(self.nodes[0].listtokens())

        # collateral address
        collateral_a = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        create_token_tx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Get history node 0 from wallet TX
        result = self.nodes[0].getcustomtx(create_token_tx)
        self.check_result(result)
        assert_equal(result['type'], "CreateToken")
        assert_equal(result['results']['creationTx'], create_token_tx)
        assert_equal(result['results']['symbol'], "GOLD")
        assert_equal(result['results']['name'], "gold")
        assert_equal(result['results']['mintable'], True)
        assert_equal(result['results']['tradeable'], True)
        assert_equal(result['results']['finalized'], False)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_silver = idx

        # Mint some tokens
        minttx = self.nodes[0].minttokens(["300@" + token_silver])
        self.nodes[0].generate(1)
        self.sync_blocks()

        # Check tokens are minted
        assert_equal(self.nodes[0].getaccount(collateral_a, {}, True)[token_silver], 300)

        # All nodes agree
        assert_equal(self.nodes[0].gettoken(token_silver)[token_silver]['minted'], 300)
        assert_equal(self.nodes[1].gettoken(token_silver)[token_silver]['minted'], 300)
        assert_equal(self.nodes[2].gettoken(token_silver)[token_silver]['minted'], 300)

        # Get history node 0 from wallet TX
        result = self.nodes[0].getcustomtx(minttx)
        self.check_result(result)
        assert_equal(result['type'], "MintToken")
        assert_equal(result['results'][str(token_silver)], Decimal("300.00000000"))

        # Get history node 1 with transaction index
        result = self.nodes[1].getcustomtx(minttx)
        self.check_result(result)
        assert_equal(result['type'], "MintToken")
        assert_equal(result['results'][str(token_silver)], Decimal("300.00000000"))

        # Try and get history node 2
        try:
            result = self.nodes[2].getcustomtx(minttx)
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No such mempool or wallet transaction. Use -txindex or provide a block hash." in errorString)

        # Try and get history node 2 using block hash
        result = self.nodes[2].getcustomtx(minttx, self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.check_result(result)
        assert_equal(result['type'], "MintToken")
        assert_equal(result['results'][str(token_silver)], Decimal("300.00000000"))

        # Node 2 no longer needed
        disconnect_nodes(self.nodes[0], 2)
        disconnect_nodes(self.nodes[1], 2)

        # Update token
        updatetx = self.nodes[0].updatetoken(token_silver, {"symbol":"SILVER","name":"silver","mintable":False,"tradeable":False})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get history node 1 with transaction index
        result = self.nodes[1].getcustomtx(updatetx)
        self.check_result(result)
        assert_equal(result['type'], "UpdateTokenAny")
        assert_equal(result['results']['symbol'], "SILVER")
        assert_equal(result['results']['name'], "silver")
        assert_equal(result['results']['mintable'], False)
        assert_equal(result['results']['tradeable'], False)

        # Test MN call
        num_mns = len(self.nodes[0].listmasternodes())
        collateral = self.nodes[0].getnewaddress("", "legacy")
        mn_txid = self.nodes[0].createmasternode(collateral)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Make sure new MN was successfully created
        assert_equal(len(self.nodes[0].listmasternodes()), num_mns + 1)

        # Get custom TX
        result = self.nodes[1].getcustomtx(mn_txid)
        self.check_result(result)
        assert_equal(result['type'], "CreateMasternode")
        assert_equal(result['results']['collateralamount'], Decimal("10.00000000"))
        assert_equal(result['results']['masternodeoperator'], collateral)

        # Test resign MN call
        resign_tx = self.nodes[0].resignmasternode(mn_txid)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Check MN in PRE_RESIGNED state
        assert_equal(self.nodes[0].listmasternodes()[mn_txid]['state'], "PRE_RESIGNED")

        # Get custom TX
        result = self.nodes[1].getcustomtx(resign_tx)
        self.check_result(result)
        assert_equal(result['type'], "ResignMasternode")
        assert_equal(result['results']['id'], mn_txid)

        # Test poolpair call
        num_tokens = len(self.nodes[0].listtokens())

        # collateral address
        collateral_b = self.nodes[0].getnewaddress("", "legacy")

        # Create token
        create_token_tx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "gold",
            "collateralAddress": collateral_b
        })
        self.nodes[0].generate(1)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_gold = idx

        # Mint some tokens for use later
        self.nodes[0].minttokens(["300@" + token_gold])
        self.nodes[0].generate(1)

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Create pool pair
        pool_collateral = self.nodes[0].getnewaddress("", "legacy")
        poolpair_tx = self.nodes[0].createpoolpair({
            "tokenA": "SILVER#" + token_silver,
            "tokenB": "GOLD#" + token_gold,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOLD"
        })
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(poolpair_tx)
        assert_equal(result['type'], "CreatePoolPair")
        self.check_result(result)
        assert_equal(result['results']['creationTx'], poolpair_tx)
        assert_equal(result['results']['name'], "silver-gold")
        assert_equal(result['results']['symbol'], "SILVGOLD")
        assert_equal(result['results']['tokenA'], "silver")
        assert_equal(result['results']['tokenB'], "gold")
        assert_equal(result['results']['commission'], Decimal("0.00100000"))
        assert_equal(result['results']['status'], True)
        assert_equal(result['results']['ownerAddress'], pool_collateral)
        assert_equal(result['results']['isDAT'], True)
        assert_equal(result['results']['mintable'], False)
        assert_equal(result['results']['tradeable'], True)
        assert_equal(result['results']['finalized'], True)

        # Test account to account TX
        accounttoaccount_tx = self.nodes[0].accounttoaccount(collateral_b, {collateral_a: "100@" + token_gold})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(accounttoaccount_tx)
        self.check_result(result)
        assert_equal(result['type'], "AccountToAccount")
        assert_equal(result['results']['from'], collateral_b)
        assert_equal([*result['results']['to']][0], collateral_a)
        assert_equal(list(result['results']['to'].values())[0], "100.00000000@" + token_gold)

        # Test add pool liquidity TX
        pool_share = self.nodes[0].getnewaddress("", "legacy")
        add_liquidity_tx = self.nodes[0].addpoolliquidity({
            collateral_a: ['100@' + token_silver, '100@' + token_gold]
            }, pool_share)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(add_liquidity_tx)
        self.check_result(result)
        assert_equal(result['type'], "AddPoolLiquidity")
        assert_equal(result['valid'], True)
        assert_equal(result['results'][token_silver], Decimal("100.00000000"))
        assert_equal(result['results'][token_gold], Decimal("100.00000000"))
        assert_equal(result['results']['shareaddress'], pool_share)

        # Test pool swap TX
        poolswap_tx = self.nodes[0].poolswap({
            "from": collateral_a,
            "tokenFrom": token_silver,
            "amountFrom": 1,
            "to": collateral_b,
            "tokenTo": token_gold,
            "maxPrice": 2
        })
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(poolswap_tx)
        self.check_result(result)
        assert_equal(result['type'], "PoolSwap")
        assert_equal(result['results']['fromAddress'], collateral_a)
        assert_equal(result['results']['fromToken'], token_silver)
        assert_equal(result['results']['fromAmount'], Decimal("1.00000000"))
        assert_equal(result['results']['toAddress'], collateral_b)
        assert_equal(result['results']['toToken'], token_gold)
        assert_equal(result['results']['maxPrice'], Decimal("2.00000000"))

        # Test remove liquidity TX
        remove_liquidity_tx = self.nodes[0].removepoolliquidity(pool_share, "25@1")
        remove_liquidity_tx_rawtx = self.nodes[0].getrawtransaction(remove_liquidity_tx, 1)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(remove_liquidity_tx)
        self.check_result(result)
        assert_equal(result['type'], "RemovePoolLiquidity")
        assert_equal(result['results']['from'], pool_share)
        assert_equal(result['results']['amount'], "25.00000000@1")

        # Check autoauth
        result = self.nodes[1].getcustomtx(remove_liquidity_tx_rawtx['vin'][0]['txid'])
        self.check_result(result)
        assert_equal(result['type'], "AutoAuth")
        assert_equal(result['results'], {})

        # Test pool update TX
        poolpair_update_tx = self.nodes[0].updatepoolpair({
            "pool": poolpair_tx,
            "status": False,
            "commission": 0.1,
            "ownerAddress": pool_collateral
        })
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(poolpair_update_tx)
        self.check_result(result)
        assert_equal(result['type'], "UpdatePoolPair")
        assert_equal(result['results']['commission'], Decimal("0.10000000"))
        assert_equal(result['results']['status'], False)
        assert_equal(result['results']['ownerAddress'], pool_collateral)

        # Test UTXOs to account TX
        utxostoaccount_tx = self.nodes[0].utxostoaccount({collateral_a: "100@0"})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(utxostoaccount_tx)
        self.check_result(result)
        assert_equal(result['type'], "UtxosToAccount")
        assert_equal(result['results'][collateral_a], "100.00000000@0")

        # Test account to UTXOs TX
        accountoutxos_tx = self.nodes[0].accounttoutxos(collateral_a, {collateral_b: "1@0"})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(accountoutxos_tx)
        self.check_result(result)
        assert_equal(result['type'], "AccountToUtxos")
        assert_equal(result['results']['from'], collateral_a)
        assert_equal(list(result['results']['to'].keys())[0], collateral_b)
        assert_equal(list(result['results']['to'].values())[0], "1.00000000@0")


        # Test send tokens to address TX
        self.nodes[0].utxostoaccount({collateral_a: "1@0"})
        self.nodes[0].generate(1)

        tokenstoaddress_tx = self.nodes[0].sendtokenstoaddress({collateral_a:"1@0"}, {collateral_b:"1@0"})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(tokenstoaddress_tx)
        self.check_result(result)
        assert_equal(result['type'], "AnyAccountsToAccounts")
        assert_equal(list(result['results']['from'].keys())[0], collateral_a)
        assert_equal(list(result['results']['from'].values())[0], "1.00000000@0")
        assert_equal(list(result['results']['to'].keys())[0], collateral_b)
        assert_equal(list(result['results']['to'].values())[0], "1.00000000@0")

        # Test setgox TX
        setgov_tx = self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(setgov_tx)
        self.check_result(result)
        assert_equal(result['type'], "SetGovVariable")
        assert_equal(list(result['results'].keys())[0], "LP_DAILY_DFI_REWARD")
        assert_equal(list(result['results'].values())[0], Decimal("35.00000000"))

        # Dakota / EunosPaya height
        self.nodes[0].generate(120 - self.nodes[0].getblockcount())
        assert_equal(120, self.nodes[0].getblockcount())

        # Create new masternode with timelock
        collateral = self.nodes[0].getnewaddress("", "legacy")
        mn_txid = self.nodes[0].createmasternode(collateral, '', [], 'TENYEARTIMELOCK')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(mn_txid)
        self.check_result(result)
        assert_equal(result['type'], "CreateMasternode")
        assert_equal(result['results']['collateralamount'], Decimal("2.00000000"))
        assert_equal(result['results']['masternodeoperator'], collateral)
        assert_equal(result['results']['timelock'], 'TENYEARTIMELOCK')

        # Activate MN
        self.nodes[0].generate(19)

        # Test set loan scheme
        loan_txid = self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(loan_txid)
        self.check_result(result)
        assert_equal(result['type'], "LoanScheme")
        assert_equal(result['results']['id'], "LOANMAX")
        assert_equal(result['results']['mincolratio'], 1000)
        assert_equal(result['results']['interestrate'], Decimal("0.50000000"))
        assert_equal(result['results']['updateHeight'], False)

        # Test changing the loan scheme
        self.nodes[0].createloanscheme(100, 5, 'LOAN001')
        self.nodes[0].generate(1)
        default_txid = self.nodes[0].setdefaultloanscheme('LOAN001')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(default_txid)
        self.check_result(result)
        assert_equal(result['type'], "DefaultLoanScheme")
        assert_equal(result['results']['id'], "LOAN001")

        # Test destroying a loan scheme
        destroy_txid = self.nodes[0].destroyloanscheme('LOANMAX')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(destroy_txid)
        self.check_result(result)
        assert_equal(result['type'], "DestroyLoanScheme")
        assert_equal(result['results']['id'], "LOANMAX")

        # Test setting a forced address
        #reward_address = self.nodes[0].getnewaddress('', 'legacy')
        #forced_address_txid = self.nodes[0].setforcedrewardaddress(mn_txid, reward_address)
        #self.nodes[0].generate(1)
        #self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        #result = self.nodes[1].getcustomtx(forced_address_txid)
        #self.check_result(result)
        #assert_equal(result['type'], "SetForcedRewardAddress")
        #assert_equal(result['results']['mc_id'], mn_txid)
        #assert_equal(result['results']['rewardAddress'], reward_address)

        # Test removing a forced address
        #reward_address = self.nodes[0].getnewaddress('', 'legacy')
        #forced_address_txid = self.nodes[0].remforcedrewardaddress(mn_txid)
        #self.nodes[0].generate(1)
        #self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        #result = self.nodes[1].getcustomtx(forced_address_txid)
        #self.check_result(result)
        #assert_equal(result['type'], "RemForcedRewardAddress")
        #assert_equal(result['results']['mc_id'], mn_txid)

        # Test updating a masternode
        #new_operator_address = self.nodes[0].getnewaddress('', 'legacy')
        #update_mn_txid = self.nodes[0].updatemasternode(mn_txid, new_operator_address)
        #self.nodes[0].generate(1)
        #self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        #result = self.nodes[1].getcustomtx(update_mn_txid)
        #self.check_result(result)
        #assert_equal(result['type'], "UpdateMasternode")
        #assert_equal(result['results']['id'], mn_txid)
        #assert_equal(result['results']['masternodeoperator'], new_operator_address)

        # Test appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        appoint_oracle_tx = self.nodes[0].appointoracle(oracle_address, [{"currency": "GBP", "token": "TSLA"}], 1)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(appoint_oracle_tx)
        self.check_result(result)
        assert_equal(result['type'], "AppointOracle")
        assert_equal(result['results']['oracleAddress'], oracle_address)
        assert_equal(result['results']['weightage'], 1)
        assert_equal(result['results']['availablePairs'], [{'token': 'TSLA', 'currency': 'GBP'}])

        # Test appoint oracle
        new_oracle_address = self.nodes[0].getnewaddress("", "legacy")
        update_oracle_tx = self.nodes[0].updateoracle(appoint_oracle_tx, new_oracle_address, [{"currency": "USD", "token": "DFI"},{"currency": "USD", "token": "TSLA"}], 10)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(update_oracle_tx)
        self.check_result(result)
        assert_equal(result['type'], "UpdateOracleAppoint")
        assert_equal(result['results']['oracleId'], appoint_oracle_tx)
        assert_equal(result['results']['oracleAddress'], new_oracle_address)
        assert_equal(result['results']['weightage'], 10)
        assert_equal(result['results']['availablePairs'], [{'token': 'DFI', 'currency': 'USD'}, {'token': 'TSLA', 'currency': 'USD'}])

        # Test set oracle data
        oracle_prices = [{"currency": "USD", "tokenAmount": "100.00000000@DFI"},{"currency": "USD", "tokenAmount": "100.00000000@TSLA"}]
        timestamp = calendar.timegm(time.gmtime())
        oracle_data_tx = self.nodes[0].setoracledata(appoint_oracle_tx, timestamp, oracle_prices)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(oracle_data_tx)
        self.check_result(result)
        assert_equal(result['type'], "SetOracleData")
        assert_equal(result['results']['oracleId'], appoint_oracle_tx)
        assert_equal(result['results']['timestamp'], timestamp)
        assert_equal(result['results']['tokenPrices'], oracle_prices)

        # Composite poolswap set up
        self.nodes[0].createtoken({
            "symbol": "GOOGL",
            "name": "Google",
            "collateralAddress": collateral_a
        })
        self.nodes[0].generate(1)

        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOOGL"):
                token_googl = idx

        self.nodes[0].createpoolpair({
            "tokenA": "SILVER#" + token_silver,
            "tokenB": "GOOGL#" + token_googl,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOOGL"
        })
        self.nodes[0].generate(1)

        self.nodes[0].minttokens(["300@" + token_googl])
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            collateral_a: ['100@' + token_silver, '100@' + token_googl]
            }, pool_share)
        self.nodes[0].generate(1)

        self.nodes[0].updatepoolpair({
            "pool": poolpair_tx,
            "status": True,
            "commission": 0.001
        })
        self.nodes[0].generate(1)

        # Finally test the actual compositeswap
        destination = self.nodes[0].getnewaddress("", "legacy")
        composite_tx = self.nodes[0].compositeswap({
            "from": collateral_a,
            "tokenFrom": token_googl,
            "amountFrom": 1,
            "to": destination,
            "tokenTo": token_gold,
            "maxPrice": 2
        }, [])
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(composite_tx)
        self.check_result(result)
        assert_equal(result['type'], "PoolSwap")
        assert_equal(result['results']['fromAddress'], collateral_a)
        assert_equal(result['results']['fromToken'], token_googl)
        assert_equal(result['results']['fromAmount'], Decimal("1.00000000"))
        assert_equal(result['results']['toAddress'], destination)
        assert_equal(result['results']['toToken'], token_gold)
        assert_equal(result['results']['maxPrice'], Decimal("2.00000000"))
        assert_equal(result['results']['compositeDex'], 'SILVGOOGL/SILVGOLD')

        # Test set Governanace variable by height
        setgov_height_tx = self.nodes[0].setgovheight({ "ORACLE_DEVIATION": Decimal('0.04000000')}, 1000)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(setgov_height_tx)
        self.check_result(result)
        assert_equal(result['type'], "SetGovVariableHeight")
        assert_equal(list(result['results'].keys())[0], "ORACLE_DEVIATION")
        assert_equal(list(result['results'].values())[0], Decimal('0.04000000'))
        assert_equal(result['results']['startHeight'], 1000)

        # Setup for loan tests
        self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        self.nodes[0].generate(1)
        self.nodes[0].createloanscheme(150, 5, 'LOAN0001')
        self.nodes[0].generate(1)

        self.nodes[0].createtoken({
            "symbol": "DUSD",
            "name": "DUSD",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        # Test set loan token
        loan_token_tx = self.nodes[0].setloantoken({
                                    'symbol': 'TSLA',
                                    'name': "TSLA",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': False,
                                    'interest': 5})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(loan_token_tx)
        self.check_result(result)
        assert_equal(result['type'], "SetLoanToken")
        assert_equal(result['results']['symbol'], 'TSLA')
        assert_equal(result['results']['name'], 'TSLA')
        assert_equal(result['results']['mintable'], False)
        assert_equal(result['results']['interest'], Decimal('5.00000000'))

        # Test update loan token
        update_token_tx = self.nodes[0].updateloantoken(loan_token_tx ,{
                                    'name': "Tesla stock token",
                                    'mintable': True,
                                    'interest': 1})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(update_token_tx)
        self.check_result(result)
        assert_equal(result['type'], "UpdateLoanToken")
        assert_equal(result['results']['id'], loan_token_tx)
        assert_equal(result['results']['symbol'], 'TSLA')
        assert_equal(result['results']['name'], 'Tesla stock token')
        assert_equal(result['results']['mintable'], True)
        assert_equal(result['results']['interest'], Decimal('1.00000000'))

        self.nodes[0].createpoolpair({
            "tokenA": "TSLA",
            "tokenB": "DUSD",
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "TSLA-DUSD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].createpoolpair({
            "tokenA": "DFI",
            "tokenB": "DUSD",
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "DFI-DUSD"
        })
        self.nodes[0].generate(1)

        self.nodes[0].minttokens(["300@TSLA", "300@DUSD"])
        self.nodes[0].generate(1)

        self.nodes[0].utxostoaccount({self.nodes[0].get_genesis_keys().ownerAuthAddress: "100@DFI"})
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.nodes[0].get_genesis_keys().ownerAuthAddress: ['100@TSLA', '100@DUSD']
            }, pool_share)
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.nodes[0].get_genesis_keys().ownerAuthAddress: ['100@DUSD', '100@DFI']
            }, pool_share)
        self.nodes[0].generate(1)

        # Test set collateral token
        collateral_toke_tx = self.nodes[0].setcollateraltoken({
                                    'token': "DFI",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)

        # Get custom TX
        result = self.nodes[1].getcustomtx(collateral_toke_tx)
        assert_equal(result['type'], "SetLoanCollateralToken")
        assert_equal(result['results']['token'], 'DFI')
        assert_equal(result['results']['factor'], Decimal('1.00000000'))
        assert_equal(result['results']['fixedIntervalPriceId'], 'DFI/USD')

        # Get loan token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["name"] == "Tesla stock token"):
                token_loan_tesla = idx

        # Test create vault
        vault_owner = self.nodes[0].getnewaddress("", "legacy")
        create_vault_tx = self.nodes[0].createvault(vault_owner, 'LOANMAX')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(create_vault_tx)
        self.check_result(result)
        assert_equal(result['type'], "Vault")
        assert_equal(result['results']['ownerAddress'], vault_owner)
        assert_equal(result['results']['loanSchemeId'], 'LOANMAX')

        # Test update vault
        new_vault_owner = self.nodes[0].getnewaddress("", "legacy")
        update_vault_tx = self.nodes[0].updatevault(create_vault_tx, {'ownerAddress':new_vault_owner,'loanSchemeId': 'LOAN0001'})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(update_vault_tx)
        self.check_result(result)
        assert_equal(result['type'], "UpdateVault")
        assert_equal(result['results']['ownerAddress'], new_vault_owner)
        assert_equal(result['results']['loanSchemeId'], 'LOAN0001')

        # Test deposit to vault
        self.nodes[0].generate(5)
        deposit_vault_tx = self.nodes[0].deposittovault(create_vault_tx, collateral_a, '10@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(deposit_vault_tx)
        self.check_result(result)
        assert_equal(result['type'], "DepositToVault")
        assert_equal(result['results']['vaultId'], create_vault_tx)
        assert_equal(result['results']['from'], collateral_a)
        assert_equal(result['results']['amount'], '10.00000000@0')

        # Test take loan
        take_loan_tx = self.nodes[0].takeloan({'vaultId':create_vault_tx,'amounts': "1@TSLA"})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(take_loan_tx)
        self.check_result(result)
        assert_equal(result['type'], "TakeLoan")
        assert_equal(result['results']['vaultId'], create_vault_tx)
        assert_equal(result['results'][token_loan_tesla], Decimal('1.00000000'))

        # Give vault own some TSLA to cover fee
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {new_vault_owner: "1@TSLA"})
        self.nodes[0].generate(1)

        # Test pay back loan
        vault = self.nodes[0].getvault(create_vault_tx)
        payback_loan_tx = self.nodes[0].paybackloan({
            'vaultId':create_vault_tx,
            'from':new_vault_owner,
            'amounts':vault['loanAmounts']
            })
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(payback_loan_tx)
        self.check_result(result)
        assert_equal(result['type'], "PaybackLoan")
        assert_equal(result['results']['vaultId'], create_vault_tx)
        assert_equal(result['results']['from'], new_vault_owner)
        assert_equal(result['results'][token_loan_tesla], Decimal(vault['loanAmounts'][0][:10]))

        # Test withdraw from vault
        vault_withdraw_tx = self.nodes[0].withdrawfromvault(create_vault_tx, new_vault_owner, '0.5@DFI')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(vault_withdraw_tx)
        self.check_result(result)
        assert_equal(result['type'], "WithdrawFromVault")
        assert_equal(result['results']['vaultId'], create_vault_tx)
        assert_equal(result['results']['to'], new_vault_owner)
        assert_equal(result['results']['amount'], '0.50000000@0')

        # Test close vault
        vault_close_tx = self.nodes[0].closevault(create_vault_tx, new_vault_owner)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(vault_close_tx)
        self.check_result(result)
        assert_equal(result['type'], "CloseVault")
        assert_equal(result['results']['vaultId'], create_vault_tx)
        assert_equal(result['results']['to'], new_vault_owner)

        # Set up for auction bid test
        new_vault = self.nodes[0].createvault(new_vault_owner, 'LOAN0001')
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(new_vault, new_vault_owner, '10@DFI')
        self.nodes[0].generate(1)

        self.nodes[0].takeloan({'vaultId': new_vault,'amounts': '6.68896@TSLA'})
        self.nodes[0].generate(5)

        # Test auction bid
        auction_tx = self.nodes[0].placeauctionbid(new_vault, 0, self.nodes[0].get_genesis_keys().ownerAuthAddress, '7.1@TSLA')
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(auction_tx)
        self.check_result(result)
        assert_equal(result['type'], "AuctionBid")
        assert_equal(result['results']['vaultId'], new_vault)
        assert_equal(result['results']['index'], 0)
        assert_equal(result['results']['amount'], '7.10000000@' + token_loan_tesla)

        # Test remove oracle
        oracle_rem_tx = self.nodes[0].removeoracle(appoint_oracle_tx)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(oracle_rem_tx)
        self.check_result(result)
        assert_equal(result['type'], "RemoveOracleAppoint")
        assert_equal(result['results']['oracleId'], appoint_oracle_tx)

        setgov_tx = self.nodes[0].setgov({"ATTRIBUTES":{'v0/params/dfip2201/active':'true','v0/params/dfip2201/minswap':'0.001','v0/params/dfip2201/premium':'0.025'}})
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Get custom TX
        result = self.nodes[1].getcustomtx(setgov_tx)
        self.check_result(result)
        assert_equal(result['type'], "SetGovVariable")
        attributes = result['results']['ATTRIBUTES']
        assert_equal(len(attributes), 3)
        assert_equal(attributes['v0/params/dfip2201/active'], 'true')
        assert_equal(attributes['v0/params/dfip2201/premium'], '0.025')
        assert_equal(attributes['v0/params/dfip2201/minswap'], '0.001')

if __name__ == '__main__':
    TokensRPCGetCustomTX().main ()

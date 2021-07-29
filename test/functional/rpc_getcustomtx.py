#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getcustomtx RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal
from decimal import Decimal

class TokensRPCGetCustomTX(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120', '-fortcanningheight=120'], # Wallet TXs
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120', '-fortcanningheight=120', '-txindex=1'], # Transaction index
                           ['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=120', '-fortcanningheight=120']] # Will not find historical TXs

    def run_test(self):
        self.nodes[0].generate(101)
        self.sync_all()
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
        self.sync_all()

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Get block hash and height of mint tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get history node 0 from wallet TX
        result = self.nodes[0].getcustomtx(create_token_tx)
        assert_equal(result['type'], "CreateToken")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['creationTx'], create_token_tx)
        assert_equal(result['results']['symbol'], "GOLD")
        assert_equal(result['results']['name'], "gold")
        assert_equal(result['results']['mintable'], True)
        assert_equal(result['results']['tradeable'], True)
        assert_equal(result['results']['finalized'], False)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_a = idx

        # Mint some tokens
        minttx = self.nodes[0].minttokens(["300@" + token_a])
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of mint tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Check tokens are minted
        assert_equal(self.nodes[0].getaccount(collateral_a, {}, True)[token_a], 300)

        # All nodes agree
        assert_equal(self.nodes[0].gettoken(token_a)[token_a]['minted'], 300)
        assert_equal(self.nodes[1].gettoken(token_a)[token_a]['minted'], 300)
        assert_equal(self.nodes[2].gettoken(token_a)[token_a]['minted'], 300)

        # Get history node 0 from wallet TX
        result = self.nodes[0].getcustomtx(minttx)
        assert_equal(result['type'], "MintToken")
        assert_equal(result['valid'], True)
        assert_equal(result['results'][str(token_a)], Decimal("300.00000000"))
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Get history node 1 with transaction index
        result = self.nodes[1].getcustomtx(minttx)
        assert_equal(result['type'], "MintToken")
        assert_equal(result['valid'], True)
        assert_equal(result['results'][str(token_a)], Decimal("300.00000000"))
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Try and get history node 2
        try:
            result = self.nodes[2].getcustomtx(minttx)
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No such mempool or wallet transaction. Use -txindex or provide a block hash." in errorString)

        # Try and get history node 2 using block hash
        result = self.nodes[2].getcustomtx(minttx, blockhash)
        assert_equal(result['type'], "MintToken")
        assert_equal(result['valid'], True)
        assert_equal(result['results'][str(token_a)], Decimal("300.00000000"))
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Update token
        updatetx = self.nodes[0].updatetoken(token_a, {"symbol":"SILVER","name":"silver","mintable":False,"tradeable":False})
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Make sure token updated as expected
        result = self.nodes[0].gettoken(token_a)[token_a]
        assert_equal(result['symbol'], "SILVER")
        assert_equal(result['name'], "silver")
        assert_equal(result['mintable'], False)
        assert_equal(result['tradeable'], False)

        # Get history node 1 with transaction index
        result = self.nodes[1].getcustomtx(updatetx)
        assert_equal(result['type'], "UpdateTokenAny")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['symbol'], "SILVER")
        assert_equal(result['results']['name'], "silver")
        assert_equal(result['results']['mintable'], False)
        assert_equal(result['results']['tradeable'], False)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test MN call
        num_mns = len(self.nodes[0].listmasternodes())
        collateral = self.nodes[0].getnewaddress("", "legacy")
        mn_txid = self.nodes[0].createmasternode(collateral)
        self.nodes[0].generate(1)
        self.sync_all()

        # Make sure new MN was successfully created
        assert_equal(len(self.nodes[0].listmasternodes()), num_mns + 1)

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(mn_txid)
        assert_equal(result['type'], "CreateMasternode")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['collateralamount'], Decimal("10.00000000"))
        assert_equal(result['results']['masternodeoperator'], collateral)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test resign MN call
        resign_tx = self.nodes[0].resignmasternode(mn_txid)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check MN in PRE_RESIGNED state
        assert_equal(self.nodes[0].listmasternodes()[mn_txid]['state'], "PRE_RESIGNED")

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(resign_tx)
        assert_equal(result['type'], "ResignMasternode")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['id'], mn_txid)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

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
        self.sync_all()

        # Get token ID
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == "GOLD"):
                token_b = idx

        # Mint some tokens for use later
        self.nodes[0].minttokens(["300@" + token_b])
        self.nodes[0].generate(1)
        self.sync_all()

        # Make sure there's an extra token
        assert_equal(len(self.nodes[0].listtokens()), num_tokens + 1)

        # Create pool pair
        pool_collateral = self.nodes[0].getnewaddress("", "legacy")
        poolpair_tx = self.nodes[0].createpoolpair({
            "tokenA": "SILVER#" + token_a,
            "tokenB": "GOLD#" + token_b,
            "commission": 0.001,
            "status": True,
            "ownerAddress": pool_collateral,
            "pairSymbol": "SILVGOLD"
        })
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(poolpair_tx)
        assert_equal(result['type'], "CreatePoolPair")
        assert_equal(result['valid'], True)
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
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test account to account TX
        accounttoaccount_tx = self.nodes[0].accounttoaccount(collateral_b, {collateral_a: "100@" + token_b})
        self.nodes[0].generate(1)

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(accounttoaccount_tx)
        assert_equal(result['type'], "AccountToAccount")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['from'], collateral_b)
        assert_equal([*result['results']['to']][0], collateral_a)
        assert_equal(list(result['results']['to'].values())[0], "100.00000000@" + token_b)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test add pool liquidity TX
        pool_share = self.nodes[0].getnewaddress("", "legacy")
        add_liquidity_tx = self.nodes[0].addpoolliquidity({
            collateral_a: ['100@' + token_a, '100@' + token_b]
            }, pool_share)
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(add_liquidity_tx)
        assert_equal(result['type'], "AddPoolLiquidity")
        assert_equal(result['valid'], True)
        assert_equal(result['results'][token_a], Decimal("100.00000000"))
        assert_equal(result['results'][token_b], Decimal("100.00000000"))
        assert_equal(result['results']['shareaddress'], pool_share)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test pool swap TX
        poolswap_tx = self.nodes[0].poolswap({
            "from": collateral_a,
            "tokenFrom": token_a,
            "amountFrom": 1,
            "to": collateral_b,
            "tokenTo": token_b,
            "maxPrice": 2
        })
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(poolswap_tx)
        assert_equal(result['type'], "PoolSwap")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['fromAddress'], collateral_a)
        assert_equal(result['results']['fromToken'], token_a)
        assert_equal(result['results']['fromAmount'], Decimal("1.00000000"))
        assert_equal(result['results']['toAddress'], collateral_b)
        assert_equal(result['results']['toToken'], token_b)
        assert_equal(result['results']['maxPrice'], Decimal("2.00000000"))
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test remove liquidity TX
        remove_liquidity_tx = self.nodes[0].removepoolliquidity(pool_share, "25@1")
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(remove_liquidity_tx)
        assert_equal(result['type'], "RemovePoolLiquidity")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['from'], pool_share)
        assert_equal(result['results']['amount'], "25.00000000@1")
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test pool update TX
        poolpair_update_tx = self.nodes[0].updatepoolpair({
            "pool": poolpair_tx,
            "status": False,
            "commission": 0.1,
            "ownerAddress": pool_collateral
        })
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(poolpair_update_tx)
        assert_equal(result['type'], "UpdatePoolPair")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['commission'], Decimal("0.10000000"))
        assert_equal(result['results']['status'], False)
        assert_equal(result['results']['ownerAddress'], pool_collateral)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test UTXOs to account TX
        utxostoaccount_tx = self.nodes[0].utxostoaccount({collateral_a: "1@0"})
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(utxostoaccount_tx)
        assert_equal(result['type'], "UtxosToAccount")
        assert_equal(result['valid'], True)
        assert_equal(result['results'][collateral_a], "1.00000000@0")
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test account to UTXOs TX
        accountoutxos_tx = self.nodes[0].accounttoutxos(collateral_a, {collateral_b: "1@0"})
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(accountoutxos_tx)
        assert_equal(result['type'], "AccountToUtxos")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['from'], collateral_a)
        assert_equal(list(result['results']['to'].keys())[0], collateral_b)
        assert_equal(list(result['results']['to'].values())[0], "1.00000000@0")
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test send tokens to address TX
        self.nodes[0].utxostoaccount({collateral_a: "1@0"})
        self.nodes[0].generate(1)
        self.sync_all()

        tokenstoaddress_tx = self.nodes[0].sendtokenstoaddress({collateral_a:"1@0"}, {collateral_b:"1@0"})
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(tokenstoaddress_tx)
        assert_equal(result['type'], "AnyAccountsToAccounts")
        assert_equal(result['valid'], True)
        assert_equal(list(result['results']['from'].keys())[0], collateral_a)
        assert_equal(list(result['results']['from'].values())[0], "1.00000000@0")
        assert_equal(list(result['results']['to'].keys())[0], collateral_b)
        assert_equal(list(result['results']['to'].values())[0], "1.00000000@0")
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test setgox TX
        setgov_tx = self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35})
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(setgov_tx)
        assert_equal(result['type'], "SetGovVariable")
        assert_equal(result['valid'], True)
        assert_equal(list(result['results'].keys())[0], "LP_DAILY_DFI_REWARD")
        assert_equal(list(result['results'].values())[0], Decimal("35.00000000"))
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        collateral = self.nodes[0].getnewaddress("", "legacy")
        mn_txid = self.nodes[0].createmasternode(collateral)
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        assert_equal(120, blockheight) # Dakota height
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(mn_txid)
        assert_equal(result['type'], "CreateMasternode")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['collateralamount'], Decimal("2.00000000"))
        assert_equal(result['results']['masternodeoperator'], collateral)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test set loan scheme
        loan_txid = self.nodes[0].createloanscheme(1000, 0.5, 'LOANMAX')
        self.nodes[0].generate(1)
        self.sync_all()

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(loan_txid)
        assert_equal(result['type'], "LoanScheme")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['id'], "LOANMAX")
        assert_equal(result['results']['mincolratio'], 1000)
        assert_equal(result['results']['interestrate'], Decimal("0.50000000"))
        assert_equal(result['results']['update'], False)
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test changing the loan scheme
        self.nodes[0].createloanscheme(100, 5, 'LOAN001')
        self.nodes[0].generate(1)
        default_txid = self.nodes[0].setdefaultloanscheme('LOAN001')
        self.nodes[0].generate(1)

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(default_txid)
        assert_equal(result['type'], "DefaultLoanScheme")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['id'], "LOAN001")
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

        # Test destroying a loan scheme
        destroy_txid = self.nodes[0].destroyloanscheme('LOANMAX')
        self.nodes[0].generate(1)

        # Get block hash and height of update tx
        blockheight = self.nodes[0].getblockcount()
        blockhash = self.nodes[0].getblockhash(blockheight)

        # Get custom TX
        result = self.nodes[1].getcustomtx(destroy_txid)
        assert_equal(result['type'], "DestroyLoanScheme")
        assert_equal(result['valid'], True)
        assert_equal(result['results']['id'], "LOANMAX")
        assert_equal(result['blockHeight'], blockheight)
        assert_equal(result['blockhash'], blockhash)
        assert_equal(result['confirmations'], 1)

if __name__ == '__main__':
    TokensRPCGetCustomTX().main ()

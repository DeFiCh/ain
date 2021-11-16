#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test getvaulthistory RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from decimal import Decimal
import time

class TokensRPCGetVaultHistory(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50', '-bayfrontgardensheight=50', '-dakotaheight=100', '-eunosheight=100', '-eunospayaheight=100', '-fortcanningheight=100', '-vaultindex=1']]

    def run_test(self):
        self.nodes[0].generate(101)

        # Collateral address
        collateral_a = self.nodes[0].getnewaddress("", "legacy")

        # Appoint oracle
        oracle_address = self.nodes[0].getnewaddress("", "legacy")
        appoint_oracle_tx = self.nodes[0].appointoracle(oracle_address, [{"currency": "USD", "token": "DFI"},{"currency": "USD", "token": "TSLA"}], 10)
        self.nodes[0].generate(1)

        # Test set oracle data
        oracle_prices = [{"currency": "USD", "tokenAmount": "100.00000000@DFI"},{"currency": "USD", "tokenAmount": "100.00000000@TSLA"}]
        self.nodes[0].setoracledata(appoint_oracle_tx, int(time.time()), oracle_prices)
        self.nodes[0].generate(1)
        self.sync_blocks(self.nodes[0:2])

        # Create loan schemes
        self.nodes[0].createloanscheme(150, 5, 'LOAN0001')
        self.nodes[0].generate(1)

        # Create DUSD token
        self.nodes[0].createtoken({
            "symbol": "DUSD",
            "name": "DUSD",
            "isDAT": True,
            "collateralAddress": self.nodes[0].get_genesis_keys().ownerAuthAddress
        })
        self.nodes[0].generate(1)

        # Set loan token
        self.nodes[0].setloantoken({
                                    'symbol': 'TSLA',
                                    'name': "Tesla stock token",
                                    'fixedIntervalPriceId': "TSLA/USD",
                                    'mintable': True,
                                    'interest': 1})
        self.nodes[0].generate(1)

        # Create pool pairs
        pool_collateral = self.nodes[0].getnewaddress("", "legacy")
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

        # Mint tokens
        self.nodes[0].minttokens(["300@TSLA", "300@DUSD"])
        self.nodes[0].generate(1)

        # Fund ownerAuthAddress
        self.nodes[0].utxostoaccount({self.nodes[0].get_genesis_keys().ownerAuthAddress: "100@DFI"})
        self.nodes[0].generate(1)

        # Add pool liquidity
        pool_share = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].addpoolliquidity({
            self.nodes[0].get_genesis_keys().ownerAuthAddress: ['100@TSLA', '100@DUSD']
            }, pool_share)
        self.nodes[0].generate(1)

        self.nodes[0].addpoolliquidity({
            self.nodes[0].get_genesis_keys().ownerAuthAddress: ['100@DUSD', '100@DFI']
            }, pool_share)
        self.nodes[0].generate(1)

        # Set collateral token
        self.nodes[0].setcollateraltoken({
                                    'token': "DFI",
                                    'factor': 1,
                                    'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)

        # Create vault
        vault_owner = self.nodes[0].getnewaddress("", "legacy")
        create_vault_tx = self.nodes[0].createvault(vault_owner, 'LOAN0001')
        self.nodes[0].generate(1)

        # Deposit to vault
        self.nodes[0].utxostoaccount({collateral_a: "100@0"})
        self.nodes[0].generate(1)
        deposit_tx = self.nodes[0].deposittovault(create_vault_tx, collateral_a, '10@DFI')
        self.nodes[0].generate(1)

        # Take loan
        takeloan_tx = self.nodes[0].takeloan({'vaultId':create_vault_tx,'amounts': "1@TSLA"})
        self.nodes[0].generate(1)

        # Pay back loan
        vault = self.nodes[0].getvault(create_vault_tx)
        payback_tx = self.nodes[0].paybackloan({
            'vaultId':create_vault_tx,
            'from':self.nodes[0].get_genesis_keys().ownerAuthAddress,
            'amounts':vault['loanAmounts']
            })
        self.nodes[0].generate(1)

        # Withdraw from vault
        withdraw_address = self.nodes[0].getnewaddress("", "legacy")
        withdraw_tx = self.nodes[0].withdrawfromvault(create_vault_tx, withdraw_address, '0.5@DFI')
        self.nodes[0].generate(1)

        # Close vault
        close_address = self.nodes[0].getnewaddress("", "legacy")
        close_tx = self.nodes[0].closevault(create_vault_tx, close_address)
        self.nodes[0].generate(1)

        # Test getvaulthistory
        result = self.nodes[0].getvaulthistory(create_vault_tx)
        assert_equal(len(result), 6)
        assert_equal(result[0]['vault'], create_vault_tx)
        assert_equal(result[0]['type'], 'CloseVault')
        assert_equal(result[0]['address'], close_address)
        assert_equal(result[0]['amounts'], ['10.00000000@DFI'])
        assert_equal(result[0]['txid'], close_tx)
        assert_equal(result[1]['vault'], create_vault_tx)
        assert_equal(result[1]['type'], 'WithdrawFromVault')
        assert_equal(result[1]['address'], withdraw_address)
        assert_equal(result[1]['amounts'], ['0.50000000@DFI'])
        assert_equal(result[1]['txid'], withdraw_tx)
        assert_equal(result[2]['vault'], create_vault_tx)
        assert_equal(result[2]['type'], 'PaybackLoan')
        assert_equal(result[2]['address'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[2]['amounts'], ['0.00000114@DFI'])
        assert_equal(result[2]['txid'], payback_tx)
        assert_equal(result[3]['vault'], create_vault_tx)
        assert_equal(result[3]['type'], 'PaybackLoan')
        assert_equal(result[3]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[3]['amounts'], ['-1.00000114@TSLA'])
        assert_equal(result[3]['txid'], payback_tx)
        assert_equal(result[4]['vault'], create_vault_tx)
        assert_equal(result[4]['type'], 'TakeLoan')
        assert_equal(result[4]['address'], vault_owner)
        assert_equal(result[4]['amounts'], ['1.00000000@TSLA'])
        assert_equal(result[4]['txid'], takeloan_tx)
        assert_equal(result[5]['vault'], create_vault_tx)
        assert_equal(result[5]['type'], 'DepositToVault')
        assert_equal(result[5]['address'], collateral_a)
        assert_equal(result[5]['amounts'], ['-10.00000000@DFI'])
        assert_equal(result[5]['txid'], deposit_tx)

        # Test getvaulthistory block height and depth
        result = self.nodes[0].getvaulthistory(create_vault_tx, {'maxBlockHeight':119,'depth':1})
        assert_equal(len(result), 3)
        assert_equal(result[0]['vault'], create_vault_tx)
        assert_equal(result[0]['type'], 'WithdrawFromVault')
        assert_equal(result[0]['address'], withdraw_address)
        assert_equal(result[0]['amounts'], ['0.50000000@DFI'])
        assert_equal(result[0]['txid'], withdraw_tx)
        assert_equal(result[1]['vault'], create_vault_tx)
        assert_equal(result[1]['type'], 'PaybackLoan')
        assert_equal(result[1]['address'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[1]['amounts'], ['0.00000114@DFI'])
        assert_equal(result[1]['txid'], payback_tx)
        assert_equal(result[2]['vault'], create_vault_tx)
        assert_equal(result[2]['type'], 'PaybackLoan')
        assert_equal(result[2]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[2]['amounts'], ['-1.00000114@TSLA'])
        assert_equal(result[2]['txid'], payback_tx)

        # Test getvaulthistory token type
        result = self.nodes[0].getvaulthistory(create_vault_tx, {'token':'TSLA'})
        assert_equal(len(result), 2)
        assert_equal(result[0]['vault'], create_vault_tx)
        assert_equal(result[0]['type'], 'PaybackLoan')
        assert_equal(result[0]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[0]['amounts'], ['-1.00000114@TSLA'])
        assert_equal(result[0]['txid'], payback_tx)
        assert_equal(result[1]['vault'], create_vault_tx)
        assert_equal(result[1]['type'], 'TakeLoan')
        assert_equal(result[1]['address'], vault_owner)
        assert_equal(result[1]['amounts'], ['1.00000000@TSLA'])
        assert_equal(result[1]['txid'], takeloan_tx)

        # Test getvaulthistory TX type
        result = self.nodes[0].getvaulthistory(create_vault_tx, {'txtype':'J'})
        assert_equal(len(result), 1)
        assert_equal(result[0]['vault'], create_vault_tx)
        assert_equal(result[0]['type'], 'WithdrawFromVault')
        assert_equal(result[0]['address'], withdraw_address)
        assert_equal(result[0]['amounts'], ['0.50000000@DFI'])
        assert_equal(result[0]['txid'], withdraw_tx)

        # Test getvaulthistory limit
        result = self.nodes[0].getvaulthistory(create_vault_tx, {'limit':1})
        assert_equal(len(result), 1)
        assert_equal(result[0]['vault'], create_vault_tx)
        assert_equal(result[0]['type'], 'CloseVault')
        assert_equal(result[0]['address'], close_address)
        assert_equal(result[0]['amounts'], ['10.00000000@DFI'])
        assert_equal(result[0]['txid'], close_tx)

        # Test rollback of tip to remove CloseVault TX
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()
        result = self.nodes[0].getvaulthistory(create_vault_tx)

        # Quick check of last entry
        assert_equal(len(result), 5)
        assert_equal(result[0]['type'], 'WithdrawFromVault')

        # Create new vault
        new_vault = self.nodes[0].createvault(vault_owner, 'LOAN0001')
        self.nodes[0].generate(1)

        # Fund vault owner
        self.nodes[0].utxostoaccount({vault_owner: "100@DFI"})
        self.nodes[0].generate(1)

        # Deposit to vault
        deposit_tx = self.nodes[0].deposittovault(new_vault, vault_owner, '10@DFI')
        self.nodes[0].generate(1)

        # Take loan
        takeloan_tx = self.nodes[0].takeloan({'vaultId': new_vault,'amounts': '6.68896@TSLA'})
        self.nodes[0].generate(4)

        # Fund another bidding address
        new_bidding_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {new_bidding_address: "10@TSLA"})
        self.nodes[0].generate(1)

        # Place bids
        bid1_tx = self.nodes[0].placeauctionbid(new_vault, 0, self.nodes[0].get_genesis_keys().ownerAuthAddress, '8@TSLA')
        self.nodes[0].generate(1)
        bid2_tx = self.nodes[0].placeauctionbid(new_vault, 0, new_bidding_address, '9@TSLA')
        self.nodes[0].generate(33)

        # Check bids in result
        result = self.nodes[0].getvaulthistory(new_vault)
        assert_equal(len(result), 7)
        assert_equal(result[0]['vault'], new_vault)
        assert_equal(result[0]['vaultSnapshot']['state'], 'active')
        assert_equal(result[0]['vaultSnapshot']['collateralAmounts'], ['1.87309846@DFI'])
        assert_equal(result[0]['vaultSnapshot']['collateralValue'], Decimal('187.30984600'))
        assert_equal(result[0]['vaultSnapshot']['collateralRatio'], 0)
        assert_equal(result[0]['vaultSnapshot']['schemeID'], 'LOAN0001')
        assert_equal(result[0]['vaultSnapshot']['batches'], [])
        assert_equal(result[1]['vault'], new_vault)
        assert_equal(result[1]['type'], 'AuctionBid')
        assert_equal(result[1]['txid'], bid2_tx)
        assert_equal(result[2]['vault'], new_vault)
        assert_equal(result[2]['type'], 'AuctionBid')
        assert_equal(result[2]['txid'], bid2_tx)

        if result[0]['address'] == new_bidding_address:
            assert_equal(result[1]['amounts'], ['-9.00000000@TSLA'])
            assert_equal(result[2]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
            assert_equal(result[2]['amounts'], ['8.00000000@TSLA'])
        elif result[0]['address'] == self.nodes[0].get_genesis_keys().ownerAuthAddress:
            assert_equal(result[1]['amounts'], ['8.00000000@TSLA'])
            assert_equal(result[2]['address'], new_bidding_address)
            assert_equal(result[2]['amounts'], ['-9.00000000@TSLA'])
        else:
            assert('Expected address not found in results')

        assert_equal(result[3]['vault'], new_vault)
        assert_equal(result[3]['type'], 'AuctionBid')
        assert_equal(result[3]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[3]['amounts'], ['-8.00000000@TSLA'])
        assert_equal(result[3]['txid'], bid1_tx)
        assert_equal(result[4]['vault'], new_vault)
        assert_equal(result[4]['vaultSnapshot']['state'], 'inLiquidation')
        assert_equal(result[4]['vaultSnapshot']['collateralAmounts'], [])
        assert_equal(result[4]['vaultSnapshot']['collateralValue'], Decimal('0.00000000'))
        assert_equal(result[4]['vaultSnapshot']['collateralRatio'], 149)
        assert_equal(result[4]['vaultSnapshot']['schemeID'], 'LOAN0001')
        assert_equal(len(result[4]['vaultSnapshot']['batches']), 1)
        assert_equal(result[4]['vaultSnapshot']['batches'][0]['index'], 0)
        assert_equal(result[4]['vaultSnapshot']['batches'][0]['collaterals'], ['10.00000000@DFI'])
        assert_equal(result[4]['vaultSnapshot']['batches'][0]['loan'], '6.68896763@TSLA')
        assert_equal(result[5]['vault'], new_vault)
        assert_equal(result[5]['type'], 'TakeLoan')
        assert_equal(result[5]['address'], vault_owner)
        assert_equal(result[5]['amounts'], ['6.68896000@TSLA'])
        assert_equal(result[5]['txid'], takeloan_tx)
        assert_equal(result[6]['vault'], new_vault)
        assert_equal(result[6]['type'], 'DepositToVault')
        assert_equal(result[6]['address'], vault_owner)
        assert_equal(result[6]['amounts'], ['-10.00000000@DFI'])
        assert_equal(result[6]['txid'], deposit_tx)

if __name__ == '__main__':
    TokensRPCGetVaultHistory().main()

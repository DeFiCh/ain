#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test listvaulthistory RPC."""

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
        self.nodes[0].createloanscheme(300, 2.5, 'LOAN0002')
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
        create_vault_tx = self.nodes[0].createvault(vault_owner, 'LOAN0002')
        self.nodes[0].generate(1)

        # Deposit to vault
        self.nodes[0].utxostoaccount({collateral_a: "100@0"})
        self.nodes[0].generate(1)
        deposit_tx = self.nodes[0].deposittovault(create_vault_tx, collateral_a, '10@DFI')

        # Create new vault, make sure records do not get mixed up
        another_owner = self.nodes[0].getnewaddress("", "legacy")
        create_another_tx = self.nodes[0].createvault(another_owner, 'LOAN0002')
        self.nodes[0].generate(1)

        # Update global loan scheme change
        update_002_tx = self.nodes[0].updateloanscheme(600, 1, 'LOAN0002')
        self.nodes[0].generate(1)

        # Make sure last entry is update vault
        result = self.nodes[0].listvaulthistory(create_vault_tx)
        assert_equal(len(result), 3)
        assert_equal(result[0]['type'], 'LoanScheme')

        # Test rollback of global loan scheme
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        result = self.nodes[0].listvaulthistory(create_vault_tx)
        assert_equal(len(result), 2)
        assert_equal(result[0]['type'], 'DepositToVault')
        self.nodes[0].generate(1)

        # Update other vault's loan scheme
        update_tx = self.nodes[0].updatevault(create_another_tx, {'loanSchemeId': 'LOAN0001'})

        # Change loan scheme
        update_tx = self.nodes[0].updatevault(create_vault_tx, {'loanSchemeId': 'LOAN0001'})
        self.nodes[0].generate(1)

        # Make sure last entry is update vault
        result = self.nodes[0].listvaulthistory(create_vault_tx)
        assert_equal(len(result), 4)
        assert_equal(result[0]['type'], 'UpdateVault')

        # Test rollback of loan scheme
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        result = self.nodes[0].listvaulthistory(create_vault_tx)
        assert_equal(len(result), 3)
        assert_equal(result[0]['type'], 'LoanScheme')
        self.nodes[0].generate(1)

        # Destroy old loan scheme
        self.nodes[0].destroyloanscheme('LOAN0002')
        self.nodes[0].generate(1)

        # Create replacement. Should not show up in vault history.
        self.nodes[0].createloanscheme(100, 1, 'LOAN0002')
        self.nodes[0].generate(1)

        # Update loan scheme. Should not show up in vault history.
        self.nodes[0].updateloanscheme(200, 2, 'LOAN0002')
        self.nodes[0].generate(1)

        # Take loan
        takeloan_tx = self.nodes[0].takeloan({'vaultId':create_vault_tx,'amounts': "1@TSLA"})
        self.nodes[0].generate(1)

        # Update loan scheme
        update_001_tx = self.nodes[0].updateloanscheme(200, 6, 'LOAN0001')
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
        withdraw_height = self.nodes[0].getblockcount()

        # Close vault
        close_address = self.nodes[0].getnewaddress("", "legacy")
        close_tx = self.nodes[0].closevault(create_vault_tx, close_address)
        self.nodes[0].generate(1)

        # Test listvaulthistory
        result = self.nodes[0].listvaulthistory(create_vault_tx)
        assert_equal(len(result), 10)
        assert_equal(result[0]['type'], 'CloseVault')
        assert_equal(result[0]['address'], close_address)
        assert_equal(result[0]['amounts'], ['10.00000000@DFI'])
        assert_equal(result[0]['txid'], close_tx)
        assert_equal(result[1]['type'], 'WithdrawFromVault')
        assert_equal(result[1]['address'], withdraw_address)
        assert_equal(result[1]['amounts'], ['0.50000000@DFI'])
        assert_equal(result[1]['txid'], withdraw_tx)
        assert_equal(result[2]['type'], 'PaybackLoan')
        assert_equal(result[2]['address'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[2]['amounts'], ['0.00000228@DFI'])
        assert_equal(result[2]['txid'], payback_tx)
        assert_equal(result[3]['type'], 'PaybackLoan')
        assert_equal(result[3]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[3]['amounts'], ['-1.00000228@TSLA'])
        assert_equal(result[3]['txid'], payback_tx)
        assert_equal(result[4]['type'], 'LoanScheme')
        assert_equal(result[4]['loanScheme']['id'], 'LOAN0001')
        assert_equal(result[4]['loanScheme']['rate'], 600000000)
        assert_equal(result[4]['loanScheme']['ratio'], 200)
        assert_equal(result[4]['txid'], update_001_tx)
        assert_equal(result[5]['type'], 'TakeLoan')
        assert_equal(result[5]['address'], vault_owner)
        assert_equal(result[5]['amounts'], ['1.00000000@TSLA'])
        assert_equal(result[5]['txid'], takeloan_tx)
        assert_equal(result[6]['type'], 'UpdateVault')
        assert_equal(result[6]['loanScheme']['id'], 'LOAN0001')
        assert_equal(result[6]['loanScheme']['rate'], 500000000)
        assert_equal(result[6]['loanScheme']['ratio'], 150)
        assert_equal(result[6]['txid'], update_tx)
        assert_equal(result[7]['type'], 'LoanScheme')
        assert_equal(result[7]['loanScheme']['id'], 'LOAN0002')
        assert_equal(result[7]['loanScheme']['rate'], 100000000)
        assert_equal(result[7]['loanScheme']['ratio'], 600)
        assert_equal(result[7]['txid'], update_002_tx)
        assert_equal(result[8]['type'], 'DepositToVault')
        assert_equal(result[8]['address'], collateral_a)
        assert_equal(result[8]['amounts'], ['-10.00000000@DFI'])
        assert_equal(result[8]['txid'], deposit_tx)
        assert_equal(result[9]['type'], 'Vault')
        assert_equal(result[9]['loanScheme']['id'], 'LOAN0002')
        assert_equal(result[9]['loanScheme']['rate'], 250000000)
        assert_equal(result[9]['loanScheme']['ratio'], 300)
        assert_equal(result[9]['txid'], create_vault_tx)

        # Test listvaulthistory block height and depth
        result = self.nodes[0].listvaulthistory(create_vault_tx, {'maxBlockHeight':withdraw_height,'depth':1})
        assert_equal(len(result), 3)
        assert_equal(result[0]['type'], 'WithdrawFromVault')
        assert_equal(result[0]['address'], withdraw_address)
        assert_equal(result[0]['amounts'], ['0.50000000@DFI'])
        assert_equal(result[0]['txid'], withdraw_tx)
        assert_equal(result[1]['type'], 'PaybackLoan')
        assert_equal(result[1]['address'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[1]['amounts'], ['0.00000228@DFI'])
        assert_equal(result[1]['txid'], payback_tx)
        assert_equal(result[2]['type'], 'PaybackLoan')
        assert_equal(result[2]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[2]['amounts'], ['-1.00000228@TSLA'])
        assert_equal(result[2]['txid'], payback_tx)

        # Test listvaulthistory token type
        result = self.nodes[0].listvaulthistory(create_vault_tx, {'token':'TSLA'})
        assert_equal(len(result), 2)
        assert_equal(result[0]['type'], 'PaybackLoan')
        assert_equal(result[0]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[0]['amounts'], ['-1.00000228@TSLA'])
        assert_equal(result[0]['txid'], payback_tx)
        assert_equal(result[1]['type'], 'TakeLoan')
        assert_equal(result[1]['address'], vault_owner)
        assert_equal(result[1]['amounts'], ['1.00000000@TSLA'])
        assert_equal(result[1]['txid'], takeloan_tx)

        # Test listvaulthistory TX type
        result = self.nodes[0].listvaulthistory(create_vault_tx, {'txtype':'J'})
        assert_equal(len(result), 1)
        assert_equal(result[0]['type'], 'WithdrawFromVault')
        assert_equal(result[0]['address'], withdraw_address)
        assert_equal(result[0]['amounts'], ['0.50000000@DFI'])
        assert_equal(result[0]['txid'], withdraw_tx)

        # Test listvaulthistory limit
        result = self.nodes[0].listvaulthistory(create_vault_tx, {'limit':1})
        assert_equal(len(result), 1)
        assert_equal(result[0]['type'], 'CloseVault')
        assert_equal(result[0]['address'], close_address)
        assert_equal(result[0]['amounts'], ['10.00000000@DFI'])
        assert_equal(result[0]['txid'], close_tx)

        # Test rollback of tip to remove CloseVault TX
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        self.nodes[0].clearmempool()
        result = self.nodes[0].listvaulthistory(create_vault_tx)

        # Quick check of last entry
        assert_equal(len(result), 9)
        assert_equal(result[0]['type'], 'WithdrawFromVault')

        # Restore original scheme values
        self.nodes[0].updateloanscheme(150, 5, 'LOAN0001')
        self.nodes[0].generate(1)

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
        self.nodes[0].generate(2)

        # Test state change
        result = self.nodes[0].listvaulthistory(new_vault)
        assert_equal(len(result), 4)
        assert_equal(result[0]['vaultSnapshot']['state'], 'inLiquidation')

        # Test rollback of state change
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(self.nodes[0].getblockcount()))
        result = self.nodes[0].listvaulthistory(new_vault)
        assert_equal(len(result), 3)
        assert_equal(result[0]['type'], 'TakeLoan')
        self.nodes[0].generate(1)

        # Fund another bidding address
        new_bidding_address = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].accounttoaccount(self.nodes[0].get_genesis_keys().ownerAuthAddress, {new_bidding_address: "10@TSLA"})
        self.nodes[0].generate(1)

        # Place bids
        bid1_tx = self.nodes[0].placeauctionbid(new_vault, 0, self.nodes[0].get_genesis_keys().ownerAuthAddress, '8@TSLA')
        self.nodes[0].generate(1)
        bid2_tx = self.nodes[0].placeauctionbid(new_vault, 0, new_bidding_address, '9@TSLA')
        self.nodes[0].generate(35)

        # Check bids in result
        result = self.nodes[0].listvaulthistory(new_vault)
        assert_equal(len(result), 8)
        assert_equal(result[0]['vaultSnapshot']['state'], 'active')
        assert_equal(result[0]['vaultSnapshot']['collateralAmounts'], ['1.87309837@DFI'])
        assert_equal(result[0]['vaultSnapshot']['collateralValue'], Decimal('187.30983700'))
        assert_equal(result[0]['vaultSnapshot']['collateralRatio'], 0)
        assert_equal(result[1]['type'], 'AuctionBid')
        assert_equal(result[1]['txid'], bid2_tx)
        assert_equal(result[2]['type'], 'AuctionBid')
        assert_equal(result[2]['txid'], bid2_tx)

        if result[1]['address'] == new_bidding_address:
            assert_equal(result[1]['amounts'], ['-9.00000000@TSLA'])
            assert_equal(result[2]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
            assert_equal(result[2]['amounts'], ['8.00000000@TSLA'])
        elif result[1]['address'] == self.nodes[0].get_genesis_keys().ownerAuthAddress:
            assert_equal(result[1]['amounts'], ['8.00000000@TSLA'])
            assert_equal(result[2]['address'], new_bidding_address)
            assert_equal(result[2]['amounts'], ['-9.00000000@TSLA'])
        else:
            assert('Expected address not found in results')

        assert_equal(result[3]['type'], 'AuctionBid')
        assert_equal(result[3]['address'], self.nodes[0].get_genesis_keys().ownerAuthAddress)
        assert_equal(result[3]['amounts'], ['-8.00000000@TSLA'])
        assert_equal(result[3]['txid'], bid1_tx)
        assert_equal(result[4]['vaultSnapshot']['state'], 'inLiquidation')
        assert_equal(result[4]['vaultSnapshot']['collateralAmounts'], [])
        assert_equal(result[4]['vaultSnapshot']['collateralValue'], Decimal('0.00000000'))
        assert_equal(result[4]['vaultSnapshot']['collateralRatio'], 149)
        assert_equal(len(result[4]['vaultSnapshot']['batches']), 1)
        assert_equal(result[4]['vaultSnapshot']['batches'][0]['index'], 0)
        assert_equal(result[4]['vaultSnapshot']['batches'][0]['collaterals'], ['10.00000000@DFI'])
        assert_equal(result[4]['vaultSnapshot']['batches'][0]['loan'], '6.68896763@TSLA')
        assert_equal(result[5]['type'], 'TakeLoan')
        assert_equal(result[5]['address'], vault_owner)
        assert_equal(result[5]['amounts'], ['6.68896000@TSLA'])
        assert_equal(result[5]['txid'], takeloan_tx)
        assert_equal(result[6]['type'], 'DepositToVault')
        assert_equal(result[6]['address'], vault_owner)
        assert_equal(result[6]['amounts'], ['-10.00000000@DFI'])
        assert_equal(result[6]['txid'], deposit_tx)
        assert_equal(result[7]['type'], 'Vault')
        assert_equal(result[7]['loanScheme']['id'], 'LOAN0001')
        assert_equal(result[7]['loanScheme']['rate'], 500000000)
        assert_equal(result[7]['loanScheme']['ratio'], 150)
        assert_equal(result[7]['txid'], new_vault)

if __name__ == '__main__':
    TokensRPCGetVaultHistory().main()

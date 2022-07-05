#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test burn address tracking"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal

from decimal import Decimal

class BurnAddressTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-eunosheight=1', '-dakotaheight=1']]

    def run_test(self):

        self.nodes[0].generate(101)

        # Burn address
        burn_address = "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"
        self.nodes[0].importprivkey("93ViFmLeJVgKSPxWGQHmSdT5RbeGDtGW4bsiwQM2qnQyucChMqQ")

        # Test masternode creation fee burn
        collateral = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].createmasternode(collateral)
        self.nodes[0].generate(1)

        # Check create masternode burn fee
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'][4:14], "4466547843") # OP_RETURN data DfTxC
        assert_equal(result[0]['txn'], 2)
        assert_equal(result[0]['type'], 'CreateMasternode')
        assert_equal(result[0]['amounts'][0], '1.00000000@DFI')

        result = self.nodes[0].getburninfo()
        assert_equal(result['address'], burn_address)
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(len(result['tokens']), 0)
        assert_equal(result['feeburn'], Decimal('1.00000000'))

        # Create funded account address
        funded_address = self.nodes[0].getnewaddress()
        self.nodes[0].sendtoaddress(funded_address, 1)
        self.nodes[0].utxostoaccount({funded_address:"3@0"})
        self.nodes[0].generate(1)

        # Test burn token
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": funded_address
        })
        self.nodes[0].generate(1)

        # Check token burn fee
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], "6a1f446654785404474f4c440a7368696e7920676f6c6408000000000000000003") # OP_RETURN data
        assert_equal(result[0]['txn'], 1)
        assert_equal(result[0]['type'], 'CreateToken')
        assert_equal(result[0]['amounts'][0], '1.00000000@DFI')

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(len(result['tokens']), 0)
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Mint tokens
        self.nodes[0].minttokens(["100@128"])
        self.nodes[0].generate(1)

        # Send tokens to burn address
        self.nodes[0].accounttoaccount(funded_address, {burn_address:"100@128"})
        self.nodes[0].generate(1)

        # Check burn history
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[0]['type'], 'AccountToAccount')
        assert_equal(result[0]['amounts'][0], '100.00000000@GOLD#128')

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(result['tokens'][0], '100.00000000@GOLD#128')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Track utxostoaccount burn
        self.nodes[0].utxostoaccount({burn_address:"1@0"})
        self.nodes[0].generate(1)

        # Check burn history
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[0]['type'], 'UtxosToAccount')
        assert_equal(result[0]['amounts'][0], '1.00000000@DFI')

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(result['tokens'][0], '1.00000000@DFI')
        assert_equal(result['tokens'][1], '100.00000000@GOLD#128')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Try and spend from burn address account
        try:
            self.nodes[0].accounttoaccount(burn_address, {funded_address:"1@0"})
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("burnt-output" in errorString)

        # Send to burn address with accounttoaccount
        self.nodes[0].accounttoaccount(funded_address, {burn_address:"1@0"})
        self.nodes[0].generate(1)

        # Check burn history
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[0]['type'], 'AccountToAccount')
        assert_equal(result[0]['amounts'][0], '1.00000000@DFI')

        # Auto auth burnt amount
        auth_burn_amount = result[1]['amounts'][0][0:10]

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('0.00000000') + Decimal(auth_burn_amount))
        assert_equal(result['tokens'][0], '2.00000000@DFI')
        assert_equal(result['tokens'][1], '100.00000000@GOLD#128')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Send to burn address with accounttoutxos
        self.nodes[0].accounttoutxos(funded_address, {burn_address:"2@0"})
        self.nodes[0].generate(1)

        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[0]['type'], 'None')
        assert_equal(result[0]['amounts'][0], '2.00000000@DFI')

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('2.00000000') + Decimal(auth_burn_amount))
        assert_equal(result['tokens'][0], '2.00000000@DFI')
        assert_equal(result['tokens'][1], '100.00000000@GOLD#128')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Send utxo to burn address
        txid = self.nodes[0].sendtoaddress(burn_address, 10)
        decodedtx = self.nodes[0].getrawtransaction(txid, 1)
        self.nodes[0].generate(1)

        # Check burn history
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], 'mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG')
        assert_equal(result[0]['type'], 'None')
        assert_equal(result[0]['amounts'][0], '10.00000000@DFI')

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('12.00000000') + Decimal(auth_burn_amount))
        assert_equal(result['tokens'][0], '2.00000000@DFI')
        assert_equal(result['tokens'][1], '100.00000000@GOLD#128')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Spend TX from burn address
        for outputs in decodedtx['vout']:
            if outputs['scriptPubKey']['addresses'][0] == burn_address:
                vout = outputs['n']

        rawtx = self.nodes[0].createrawtransaction([{"txid":txid,"vout":vout}], [{burn_address:9.9999}])
        signed_rawtx = self.nodes[0].signrawtransactionwithwallet(rawtx)
        assert_equal(signed_rawtx['complete'], True)

        # Send should fail as transaction is invalid
        try:
            self.nodes[0].sendrawtransaction(signed_rawtx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("burnt-output" in errorString)

        # Test output of getburninfo
        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('12.00000000') + Decimal(auth_burn_amount))
        assert_equal(result['tokens'][0], '2.00000000@DFI')
        assert_equal(result['tokens'][1], '100.00000000@GOLD#128')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Filter on tx type None
        result = self.nodes[0].listburnhistory({"txtype":'0'})
        assert_equal(len(result), 3)
        assert_equal(result[0]['type'], 'None')
        assert_equal(result[1]['type'], 'None')
        assert_equal(result[2]['type'], 'None')

        # Filter on tx type UtxosToAccount
        result = self.nodes[0].listburnhistory({"txtype":'U'})
        assert_equal(len(result), 1)
        assert_equal(result[0]['type'], 'UtxosToAccount')

        # Filter on tx type AccountToAccount
        result = self.nodes[0].listburnhistory({"txtype":'B'})
        assert_equal(len(result), 2)
        assert_equal(result[0]['type'], 'AccountToAccount')
        assert_equal(result[1]['type'], 'AccountToAccount')

        # Filter on tx type CreateMasternode
        result = self.nodes[0].listburnhistory({"txtype":'C'})
        assert_equal(len(result), 1)
        assert_equal(result[0]['type'], 'CreateMasternode')

        # Filter on tx type CreateToken
        result = self.nodes[0].listburnhistory({"txtype":'T'})
        assert_equal(len(result), 1)
        assert_equal(result[0]['type'], 'CreateToken')

        # Revert all TXs and check that burn history is empty
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(101))
        result = self.nodes[0].listburnhistory()
        assert_equal(len(result), 0)

        result = self.nodes[0].getburninfo()
        assert_equal(result['amount'], Decimal('0.0'))
        assert_equal(len(result['tokens']), 0)

if __name__ == '__main__':
    BurnAddressTest().main()

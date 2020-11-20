#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token multisig ownership RPC."""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

class TokensMultisigOwnerTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=50', '-bayfrontheight=50']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        self.nodes[0].generate(101)

        # Create owners for multisig
        owner_1 = self.nodes[0].getnewaddress("", "legacy")
        owner_2 = self.nodes[0].getnewaddress("", "legacy")
        owner_3 = self.nodes[0].getnewaddress("", "legacy")
        owner_1_pubkey = self.nodes[0].getaddressinfo(owner_1)['pubkey']
        owner_2_pubkey = self.nodes[0].getaddressinfo(owner_2)['pubkey']
        owner_3_pubkey = self.nodes[0].getaddressinfo(owner_3)['pubkey']
        owner_1_privkey = self.nodes[0].dumpprivkey(owner_1)
        owner_2_privkey = self.nodes[0].dumpprivkey(owner_2)
        owner_3_privkey = self.nodes[0].dumpprivkey(owner_3)

        # Create 1-of-3 multisig
        multisig = self.nodes[0].createmultisig(1, [owner_1_pubkey, owner_2_pubkey, owner_3_pubkey])
        multisig_address = multisig['address']
        multisig_scriptpubkey = self.nodes[0].getaddressinfo(multisig_address)['scriptPubKey']

        createTokenTx = self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny",
            "collateralAddress": multisig_address
        }, [])

        self.nodes[0].generate(1)

        # Make sure owner as expected
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['name'], "shiny")
        assert_equal(t128['128']['collateralAddress'], multisig_address)

        # Fund multisig addresses for three TXs
        txid_1 = self.nodes[0].sendtoaddress(multisig_address, 1)
        txid_2 = self.nodes[0].sendtoaddress(multisig_address, 1)
        txid_3 = self.nodes[0].sendtoaddress(multisig_address, 1)

        decodedtx = self.nodes[0].getrawtransaction(txid_1, 1)
        for vout in decodedtx['vout']:
            if vout['scriptPubKey']['addresses'][0] == multisig_address:
                vout_1 = vout['n']

        decodedtx = self.nodes[0].getrawtransaction(txid_2, 1)
        for vout in decodedtx['vout']:
            if vout['scriptPubKey']['addresses'][0] == multisig_address:
                vout_2 = vout['n']

        decodedtx = self.nodes[0].getrawtransaction(txid_3, 1)
        for vout in decodedtx['vout']:
            if vout['scriptPubKey']['addresses'][0] == multisig_address:
                vout_3 = vout['n']

        # Mint TXs
        self.nodes[0].generate(1)

        # Payloads to change name.
        creationTxReversed = "".join(reversed([createTokenTx[i:i+2] for i in range(0, len(createTokenTx), 2)]))
        name_change_1 = "446654786e" + creationTxReversed + "04474f4c44034f4e4508000000000000000003" # name ONE
        name_change_2 = "446654786e" + creationTxReversed + "04474f4c440354574f08000000000000000003" # name TWO
        name_change_3 = "446654786e" + creationTxReversed + "04474f4c4405544852454508000000000000000003" # name THREE

        # Create TXs to change names
        rawtx_1 = self.nodes[0].createrawtransaction([{"txid":txid_1,"vout":vout_1}], [{"data":name_change_1},{owner_1:0.9999}])
        rawtx_2 = self.nodes[0].createrawtransaction([{"txid":txid_2,"vout":vout_2}], [{"data":name_change_2},{owner_2:0.9999}])
        rawtx_3 = self.nodes[0].createrawtransaction([{"txid":txid_3,"vout":vout_3}], [{"data":name_change_3},{owner_3:0.9999}])

        # Sign TXs
        signed_rawtx_1 = self.nodes[0].signrawtransactionwithkey(rawtx_1, [owner_1_privkey], [{"txid":txid_1,"vout":vout_1,"scriptPubKey":multisig_scriptpubkey,"redeemScript":multisig['redeemScript']}])
        signed_rawtx_2 = self.nodes[0].signrawtransactionwithkey(rawtx_2, [owner_2_privkey], [{"txid":txid_2,"vout":vout_2,"scriptPubKey":multisig_scriptpubkey,"redeemScript":multisig['redeemScript']}])
        signed_rawtx_3 = self.nodes[0].signrawtransactionwithkey(rawtx_3, [owner_3_privkey], [{"txid":txid_3,"vout":vout_3,"scriptPubKey":multisig_scriptpubkey,"redeemScript":multisig['redeemScript']}])

        # Check TXs marked as complete
        assert_equal(signed_rawtx_1['complete'], True)
        assert_equal(signed_rawtx_2['complete'], True)
        assert_equal(signed_rawtx_3['complete'], True)

        # Send first name change TX
        self.nodes[0].sendrawtransaction(signed_rawtx_1['hex'])
        self.nodes[0].generate(1)

        # Check that name has changed as expected
        t128 = self.nodes[0].gettoken(128)
        assert_equal(t128['128']['name'], "ONE")

        # Send second name change TX
        self.nodes[0].sendrawtransaction(signed_rawtx_2['hex'])
        self.nodes[0].generate(1)

        # Check that name has changed as expected
        t128 = self.nodes[0].gettoken(createTokenTx)
        assert_equal(t128['128']['name'], "TWO")

        # Send third name change TX
        self.nodes[0].sendrawtransaction(signed_rawtx_3['hex'])
        self.nodes[0].generate(1)

        # Check that name has changed as expected
        t128 = self.nodes[0].gettoken(createTokenTx)
        assert_equal(t128['128']['name'], "THREE")

if __name__ == '__main__':
    TokensMultisigOwnerTest().main()

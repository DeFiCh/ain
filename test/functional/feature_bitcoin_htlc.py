#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test Bitcoin SPV HTLC"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal
from test_framework.authproxy import JSONRPCException
from decimal import Decimal
import os

class BitcoinHTLCTests(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [
            [ "-dummypos=1", "-spv=1"],
        ]
        self.setup_clean_chain = True

    @DefiTestFramework.rollback
    def run_test(self):
        # Set up wallet
        address = self.nodes[0].spv_getnewaddress()
        self.nodes[0].spv_fundaddress(address)

        # Should now have a balance of 1 Bitcoin
        result = self.nodes[0].spv_getbalance()
        assert_equal(result, Decimal("1.00000000"))

        # Import SPV addresses
        self.nodes[0].importwallet(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'data/spv_addresses.txt'))

        seed = "aba5f7e9aecf6ec4372c8a1e49562680d066da4655ee8b4bb01640479fffeaa8"
        seed_hash = "df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e"

        # Make sure addresses were imported and added to Bitcoin wallet
        assert_equal(self.nodes[0].spv_dumpprivkey("bcrt1qs2qsynezncmkzef3qsaaumf5r5uvyeh8ykrg37"), "cSkTV5jWJnKiqMUuuBWo6Sb99UMtddxXerDNQGfU1jJ8WiZoSTRh")
        assert_equal(self.nodes[0].spv_dumpprivkey("bcrt1q28ldz0kwh0ltfad95fzpdqmuxu5getf05jlqu7"), "cSumkzL3QT3aGqeQNuswkLeC5n9BMuhBNvWcCST3VEsLpwVasuQR")

        # Test getpubkey call
        assert_equal(self.nodes[0].spv_getaddresspubkey("bcrt1qs2qsynezncmkzef3qsaaumf5r5uvyeh8ykrg37"), "0224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86")
        assert_equal(self.nodes[0].spv_getaddresspubkey("bcrt1q28ldz0kwh0ltfad95fzpdqmuxu5getf05jlqu7"), "035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be490")

        # Try annd create a HTLC script with relative time
        try:
            self.nodes[0].spv_createhtlc("0224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86", "035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be490", "4194304", seed_hash)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid block denominated relative timeout" in errorString)

        # Try annd create a HTLC script below min blocks
        try:
            self.nodes[0].spv_createhtlc("0224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86", "035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be490", "8", seed_hash)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Timeout below minimum of" in errorString)

        # Try annd create a HTLC script with incorrect pubkey
        try:
            self.nodes[0].spv_createhtlc("0224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea", "035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be490", "10", seed_hash)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Invalid public key" in errorString)

        # Create and learn HTLC script
        htlc_script = self.nodes[0].spv_createhtlc("0224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86", "035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be490", "10", seed_hash)

        # Make sure address and redeemscript are as expected.
        assert_equal(htlc_script['address'], "2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku")
        assert_equal(htlc_script['redeemScript'], "63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068ac")

        # Test script decoding
        result = self.nodes[0].spv_decodehtlcscript(htlc_script['redeemScript'])
        assert_equal(result["sellerkey"], "0224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86")
        assert_equal(result["buyerkey"], "035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be490")
        assert_equal(result["blocks"], 10)
        assert_equal(result["hash"], "df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e")

        # Try and claim before output present
        try:
            self.nodes[0].spv_claimhtlc("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku", address, seed, 1000)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No unspent HTLC outputs found" in errorString)

        # Send to contract for seller claim
        result = self.nodes[0].spv_sendtoaddress(htlc_script['address'], 0.1)
        assert_equal(result['sendmessage'], "")

        # Make sure output present in HTLC address
        output = self.nodes[0].spv_listhtlcoutputs("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku")
        assert_equal(len(output), 1)
        assert_equal(output[0]['amount'], Decimal("0.1"))

        # Try and claim with incorrect seed
        try:
            self.nodes[0].spv_claimhtlc("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku", address, "deadbeef", 1000)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Seed provided does not match seed hash in contract" in errorString)

        # Try and claim with unknown script address
        try:
            self.nodes[0].spv_claimhtlc("2NGT3gZvc75NX8DWGqfuEvniHGj5LiY33Ui", address, "deadbeef", 1000)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Redeem script not found in wallet" in errorString)

        # seller claim HTLC
        result = self.nodes[0].spv_claimhtlc("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku", address, seed, 1000)
        assert_equal(result['sendmessage'], "")

        # Check output spent
        output = self.nodes[0].spv_listhtlcoutputs("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku")
        assert_equal(len(output[0]['spent']), 2)

        # Get raw TX and check secret and redeemscript part of unlock script
        rawtx = self.nodes[0].spv_getrawtransaction(result["txid"])
        assert(rawtx.find("0120aba5f7e9aecf6ec4372c8a1e49562680d066da4655ee8b4bb01640479fffeaa8514c6e63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068ac") != -1)

        # Test getting seed from HTLC transaction
        assert_equal(self.nodes[0].spv_gethtlcseed("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku"), "aba5f7e9aecf6ec4372c8a1e49562680d066da4655ee8b4bb01640479fffeaa8")

        # Send to contract for buyer refund
        result = self.nodes[0].spv_sendtoaddress(htlc_script['address'], 0.1)
        assert_equal(result['sendmessage'], "")

        # Make sure output present in HTLC address
        output = self.nodes[0].spv_listhtlcoutputs("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku")
        assert_equal(len(output), 2)
        assert_equal(output[0]['amount'], Decimal("0.1"))
        assert_equal(output[1]['amount'], Decimal("0.1"))

        # Try and refund before expiration
        try:
            self.nodes[0].spv_refundhtlc("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku", address, 1000)
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("No unspent HTLC outputs found" in errorString)

        # Move confirtmation count to meet refund requirement
        self.nodes[0].spv_setlastheight(10)

        print(self.nodes[0].spv_listhtlcoutputs("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku"))

        # seller claim HTLC
        result = self.nodes[0].spv_refundhtlc("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku", address, 1000)
        assert_equal(result['sendmessage'], "")

        # Check outputs spent
        output = self.nodes[0].spv_listhtlcoutputs("2N1WoHKzHY59uNpXouLQc32h9k5Y3hXK4Ku")
        assert_equal(len(output[0]['spent']), 2)
        assert_equal(len(output[1]['spent']), 2)

        # Get raw TX and check redeemscript part of unlock script
        rawtx = self.nodes[0].spv_getrawtransaction(result["txid"])
        assert(rawtx.find("01004c6e63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068ac") != -1)

        # Generate all new addresses and seeds and test multiple UTXOs claim in script
        seller = self.nodes[0].spv_getnewaddress()
        buyer = self.nodes[0].spv_getnewaddress()
        seller_pubkey = self.nodes[0].spv_getaddresspubkey(seller)
        buyer_pubkey = self.nodes[0].spv_getaddresspubkey(buyer)

        # Create and learn HTLC script, seed generated by RPC call.
        htlc_script = self.nodes[0].spv_createhtlc(seller_pubkey, buyer_pubkey, "10")

        # Get seed and check lengths
        seed = htlc_script['seed']
        assert_equal(len(seed), 64)
        assert_equal(len(htlc_script['seedhash']), 64)

        # Send multiple TX to script address
        for _ in range(3):
            result = self.nodes[0].spv_sendtoaddress(htlc_script['address'], 0.1)
            assert_equal(result['sendmessage'], "")

        # Make sure output present in HTLC address
        output = self.nodes[0].spv_listhtlcoutputs(htlc_script['address'])
        assert_equal(len(output), 3)
        assert_equal(output[0]['amount'], Decimal("0.1"))
        assert_equal(output[1]['amount'], Decimal("0.1"))
        assert_equal(output[2]['amount'], Decimal("0.1"))

        # seller claim HTLC
        result = self.nodes[0].spv_claimhtlc(htlc_script['address'], address, seed, 1000)
        assert_equal(result['sendmessage'], "")

        # Check output spent
        output = self.nodes[0].spv_listhtlcoutputs(htlc_script['address'])
        assert_equal(len(output[0]['spent']), 2)
        assert_equal(len(output[1]['spent']), 2)
        assert_equal(len(output[2]['spent']), 2)

        # Generate all new addresses and seeds and test multiple UTXOs in script
        seller = self.nodes[0].spv_getnewaddress()
        buyer = self.nodes[0].spv_getnewaddress()
        seller_pubkey = self.nodes[0].spv_getaddresspubkey(seller)
        buyer_pubkey = self.nodes[0].spv_getaddresspubkey(buyer)

        # Create and learn HTLC script, seed generated by RPC call.
        htlc_script = self.nodes[0].spv_createhtlc(seller_pubkey, buyer_pubkey, "10")

        # Send multiple TX to script address
        for _ in range(3):
            result = self.nodes[0].spv_sendtoaddress(htlc_script['address'], 0.1)
            assert_equal(result['sendmessage'], "")

        # Make sure output present in HTLC address
        output = self.nodes[0].spv_listhtlcoutputs(htlc_script['address'])
        assert_equal(len(output), 3)
        assert_equal(output[0]['amount'], Decimal("0.1"))
        assert_equal(output[1]['amount'], Decimal("0.1"))
        assert_equal(output[2]['amount'], Decimal("0.1"))

        # Move confirtmation count to meet refund requirement
        self.nodes[0].spv_setlastheight(20)

        # seller claim HTLC
        result = self.nodes[0].spv_refundhtlc(htlc_script['address'], address, 1000)
        assert_equal(result['sendmessage'], "")

        # Check output spent
        output = self.nodes[0].spv_listhtlcoutputs(htlc_script['address'])
        assert_equal(len(output[0]['spent']), 2)
        assert_equal(len(output[1]['spent']), 2)
        assert_equal(len(output[2]['spent']), 2)

        # Test HTLC script decoding failures

        # Incorrect seed length
        try:
            self.nodes[0].spv_decodehtlcscript("63a821df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068ac")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect seed hash length" in errorString)

        # Incorrect seller pubkey length
        try:
            self.nodes[0].spv_decodehtlcscript("63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88200224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068ac")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Seller pubkey incorrect pubkey length" in errorString)

        # Incorrect time length
        try:
            self.nodes[0].spv_decodehtlcscript("63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea866750b27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect timeout length" in errorString)

        # Incorrect buyer pubkey length
        try:
            self.nodes[0].spv_decodehtlcscript("63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27520035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068ac")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Buyer pubkey incorrect pubkey length" in errorString)

        # Incorrect redeemscript length
        try:
            self.nodes[0].spv_decodehtlcscript("63a820df95183883789f237977543885e1f82ddc045a3ba90c8f25b43a5b797a35d20e88210224e7de2f3a9d4cdc4fdc14601c75176287297c212aae9091404956955f1aea86675ab27521035fb3eadde611a39036e61d4c8288d1b896f2c94cee49e60a3d1c02236f4be49068")
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Incorrect redeemscript length" in errorString)

if __name__ == '__main__':
    BitcoinHTLCTests().main()

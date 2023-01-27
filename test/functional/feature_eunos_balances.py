#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test burn address tracking"""

from test_framework.test_framework import DefiTestFramework

from test_framework.util import assert_equal

from decimal import Decimal

class TransferBurnTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-amkheight=1', '-eunosheight=200', '-dakotaheight=1']]

    @DefiTestFramework.rollback
    def run_test(self):

        # Burn address
        old_burn_address = "mfdefichainDSTBurnAddressXXXZcE1vs"
        burn_address = "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"

        # Destruction addresses
        destruction_one = "2MxJf6Ak8MGrLoGdekrU6AusW29szZUFphH"
        destruction_two = "mxiaFfAnCoXEUy4RW8NgsQM7yU5YRCiFSh"

        # Import priv keys to allow sending account DFI to them
        self.nodes[0].importprivkey("cVx5Bn1MQREmdR1STQZK1dMD7XwzvUQCajTRjYjtppXzDk7F6gx2")
        self.nodes[0].importprivkey("cQ3NeYsQfNT4LsEWQQTYm4iSRbKJcuvdyPLTpVxzaRxkPfnQcQxn")

        self.nodes[0].generate(101)

        # Create funded account address
        address = self.nodes[0].getnewaddress()
        self.nodes[0].sendtoaddress(address, 1)
        self.nodes[0].generate(1)

        # Create tokens
        self.nodes[0].createtoken({
            "symbol": "GOLD",
            "name": "shiny gold",
            "collateralAddress": address
        })
        self.nodes[0].generate(1)

        # Check token burn fee
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], "6a1f446654785404474f4c440a7368696e7920676f6c6408000000000000000003") # OP_RETURN data
        assert_equal(result[0]['txn'], 1)
        assert_equal(result[0]['type'], 'CreateToken')
        assert_equal(result[0]['amounts'][0], '1.00000000@DFI')

        self.nodes[0].createtoken({
            "symbol": "SILVER",
            "name": "shiny silver",
            "collateralAddress": address
        })
        self.nodes[0].generate(1)

        # Check token burn fee
        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], "6a2344665478540653494c5645520c7368696e792073696c76657208000000000000000003") # OP_RETURN data
        assert_equal(result[0]['txn'], 1)
        assert_equal(result[0]['type'], 'CreateToken')
        assert_equal(result[0]['amounts'][0], '1.00000000@DFI')

        # Mint tokens
        self.nodes[0].minttokens(["100@128"])
        self.nodes[0].generate(1)

        self.nodes[0].minttokens(["100@129"])
        self.nodes[0].generate(1)

        # Send tokens to burn address
        self.nodes[0].accounttoaccount(address, {old_burn_address:"100@128"})
        self.nodes[0].generate(1)

        self.nodes[0].accounttoaccount(address, {old_burn_address:"100@129"})
        self.nodes[0].generate(1)

        # Check balance
        result = self.nodes[0].getaccount(old_burn_address)
        assert_equal(result[0], "100.00000000@GOLD#128")
        assert_equal(result[1], "100.00000000@SILVER#129")

        # Send funds to destruction addresses
        self.nodes[0].utxostoaccount({destruction_one:"5@0"})
        self.nodes[0].utxostoaccount({destruction_one:"5@0"})
        self.nodes[0].utxostoaccount({destruction_two:"20@0"})
        self.nodes[0].generate(1)

        # Check destruction balance
        result = self.nodes[0].getaccount(destruction_one)
        assert_equal(result[0], "10.00000000@DFI")
        result = self.nodes[0].getaccount(destruction_two)
        assert_equal(result[0], "20.00000000@DFI")

        # Move one past fork height
        self.nodes[0].generate(201 - self.nodes[0].getblockcount())

        # Check funds have moved
        assert_equal(len(self.nodes[0].getaccount(old_burn_address)), 0)
        result = self.nodes[0].getaccount(burn_address)
        assert_equal(result[0], "100.00000000@GOLD#128")
        assert_equal(result[1], "100.00000000@SILVER#129")

        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['txn'], 2)
        assert_equal(result[0]['type'], 'AccountToAccount')
        assert_equal(result[0]['amounts'][0], '100.00000000@SILVER#129')
        assert_equal(result[1]['owner'], burn_address)
        assert_equal(result[1]['txn'], 1)
        assert_equal(result[1]['type'], 'AccountToAccount')
        assert_equal(result[1]['amounts'][0], '100.00000000@GOLD#128')

        result = self.nodes[0].getburninfo()
        assert_equal(result['address'], burn_address)
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(result['tokens'][0], '100.00000000@GOLD#128')
        assert_equal(result['tokens'][1], '100.00000000@SILVER#129')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Check destruction balance
        result = self.nodes[0].getaccount(destruction_one)
        assert_equal(len(result), 0)
        result = self.nodes[0].getaccount(destruction_two)
        assert_equal(len(result), 0)

        # Revert fork block
        self.nodes[0].invalidateblock(self.nodes[0].getblockhash(200))

        assert_equal(len(self.nodes[0].getaccount(burn_address)), 0)
        result = self.nodes[0].getaccount(old_burn_address)
        assert_equal(result[0], "100.00000000@GOLD#128")
        assert_equal(result[1], "100.00000000@SILVER#129")

        assert_equal(len(self.nodes[0].listburnhistory()), 2) # Creation fee burns still present

        result = self.nodes[0].getburninfo()
        assert_equal(result['address'], burn_address)
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(len(result['tokens']), 0)

        # Check destruction balance
        result = self.nodes[0].getaccount(destruction_one)
        assert_equal(result[0], "10.00000000@DFI")
        result = self.nodes[0].getaccount(destruction_two)
        assert_equal(result[0], "20.00000000@DFI")

        # Move to fork height to reburn
        self.nodes[0].generate(2)
        assert_equal(len(self.nodes[0].getaccount(old_burn_address)), 0)
        result = self.nodes[0].getaccount(burn_address)
        assert_equal(result[0], "100.00000000@GOLD#128")
        assert_equal(result[1], "100.00000000@SILVER#129")

        result = self.nodes[0].listburnhistory()
        assert_equal(result[0]['owner'], burn_address)
        assert_equal(result[0]['txn'], 2)
        assert_equal(result[0]['type'], 'AccountToAccount')
        assert_equal(result[0]['amounts'][0], '100.00000000@SILVER#129')
        assert_equal(result[1]['owner'], burn_address)
        assert_equal(result[1]['txn'], 1)
        assert_equal(result[1]['type'], 'AccountToAccount')
        assert_equal(result[1]['amounts'][0], '100.00000000@GOLD#128')

        result = self.nodes[0].getburninfo()
        assert_equal(result['address'], burn_address)
        assert_equal(result['amount'], Decimal('0.00000000'))
        assert_equal(result['tokens'][0], '100.00000000@GOLD#128')
        assert_equal(result['tokens'][1], '100.00000000@SILVER#129')
        assert_equal(result['feeburn'], Decimal('2.00000000'))

        # Check destruction balance
        result = self.nodes[0].getaccount(destruction_one)
        assert_equal(len(result), 0)
        result = self.nodes[0].getaccount(destruction_two)
        assert_equal(len(result), 0)

if __name__ == '__main__':
    TransferBurnTest().main()

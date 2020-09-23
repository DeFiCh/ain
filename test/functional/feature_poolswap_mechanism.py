#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test pool's RPC.

- verify basic accounts operation
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

import random
import time
from decimal import Decimal

class PoolSwapTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # node0: main
        # node1: secondary tester
        # node2: revert create (all)
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0', '-dip1height=0'], ['-txnotokens=0', '-dip1height=0'], ['-txnotokens=0', '-dip1height=0']]

        # SET parameters for create tokens and pools
        #========================
        self.count_pools = 1     # 10
        self.count_account = 2  # 1000
        self.commission = 0.001
        self.amount_token = 1000 # 1000
        self.decimal = 100000000
        
        self.tokens = []
        self.accounts = []
        self.pools = []
        self.liquidity = {}
        self.pollswap_liquidity = {}

        self.totalDistributed = 0

        # Generate pool: 1 pool = 1 + 2 token = 3 tx
        # Minted tokens: 1 pool = 2 token = 4 tx
        # Sent token:    1 pool = 2 token = 4 tx
        # Liquidity:     1 pool * 10 acc = 2 token * 10 acc = 20 tx
        # Set gov:       2 tx
        # PoolSwap:      1 pool * 10 acc = 2 token * 10 acc = 20 tx
        count_create_pool_tx = self.count_pools * 3
        count_pool_token = self.count_pools * 2
        count_mint_and_sent = count_pool_token * 4
        count_add_liquidity = count_pool_token * self.count_account
        self.count_poolswap = count_pool_token * self.count_account

        self.count_tx = count_create_pool_tx + count_mint_and_sent + count_add_liquidity + 2 + self.count_poolswap

    # TODO TODO TODO
    def get_id_token(self, symbol):
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == symbol):
                return str(idx)

    def generate_accounts(self):
        for i in range(self.count_account):
            self.accounts.append(self.nodes[0].getnewaddress("", "legacy"))

    def create_token(self, symbol, address):
        self.nodes[0].createtoken([], {
            "symbol": symbol,
            "name": "Token " + symbol,
            "isDAT": False,
            "collateralAddress": address
        })
        self.nodes[0].generate(1)
        self.tokens.append(symbol)

    def create_pool(self, tokenA, tokenB, owner):
        self.pools.append(self.nodes[0].createpoolpair({
            "tokenA": tokenA,
            "tokenB": tokenB,
            "commission": self.commission,
            "status": True,
            "ownerFeeAddress": owner
        }, []))
        self.nodes[0].generate(1)

    def create_pools(self, owner):
        for i in range(self.count_pools):
            tokenA = "GOLD" + str(i)
            tokenB = "SILVER" + str(i)
            self.create_token(tokenA, owner)
            self.create_token(tokenB, owner)

            tokenA = tokenA + "#" + self.get_id_token(tokenA)
            tokenB = tokenB + "#" + self.get_id_token(tokenB)
            self.create_pool(tokenA, tokenB, owner)

    def mint_tokens(self, owner):
        mint_amount = str(self.count_account * self.amount_token)

        for item in self.tokens:
            self.nodes[0].sendmany("", { owner : 0.02 })
            self.nodes[0].generate(1)
            self.nodes[0].minttokens([], mint_amount + "@" + self.get_id_token(item))
            self.nodes[0].generate(1)

        return mint_amount

    def send_tokens(self, owner):
        send_amount = str(self.amount_token)
        for token in self.tokens:
            for start in range(0, self.count_account, 10):
                outputs = {}
                if start + 10 > self.count_account:
                    end = self.count_account
                else:
                    end = start + 10

                for idx in range(start, end):
                    outputs[self.accounts[idx]] = send_amount + "@" + self.get_id_token(token)

                self.nodes[0].sendmany("", { owner : 0.02 })
                self.nodes[0].generate(1)

                self.nodes[0].accounttoaccount([], owner, outputs)
                self.nodes[0].generate(1)

    def add_pools_liquidity(self, owner):
        for item in range(self.count_pools):
            tokenA = "GOLD" + str(item)
            tokenB = "SILVER" + str(item)

            self.liquidity[self.get_id_token(tokenA)] = 0
            self.liquidity[self.get_id_token(tokenB)] = 0

            for start in range(0, self.count_account, 10):
                if start + 10 > self.count_account:
                    end = self.count_account
                else:
                    end = start + 10

                for idx in range(start, end):
                    self.nodes[0].sendmany("", { self.accounts[idx] : 0.02 })
                self.nodes[0].generate(1)

                for idx in range(start, end):
                    amountA = random.randint(1, self.amount_token // 2)
                    amountB = random.randint(1, self.amount_token // 2)

                    self.liquidity[self.get_id_token(tokenA)] += amountA
                    self.liquidity[self.get_id_token(tokenB)] += amountB

                    amountA = str(amountA) + "@" + self.get_id_token(tokenA)
                    amountB = str(amountB) + "@" + self.get_id_token(tokenB)
                    self.nodes[0].addpoolliquidity({
                        self.accounts[idx]: [amountA, amountB]
                    }, self.accounts[idx], [])
                    
                    print("add liquidity " + amountA + " | " + amountB)
                self.nodes[0].generate(1)

    def slope_swap(self, unswapped, poolFrom, poolTo):
        while unswapped > 0:
            if poolFrom / 1000 > unswapped:
                stepFrom = unswapped
            else:
                stepFrom = poolFrom / 1000

            stepTo = poolTo * stepFrom / poolFrom
            poolFrom += stepFrom
            poolTo -= stepTo
            unswapped -= stepFrom

        return (poolFrom, poolTo)

    def pollswap(self):
        for item in range(self.count_pools):
            tokenA = "GOLD" + str(item)
            tokenB = "SILVER" + str(item)

            self.pollswap_liquidity[self.get_id_token(tokenA)] = 0
            pool = self.pools[item]
            idPool = list(self.nodes[0].getpoolpair(pool, True).keys())[0]

            for start in range(0, self.count_account, 10):
                if start + 10 > self.count_account:
                    end = self.count_account
                else:
                    end = start + 10

                for idx in range(start, end):
                    self.nodes[0].sendmany("", { self.accounts[idx] : 0.02 })
                self.nodes[0].generate(1)

                amount = random.randint(1, self.amount_token // 2)
                self.pollswap_liquidity[self.get_id_token(tokenA)] += amount

                amountsA = {}
                amountsB = {}
                reserveA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA']
                reserveB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB']
                newReserveA = 0
                newReserveB = 0

                for idx in range(start, end):
                    amountsA[idx] = self.nodes[0].getaccount(self.accounts[idx], {}, True)[self.get_id_token(tokenA)]
                    amountsB[idx] = self.nodes[0].getaccount(self.accounts[idx], {}, True)[self.get_id_token(tokenB)]
                    hash = self.nodes[0].poolswap({
                        "from": self.accounts[idx],
                        "tokenFrom": self.get_id_token(tokenB),
                        "amountFrom": amount,
                        "to": self.accounts[idx],
                        "tokenTo": str(self.get_id_token(tokenA)),
                    }, [])
                    print("swap " + hash + "|" + str(amount))
                self.nodes[0].generate(1)

                for idx in range(start, end):
                    liquidity = self.nodes[0].getaccount(self.accounts[idx], {}, True)[idPool]
                    totalLiquidity = self.nodes[0].getpoolpair(pool, True)[idPool]['totalLiquidity']
                    liqWeight = liquidity * 10000 // totalLiquidity
                    assert(liqWeight < 10000)

                    blockCommissionA = self.nodes[0].getpoolpair(pool, True)[idPool]['blockCommissionA']
                    blockCommissionB = self.nodes[0].getpoolpair(pool, True)[idPool]['blockCommissionB']
                    feeA = (blockCommissionA * liqWeight) / Decimal(self.count_account / 2) # Divide by the number of accounts
                    feeB = (blockCommissionB * liqWeight) / Decimal(self.count_account / 2) # Divide by the number of accounts

                    #print("FEE A: " + str(feeA))
                    #print("FEE B: " + str(feeB))

                    (reserveB, reserveA) = self.slope_swap(Decimal(amount - (amount * self.commission)), reserveB, reserveA)
                    newReserveA = reserveA
                    newReserveB = reserveB
                    # TODO Inaccurate calculations
                    assert_equal(amountsB[idx] - amount + feeB, self.nodes[0].getaccount(self.accounts[idx], {}, True)[self.get_id_token(tokenB)])

                    yieldFarming = 1 # TODO
                    rewardPct = self.nodes[0].getpoolpair(pool, True)[idPool]['rewardPct']
                    poolReward = yieldFarming * rewardPct

                    if poolReward:
                        providerReward = poolReward * liqWeight
                        if providerReward:
                            self.totalDistributed += providerReward;

                reserveA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA']
                reserveB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB']
                #assert_equal(reserveA, format(newReserveA, '.8f'))
                assert_equal(str(reserveB), format(newReserveB, '.8f'))

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")

        self.nodes[0].generate(100)
        self.sync_all()

        # Stop node #2 for future revert
        self.stop_node(2)

        owner = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].generate(1)

        # START
        #========================
        print("Generating accounts...")
        self.generate_accounts()
        assert_equal(len(self.accounts), self.count_account)
        print("Generate " + str(self.count_account) + " accounts")

        print("Generating pools...")
        self.create_pools(owner)
        assert_equal(len(self.nodes[0].listtokens({}, False)), self.count_pools * 3)
        assert_equal(len(self.nodes[0].listpoolpairs({}, False)), self.count_pools)
        print("Generate " + str(self.count_pools) + " pools and " + str(self.count_pools * 2) + " tokens")

        print("Minting tokens...")
        mint_amount = self.mint_tokens(owner)
        assert_equal(len(self.nodes[0].getaccount(owner, {}, True)), self.count_pools * 2)
        print("Minted " + mint_amount + " of every coin")

        print("Sending tokens...")
        self.send_tokens(owner)
        for account in self.accounts:
            assert_equal(self.nodes[0].getaccount(account, {}, True)[self.get_id_token(self.tokens[0])], self.amount_token)
        print("Tokens sent out")

        print("Adding liquidity...")
        self.add_pools_liquidity(owner)
        for pool in self.pools:
            #print(self.nodes[0].getpoolpair(pool, True))
            idPool = list(self.nodes[0].getpoolpair(pool, True).keys())[0]

            idTokenA = self.nodes[0].getpoolpair(pool, True)[idPool]['idTokenA']
            idTokenB = self.nodes[0].getpoolpair(pool, True)[idPool]['idTokenB']

            reserveA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA']
            reserveB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB']
            assert_equal(self.liquidity[idTokenA], reserveA)
            assert_equal(self.liquidity[idTokenB], reserveB)

            reserveAB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA/reserveB']
            reserveBA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB/reserveA']
            resAB = int((self.liquidity[idTokenA] * self.decimal) / (self.liquidity[idTokenB] * self.decimal) * self.decimal)
            resBA = int((self.liquidity[idTokenB] * self.decimal) / (self.liquidity[idTokenA] * self.decimal) * self.decimal)
            assert_equal(reserveAB * self.decimal, resAB)
            assert_equal(reserveBA * self.decimal, resBA)
        print("Liquidity added")

        print("Setting governance variables...")
        obj = {}
        for i in range(self.count_pools):
            obj[str(i + 1)] = 1 / self.count_pools

        self.nodes[0].setgov({ "LP_SPLITS": obj })
        self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": 35.5 })
        self.nodes[0].generate(1)

        g1 = self.nodes[0].getgov("LP_SPLITS")
        for i in range(self.count_pools):
            assert_equal(g1['LP_SPLITS'][str(i + 1)], 1 / self.count_pools)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal('35.50000000')} )
        print("Set governance variables")

        print("Swapping tokens...")
        start_time = time.time()
        self.pollswap()
        end_time = time.time() - start_time
        print("Tokens exchanged")
        print("Elapsed time: {} s".format(end_time))

        print("Generating block...")
        start_time = time.time()
        self.nodes[0].generate(self.count_poolswap)
        end_time = time.time() - start_time
        print("Elapsed time: {} s".format(end_time))

        # REVERTING:
        #========================
        print ("Reverting...")
        self.start_node(2)
        self.nodes[2].generate(20)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        #assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold)
        #assert_equal(self.nodes[0].getaccount(accountSilver, {}, True)[idSilver], initialSilver)

        #assert_equal(len(self.nodes[0].getrawmempool()), self.count_tx)


if __name__ == '__main__':
    PoolSwapTest ().main ()

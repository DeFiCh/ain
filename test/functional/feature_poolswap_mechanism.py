#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test poolswap mechanism RPC.

- poolswap mechanism check
"""

from test_framework.test_framework import DefiTestFramework

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
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=0', '-bayfrontheight=0', '-bayfrontgardensheight=0', '-eunosheight=120', '-subsidytest=1'],
            ['-txnotokens=0', '-amkheight=0', '-bayfrontheight=0', '-bayfrontgardensheight=0', '-eunosheight=120', '-subsidytest=1'],
            ['-txnotokens=0', '-amkheight=0', '-bayfrontheight=0', '-bayfrontgardensheight=0', '-eunosheight=120', '-subsidytest=1']
        ]

        # SET parameters for create tokens and pools
        #========================
        self.COUNT_POOLS = 1     # 10
        self.COUNT_ACCOUNT = 10  # 1000
        self.COMMISSION = 0.001
        self.AMOUNT_TOKEN = 1000
        self.DECIMAL = 100000000
        self.LP_DAILY_DFI_REWARD = 35.5

        self.tokens = []
        self.accounts = []
        self.pools = []
        self.liquidity = {}

        # Generate pool: 1 pool = 1 + 2 token = 3 tx
        # Minted tokens: 1 pool = 2 token = 4 tx
        # Sent token:    1 pool = 2 token = 4 tx
        # Liquidity:     1 pool * 10 acc = 2 token * 10 acc = 20 tx
        # Set gov:       2 tx
        # PoolSwap:      1 pool * 10 acc = 2 token * 10 acc = 20 tx

        # self.COUNT_TX = count_create_pool_tx + count_mint_and_sent + count_add_liquidity + 2 + self.COUNT_POOLSWAP

    def get_id_token(self, symbol):
        list_tokens = self.nodes[0].listtokens()
        for idx, token in list_tokens.items():
            if (token["symbol"] == symbol):
                return str(idx)

    def generate_accounts(self):
        for i in range(self.COUNT_ACCOUNT):
            self.accounts.append(self.nodes[0].getnewaddress("", "legacy"))

    def create_token(self, symbol, address):
        self.nodes[0].createtoken({
            "symbol": symbol,
            "name": "Token " + symbol,
            "isDAT": False,
            "collateralAddress": address
        }, [])
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.tokens.append(symbol)

    def create_pool(self, tokenA, tokenB, owner):
        self.pools.append(self.nodes[0].createpoolpair({
            "tokenA": tokenA,
            "tokenB": tokenB,
            "commission": self.COMMISSION,
            "status": True,
            "ownerAddress": owner
        }, []))
        self.nodes[0].generate(1)
        self.sync_blocks()

    def create_pools(self, owner):
        for i in range(self.COUNT_POOLS):
            tokenA = "GOLD" + str(i)
            tokenB = "SILVER" + str(i)
            self.create_token(tokenA, owner)
            self.create_token(tokenB, owner)
            tokenA = tokenA + "#" + self.get_id_token(tokenA)
            tokenB = tokenB + "#" + self.get_id_token(tokenB)
            self.create_pool(tokenA, tokenB, owner)

    def mint_tokens(self, owner):
        mint_amount = str(self.COUNT_ACCOUNT * self.AMOUNT_TOKEN)
        for item in self.tokens:
            self.nodes[0].sendmany("", { owner : 0.02 })
            self.nodes[0].generate(1)
            self.nodes[0].minttokens(mint_amount + "@" + self.get_id_token(item), [])
            self.nodes[0].generate(1)
            self.sync_blocks()
        return mint_amount

    def send_tokens(self, owner):
        send_amount = str(self.AMOUNT_TOKEN)
        for token in self.tokens:
            for start in range(0, self.COUNT_ACCOUNT, 10):
                outputs = {}
                if start + 10 > self.COUNT_ACCOUNT:
                    end = self.COUNT_ACCOUNT
                else:
                    end = start + 10
                for idx in range(start, end):
                    outputs[self.accounts[idx]] = send_amount + "@" + self.get_id_token(token)
                self.nodes[0].sendmany("", { owner : 0.02 })
                self.nodes[0].generate(1)
                self.nodes[0].accounttoaccount(owner, outputs, [])
                self.nodes[0].generate(1)
                self.sync_blocks()

    def add_pools_liquidity(self, owner):
        for item in range(self.COUNT_POOLS):
            tokenA = "GOLD" + str(item)
            tokenB = "SILVER" + str(item)
            self.liquidity[self.get_id_token(tokenA)] = 0
            self.liquidity[self.get_id_token(tokenB)] = 0
            for start in range(0, self.COUNT_ACCOUNT, 10):
                if start + 10 > self.COUNT_ACCOUNT:
                    end = self.COUNT_ACCOUNT
                else:
                    end = start + 10
                for idx in range(start, end):
                    self.nodes[0].sendmany("", { self.accounts[idx] : 0.02 })
                self.nodes[0].generate(1)
                for idx in range(start, end):
                    amountA = random.randint(1, self.AMOUNT_TOKEN // 2)
                    amountB = random.randint(1, self.AMOUNT_TOKEN // 2)
                    self.liquidity[self.get_id_token(tokenA)] += amountA
                    self.liquidity[self.get_id_token(tokenB)] += amountB
                    amountA = str(amountA) + "@" + self.get_id_token(tokenA)
                    amountB = str(amountB) + "@" + self.get_id_token(tokenB)
                    self.nodes[0].addpoolliquidity({
                        self.accounts[idx]: [amountA, amountB]
                    }, self.accounts[idx], [])
                self.nodes[0].generate(1)
                self.sync_blocks()

    def slope_swap(self, unswapped, poolFrom, poolTo):
        swapped = poolTo - (poolTo * poolFrom / (poolFrom + unswapped))
        poolFrom += unswapped
        poolTo -= swapped

        return (poolFrom, poolTo)

    def poolswap(self, nodes):
        for item in range(self.COUNT_POOLS):
            tokenA = "GOLD" + str(item)
            tokenB = "SILVER" + str(item)
            pool = self.pools[item]
            idPool = list(self.nodes[0].getpoolpair(pool, True).keys())[0]
            for start in range(0, self.COUNT_ACCOUNT, 10):
                if start + 10 > self.COUNT_ACCOUNT:
                    end = self.COUNT_ACCOUNT
                else:
                    end = start + 10
                for idx in range(start, end):
                    self.nodes[0].sendmany("", { self.accounts[idx] : 0.02 })
                self.nodes[0].generate(1)
                self.sync_blocks(nodes)

                amount = random.randint(1, self.AMOUNT_TOKEN // 2)
                amountsB = {}
                reserveA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA']
                reserveB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB']
                commission = self.nodes[0].getpoolpair(pool, True)[idPool]['commission']
                newReserveB = 0
                poolRewards = {}
                blockCommissionB = 0

                for idx in range(start, end):
                    poolRewards[idx] = self.nodes[0].getaccount(self.accounts[idx], {}, True)['0']
                    amountsB[idx] = self.nodes[0].getaccount(self.accounts[idx], {}, True)[self.get_id_token(tokenB)]
                    blockCommissionB += (amount * self.DECIMAL) * (commission * self.DECIMAL) / self.DECIMAL / self.DECIMAL
                    self.nodes[0].poolswap({
                        "from": self.accounts[idx],
                        "tokenFrom": self.get_id_token(tokenB),
                        "amountFrom": amount,
                        "to": self.accounts[idx],
                        "tokenTo": str(self.get_id_token(tokenA)),
                    }, [])
                self.nodes[0].generate(1)
                self.sync_blocks(nodes)

                for idx in range(start, end):
                    liquidity = self.nodes[0].getaccount(self.accounts[idx], {}, True)[idPool]
                    totalLiquidity = self.nodes[0].getpoolpair(pool, True)[idPool]['totalLiquidity']

                    liquidity = int(liquidity * self.DECIMAL)
                    totalLiquidity = int(totalLiquidity * self.DECIMAL)

                    feeB = (int(blockCommissionB * self.DECIMAL) * liquidity) // totalLiquidity
                    (reserveB, reserveA) = self.slope_swap(Decimal(amount - (amount * self.COMMISSION)), reserveB, reserveA)
                    newReserveB = reserveB

                    assert_equal(amountsB[idx] - amount + Decimal(str(feeB / self.DECIMAL)), self.nodes[0].getaccount(self.accounts[idx], {}, True)[self.get_id_token(tokenB)])

                    realPoolReward = self.nodes[0].getaccount(self.accounts[idx], {}, True)['0'] - poolRewards[idx]

                    yieldFarming = int(self.LP_DAILY_DFI_REWARD * self.DECIMAL) / (60 * 60 * 24 / 600) # Regression test in chainparams.cpp
                    rewardPct = self.nodes[0].getpoolpair(pool, True)[idPool]['rewardPct']
                    assert(rewardPct > 0)
                    poolReward = yieldFarming * int(rewardPct * self.DECIMAL) // self.DECIMAL

                    if poolReward:
                        providerReward = poolReward * liquidity // totalLiquidity
                        if providerReward:
                            assert_equal(int(realPoolReward * self.DECIMAL), int(providerReward))

                reserveB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB']
                assert_equal(str(reserveB), format(newReserveB, '.8f'))

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")

        self.nodes[0].generate(100)
        owner = self.nodes[0].getnewaddress("", "legacy")
        self.nodes[0].generate(1)
        self.sync_blocks()

        # START
        #========================
        print("Generating accounts...")
        self.generate_accounts()
        assert_equal(len(self.accounts), self.COUNT_ACCOUNT)
        print("Generate " + str(self.COUNT_ACCOUNT) + " accounts")

        print("Generating pools...")
        self.create_pools(owner)
        assert_equal(len(self.nodes[0].listtokens({}, False)), (3 * self.COUNT_POOLS) + 1)
        assert_equal(len(self.nodes[0].listpoolpairs({}, False)), self.COUNT_POOLS)
        print("Generate " + str(self.COUNT_POOLS) + " pools and " + str(self.COUNT_POOLS * 2) + " tokens")

        print("Minting tokens...")
        mint_amount = self.mint_tokens(owner)
        assert_equal(len(self.nodes[0].getaccount(owner, {}, True)), self.COUNT_POOLS * 2)
        print("Minted " + mint_amount + " of every coin")

        print("Sending tokens...")
        self.send_tokens(owner)
        for account in self.accounts:
            assert_equal(self.nodes[0].getaccount(account, {}, True)[self.get_id_token(self.tokens[0])], self.AMOUNT_TOKEN)
        print("Tokens sent out")

        print("Adding liquidity...")
        self.add_pools_liquidity(owner)
        for pool in self.pools:
            idPool = list(self.nodes[0].getpoolpair(pool, True).keys())[0]

            idTokenA = self.nodes[0].getpoolpair(pool, True)[idPool]['idTokenA']
            idTokenB = self.nodes[0].getpoolpair(pool, True)[idPool]['idTokenB']

            reserveA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA']
            reserveB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB']
            assert_equal(self.liquidity[idTokenA], reserveA)
            assert_equal(self.liquidity[idTokenB], reserveB)

            reserveAB = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveA/reserveB']
            reserveBA = self.nodes[0].getpoolpair(pool, True)[idPool]['reserveB/reserveA']
            resAB = int((self.liquidity[idTokenA] * self.DECIMAL) / (self.liquidity[idTokenB] * self.DECIMAL) * self.DECIMAL)
            resBA = int((self.liquidity[idTokenB] * self.DECIMAL) / (self.liquidity[idTokenA] * self.DECIMAL) * self.DECIMAL)
            assert_equal(reserveAB * self.DECIMAL, resAB)
            assert_equal(reserveBA * self.DECIMAL, resBA)
        print("Liquidity added")

        print("Setting governance variables...")
        obj = {}
        for i in range(self.COUNT_POOLS):
            obj[str(i + 1)] = 1 / self.COUNT_POOLS

        self.nodes[0].setgov({ "LP_SPLITS": obj })
        self.nodes[0].setgov({ "LP_DAILY_DFI_REWARD": self.LP_DAILY_DFI_REWARD })
        self.nodes[0].generate(1)
        self.sync_blocks()

        g1 = self.nodes[0].getgov("LP_SPLITS")
        for i in range(self.COUNT_POOLS):
            assert_equal(g1['LP_SPLITS'][str(i + 1)], 1 / self.COUNT_POOLS)

        g2 = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")
        assert(g2 == {'LP_DAILY_DFI_REWARD': Decimal(self.LP_DAILY_DFI_REWARD)} )
        print("Set governance variables")

        # Stop node #2 for future revert
        self.stop_node(2)

        print("Swapping tokens...")
        start_time = time.time()
        nodes = self.nodes[0:2]
        self.poolswap(nodes)
        end_time = time.time() - start_time
        print("Tokens exchanged")
        print("Elapsed time: {} s".format(end_time))

        # REVERTING:
        #========================
        print ("Reverting...")
        self.start_node(2)
        self.nodes[2].generate(5)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        # Wipe mempool
        self.nodes[0].clearmempool()
        self.nodes[1].clearmempool()
        self.nodes[2].clearmempool()

        assert(self.nodes[0].getblockcount() == 120) # eunos

        self.LP_DAILY_DFI_REWARD = self.nodes[0].getgov("LP_DAILY_DFI_REWARD")['LP_DAILY_DFI_REWARD']
        assert_equal(self.LP_DAILY_DFI_REWARD, Decimal('14843.90592000')) # 144 blocks a day times 103.08268000

        print("Swapping tokens after eunos height...")
        self.poolswap(self.nodes)


if __name__ == '__main__':
    PoolSwapTest ().main ()

#!/usr/bin/env python3
import sys

if len(sys.argv) != 2:
    print("Usage: {} <height>".format(sys.argv[0]))
    sys.exit(1)

eunos_fork = 894000
emission_reduction_interval = 32690
emission_reduction_amount = 1658

COIN = 100000000
initial_masternodes = 1000010 * COIN  # 100m collateral + 10 creation fee
initial_dist = 58800000 * COIN
initial_dist += 44100000 * COIN
initial_dist += 11760000 * COIN
initial_dist += 11760000 * COIN
initial_dist += 29400000 * COIN
initial_dist += 14700000 * COIN
initial_dist += 64680000 * COIN
initial_dist += 235200000 * COIN
initial_dist += 117600000 * COIN

foundation_burn = 26571399989000000
foundation_burn += 287883508826675

subsidy_reductions = {}


def get_subsidy(height):
    base_subsidy = 200 * COIN

    if height >= eunos_fork:
        base_subsidy = 40504000000
        reductions = int((height - eunos_fork) / emission_reduction_interval)

        if reductions in subsidy_reductions:
            return subsidy_reductions[reductions]

        for _ in range(reductions, 0, -1):
            reduction_amount = (base_subsidy * emission_reduction_amount) / 100000
            if reduction_amount == 0:
                return 0

            base_subsidy -= reduction_amount

        subsidy_reductions[reductions] = base_subsidy

    return base_subsidy


total_supply = 3 * initial_masternodes
total_supply += initial_dist
total_supply -= foundation_burn

for i in range(int(sys.argv[1])):
    total_supply += get_subsidy(i)

print(f"{total_supply / 1e8:.8f}")

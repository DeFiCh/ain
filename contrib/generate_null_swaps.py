#!/usr/bin/env python3

from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
import re
import subprocess
import sys
import time

rpcuser = "your_username"
rpcpassword = "your_strong_password"
rpc_host = "localhost"
rpc_port = 8554

defid_binary = "/mnt/dev/github/ain-new/build/src/defid"
log_file = "/home/user/.defi/debug.log"

last_null_swap = 2269592

max_attempts = 10
time_to_sleep = 60


def start_defid(height):
    args = [
        f"-stopatheight={height + 1}",
        f"-rpcuser={rpcuser}",
        f"-rpcpassword={rpcpassword}",
        "-rpcallowip=0.0.0.0/0",
        "-rpcbind=0.0.0.0",
        "-server=1",
        "-daemon=1",
        "-debug=swapresult",
    ]
    command = [defid_binary] + args

    attempts = 0
    while attempts < max_attempts:
        result = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )

        if result.returncode == 0:
            return
        else:
            if "Cannot obtain a lock on data directory" in result.stderr:
                attempts += 1
                time.sleep(time_to_sleep)
            else:
                print("Error executing the command:", result.stderr, file=sys.stderr)
                sys.exit(result.returncode)

    raise Exception(f"Failed to start DeFi daemon after {max_attempts} attempts.")


def attempt_rpc_connection():
    attempts = 0
    while attempts < max_attempts:
        try:
            node = AuthServiceProxy(
                f"http://{rpcuser}:{rpcpassword}@{rpc_host}:{rpc_port}"
            )
            node.getblockcount()  # Test connection
            return node
        except (
            BrokenPipeError,
            ConnectionRefusedError,
            JSONRPCException,
            TimeoutError,
        ):
            time.sleep(time_to_sleep)
            attempts += 1

    raise Exception(
        f"Failed to establish RPC connection after {max_attempts} attempts."
    )


def wait_till_block_height(height):
    print(f"Syncing to height {height}")

    attempts = 0
    while attempts < max_attempts:
        try:
            node = attempt_rpc_connection()
            block_count = node.getblockcount()
            if block_count == height:
                node.stop()
                return
            attempts = 0
            time.sleep(time_to_sleep)
        except (
            BrokenPipeError,
            ConnectionRefusedError,
            JSONRPCException,
            TimeoutError,
        ):
            time.sleep(time_to_sleep)
            attempts += 1


def print_out_results():
    pattern = re.compile(
        r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z (\S+: .+destination= .+)"
    )

    matches = []

    with open(log_file, "r") as file:
        for line in file:
            match = pattern.search(line)
            if match:
                matches.append(match.group(1))

    for match in matches:
        print(match)


# Start the DeFiChain client
start_defid(last_null_swap)

# Wait till the block height is reached
wait_till_block_height(last_null_swap)

# Grep for null swaps
print_out_results()

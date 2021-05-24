# How to run a DeFiChain masternode

## Introduction

Setting up a masternode on DefiChain allows you to participate in the consensus protocol and receive staking awards. One thing to note is that in order to set up a masternode, you must have a minimum of 20,000 DFI.

> NOTE: This how-to expects some basic familiarity with the Linux terminal

## Masternode roles
There are two distinct roles: 'masternode owner' and 'masternode operator'. The owner holds the collateral and the primary activity (minting new coins, voting for anchors) is performed by operator. In general, a node can play both roles.

## For owners who operate their own masternodes
In this scenario, operator's address will be equal to the owner's (collateral) address.

### Step 1 - Download and extract node software

The first step is to download the binaries. Here are links to binaries for Windows, Linux and Macosx (Please download the latest release):

[Downloads](https://defichain.com/downloads/)

Following that we can extract the tar file by running (Replace 1.x.x with your version number):
```
tar -xvzf defichain-1.x.x-x86_64-pc-linux-gnu.tar.gz
```

### Step 2 - Copy binaries to user directory

We would like the node software to be stored in a consistent and easily accessible directory, so let's create one in our home folder. This can be done by running:

```
mkdir ~/.defi
```

Now copy the binaries by running:
```
cp ./defichain-1.x.x/bin/* ~/.defi
```

### Step 3 - Setting up crontab to keep our node running in the background

Now we may directly run our node by running `~/.defi/defid` , but this would not be very convenient, as we would have to keep the terminal session open the whole time, and run this command every time we restart our computer or SSH session. 

Instead, we'll use crontab to keep the process running. Run `crontab -e`, and select an editor (I recommend Nano if you are unsure which to pick), afterwards paste:

```
* * * * * pidof defid || ~/.defi/defid
```

into the file and hit `Ctrl-X` then enter to save the file.

### Step 4 - Setting up owner address with sufficient funds

In order to run a masternode, you must own at least 20,000 DFI. Let's set up an address with sufficient funds to use as an owner. Masternodes currently only support legacy addresses, so create a masternode address using:

```
~/.defi/defi-cli getnewaddress "<label>" legacy
```

where "label" is any label you would like to give the address.

Now in order to transfer the funds to this address, you may use: 

```
~/.defi/defi-cli sendtoaddress address
```

where address is the new owner address you have created.

### Step 5 - Register as a masternode on the network

In order to participate in the staking algorithm, you must broadcast to the network that you intend to participate, this can be done by running a command using the Defi CLI, the command is:

```
~/.defi/defi-cli createmasternode address
```

where `address` for both operator and collateral address should be the new legacy address you created. Please note that it costs 10 DFI to run this command.

### Step 6 - Configure the masternode and restart

We're almost done, in order for the master node to operate correctly, we must make a couple of configuration changes. The configuration file will sit in `~/.defi/defi.conf`. Let's make sure the configuration file exists by running `touch ~/.defi/defi.conf`. Now open this configuration file in the editor of your choice and add the following:

```
gen=1
spv=1
masternode_operator=OPERATOR_ADDRESS
```

Because we have decided to run owner and operator on the same address, just substitute the same legacy address you created for `OPERATOR_ADDRESS`. There is no need to specify owner address in this case.

Now the final step is to restart the node. Since we have crontab running, we just have to kill the process and crontab will start it again for us after one minute. We can do this final step by running `killall defi-init`. One minute later, we should have our masternode running and minting coins.

We can confirm the masternode is running by running:

```
~/.defi/defi-cli listmasternodes
```

look for your masternode address in the list of masternodes to confirm that you have successfully set everything up.

You may run the command `getmasternodeblocks OPERATOR_ADDRESS` to see how many blocks your masternode has minted so far.

## For owners who would like to delegate the masternode duties to another node
In this scenario, operator's address will be different to the owner's (collateral) address.

### Step 1-4 - Same as above

Perform steps 1-4 the same as the above section for operating your own masternodes. If you are also setting up the operator node yourself, then repeat steps 1-4 again for the operator node on a different machine.

### Step 5 - Register as a masternode on the network

In order to participate in the staking algorithm, you must broadcast to the network that you intend to participate, this can be done by running a command using the Defi CLI, the command is:

```
~/.defi/defi-cli createmasternode OWNER_ADDRESS OPERATOR_ADDRESS
``` 

where `OWNER_ADDRESS` is the address for the collateral/owner node and `OPERATOR_ADDRESS` is the address for the operator. Please note that it costs 10 DFI to run this command.

There is nothing to do after this, you may simply ensure that your masternode operator is correctly running the masternode on their side by passing them the operator address.

We can confirm the masternode is running by running: 

```
~/.defi/defi-cli listmasternodes
```

Look for your masternode address in the list of masternodes to confirm that you have successfully set everything up.

You may run the command `getmasternodeblocks OPERATOR_ADDRESS` to see how many blocks your masternode has minted so far.

## Creating via the DeFiChain Desktop Wallet

It's easy to set up your masternode through the DeFiChain Desktop Wallet, simply browse to the Masternodes tab and click on "Create +" in the upper right corner of the screen. The process is automated and seamless.

<img width="1020" alt="Masternode1" src="https://user-images.githubusercontent.com/3271586/112108417-2472a280-8beb-11eb-91f1-896904d46a85.png">

## Running multiple masternodes on the same machine

If you would like to run multiple masternodes on the same machine, you simply need to specify multiple `masternode_operator` entries in your `defi.conf`:

```
gen=1
spv=1
masternode_operator=OPERATOR_ADDRESS_1
masternode_operator=OPERATOR_ADDRESS_2
masternode_operator=OPERATOR_ADDRESS_3
```

On the next run, the node will begin minting for all the specified masternodes. There is no hard limit to how many masternodes a single machine can run.

## Resigning masternodes

If you decide to resign your masternode, you may run 

```
~/.defi/defi-cli resignmasternode
```
## Masternode states
Sending `createmasternode` (or 'resignmasternode') transaction doesn't mean that it acts immediately after submitting to the blockchain. There are special delays for each state.

Masternodes can exist in these states:
```
        PRE_ENABLED,
        ENABLED,
        PRE_RESIGNED,
        RESIGNED,
        PRE_BANNED,
        BANNED
```
- `PRE_ENABLED` - masternode was created, but waits for enough blocks after creation to get activated.
- `ENABLED` - masternode is in fully operable state, can mint blocks and sign anchors
- `PRE_RESIGNED` - masternode is still operable, but have received a 'resign' transaction and will wait for a special delay to get resigned
- `RESIGNED` - masternode resigned, collateral unlocked and is available to be reclaimed
- `PRE_BANNED` - masternode was caught as a 'criminal' (signing two blocks from parallel forks on close heights and we got special proofing tx on chain) but still operable (waiting, as in the case of PRE_RESIGNED)
- `BANNED` - masternode deactivated, collateral unlocked and can be reclaimed (same as RESIGNED, but forced through deactivation)

# How to run a Defichain masternode on Linux/Mac OS

## Introduction

Setting up a masternode on Defichain allows you to participate in the consensus protocol and receive staking awards. One thing to note is that in order to set up a masternode, you must have a minimum of 1 million DFI tokens.

> NOTE: This how-to expects some basic familiarity with the Linux terminal

## Masternode roles
There are two distinct roles: 'masternode owner' and 'masternode operator'. The owner holds the collateral and the primary activity (minting new coins, voting for anchors) is performed by operator. In general, a node can play both roles.

## For owners who operate their own masternodes
In this scenario, operator's address will be equal to the owner's (collateral) address.

### Step 1 - Download and extract node software

The first step is to download the binaries. Here are links to binaries for Linux and Macosx:

[Linux v1.0.0-rc1](https://github.com/DeFiCh/ain/releases/download/v1.0.0-rc1/defichain-1.0.0-rc1-x86_64-pc-linux-gnu.tar.gz)
[Mac OSX v1.0.0-rc1](https://github.com/DeFiCh/ain/releases/download/v1.0.0-rc1/defichain-1.0.0-rc1-x86_64-apple-darwin11.tar.gz)

We can download this on Linux using the command:

```
wget https://github.com/DeFiCh/ain/releases/download/v1.0.0-rc1/defichain-1.0.0-rc1-x86_64-pc-linux-gnu.tar.gz
```

Following that we can extract the tar file by running:
```
tar -xvzf defichain-1.0.0-rc1-x86_64-pc-linux-gnu.tar.gz
```

### Step 2 - Copy binaries to user directory

We would like the node software to be stored in a consistent and easily accessible directory, so let's create one in our home folder. This can be done by running:

```
mkdir ~/.defi
```

Now copy the binaries by running:
```
cp ./defichain-1.0.0-beta4/bin/* ~/.defi
```

### Step 3 - Setting up crontab to keep our node running in the background

Now we may directly run our node by running `~/.defi/defid` , but this would not be very convenient, as we would have to keep the terminal session open the whole time, and run this command every time we restart our computer or SSH session. 

Instead, we'll use crontab to keep the process running. Run `crontab -e`, and select an editor (I recommend Nano if you are unsure which to pick), afterwards paste:

```
* * * * * pidof defid || ~/.defi/defid
```

into the file and hit `Ctrl-X` then enter to save the file.

### Step 4 - Setting up owner address with sufficient funds

In order to run a masternode, you must own atleast 1 million DFI tokens. Let's set up an address with sufficient funds to use as an owner. Masternodes currently only support legacy addresses, so create a masternode address using:

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
defi-cli createmasternode "[]" "{\"operatorAuthAddress\":\"address\",\"collateralAddress\":\"address\"}"
```

where "address" for both operator and collateral address should be the new legacy address you created.

### Step 6 - Configure the masternode and restart

We're almost done, in order for the master node to operate correctly, we must make a couple of configuration changes. The configuration file will sit in `~/.defi/defi.conf`. Let's make sure the configuration file exists by running `touch ~/.defi/defi.conf`. Now open this configuration file in the editor of your choice and add the following:

```
gen=1
masternode_operator=OPERATOR_ADDRESS
masternode_owner=OWNER_ADDRESS
```

Because we have decided to run owner and operator on the same address, just substitue the same legacy address you created for `OPERATOR_ADDRESS` and `OWNER_ADDRESS`.

Now the final step is to restart the node. Since we have crontab running, we just have to kill the process and crontab will start it again for us after one minute. We can do this final step by running `killall defi-init`. One minute later, we should have our masternode running and minting coins.

We can confirm the masternode is running by running:

```
~/.defi/defi-cli listmasternodes
```

look for your masternode address in the list of masternodes to confirm that you have successfully set everything up.

## For owners who would like to delegate the masternode duties to another node
In this scenario, operator's address will be different to the owner's (collateral) address.

### Step 1-4 - Same as above

Perform steps 1-4 the same as the above section for operating your own masternodes. If you are also setting up the operator node yourself, then repeat steps 1-4 again for the operator node on a different machine.

### Step 5 - Register as a masternode on the network

In order to participate in the staking algorithm, you must broadcast to the network that you intend to participate, this can be done by running a command using the Defi CLI, the command is:

```
defi-cli createmasternode "[]" "{\"operatorAuthAddress\":\"OPERATOR_ADDRESS\",\"collateralAddress\":\"OWNER_ADDRESS\"}"
``` 

where `OPERATOR_ADDRESS` is the address for the operator and `OWNER_ADDRESS` is the address for the collateral/owner node.

### Step 6 - Configure the masternode and restart

We're almost done, in order for the master node to operate correctly, we must make a couple of configuration changes. The configuration file will sit in `~/.defi/defi.conf`. Let's make sure the configuration file exists by running `touch ~/.defi/defi.conf`. Now open this configuration file on the owner node with editor of your choice and add the following:

```
masternode_owner=OWNER_ADDRESS
```

In this case `OWNER_ADDRESS` should be the address of the owner node you have set up. If you are also setting up the operator node yourself, make sure the configuration file exists and open this file on the operator machine, this time add the following:

```
gen=1
masternode_operator=OPERATOR_ADDRESS
```

Now the final step is to restart the node. Since we have crontab running, we just have to kill the process and crontab will start it again for us after one minute. We can do this final step by running `killall defi-init`. Perform this step both on the owner node and the operator node if you have control of it. One minute later, we should have our masternode running and minting coins.

We can confirm the masternode is running by running: 

```
~/.defi/defi-cli listmasternodes
```

Look for your masternode address in the list of masternodes to confirm that you have successfully set everything up.

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
- `PRE_BANNED` - masternode was caught as a 'criminal' (signing two blocks from parrallel forks on close heights and we got special proofing tx on chain) but still operable (waiting, as in the case of PRE_RESIGNED)
- `BANNED` - masternode deactivated, collateral unlocked and can be reclaimed (same as RESIGNED, but forced through deactivation)
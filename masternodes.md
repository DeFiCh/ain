Masternodes short manual
-----------------
To start minting, user should become a "masternode" by sending special transaction, which locks collateral and burns an announcement fee. After got banned or resigning, announcement fee isn't refunded (only masternode collateral is refunded).

#### Masternode roles
There are two distinct roles: 'masternode owner' and 'masternode operator'. The only ability for owner is to resign masternode (and reclaiming collateral after that). Owner holds the collateral.
The primary activity (minting new coins, voting for anchors) is performed by operator. In general, node hoster can play both.

#### For owners who operate their masternodes
In this case, operator's address is equal to owner's (collateral) address.
1. Announce masternode with your collateral address. You should keep control of that address (have a private key) or you'll loose your collateral. THE OPERATION WILL BURN ANNOUNCEMENT FEE! Don't do it if you're not sure that the address is correct.
```
defi-cli createmasternode \{\"collateralAddress\":\"YOUR_ADDRESS\"\}
```
The transaction will be funded automatically by any accessible coins in your wallet (the same way as 'fundrawtransaction' acts). Or you can specify custom UTXOs for that (run ```defi-cli help createmasternode```).
The result is the transaction hash being created that acts as masternode's ID. When this transaction gets into mempool (and then, in chain), your collateral will be locked until resigning or ban. Don't be afraid if you can't see it in UTXO lists/wallet - it wasn't lost, just hidden.
2. Place your addresses in config file:
```
masternode_owner=YOUR_ADDRESS
masternode_operator=YOUR_ADDRESS
```
3. Restart the node. After tx got into blockchain, you can see the result of masternode's creation by issuing
```
defi-cli listmasternodes
```
4. If you decide to resign your masternode, issue
```
defi-cli resignmasternode
```
Important: you should keep small amount of coins on a collateral address in different from collateral's UTXO due to this transaction will be authorized by matching collateral (owner) address and should be funded (at least by one TxOut) from that UTXO! Funding will be performed automatically (as in 'createmasternode') or you can specify custom UTXO (as in case with 'createmasternode', see rpc's help).

#### For owners who outsource operation
The same as in previous scenario, but the 'operator' should provide you his address before masternode creation:
1. Announce masternode as before, but specify both: operator's address and YOUR collateral address.
```
defi-cli createmasternode \{\"operatorAuthAddress\":\"OPERATOR_ADDRESS\",\"collateralAddress\":\"YOUR_ADDRESS\"\}
```
2. Operator places ```masternode_operator=OPERATOR_ADDRESS```
in his config file, and you place ```masternode_owner=YOUR_ADDRESS``` in your own.
3. Operator runs your masternode. Owner has no need to keep his node running until he decide to resign it.

#### Masternode's states and delays
Sending `createmasternode` (or 'resignmasternode') transaction doesn't mean that it acts immidiately after getting in blockchain. There are special delays for each state. All delays will be adjusted before final network deploy.
Masternode can exists in those states:
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
- `PRE_RESIGNED` - masternode is still operable, but got a 'resign' transaction and waits for special delay to get resigned
- `RESIGNED` - masternode resigned, collateral unlocked and can be reclaimed
- `PRE_BANNED` - masternode was caught as 'criminal' (signing two blocks from parrallel forks on close heights and we got special proofing tx on chain) but still operable (waiting, as in the case of PRE_RESIGNED)
- `BANNED` - masternode deactivated, collateral unlocked and can be reclaimed (same as RESIGNED, but by another reason of deactivation)


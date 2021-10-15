defi-cli -regtest --rpcuser=test --rpcpassword=test stop
rm -rf /Users/johnnyb/Library/Application\ Support/DeFi/regtest
defid -regtest -gen=0 --rpcuser=test --rpcpassword=test &
sleep 5
sh /Users/johnnyb/DEFICHAIN/importkey.sh
defi-cli -regtest --rpcuser=test --rpcpassword=test stop
sleep 2
defid -regtest -gen=1 --rpcuser=test --rpcpassword=test >/dev/null &
sleep 2
cargo test -- --nocapture
defi-cli -regtest --rpcuser=test --rpcpassword=test stop



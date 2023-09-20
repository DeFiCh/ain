from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.evm_key_pair import EvmKeyPair
from test_framework.evm_contract import EVMContract
class StateRelayerTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def state_relayer(self):
        abi, bytecode, _ = EVMContract.from_file("StateRelayer.sol", "StateRelayer").compile()
        compiled = self.node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract = self.node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

        tx = self.contract.functions.initialize(self.evm_key_pair.address, self.evm_key_pair.address).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        print(self.node.w3.to_json(self.node.w3.eth.get_transaction(hash)))
        print(self.node.w3.to_json(self.node.w3.eth.get_transaction_receipt(hash)))

        # tx = self.contract.functions.updateVaultGeneralInformation([123,4,5,6,8])
        tx = self.contract.functions.updateVaultGeneralInformation({
            "noOfVaultsNoDecimals": 123,
            "totalLoanValue": 456,
            "totalCollateralValue": 789,
            "totalCollateralizationRatio": 0,
            "activeAuctionsNoDecimals": 10
        }).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )

        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        print(self.node.w3.to_json(self.node.w3.eth.get_transaction(hash)))
        print(self.node.w3.to_json(self.node.w3.eth.get_transaction_receipt(hash)))

    def setup(self):
        self.node = self.nodes[0]
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = self.nodes[0].getnewaddress("", "erc55")
        self.to_address = self.nodes[0].getnewaddress("", "erc55")
        self.evm_key_pair = EvmKeyPair.from_node(self.node)

        # Generate chain
        self.nodes[0].generate(105)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(1)
    def run_test(self):
        self.setup()

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)

        self.state_relayer()


if __name__ == "__main__":
    StateRelayerTest().main()

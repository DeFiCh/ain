import os
from typing import List, Dict

from solcx import compile_standard, install_solc


class EVMContract:
    # Solidity compiler version
    _solc_version = "0.8.10"
    _path_prefix = "../contracts"

    def __init__(self, code: str, file_name: str, contract_name: str):
        self.code = code
        self.file_name = file_name
        self.contract_name = contract_name

    @staticmethod
    def from_file(file_name: str, contract_name: str):
        with open(f"{os.path.dirname(__file__)}/{EVMContract._path_prefix}/{file_name}", "r") as file:
            return EVMContract(file.read(), file_name, contract_name)

    def compile(self) -> (List[Dict], str):
        install_solc(self._solc_version)
        compiled_sol = compile_standard(
            {
                "language": "Solidity",
                "sources": {self.file_name: {"content": self.code}},
                "settings": {
                    "outputSelection": {
                        "*": {
                            "*": [
                                "abi",
                                "evm.bytecode",
                            ]
                        }
                    }
                },
            },
            solc_version=self._solc_version,
        )

        data = compiled_sol["contracts"][self.file_name][self.contract_name]
        abi = data["abi"]
        bytecode = data["evm"]["bytecode"]["object"]

        return abi, bytecode

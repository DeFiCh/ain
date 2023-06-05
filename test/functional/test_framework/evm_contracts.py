import os
from typing import List, Dict

from solcx import compile_standard


class EVMContract:
    # Solidity compiler version
    _solc_version = "0.8.10"
    _path_prefix = "../contracts"

    def __init__(self, file_name: str, contract_name: str):
        self.file_name = file_name
        self.contract_name = contract_name

        with open(f"{os.path.dirname(__file__)}/{self._path_prefix}/{file_name}", "r") as file:
            self.code = file.read()

    def compile(self) -> (List[Dict], str):
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

import os
from typing import List, Dict

from solcx import compile_standard, compile_source


class EVMContract:
    path_prefix = "../contracts"

    def __init__(
        self,
        code: str,
        file_name: str,
        contract_name: str,
        compiler_version: str = "0.8.20",
        path_prefix: str = "../contracts",
    ):
        self.code = code
        self.file_name = file_name
        self.contract_name = contract_name
        self.compiler_version = compiler_version
        self.path_prefix = path_prefix

    @staticmethod
    def from_file(file_name: str, contract_name: str):
        with open(
            f"{os.path.dirname(__file__)}/{EVMContract.path_prefix}/{file_name}",
            "r",
            encoding="utf8",
        ) as file:
            return EVMContract(file.read(), file_name, contract_name)

    @staticmethod
    def from_str(sourceCode: str, contract_name: str):
        return EVMContract(sourceCode, "", contract_name)

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
            solc_version=self.compiler_version,
        )

        data = compiled_sol["contracts"][self.file_name][self.contract_name]
        abi = data["abi"]
        bytecode = data["evm"]["bytecode"]["object"]

        return abi, bytecode

    def compile_from_str(self) -> (List[Dict], str, str):
        compiled_output = compile_source(
            source=self.code,
            output_values=["abi", "bin", "bin-runtime"],
            solc_version="0.8.20",
        )
        abi = compiled_output[f"<stdin>:{self.contract_name}"]["abi"]
        bytecode = compiled_output[f"<stdin>:{self.contract_name}"]["bin"]
        runtimebytecode = compiled_output[f"<stdin>:{self.contract_name}"][
            "bin-runtime"
        ]

        return (abi, bytecode, runtimebytecode)

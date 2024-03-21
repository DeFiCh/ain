// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "./IDFIIntrinsicsV2.sol";

interface IDFIIntrinsicsRegistry {
    function get(uint256 _version) external view returns (address);
}

contract DFIIntrinsicsV2 is IDFIIntrinsicsV2 {
    uint256 private _version;
    address private registryAddress;

    function _getRegistry() internal view returns(IDFIIntrinsicsRegistry) {
        return IDFIIntrinsicsRegistry(registryAddress);
    }

    function _getDFIIntrinsicsV1() internal view returns(IDFIIntrinsicsV1) {
        address v1Address = _getRegistry().get(0);
        return IDFIIntrinsicsV1(v1Address);
    }

    function version() public view override returns (uint256) {
        return _version;
    }

    function evmBlockCount() public view override returns (uint256) {
        return _getDFIIntrinsicsV1().evmBlockCount();
    }

    function dvmBlockCount() public view override returns (uint256) {
        return _getDFIIntrinsicsV1().dvmBlockCount();
    }

    function migrateTokens(address tokenContract, uint256 amount) public {
        migrateTokensOnAddress(msg.sender, tokenContract, amount);
    }

    function migrateTokensOnAddress(address sender, address tokenContract, uint256 amount) private {
        address precompileAddress = address(0x0a);
        bytes memory inputData = abi.encode(sender, tokenContract, amount);
        bytes memory outputData = "";
        bool success;

        assembly {
            success := call(
                gas(),
                precompileAddress,
                0,
                add(inputData, 32),
                mload(inputData),
                add(outputData, 32),
                mload(outputData)
            )
        }
        require(success, "Precompile call failed");
    }
}

// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IDFIIntrinsicsV1 { 
    function version() external view returns (uint256);
    function evmBlockCount() external view returns(uint256);
    function dvmBlockCount() external view returns(uint256);
}

interface IDFIIntrinsicsRegistry {
    function getAddressForVersion(uint256 _version) external view returns (address);
}


contract ExampleDFIIntrinsicsV1Integration {
    address private _DFIIntrinsicsRegistry;

    constructor(address _registryAddress) {
        _DFIIntrinsicsRegistry = _registryAddress;
    }

    function _getDFIIntrinsicsV1() internal view returns(address) {
        return IDFIIntrinsicsRegistry(_DFIIntrinsicsRegistry).getAddressForVersion(0);
    }
    
    function getVersion() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).version();
    }

    function getEvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).evmBlockCount();
    }

    function getDvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).dvmBlockCount();
    }
}

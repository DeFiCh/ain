// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface IDFIIntrinsicsV1 { 
    function version() external view returns (uint256);
    function evmBlockCount() external view returns(uint256);
    function dvmBlockCount() external view returns(uint256);
}

interface IDFIIntrinsicsRegistry {
    function get(uint256 _version) external view returns (address);
}


contract ExampleDFIIntrinsicsV1Integration {
    IDFIIntrinsicsRegistry private _registry;

    constructor(address _registryAddress) {
        _registry = IDFIIntrinsicsRegistry(_registryAddress);
    }

    function _getDFIIntrinsicsV1() internal view returns(IDFIIntrinsicsV1) {
        return IDFIIntrinsicsV1(_registry.get(0));
    }
    
    function getVersion() external view returns (uint256) {
        return _getDFIIntrinsicsV1().version();
    }

    function getEvmBlockCount() external view returns (uint256) {
        return _getDFIIntrinsicsV1().evmBlockCount();
    }

    function getDvmBlockCount() external view returns (uint256) {
        return IDFIIntrinsicsV1(_getDFIIntrinsicsV1()).dvmBlockCount();
    }
}

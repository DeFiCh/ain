pragma solidity ^0.8.0;

contract DFIIntrinsicsRegistry {
    mapping(uint256 => address) private _versionToAddress;

    function getAddressForVersion(string calldata _version) public view returns (address) {
        return _versionToAddress[_version];
    }
}

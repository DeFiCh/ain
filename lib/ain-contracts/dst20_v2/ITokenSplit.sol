// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

interface ITokenSplit {
    event SplitResult(
        address indexed newTokenContractAddress,
        uint256 newAmount
    );

    function split(uint256 amount) external returns (address, uint256);
}

// SPDX-License-Identifier: MIT

pragma solidity >=0.8.2 <0.9.0;

/**
 * @title TransferDomain
 */
contract TransferDomain {
    event Transfer(address indexed from, address indexed to, uint256 amount);
    event NativeAddress(string nativeAddress);

    function transfer(
        address from,
        address payable to,
        uint256 amount,
        string memory nativeAddress
    ) external payable {
        if (to != address(this)) {
            require(
                address(this).balance >= amount,
                "Insufficient contract balance"
            );
            to.transfer(amount);
        }

        emit Transfer(from, to, amount / 1 ether);
        emit NativeAddress(nativeAddress);
    }
}

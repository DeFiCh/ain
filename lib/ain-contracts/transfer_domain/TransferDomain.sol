// SPDX-License-Identifier: MIT

pragma solidity >=0.8.2 <0.9.0;

/**
 * @title TransferDomain
 */
contract TransferDomain {
    event TransferFrom(
        address indexed from,
        address indexed to,
        uint256 amount
    );

    function transfer(
        address from,
        address payable to,
        uint256 amount
    ) external {
        if (to != address(this)) {
            require(
                address(this).balance >= amount,
                "Insufficient contract balance"
            );
            to.transfer(amount);
        }

        emit TransferFrom(from, to, amount);
    }
}

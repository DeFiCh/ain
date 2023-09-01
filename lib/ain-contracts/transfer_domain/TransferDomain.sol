// SPDX-License-Identifier: MIT

pragma solidity >=0.8.2 <0.9.0;

/**
 * @title TransferDomain
 */
contract TransferDomain {
    event Transfer(address indexed from, address indexed to, uint256 amount);

    function transfer(
        address from,
        address payable to // uint256 amount
    ) external payable {
        if (to != address(this)) {
            require(
                address(this).balance >= msg.value,
                "Insufficient contract balance"
            );
            to.transfer(msg.value);
        }

        emit Transfer(from, to, msg.value);
    }
}

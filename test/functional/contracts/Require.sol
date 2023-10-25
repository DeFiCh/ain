// SPDX-License-Identifier: GPL-3.0

pragma solidity >=0.7.0 <0.9.0;

contract Require {

  function gt0(int value) pure public {
    require(value > 0, "Value must be greater than 0");
  }
}

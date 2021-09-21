#ifndef DEFI_RUST_EVM_OPCODES_H
#define DEFI_RUST_EVM_OPCODES_H

enum Opcodes
{
	STOP = 0x00,		        ///< halts execution
	ADD,				            ///< addition operation
	MUL,				            ///< multiplication operation
	SUB,				            ///< subtraction operation
	DIV,				            ///< integer division operation
	SDIV,				            ///< signed integer division operation
	MOD,				            ///< modulo remainder operation
	SMOD,				            ///< signed modulo remainder operation
	ADDMOD,				          ///< unsigned modular addition
	MULMOD,				          ///< unsigned modular multiplication
	EXP,				            ///< exponential operation
	SIGNEXTEND,			        ///< extend length of signed integer

	LT = 0x10,			        ///< less-than comparison
	GT,					            ///< greater-than comparison
	SLT,				            ///< signed less-than comparison
	SGT,				            ///< signed greater-than comparison
	EQ,					            ///< equality comparison
	ISZERO,			            ///< simple not operator
	AND,				            ///< bitwise AND operation
	OR,					            ///< bitwise OR operation
	XOR,				            ///< bitwise XOR operation
	NOT,				            ///< bitwise NOT operation
	BYTE,				            ///< retrieve single byte from word
	SHL,				            ///< bitwise SHL operation
	SHR,				            ///< bitwise SHR operation
	SAR,				            ///< bitwise SAR operation

	KECCAK256 = 0x20,	      ///< compute KECCAK-256 hash

	ADDRESS = 0x30,		      ///< get address of currently executing account
	BALANCE,			          ///< get balance of the given account
	ORIGIN,				          ///< get execution origination address
	CALLER,				          ///< get caller address
	CALLVALUE,		  	      ///< get deposited value by the instruction/transaction responsible for this execution
	CALLDATALOAD,	  	      ///< get input data of current environment
	CALLDATASIZE,	  	      ///< get size of input data in current environment
	CALLDATACOPY,	  	      ///< copy input data in current environment to memory
	CODESIZE,			          ///< get size of code running in current environment
	CODECOPY,			          ///< copy code running in current environment to memory
	GASPRICE,			          ///< get price of gas in current environment
	EXTCODESIZE,	  	      ///< get external code size (from another contract)
	EXTCODECOPY,	  	      ///< copy external code (from another contract)
	RETURNDATASIZE = 0x3d,	///< get size of return data buffer
	RETURNDATACOPY = 0x3e,	///< copy return data in current environment to memory
	EXTCODEHASH = 0x3f,	    ///< get external code hash (from another contract)

	BLOCKHASH = 0x40,	      ///< get hash of most recent complete block
	COINBASE,			          ///< get the block's coinbase address
	TIMESTAMP,			        ///< get the block's timestamp
	NUMBER,				          ///< get the block's number
	DIFFICULTY,		         	///< get the block's difficulty
	GASLIMIT,			          ///< get the block's gas limit
	CHAINID,			          ///< get the config's chainid param
	SELFBALANCE,	         	///< get balance of the current account
	BASEFEE,                ///< get the block's basefee

	POP = 0x50,	          	///< remove item from stack
	MLOAD,			          	///< load word from memory
	MSTORE,			          	///< save word to memory
	MSTORE8,		          	///< save byte to memory
	SLOAD,			          	///< load word from storage
	SSTORE,			          	///< save word to storage
	JUMP,				            ///< alter the program counter
	JUMPI,			          	///< conditionally alter the program counter
	PC,					            ///< get the program counter
	MSIZE,			          	///< get the size of active memory
	GAS,				            ///< get the amount of available gas
	JUMPDEST,		          	///< set a potential jump destination

	PUSH1 = 0x60,		        ///< place 1 byte item on stack
	PUSH2,				          ///< place 2 byte item on stack
	PUSH3,				          ///< place 3 byte item on stack
	PUSH4,				          ///< place 4 byte item on stack
	PUSH5,				          ///< place 5 byte item on stack
	PUSH6,				          ///< place 6 byte item on stack
	PUSH7,				          ///< place 7 byte item on stack
	PUSH8,				          ///< place 8 byte item on stack
	PUSH9,				          ///< place 9 byte item on stack
	PUSH10,				          ///< place 10 byte item on stack
	PUSH11,				          ///< place 11 byte item on stack
	PUSH12,				          ///< place 12 byte item on stack
	PUSH13,				          ///< place 13 byte item on stack
	PUSH14,				          ///< place 14 byte item on stack
	PUSH15,				          ///< place 15 byte item on stack
	PUSH16,				          ///< place 16 byte item on stack
	PUSH17,				          ///< place 17 byte item on stack
	PUSH18,				          ///< place 18 byte item on stack
	PUSH19,				          ///< place 19 byte item on stack
	PUSH20,				          ///< place 20 byte item on stack
	PUSH21,				          ///< place 21 byte item on stack
	PUSH22,				          ///< place 22 byte item on stack
	PUSH23,				          ///< place 23 byte item on stack
	PUSH24,				          ///< place 24 byte item on stack
	PUSH25,				          ///< place 25 byte item on stack
	PUSH26,				          ///< place 26 byte item on stack
	PUSH27,				          ///< place 27 byte item on stack
	PUSH28,				          ///< place 28 byte item on stack
	PUSH29,				          ///< place 29 byte item on stack
	PUSH30,				          ///< place 30 byte item on stack
	PUSH31,				          ///< place 31 byte item on stack
	PUSH32,				          ///< place 32 byte item on stack

	DUP1 = 0x80,		        ///< copies the highest item in the stack to the top of the stack
	DUP2,				            ///< copies the second highest item in the stack to the top of the stack
	DUP3,				            ///< copies the third highest item in the stack to the top of the stack
	DUP4,				            ///< copies the 4th highest item in the stack to the top of the stack
	DUP5,				            ///< copies the 5th highest item in the stack to the top of the stack
	DUP6,				            ///< copies the 6th highest item in the stack to the top of the stack
	DUP7,				            ///< copies the 7th highest item in the stack to the top of the stack
	DUP8,				            ///< copies the 8th highest item in the stack to the top of the stack
	DUP9,				            ///< copies the 9th highest item in the stack to the top of the stack
	DUP10,				          ///< copies the 10th highest item in the stack to the top of the stack
	DUP11,				          ///< copies the 11th highest item in the stack to the top of the stack
	DUP12,				          ///< copies the 12th highest item in the stack to the top of the stack
	DUP13,				          ///< copies the 13th highest item in the stack to the top of the stack
	DUP14,				          ///< copies the 14th highest item in the stack to the top of the stack
	DUP15,				          ///< copies the 15th highest item in the stack to the top of the stack
	DUP16,				          ///< copies the 16th highest item in the stack to the top of the stack

	SWAP1 = 0x90,		        ///< swaps the highest and second highest value on the stack
	SWAP2,				          ///< swaps the highest and third highest value on the stack
	SWAP3,				          ///< swaps the highest and 4th highest value on the stack
	SWAP4,				          ///< swaps the highest and 5th highest value on the stack
	SWAP5,				          ///< swaps the highest and 6th highest value on the stack
	SWAP6,				          ///< swaps the highest and 7th highest value on the stack
	SWAP7,				          ///< swaps the highest and 8th highest value on the stack
	SWAP8,				          ///< swaps the highest and 9th highest value on the stack
	SWAP9,				          ///< swaps the highest and 10th highest value on the stack
	SWAP10,				          ///< swaps the highest and 11th highest value on the stack
	SWAP11,				          ///< swaps the highest and 12th highest value on the stack
	SWAP12,				          ///< swaps the highest and 13th highest value on the stack
	SWAP13,				          ///< swaps the highest and 14th highest value on the stack
	SWAP14,				          ///< swaps the highest and 15th highest value on the stack
	SWAP15,				          ///< swaps the highest and 16th highest value on the stack
	SWAP16,				          ///< swaps the highest and 17th highest value on the stack

	LOG0 = 0xa0,		        ///< Makes a log entry; no topics.
	LOG1,				            ///< Makes a log entry; 1 topic.
	LOG2,				            ///< Makes a log entry; 2 topics.
	LOG3,				            ///< Makes a log entry; 3 topics.
	LOG4,				            ///< Makes a log entry; 4 topics.

	CREATE = 0xf0,		      ///< create a new account with associated code
	CALL,				            ///< message-call into an account
	CALLCODE,			          ///< message-call with another account's code only
	RETURN,				          ///< halt execution returning output data
	DELEGATECALL,	         	///< like CALLCODE but keeps caller's value and sender
	CREATE2 = 0xf5,		      ///< create new account with associated code at address `sha3(0xff + sender + salt + init code) % 2**160`
	STATICCALL = 0xfa,     	///< like CALL but disallow state modifications

	REVERT = 0xfd,		      ///< halt execution, revert state and return output data
	INVALID = 0xfe,		      ///< invalid instruction for expressing runtime errors (e.g., division-by-zero)
	SELFDESTRUCT = 0xff	    ///< halt execution and register account for later deletion
};

#endif // DEFI_RUST_EVM_OPCODES_H

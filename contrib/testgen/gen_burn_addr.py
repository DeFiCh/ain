# Copyright (c) 2012-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
'''
Defi base58 encoding and decoding.

Based on https://bitcointalk.org/index.php?topic=1026.0 (public domain)
'''

import hashlib
import sys

# for compatibility with following code...
class SHA256:
    new = hashlib.sha256

if str != bytes:
    # Python 3.x
    def ord(c):
        return c
    def chr(n):
        return bytes( (n,) )

__b58chars = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
__b58base = len(__b58chars)
b58chars = __b58chars

__unusedChars = '0OIl'

def b58encode(v):
    """ encode v, which is a string of bytes, to base58.
    """
    long_value = 0
    for (i, c) in enumerate(v[::-1]):
        if isinstance(c, str):
            c = ord(c)
        long_value += (256**i) * c

    result = ''
    while long_value >= __b58base:
        div, mod = divmod(long_value, __b58base)
        result = __b58chars[mod] + result
        long_value = div
    result = __b58chars[long_value] + result

    # Defi does a little leading-zero-compression:
    # leading 0-bytes in the input become leading-1s
    nPad = 0
    for c in v:
        if c == 0:
            nPad += 1
        else:
            break

    return (__b58chars[0]*nPad) + result

def b58decode(v, length = None):
    """ decode v into a string of len bytes
    """
    long_value = 0
    for i, c in enumerate(v[::-1]):
        pos = __b58chars.find(c)
        assert pos != -1
        long_value += pos * (__b58base**i)

    result = bytes()
    while long_value >= 256:
        div, mod = divmod(long_value, 256)
        result = chr(mod) + result
        long_value = div
    result = chr(long_value) + result

    nPad = 0
    for c in v:
        if c == __b58chars[0]:
            nPad += 1
            continue
        break

    result = bytes(nPad) + result
    if length is not None and len(result) != length:
        return None

    return result

def checksum(v):
    """Return 32-bit checksum based on SHA256"""
    return SHA256.new(SHA256.new(v).digest()).digest()[0:4]

def b58encode_chk(v):
    """b58encode a string, with 32-bit checksum"""
    return b58encode(v + checksum(v))

def b58decode_chk(v):
    """decode a base58 string, check and remove checksum"""
    result = b58decode(v)
    if result is None:
        print("result is none")
        return None
    print("decode result: ", bytes(result).hex())
    checksumResult = checksum(result[:-4])
    if result[-4:] == checksumResult:
        return result[:-4]
    else:
        print(bytes(result[-4:]).hex(), "\n")
        print("check sum result: ", bytes(checksumResult).hex())
        return None

def get_bcaddress_version(strAddress):
    """ Returns None if strAddress is invalid.  Otherwise returns integer version of address. """
    addr = b58decode_chk(strAddress)
    if addr is None or len(addr)!=21:
        return None
    version = addr[0]
    return ord(version)

def print_usage():
    print('Usage of this script(Need python3):')
    print('python3 ' + sys.argv[0] + ' AddressStartString')
    print('Mainnet address start with string from 8F ~ 8d')
    print('Testnet address start with string from 73 ~ 7R')
    print('Regtest address start with string from mf ~ n4')
    print('The address start string cannot contain these characters: 0OIl')
    print('For example:')
    print('  python3 gen_burn_addr.py 8addressForBurn')
    print('  python3 gen_burn_addr.py 7AddressForBurn')

def check_start_range(fst2):
    if fst2 >= '73' and fst2 <= '7R':
        return True

    if fst2 >= '8F' and fst2 <= '8d':
        return True

    if fst2 >= 'mf' and fst2 <= 'n4':
        return True

    return False

if __name__ == '__main__':
    # Check our input parameters for this script
    if (len(sys.argv) < 2):
        print_usage()
        sys.exit(0)

    if (len(sys.argv) > 2):
        print("Too many input arguments!")
        print_usage()
        sys.exit(0)

    if sys.argv[1] == '-h' or sys.argv[1] == '--help':
        print_usage()
        sys.exit(0)

    startString = sys.argv[1]
    if (len(startString) > 28):
        print('Address start string is too long!')
        sys.exit(0)

    if not startString.isalnum():
        print('Address start string containts invalid characters!')
        sys.exit(0)

    if any((c in startString) for c in __unusedChars):
        print('Address start string cannot contain 0OIl')
        sys.exit(0)

    if (len(startString) < 2):
        print('The start string is too short')
        sys.exit(0)

    fst2 = startString[0:2]
    if not check_start_range(fst2):
        print('Address start is not correct!')
        print('Mainnet address start with string from 8F ~ 8d')
        print('Testnet address start with string from 73 ~ 7R')
        print('Regtest address start with string from mf ~ n4')
        sys.exit(0)

    anotherString = startString + "X" * (34 - len(startString))

    result = b58decode(anotherString)
    if result is None:
        print("result is none")
        exit(-1)

    checksumResult = checksum(result[:-4])

    mutableResult = bytearray(result)
    mutableResult[-4] = checksumResult[0]
    mutableResult[-3] = checksumResult[1]
    mutableResult[-2] = checksumResult[2]
    mutableResult[-1] = checksumResult[3]

    finalResult = b58encode(mutableResult)
    print("Generated address: ", finalResult)

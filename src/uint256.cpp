// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <uint256.h>

#include <util/strencodings.h>

#include <stdio.h>
#include <string.h>

template <unsigned int BITS>
base_blob<BITS>::base_blob(const std::vector<unsigned char>& vch)
{
    assert(vch.size() == sizeof(m_data));
    memcpy(m_data, vch.data(), sizeof(m_data));
}

template <unsigned int BITS>
std::string base_blob<BITS>::GetHex() const
{
    return HexStr(std::reverse_iterator<const uint8_t*>(m_data + sizeof(m_data)), std::reverse_iterator<const uint8_t*>(m_data));
}

template <unsigned int BITS>
void base_blob<BITS>::SetHex(const char* psz)
{
    memset(m_data, 0, sizeof(m_data));

    // skip leading spaces
    while (IsSpace(*psz))
        psz++;

    // skip 0x
    if (psz[0] == '0' && ToLower(psz[1]) == 'x')
        psz += 2;

    // hex string to uint
    size_t digits = 0;
    while (::HexDigit(psz[digits]) != -1)
        digits++;
    unsigned char* p1 = (unsigned char*)m_data;
    unsigned char* pend = p1 + WIDTH;
    while (digits > 0 && p1 < pend) {
        *p1 = ::HexDigit(psz[--digits]);
        if (digits > 0) {
            *p1 |= ((unsigned char)::HexDigit(psz[--digits]) << 4);
            p1++;
        }
    }
}

template <unsigned int BITS>
void base_blob<BITS>::SetHex(const std::string& str)
{
    SetHex(str.c_str());
}

template <unsigned int BITS>
std::string base_blob<BITS>::ToString() const
{
    return (GetHex());
}

// Explicit instantiations for base_blob<128>
template class base_blob<128>;

// Explicit instantiations for base_blob<160>
template class base_blob<160>;

// Explicit instantiations for base_blob<256>
template class base_blob<256>;

const uint256 uint256::ZERO(0);
const uint256 uint256::ONE(1);

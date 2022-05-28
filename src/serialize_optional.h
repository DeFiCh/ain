#ifndef BITCOIN_SERIALIZE_OPTIONAL_H
#define BITCOIN_SERIALIZE_OPTIONAL_H

#include <serialize.h>
#include <boost/optional.hpp>

template<typename Stream, typename T>
inline void Serialize(Stream& s, boost::optional<T> const & a)
{
    ::ser_writedata8(s, a ? 1 : 0);
    if (a) {
        ::Serialize(s, *a);
    }
}

template<typename Stream, typename T>
inline void Unserialize(Stream& s, boost::optional<T>& a)
{
    const auto exist = ::ser_readdata8(s);
    if (exist == 1) {
        T value;
        ::Unserialize(s, value);
        a = std::move(value);
    } else if (exist == 0) {
        a = {};
    } else {
        throw std::ios_base::failure("non-canonical optional<T>");
    }
}

#endif //BITCOIN_SERIALIZE_OPTIONAL_H

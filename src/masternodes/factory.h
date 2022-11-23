// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_FACTORY_H
#define DEFI_MASTERNODES_FACTORY_H

#include <map>
#include <string>

template <typename TBaseType>
class Factory {
   public:
    typedef TBaseType *(*Creator)();  // creator function

    Factory()  = delete;
    ~Factory() = delete;

    template <typename TType>
    static bool Registrate() {
        auto res = m_creators.insert(std::pair<std::string, Creator>(TType::TypeName(), &TType::Create));
        return res.second;
    }

    static TBaseType *Create(const std::string &name) {
        typename TCreators::const_iterator creator(m_creators.find(name));
        if (creator == m_creators.end()) {
            return {};
        }
        return creator->second();
    }

   private:
    typedef std::map<std::string, Creator> TCreators;
    static TCreators m_creators;

    // force instantiation of definition of static member, do not touch!
    template <TCreators &>
    struct dummy_ref {};
    static dummy_ref<m_creators> referrer;
};

template <typename TBaseType, typename TDerivedType>
class AutoRegistrator {
    struct exec_registrate {
        exec_registrate() {
            bool res = Factory<TBaseType>::template Registrate<TDerivedType>();
            assert(res);
        }
    };
    static exec_registrate register_object;

    // force instantiation of definition of static member, do not touch!
    template <exec_registrate &>
    struct dummy_ref {};
    static dummy_ref<register_object> referrer;
};

template <typename T>
typename Factory<T>::TCreators Factory<T>::m_creators;

template <typename TBaseType, typename TDerivedType>
typename AutoRegistrator<TBaseType, TDerivedType>::exec_registrate
    AutoRegistrator<TBaseType, TDerivedType>::register_object;

#endif  // DEFI_MASTERNODES_FACTORY_H

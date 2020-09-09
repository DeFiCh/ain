// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/gv.h>
#include <masternodes/govvariables/LP_DAILY_DFI_REWARD.h>
#include <masternodes/govvariables/LP_SPLITS.h>

const unsigned char CGovView::ByName::prefix = 'g';

template <typename T>
std::shared_ptr<typename Factory<T>::TCreators> Factory<T>::m_creators = {};


Res CGovView::SetVariable(GovVariable const & var)
{
    return WriteBy<ByName>(var.GetName(), var) ? Res::Ok() : Res::Err("can't write to DB");
}

std::shared_ptr<GovVariable> CGovView::GetVariable(std::string const & name) const
{
    auto var = GovVariable::Create(name);
    if (var) {
        /// @todo empty or NO variable??
        ReadBy<ByName>(std::string(var->GetName()), *var);
        return var;
    }
    return {};
}

/// @todo Registration works only from this compilation unit (gv.cpp). Research!
namespace
{

class Registrator
{
public:
    Registrator() {
        Factory<GovVariable>::Registrate<LP_SPLITS>();
        Factory<GovVariable>::Registrate<LP_DAILY_DFI_REWARD>();
    }
} registrator;

}




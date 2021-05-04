// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/gv.h>
#include <masternodes/govvariables/icx_dfibtc_poolpair.h>
#include <masternodes/govvariables/lp_daily_dfi_reward.h>
#include <masternodes/govvariables/lp_splits.h>

const unsigned char CGovView::ByName::prefix = 'g';

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



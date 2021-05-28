#include <masternodes/loan.h>
#include <rpc/util.h> /// AmountFromValue
#include <core_io.h> /// ValueFromAmount

const unsigned char CLoanView::LoanSetCollateralTokenCreationTx           ::prefix = 0x10;
const unsigned char CLoanView::LoanSetCollateralTokenKey                  ::prefix = 0x11;

std::unique_ptr<CLoanView::CLoanSetCollateralTokenImpl> CLoanView::GetLoanSetCollateralToken(uint256 const & txid) const
{
    auto collToken = ReadBy<LoanSetCollateralTokenCreationTx,CLoanSetCollateralTokenImpl>(txid);
    if (collToken)
        return MakeUnique<CLoanSetCollateralTokenImpl>(*collToken);
    return {};
}

Res CLoanView::LoanCreateSetCollateralToken(CLoanSetCollateralTokenImpl const & collToken)
{
    //this should not happen, but for sure
    if (GetLoanSetCollateralToken(collToken.creationTx))
        return Res::Err("setCollateralToken with creation tx %s already exists!", collToken.creationTx.GetHex());
    if (collToken.factor > COIN)
        return Res::Err("setCollateralToken factor must be lower or equal than 1!");
    if (collToken.factor < 0)
        return Res::Err("setCollateralToken factor must not be negative!");

    WriteBy<LoanSetCollateralTokenCreationTx>(collToken.creationTx, collToken);
    WriteBy<LoanSetCollateralTokenKey>(collToken.idToken, collToken.creationTx);

    return Res::Ok();
}

void CLoanView::ForEachLoanSetCollateralToken(std::function<bool (DCT_ID const &, uint256 const &)> callback, DCT_ID const & token)
{
    ForEach<LoanSetCollateralTokenKey, DCT_ID, uint256>(callback, token);
}

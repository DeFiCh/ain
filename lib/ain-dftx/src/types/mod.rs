pub mod account;
pub mod balance;
pub mod common;
pub mod evmtx;
pub mod governance;
pub mod icxorderbook;
pub mod loans;
pub mod masternode;
pub mod oracles;
pub mod pool;
pub mod price;
pub mod token;
pub mod vault;

pub use ain_macros::ConsensusEncoding;
use bitcoin::{
    consensus::{Decodable, Encodable},
    io,
};

use self::{
    account::*, common::RawBytes, evmtx::*, governance::*, icxorderbook::*, loans::*,
    masternode::*, oracles::*, pool::*, token::*, vault::*,
};
use crate::custom_tx::CustomTxType;

#[derive(Debug, PartialEq, Eq)]
pub enum DfTx {
    AccountToAccount(AccountToAccount),
    AccountToUtxos(AccountToUtxos),
    AnyAccountsToAccounts(AnyAccountsToAccounts),
    AppointOracle(AppointOracle),
    CloseVault(CloseVault),
    CompositeSwap(CompositeSwap),
    CreateCfp(CreateProposal),
    CreateMasternode(CreateMasternode),
    CreateVault(CreateVault),
    CreateVoc(CreateProposal),
    DepositToVault(DepositToVault),
    DestroyLoanScheme(DestroyLoanScheme),
    EvmTx(EvmTx),
    ICXClaimDFCHTLC(ICXClaimDFCHTLC),
    ICXCloseOffer(ICXCloseOffer),
    ICXCloseOrder(ICXCloseOrder),
    ICXCreateOrder(ICXCreateOrder),
    ICXMakeOffer(ICXMakeOffer),
    ICXSubmitDFCHTLC(ICXSubmitDFCHTLC),
    ICXSubmitEXTHTLC(ICXSubmitEXTHTLC),
    PaybackLoan(PaybackLoan),
    PaybackLoanV2(PaybackLoanV2),
    PlaceAuctionBid(PlaceAuctionBid),
    PoolAddLiquidity(PoolAddLiquidity),
    PoolCreatePair(PoolCreatePair),
    PoolRemoveLiquidity(PoolRemoveLiquidity),
    PoolSwap(PoolSwap),
    PoolUpdatePair(PoolUpdatePair),
    RemoveOracle(RemoveOracle),
    ResignMasternode(ResignMasternode),
    SetCollateralToken(SetCollateralToken),
    SetDefaultLoanScheme(SetDefaultLoanScheme),
    SetFutureSwap(SetFutureSwap),
    SetGovernance(SetGovernance),
    SetGovernanceHeight(SetGovernanceHeight),
    SetLoanScheme(SetLoanScheme),
    SetLoanToken(SetLoanToken),
    SetOracleData(SetOracleData),
    TakeLoan(TakeLoan),
    BurnToken(BurnToken),
    CreateToken(CreateToken),
    MintToken(MintToken),
    UpdateToken(UpdateToken),
    UpdateTokenAny(UpdateTokenAny),
    TransferDomain(TransferDomain),
    UpdateLoanToken(UpdateLoanToken),
    UpdateMasternode(UpdateMasternode),
    UpdateOracle(UpdateOracle),
    UpdateVault(UpdateVault),
    UtxosToAccount(UtxosToAccount),
    Vote(Vote),
    WithdrawFromVault(WithdrawFromVault),
    Reject,
    None,
}

impl DfTx {
    fn to_u8(&self) -> u8 {
        match self {
            DfTx::None => 0,
            DfTx::Reject => 1,
            DfTx::ICXCreateOrder(_) => b'1',
            DfTx::ICXMakeOffer(_) => b'2',
            DfTx::ICXSubmitDFCHTLC(_) => b'3',
            DfTx::ICXSubmitEXTHTLC(_) => b'4',
            DfTx::ICXClaimDFCHTLC(_) => b'5',
            DfTx::ICXCloseOrder(_) => b'6',
            DfTx::ICXCloseOffer(_) => b'7',
            DfTx::TransferDomain(_) => b'8',
            DfTx::EvmTx(_) => b'9',
            DfTx::AccountToAccount(_) => b'B',
            DfTx::CreateMasternode(_) => b'C',
            DfTx::DestroyLoanScheme(_) => b'D',
            DfTx::CreateVoc(_) => b'E',
            DfTx::BurnToken(_) => b'F',
            DfTx::SetGovernance(_) => b'G',
            DfTx::SetGovernanceHeight(_) => b'j',
            DfTx::PaybackLoan(_) => b'H',
            DfTx::PlaceAuctionBid(_) => b'I',
            DfTx::WithdrawFromVault(_) => b'J',
            DfTx::SetLoanScheme(_) => b'L',
            DfTx::MintToken(_) => b'M',
            DfTx::UpdateToken(_) => b'N',
            DfTx::Vote(_) => b'O',
            DfTx::SetFutureSwap(_) => b'Q',
            DfTx::ResignMasternode(_) => b'R',
            DfTx::DepositToVault(_) => b'S',
            DfTx::CreateToken(_) => b'T',
            DfTx::UtxosToAccount(_) => b'U',
            DfTx::CreateVault(_) => b'V',
            DfTx::TakeLoan(_) => b'X',
            DfTx::AnyAccountsToAccounts(_) => b'a',
            DfTx::AccountToUtxos(_) => b'b',
            DfTx::SetCollateralToken(_) => b'c',
            DfTx::SetDefaultLoanScheme(_) => b'd',
            DfTx::CloseVault(_) => b'e',
            DfTx::SetLoanToken(_) => b'g',
            DfTx::RemoveOracle(_) => b'h',
            DfTx::CompositeSwap(_) => b'i',
            DfTx::PaybackLoanV2(_) => b'k',
            DfTx::PoolAddLiquidity(_) => b'l',
            DfTx::UpdateMasternode(_) => b'm',
            DfTx::UpdateTokenAny(_) => b'n',
            DfTx::AppointOracle(_) => b'o',
            DfTx::PoolCreatePair(_) => b'p',
            DfTx::PoolRemoveLiquidity(_) => b'r',
            DfTx::PoolSwap(_) => b's',
            DfTx::UpdateOracle(_) => b't',
            DfTx::PoolUpdatePair(_) => b'u',
            DfTx::UpdateVault(_) => b'v',
            DfTx::UpdateLoanToken(_) => b'x',
            DfTx::SetOracleData(_) => b'y',
            DfTx::CreateCfp(_) => b'z',
        }
    }
}

const DFTX_MARKER: [u8; 4] = *b"DfTx";

impl Decodable for DfTx {
    fn consensus_decode<R: io::Read + ?Sized>(
        r: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        let signature = <[u8; 4]>::consensus_decode(r)?;
        if signature != DFTX_MARKER {
            return Err(bitcoin::consensus::encode::Error::ParseFailed(
                "Invalid marker",
            ));
        }

        let r#type = u8::consensus_decode(r)?;
        let message = match CustomTxType::from(r#type) {
            CustomTxType::AccountToAccount => {
                DfTx::AccountToAccount(AccountToAccount::consensus_decode(r)?)
            }
            CustomTxType::AccountToUtxos => {
                DfTx::AccountToUtxos(AccountToUtxos::consensus_decode(r)?)
            }
            CustomTxType::PoolAddLiquidity => {
                DfTx::PoolAddLiquidity(PoolAddLiquidity::consensus_decode(r)?)
            }
            CustomTxType::AnyAccountsToAccounts => {
                DfTx::AnyAccountsToAccounts(AnyAccountsToAccounts::consensus_decode(r)?)
            }
            CustomTxType::AppointOracle => DfTx::AppointOracle(AppointOracle::consensus_decode(r)?),
            CustomTxType::AuctionBid => {
                DfTx::PlaceAuctionBid(PlaceAuctionBid::consensus_decode(r)?)
            }
            CustomTxType::BurnToken => DfTx::BurnToken(BurnToken::consensus_decode(r)?),
            CustomTxType::CloseVault => DfTx::CloseVault(CloseVault::consensus_decode(r)?),
            CustomTxType::CreateCfp => DfTx::CreateCfp(CreateProposal::consensus_decode(r)?),
            CustomTxType::CreateMasternode => {
                DfTx::CreateMasternode(CreateMasternode::consensus_decode(r)?)
            }
            CustomTxType::CreatePoolPair => {
                DfTx::PoolCreatePair(PoolCreatePair::consensus_decode(r)?)
            }
            CustomTxType::CreateToken => DfTx::CreateToken(CreateToken::consensus_decode(r)?),
            CustomTxType::CreateVoc => DfTx::CreateVoc(CreateProposal::consensus_decode(r)?),
            CustomTxType::DefaultLoanScheme => {
                DfTx::SetDefaultLoanScheme(SetDefaultLoanScheme::consensus_decode(r)?)
            }
            CustomTxType::DepositToVault => {
                DfTx::DepositToVault(DepositToVault::consensus_decode(r)?)
            }
            CustomTxType::DestroyLoanScheme => {
                DfTx::DestroyLoanScheme(DestroyLoanScheme::consensus_decode(r)?)
            }
            CustomTxType::EvmTx => DfTx::EvmTx(EvmTx::consensus_decode(r)?),
            CustomTxType::ICXClaimDFCHTLC => {
                DfTx::ICXClaimDFCHTLC(ICXClaimDFCHTLC::consensus_decode(r)?)
            }
            CustomTxType::ICXCloseOffer => DfTx::ICXCloseOffer(ICXCloseOffer::consensus_decode(r)?),
            CustomTxType::ICXCloseOrder => DfTx::ICXCloseOrder(ICXCloseOrder::consensus_decode(r)?),
            CustomTxType::ICXCreateOrder => {
                DfTx::ICXCreateOrder(ICXCreateOrder::consensus_decode(r)?)
            }
            CustomTxType::ICXMakeOffer => DfTx::ICXMakeOffer(ICXMakeOffer::consensus_decode(r)?),
            CustomTxType::ICXSubmitDFCHTLC => {
                DfTx::ICXSubmitDFCHTLC(ICXSubmitDFCHTLC::consensus_decode(r)?)
            }
            CustomTxType::ICXSubmitEXTHTLC => {
                DfTx::ICXSubmitEXTHTLC(ICXSubmitEXTHTLC::consensus_decode(r)?)
            }
            CustomTxType::LoanScheme => DfTx::SetLoanScheme(SetLoanScheme::consensus_decode(r)?),
            CustomTxType::MintToken => DfTx::MintToken(MintToken::consensus_decode(r)?),
            CustomTxType::PaybackLoan => DfTx::PaybackLoan(PaybackLoan::consensus_decode(r)?),
            CustomTxType::PaybackLoanV2 => DfTx::PaybackLoanV2(PaybackLoanV2::consensus_decode(r)?),
            CustomTxType::PoolSwap => DfTx::PoolSwap(PoolSwap::consensus_decode(r)?),
            CustomTxType::PoolSwapV2 => DfTx::CompositeSwap(CompositeSwap::consensus_decode(r)?),
            CustomTxType::Reject => DfTx::AccountToUtxos(AccountToUtxos::consensus_decode(r)?),
            CustomTxType::RemoveOracle => DfTx::RemoveOracle(RemoveOracle::consensus_decode(r)?),
            CustomTxType::RemovePoolLiquidity => {
                DfTx::PoolRemoveLiquidity(PoolRemoveLiquidity::consensus_decode(r)?)
            }
            CustomTxType::ResignMasternode => {
                DfTx::ResignMasternode(ResignMasternode::consensus_decode(r)?)
            }
            CustomTxType::SetGovVariable => {
                DfTx::SetGovernance(SetGovernance::consensus_decode(r)?)
            }
            CustomTxType::SetGovVariableHeight => {
                DfTx::SetGovernanceHeight(SetGovernanceHeight::consensus_decode(r)?)
            }
            CustomTxType::SetLoanCollateralToken => {
                DfTx::SetCollateralToken(SetCollateralToken::consensus_decode(r)?)
            }
            CustomTxType::SetLoanToken => DfTx::SetLoanToken(SetLoanToken::consensus_decode(r)?),
            CustomTxType::SetOracleData => DfTx::SetOracleData(SetOracleData::consensus_decode(r)?),
            CustomTxType::TakeLoan => DfTx::TakeLoan(TakeLoan::consensus_decode(r)?),
            CustomTxType::TransferDomain => {
                DfTx::TransferDomain(TransferDomain::consensus_decode(r)?)
            }
            CustomTxType::UpdateLoanToken => {
                DfTx::UpdateLoanToken(UpdateLoanToken::consensus_decode(r)?)
            }
            CustomTxType::UpdateMasternode => {
                DfTx::UpdateMasternode(UpdateMasternode::consensus_decode(r)?)
            }
            CustomTxType::UpdateOracleAppoint => {
                DfTx::UpdateOracle(UpdateOracle::consensus_decode(r)?)
            }
            CustomTxType::UpdatePoolPair => {
                DfTx::PoolUpdatePair(PoolUpdatePair::consensus_decode(r)?)
            }
            CustomTxType::UpdateToken => DfTx::UpdateToken(UpdateToken::consensus_decode(r)?),
            CustomTxType::UpdateTokenAny => {
                DfTx::UpdateTokenAny(UpdateTokenAny::consensus_decode(r)?)
            }
            CustomTxType::UpdateVault => DfTx::UpdateVault(UpdateVault::consensus_decode(r)?),
            CustomTxType::UtxosToAccount => {
                DfTx::UtxosToAccount(UtxosToAccount::consensus_decode(r)?)
            }
            CustomTxType::CreateVault => DfTx::CreateVault(CreateVault::consensus_decode(r)?),
            CustomTxType::Vote => DfTx::Vote(Vote::consensus_decode(r)?),
            CustomTxType::WithdrawFromVault => {
                DfTx::WithdrawFromVault(WithdrawFromVault::consensus_decode(r)?)
            }
            _ => DfTx::None,
        };

        Ok(message)
    }
}

impl Encodable for DfTx {
    fn consensus_encode<W: io::Write + ?Sized>(&self, w: &mut W) -> Result<usize, io::Error> {
        let mut len = DFTX_MARKER.consensus_encode(w)?;

        let r#type = self.to_u8();
        len += r#type.consensus_encode(w)?;
        len += match self {
            DfTx::AccountToAccount(data) => data.consensus_encode(w),
            DfTx::AccountToUtxos(data) => data.consensus_encode(w),
            DfTx::PoolAddLiquidity(data) => data.consensus_encode(w),
            DfTx::AnyAccountsToAccounts(data) => data.consensus_encode(w),
            DfTx::AppointOracle(data) => data.consensus_encode(w),
            DfTx::PlaceAuctionBid(data) => data.consensus_encode(w),
            DfTx::BurnToken(data) => data.consensus_encode(w),
            DfTx::CloseVault(data) => data.consensus_encode(w),
            DfTx::CreateMasternode(data) => data.consensus_encode(w),
            DfTx::PoolCreatePair(data) => data.consensus_encode(w),
            DfTx::CreateCfp(data) => data.consensus_encode(w),
            DfTx::CreateToken(data) => data.consensus_encode(w),
            DfTx::CreateVoc(data) => data.consensus_encode(w),
            DfTx::SetDefaultLoanScheme(data) => data.consensus_encode(w),
            DfTx::DepositToVault(data) => data.consensus_encode(w),
            DfTx::DestroyLoanScheme(data) => data.consensus_encode(w),
            DfTx::EvmTx(data) => data.consensus_encode(w),
            DfTx::ICXClaimDFCHTLC(data) => data.consensus_encode(w),
            DfTx::ICXCloseOffer(data) => data.consensus_encode(w),
            DfTx::ICXCloseOrder(data) => data.consensus_encode(w),
            DfTx::ICXCreateOrder(data) => data.consensus_encode(w),
            DfTx::ICXMakeOffer(data) => data.consensus_encode(w),
            DfTx::ICXSubmitDFCHTLC(data) => data.consensus_encode(w),
            DfTx::ICXSubmitEXTHTLC(data) => data.consensus_encode(w),
            DfTx::SetLoanScheme(data) => data.consensus_encode(w),
            DfTx::MintToken(data) => data.consensus_encode(w),
            DfTx::PaybackLoan(data) => data.consensus_encode(w),
            DfTx::PaybackLoanV2(data) => data.consensus_encode(w),
            DfTx::PoolSwap(data) => data.consensus_encode(w),
            DfTx::CompositeSwap(data) => data.consensus_encode(w),
            DfTx::RemoveOracle(data) => data.consensus_encode(w),
            DfTx::PoolRemoveLiquidity(data) => data.consensus_encode(w),
            DfTx::ResignMasternode(data) => data.consensus_encode(w),
            DfTx::SetGovernance(data) => data.consensus_encode(w),
            DfTx::SetGovernanceHeight(data) => data.consensus_encode(w),
            DfTx::SetCollateralToken(data) => data.consensus_encode(w),
            DfTx::SetLoanToken(data) => data.consensus_encode(w),
            DfTx::SetOracleData(data) => data.consensus_encode(w),
            DfTx::TakeLoan(data) => data.consensus_encode(w),
            DfTx::TransferDomain(data) => data.consensus_encode(w),
            DfTx::UpdateLoanToken(data) => data.consensus_encode(w),
            DfTx::UpdateMasternode(data) => data.consensus_encode(w),
            DfTx::UpdateOracle(data) => data.consensus_encode(w),
            DfTx::PoolUpdatePair(data) => data.consensus_encode(w),
            DfTx::UpdateToken(data) => data.consensus_encode(w),
            DfTx::UpdateTokenAny(data) => data.consensus_encode(w),
            DfTx::UpdateVault(data) => data.consensus_encode(w),
            DfTx::UtxosToAccount(data) => data.consensus_encode(w),
            DfTx::CreateVault(data) => data.consensus_encode(w),
            DfTx::SetFutureSwap(data) => data.consensus_encode(w),
            DfTx::Vote(data) => data.consensus_encode(w),
            DfTx::WithdrawFromVault(data) => data.consensus_encode(w),
            DfTx::Reject | DfTx::None => Ok(0),
        }?;
        Ok(len)
    }
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct Stack {
    pub dftx: DfTx,
    rest: RawBytes,
}

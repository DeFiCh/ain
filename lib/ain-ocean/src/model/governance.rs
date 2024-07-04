use defichain_rpc::json::governance::{ProposalInfo, ProposalStatus, ProposalType};
use serde::{Deserialize, Serialize, Serializer};
use serde_with::skip_serializing_none;

#[skip_serializing_none]
#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ApiProposalInfo {
    pub proposal_id: String,
    pub title: String,
    pub context: String,
    pub context_hash: String,
    pub r#type: ProposalType,
    pub status: ProposalStatus,
    #[serde(
        serialize_with = "serialize_amount",
        skip_serializing_if = "should_skip"
    )]
    pub amount: Option<String>,
    pub current_cycle: u64,
    pub total_cycles: u64,
    pub creation_height: u64,
    pub cycle_end_height: u64,
    pub proposal_end_height: u64,
    pub payout_address: Option<String>,
    pub voting_period: u64,
    pub approval_threshold: String,
    pub quorum: String,
    pub votes_possible: Option<u64>,
    pub votes_present: Option<u64>,
    pub votes_present_pct: Option<String>,
    pub votes_yes: Option<u64>,
    pub votes_invalid: Option<u64>,
    pub votes_neutral: Option<u64>,
    pub votes_no: Option<u64>,
    pub votes_yes_pct: Option<String>,
    pub fee: f64,
    pub options: Option<Vec<String>>,
    pub fee_redistribution_per_vote: Option<f64>,
    pub fee_redistribution_total: Option<f64>,
}

impl From<ProposalInfo> for ApiProposalInfo {
    fn from(proposal: ProposalInfo) -> Self {
        ApiProposalInfo {
            proposal_id: proposal.proposal_id,
            title: proposal.title,
            context: proposal.context,
            context_hash: proposal.context_hash,
            r#type: proposal.r#type,
            status: proposal.status,
            amount: proposal.amount.map(|a| format!("{:.8}", a)),
            current_cycle: proposal.current_cycle,
            total_cycles: proposal.total_cycles,
            creation_height: proposal.creation_height,
            cycle_end_height: proposal.cycle_end_height,
            proposal_end_height: proposal.proposal_end_height,
            payout_address: proposal.payout_address,
            voting_period: proposal.voting_period,
            approval_threshold: proposal.approval_threshold,
            quorum: proposal.quorum,
            votes_possible: proposal.votes_possible,
            votes_present: proposal.votes_present,
            votes_present_pct: proposal.votes_present_pct,
            votes_yes: proposal.votes_yes,
            votes_invalid: proposal.votes_invalid,
            votes_neutral: proposal.votes_neutral,
            votes_no: proposal.votes_no,
            votes_yes_pct: proposal.votes_yes_pct,
            fee: proposal.fee,
            options: proposal.options,
            fee_redistribution_per_vote: proposal.fee_redistribution_per_vote,
            fee_redistribution_total: proposal.fee_redistribution_total,
        }
    }
}
fn serialize_amount<S>(amount: &Option<String>, serializer: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    match amount {
        Some(value) => serializer.serialize_some(value),
        None => serializer.serialize_some(&serde_json::Value::String("undefined".to_string())),
    }
}

fn should_skip<T>(_option: &Option<T>) -> bool {
    false
}

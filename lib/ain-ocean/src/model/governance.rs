use defichain_rpc::json::governance::{ProposalInfo, ProposalStatus, ProposalType};
use serde::Serialize;
use serde_with::skip_serializing_none;

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ApiProposalInfo {
    pub proposal_id: String,
    pub title: String,
    pub context: String,
    pub context_hash: String,
    pub r#type: ProposalType,
    pub status: ProposalStatus,
    pub current_cycle: u64,
    pub total_cycles: u64,
    pub creation_height: u64,
    pub cycle_end_height: u64,
    pub proposal_end_height: u64,
    pub voting_period: u64,
    pub approval_threshold: String,
    pub quorum: String,
    #[serde(flatten)]
    pub confidence_vote: ApiProposalConfidenceVote,
    #[serde(flatten)]
    pub vote_info: ApiProposalVoteInfo,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ApiProposalConfidenceVote {
    pub amount: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub payout_address: Option<String>,
}

#[skip_serializing_none]
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ApiProposalVoteInfo {
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
            current_cycle: proposal.current_cycle,
            total_cycles: proposal.total_cycles,
            creation_height: proposal.creation_height,
            cycle_end_height: proposal.cycle_end_height,
            proposal_end_height: proposal.proposal_end_height,
            voting_period: proposal.voting_period,
            approval_threshold: proposal.approval_threshold,
            quorum: proposal.quorum,
            confidence_vote: ApiProposalConfidenceVote {
                amount: proposal.amount.map(|a| format!("{:.8}", a)),
                payout_address: proposal.payout_address,
            },
            vote_info: ApiProposalVoteInfo {
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
            },
        }
    }
}

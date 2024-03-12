use ethereum_types::{H160, H256, U256};

/// Revert custom error data contains:
/// Function selector (4) + offset (32) + string length (32) and the subsequent string data
const MESSAGE_LENGTH_START_IDX: usize = 36;
const MESSAGE_START_IDX: usize = 68;

pub fn format_h256(hash: H256) -> String {
    format!("{hash:#x}")
}

pub fn format_address(hash: H160) -> String {
    format!("{hash:#x}")
}

pub fn format_u256(number: U256) -> String {
    format!("{number:#x}")
}

pub fn try_get_reverted_error_or_default(data: &[u8]) -> String {
    if data.len() > MESSAGE_START_IDX {
        let message_len = U256::from(&data[MESSAGE_LENGTH_START_IDX..MESSAGE_START_IDX]);
        let Ok(message_len) = usize::try_from(message_len) else {
            return Default::default();
        };
        let Some(message_end) = MESSAGE_START_IDX.checked_add(message_len) else {
            return Default::default();
        };

        if data.len() >= message_end {
            let body: &[u8] = &data[MESSAGE_START_IDX..message_end];
            if let Ok(reason) = std::str::from_utf8(body) {
                return format!("execution reverted: {reason}");
            }
        }
    }
    // Failed to deserialize, return default
    Default::default()
}

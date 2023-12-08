use serde::Serialize;

/// ApiPagedResponse indicates that this response of data array slice is part of a sorted list of items.
/// Items are part of a larger sorted list and the slice indicates a window within the large sorted list.
/// Each ApiPagedResponse holds the data array and the "token" for next part of the slice.
/// The next token should be passed via query 'next' and only used when getting the next slice.
/// Hence the first request, the next token is always empty and not provided.
///
/// With ascending sorted list and a limit of 3 items per slice will have the behaviour as such.
///
/// SORTED  : | [1] [2] [3] | [4] [5] [6] | [7] [8] [9] | [10]
/// Query 1 : Data: [1] [2] [3], Next: 3, Operator: GT (>)
/// Query 2 : Data: [4] [5] [6], Next: 6, Operator: GT (>)
/// Query 3 : Data: [7] [8] [9], Next: 3, Operator: GT (>)
/// Query 4 : Data: [10], Next: undefined
///
/// This design is resilient also mutating sorted list, where pagination is not.
///
/// SORTED  : [2] [4] [6] [8] [10] [12] [14]
/// Query 1 : Data: [2] [4] [6], Next: 6, Operator: GT (>)
///
/// Being in a slice window, the larger sorted list can be mutated.
/// You only need the next token to get the next slice.
/// MUTATED : [2] [4] [7] [8] [9] [10] [12] [14]
/// Query 2 : Data: [7] [8] [9], Next: 6, Operator: GT (>)
///
/// Limitations of this requires your dat astructure to always be sorted in one direction and your sort
/// indexes always fixed. Hence the moving down of the slice window, your operator will be greater than (GT).
/// While moving up your operator will be less than (GT).
///
/// ASC  : | [1] [2] [3] | [4] [5] [6] | [7] [8] [9] |
///                      >3            >6             >9
/// DESC : | [9] [8] [7] | [6] [5] [4] | [3] [2] [1] |
///                      <7            <4            <1
/// For developer quality life it's unwise to allow inclusive operator, it just creates more overhead
/// to understanding our services. No GTE or LTE, always GT and LE. Services must beclean and clear,
/// when the usage narrative is clear and so will the use of ease. LIST query must be dead simple.
/// Image travelling down the path, and getting a "next token" to get the next set of itmes to
/// continue walking.
///
/// Because the limit is not part of the slice window your query mechanism should support varying size windows.
///
/// DATA: | [1] [2] [3] | [4] [5] [6] [7] | [8] [9] | ...
///       | limit 3, >3 | limit 4, >7     | limit 2, >9
/// For simplicity your API should not attempt to allow access to different sort indexes, be cognizant of
/// how our APIs are consumed. If we create a GET /blocks operation to list blocks what would the correct indexes
/// be 99% of the time?
///
/// Answer: Blocks sorted by height in descending order, that's your sorted list and your slice window.
///       : <- Latest | [100] [99] [98] [97] [...] | Oldest ->
///
#[derive(Debug, Serialize, PartialEq)]
pub struct ApiPagedResponse<T> {
    data: Vec<T>,
    page: ApiPage,
}

#[derive(Debug, Serialize, PartialEq)]
struct ApiPage {
    next: Option<String>,
}

impl<T> ApiPagedResponse<T> {
    pub fn new(data: Vec<T>, next: Option<&str>) -> Self {
        Self {
            data,
            page: ApiPage {
                next: next.map(Into::into),
            },
        } // Option<&str> -> Option<String>
    }

    pub fn next(data: Vec<T>, next: Option<&str>) -> Self {
        Self::new(data, next)
    }

    pub fn of(data: Vec<T>, limit: usize, next_provider: impl Fn(&T) -> String) -> Self {
        if data.len() == limit && data.len() > 0 && limit > 0 {
            let next = next_provider(&data[limit - 1]);
            Self::next(data, Some(next.as_str()))
        } else {
            Self::next(data, None)
        }
    }

    pub fn empty() -> Self {
        Self::new(Vec::new(), None)
    }
}

#[cfg(test)]
mod tests {
    use super::{ApiPage, ApiPagedResponse};

    #[derive(Clone, Debug)]
    struct Item {
        id: String,
        sort: String,
    }

    impl Item {
        fn new(id: &str, sort: &str) -> Self {
            Self {
                id: id.into(),
                sort: sort.into(),
            }
        }
    }

    #[test]
    fn should_next_with_none() {
        let items: Vec<Item> = vec![Item::new("0", "a"), Item::new("1", "b")];

        let next = ApiPagedResponse::next(items, None).page.next;
        assert_eq!(next, None);
    }

    #[test]
    fn should_next_with_value() {
        let items: Vec<Item> = vec![Item::new("0", "a"), Item::new("1", "b")];

        let next = ApiPagedResponse::next(items, Some("b")).page.next;
        assert_eq!(next, Some("b".into()));
    }

    #[test]
    fn should_of_with_limit_3() {
        let items: Vec<Item> = vec![
            Item::new("0", "a"),
            Item::new("1", "b"),
            Item::new("2", "c"),
        ];

        let next = ApiPagedResponse::of(items, 3, |item| item.clone().sort)
            .page
            .next;
        assert_eq!(next, Some("c".into()))
    }

    #[test]
    fn should_not_create_with_limit_3_while_size_2() {
        let items: Vec<Item> = vec![Item::new("0", "a"), Item::new("1", "b")];

        let page = ApiPagedResponse::of(items, 3, |item| item.clone().sort).page;
        assert_eq!(page, ApiPage { next: None })
    }
}

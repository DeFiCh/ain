pub trait PersistentState {
    fn save_to_disk(&self, path: &str) -> Result<(), String>;
    fn load_from_disk(path: &str) -> Result<Self, String>
    where
        Self: Sized;
}

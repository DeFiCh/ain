use crate::Result;

/// This implementation block includes a versioning system for database migrations.
/// It ensures that the database schema is up-to-date with the node's expectations
/// by applying necessary migrations upon startup. The `CURRENT_VERSION` constant reflects
/// the latest version of the database schemas expected by the node.
///
/// The `migrate` method sequentially applies any required migrations based on the current
/// database version. The version information is stored in the `metadata`` column family
/// within the RocksDB instance.
///
/// Migrations are defined as implementations of the `Migration` trait and are executed
/// in order of their version number.
///
/// The `startup` method initializes the migration process as part of the startup flow
/// and should be called on BlockStore initialization.
///
pub trait DBVersionControl {
    const VERSION_KEY: &'static str = "version";
    const CURRENT_VERSION: u32;

    /// Sets the version number in the database to the specified `version`.
    ///
    /// This operation is atomic and flushes the updated version to disk immediately
    /// to maintain consistency even in the event of a failure or shutdown following the update.
    fn set_version(&self, version: u32) -> Result<()>;

    /// Retrieves the current version number from the database.
    fn get_version(&self) -> Result<u32>;

    /// Executes the migration process.
    ///
    /// It checks for the current database version and applies all the necessary migrations
    /// that have a version number greater than the current one. After all migrations are
    /// applied, sets the database version to `CURRENT_VERSION`.
    fn migrate(&self) -> Result<()>;

    /// Startup routine to ensure the database schema is up-to-date.
    ///
    /// This should be called after DB initialization.
    fn startup(&self) -> Result<()>;
}

/// The `Migration` trait defines the core requirements for any migration: a target version
/// and a migration method. Each concrete implementation of this trait corresponds to a specific
/// migration version and contains the logic to update the generic store to that version.
///
/// Implementations are expected to be idempotent to ensure safety across re-runs, which might occur
/// in scenarios like recovery from partial migrations or failures.
///
/// Wherever possible, migrations should leverage parallel processing with `rayon`
/// to improve performance and end-user UX
///
/// ## Usage
///
/// Migrations are to be executed during the startup phase of the node, ensuring the generic store
/// reflects the current expected state of the schemas.

/// The `Migration` trait, which all migrations must implement.
pub trait Migration<T>
where
    T: DBVersionControl,
{
    /// Returns the target version number for the migration.
    fn version(&self) -> u32;
    /// Contains the logic to migrate the given T to the target version.
    ///
    /// # Idempotency
    /// Implementations should ensure that the migration can be run multiple times
    /// without causing additional changes after the first successful run.
    fn migrate(&self, store: &T) -> Result<()>;
}

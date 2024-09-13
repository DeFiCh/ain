use ain_db::{Column, ColumnName, LedgerColumn, Rocks};
use log::debug;
use rocksdb::{BlockBasedOptions, Cache, Options, SliceTransform};
use std::{fs, marker::PhantomData, path::Path, sync::Arc};

use super::{ScriptActivity, COLUMN_NAMES};
use crate::Result;

// ScriptActivity is the heaviest CF we use, sitting at 20% of disk and supporting listtransactions query.
// Optimized here for the typical usage of sequential read of 10 elements, averaging 300bytes per entry.
fn get_scriptactivity_cf_options() -> Options {
    let mut options = Options::default();

    // Optimize for small, targeted queries
    options.set_optimize_filters_for_hits(true);
    options.set_level_compaction_dynamic_level_bytes(true);

    let mut block_opts = BlockBasedOptions::default();
    block_opts.set_block_size(4 * 1024); // 4 KB ~(300 * 10)
    block_opts.set_format_version(5);
    block_opts.set_cache_index_and_filter_blocks(true);
    block_opts.set_pin_l0_filter_and_index_blocks_in_cache(true);

    // Bloom filters
    block_opts.set_bloom_filter(10.0, false);

    // Block cache
    let cache = Cache::new_lru_cache(256 * 1024 * 1024); // 256 MB
    block_opts.set_block_cache(&cache);

    options.set_block_based_table_factory(&block_opts);

    // Compression
    options.set_compression_type(rocksdb::DBCompressionType::Zstd);
    options.set_bottommost_compression_type(rocksdb::DBCompressionType::Zstd);

    // Write buffer
    options.set_write_buffer_size(32 * 1024 * 1024); // 32 MB
    options.set_max_write_buffer_number(3);
    options.set_min_write_buffer_number_to_merge(1);

    // Level options
    options.set_max_bytes_for_level_base(256 * 1024 * 1024); // 256 MB
    options.set_max_bytes_for_level_multiplier(10.0);

    // Target file size
    options.set_target_file_size_base(32 * 1024 * 1024); // 32 MB

    options.set_prefix_extractor(SliceTransform::create_fixed_prefix(32)); // hid length
    options.set_memtable_whole_key_filtering(true);
    options.set_memtable_prefix_bloom_ratio(0.1);

    // Optimize for point lookups
    options.optimize_for_point_lookup(4096);

    options
}

#[derive(Debug, Clone)]
pub struct OceanStore(Arc<Rocks>);

impl OceanStore {
    pub fn new(path: &Path) -> Result<Self> {
        let path = path.join("ocean");
        fs::create_dir_all(&path)?;

        let cf_with_opts = COLUMN_NAMES
            .into_iter()
            .map(|name| match name {
                ScriptActivity::NAME => (name, Some(get_scriptactivity_cf_options())),
                _ => (name, None),
            })
            .collect::<Vec<_>>();

        let backend = Arc::new(Rocks::open(&path, cf_with_opts, None)?);
        debug!("Dumping table size");
        if let Err(e) = backend.dump_table_sizes(&COLUMN_NAMES) {
            debug!("e dumping {e}");
        }

        Ok(Self(backend))
    }

    pub fn column<C>(&self) -> LedgerColumn<C>
    where
        C: Column + ColumnName,
    {
        LedgerColumn {
            backend: Arc::clone(&self.0),
            column: PhantomData,
        }
    }

    pub fn compact(&self) {
        self.0.compact();
    }
}

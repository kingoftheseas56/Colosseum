//! Data-directory discovery. The daemon is the only crate that knows where
//! data lives on disk; domain crates receive paths, never discover them.

use directories::ProjectDirs;
use std::path::PathBuf;

pub fn data_dir() -> PathBuf {
    let dirs = ProjectDirs::from("io", "Brotherhood", "Colosseum")
        .expect("project dirs resolve on all supported platforms");
    dirs.data_dir().to_path_buf()
}

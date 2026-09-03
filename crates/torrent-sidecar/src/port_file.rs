//! The sidecar's port/token handoff file.
//!
//! On boot the sidecar binds `127.0.0.1:0` and writes the actual port plus a
//! per-run random token to `<data_dir>/torrent-sidecar.port` (mode 0600). The
//! daemon reads this file to reach the control API; it doubles as the
//! single-instance guard. The data dir is the parent of the torrent cache dir
//! (`--cache-dir <data_dir>/torrent-cache`), so the port file lives one level
//! up from the cache.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

/// The wire shape of `torrent-sidecar.port`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PortFile {
    pub port: u16,
    pub token: String,
}

/// The fixed file name under the data dir.
pub const PORT_FILE_NAME: &str = "torrent-sidecar.port";

/// Derive the port-file path from the cache dir: `<cache-dir>/../torrent-sidecar.port`.
pub fn port_file_path(cache_dir: &Path) -> PathBuf {
    cache_dir
        .parent()
        .unwrap_or_else(|| Path::new("."))
        .join(PORT_FILE_NAME)
}

/// Write the port file atomically-ish, mode 0600, creating the parent dir.
pub fn write_port_file(cache_dir: &Path, port: u16, token: &str) -> std::io::Result<PathBuf> {
    let path = port_file_path(cache_dir);
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let body = serde_json::to_vec(&PortFile {
        port,
        token: token.to_string(),
    })
    .expect("port file serializes");
    write_private_file(&path, &body)?;
    Ok(path)
}

/// Write `bytes` to `path` with mode 0600 (best-effort on non-unix).
fn write_private_file(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    use std::io::Write;
    let mut file = std::fs::File::create(path)?;
    file.write_all(bytes)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        file.set_permissions(std::fs::Permissions::from_mode(0o600))?;
    }
    Ok(())
}

/// Read and parse the port file.
pub fn read_port_file(path: &Path) -> std::io::Result<PortFile> {
    let bytes = std::fs::read(path)?;
    parse_port_file(&bytes).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
}

/// Parse the JSON body of the port file.
pub fn parse_port_file(bytes: &[u8]) -> Result<PortFile, serde_json::Error> {
    serde_json::from_slice(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn port_file_roundtrips() {
        let parsed = parse_port_file(br#"{"port":41234,"token":"abc123"}"#).unwrap();
        assert_eq!(
            parsed,
            PortFile {
                port: 41234,
                token: "abc123".into()
            }
        );
    }

    #[test]
    fn port_file_path_is_parent_of_cache_dir() {
        let cache = Path::new("/data/colosseum/torrent-cache");
        assert_eq!(
            port_file_path(cache),
            PathBuf::from("/data/colosseum/torrent-sidecar.port")
        );
    }
}

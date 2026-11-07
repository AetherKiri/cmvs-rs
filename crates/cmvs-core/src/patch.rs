//! Read-only PKP translation overlays. No DLL execution or game assets are embedded.
//! Entries are UTF-16 basenames and independently raw-DEFLATE compressed. Only the
//! index is retained; large graphics archives are read on demand through bounded seeks.
use crate::{
    FormatError, MAX_PAYLOAD,
    filesystem::{self as fs, File},
    platform::resolve_under,
};
use std::{
    collections::BTreeMap,
    error::Error,
    io::{Read, Seek, SeekFrom},
    path::{Path, PathBuf},
};

type Result<T> = std::result::Result<T, Box<dyn Error>>;
#[derive(Debug, Clone, Copy)]
struct Entry {
    offset: u64,
    decoded: usize,
    packed: usize,
}
pub struct Archive {
    file: File,
    entries: BTreeMap<String, Entry>,
}

pub fn basename(name: &[u8]) -> Result<String> {
    let name = std::str::from_utf8(name)?;
    if !name.is_ascii() || name.is_empty() || name.contains(['\0', ':']) {
        return Err("patch resource name must be a nonempty ASCII relative path".into());
    }
    let mut last = "";
    for part in name.split(['/', '\\']) {
        if part.is_empty() || matches!(part, "." | "..") {
            return Err("invalid patch resource path".into());
        }
        last = part;
    }
    Ok(last.to_ascii_lowercase())
}
fn present(root: &Path, name: &[u8]) -> Result<Option<PathBuf>> {
    match resolve_under(root, name) {
        Ok(path) => Ok(Some(path)),
        Err(e)
            if e.downcast_ref::<std::io::Error>()
                .is_some_and(|e| e.kind() == std::io::ErrorKind::NotFound) =>
        {
            Ok(None)
        }
        Err(e) => Err(e),
    }
}
impl Archive {
    pub fn open_optional(root: &Path, name: &[u8]) -> Result<Option<Self>> {
        let Some(path) = present(root, name)? else {
            return Ok(None);
        };
        let mut file = File::open(path)?;
        let entries = read_index(&mut file)?;
        Ok(Some(Self { file, entries }))
    }
    pub fn read(&mut self, name: &[u8]) -> Result<Option<Vec<u8>>> {
        if !name.is_ascii() {
            return Ok(None);
        }
        let key = basename(name)?;
        let Some(entry) = self.entries.get(&key) else {
            return Ok(None);
        };
        self.file.seek(SeekFrom::Start(entry.offset))?;
        let mut packed = vec![0; entry.packed];
        self.file.read_exact(&mut packed)?;
        unpack(&packed, entry.decoded).map(Some)
    }
}
fn unpack(packed: &[u8], decoded: usize) -> Result<Vec<u8>> {
    let output = miniz_oxide::inflate::decompress_to_vec_with_limit(packed, decoded)
        .map_err(|_| FormatError("invalid PKP raw-DEFLATE data or declared size exceeded"))?;
    if output.len() != decoded {
        return Err("PKP decoded length differs from index".into());
    }
    Ok(output)
}
fn read_index(file: &mut (impl Read + Seek)) -> Result<BTreeMap<String, Entry>> {
    let length = file.seek(SeekFrom::End(0))?;
    file.rewind()?;
    let mut header = [0; 12];
    file.read_exact(&mut header)?;
    let u32le = |b: &[u8]| u32::from_le_bytes(b.try_into().expect("four bytes"));
    if &header[..8] != b"PKP\0\0\0\0\0" {
        return Err("unsupported PKP header".into());
    }
    let count = u32le(&header[8..]) as usize;
    if count > MAX_PAYLOAD / 176 {
        return Err("PKP index exceeds allocation limit".into());
    }
    let data_start = 12 + count as u64 * 176;
    if data_start > length {
        return Err("truncated PKP index".into());
    }
    let mut entries = BTreeMap::new();
    for _ in 0..count {
        let mut row = [0; 176];
        file.read_exact(&mut row)?;
        let entry = Entry {
            offset: u64::from(u32le(&row[..4])),
            decoded: u32le(&row[4..8]) as usize,
            packed: u32le(&row[8..12]) as usize,
        };
        if entry.offset < data_start
            || entry.offset + entry.packed as u64 > length
            || entry.packed > MAX_PAYLOAD
            || entry.decoded > MAX_PAYLOAD
        {
            return Err("PKP entry outside archive or allocation limit".into());
        }
        let words: Vec<u16> = row[48..]
            .as_chunks::<2>()
            .0
            .iter()
            .map(|b| u16::from_le_bytes([b[0], b[1]]))
            .collect();
        let end = words
            .iter()
            .position(|&c| c == 0)
            .ok_or("unterminated PKP entry name")?;
        let name = String::from_utf16(&words[..end])?;
        if name.contains(['/', '\\']) {
            return Err("PKP entry is not a basename".into());
        }
        let name = basename(name.as_bytes())?;
        if entries.insert(name, entry).is_some() {
            return Err("duplicate PKP entry name".into());
        }
    }
    Ok(entries)
}

pub struct Images {
    root: PathBuf,
    archives: Option<Vec<Archive>>,
}
impl Images {
    pub fn new(root: &Path) -> Self {
        Self {
            root: root.to_owned(),
            archives: None,
        }
    }
    pub fn read(&mut self, name: &[u8]) -> Result<Option<Vec<u8>>> {
        if !name.is_ascii() {
            return Ok(None);
        }
        if self.archives.is_none() {
            let mut archives = Vec::new();
            for name in [b"Pk/Graphic.pk".as_slice(), b"Pk/Mosic.pk"] {
                if let Some(archive) = Archive::open_optional(&self.root, name)? {
                    archives.push(archive);
                }
            }
            self.archives = Some(archives);
        }
        if self.archives.as_ref().expect("initialized").is_empty() {
            return Ok(None);
        }
        let key = basename(name)?;
        let mut names = vec![key.clone()];
        if let Some(stem) = key.strip_suffix(".pb3") {
            names.push(format!("{stem}.png"));
        }
        for name in names {
            for archive in self.archives.as_mut().expect("initialized") {
                if let Some(bytes) = archive.read(name.as_bytes())? {
                    return Ok(Some(bytes));
                }
            }
        }
        Ok(None)
    }
}
/// The script overlay is the explicit opt-in signal for this GBK patch family.
/// A stock game, including one with independent movie replacements, remains Shift-JIS.
pub fn text_encoding(root: &Path) -> Result<crate::text_encoding::TextEncoding> {
    use crate::text_encoding::TextEncoding;
    Ok(
        if present(root, b"Pk/Scripts.pk")?.is_some_and(fs::is_file) {
            TextEncoding::Gbk
        } else {
            TextEncoding::ShiftJis
        },
    )
}

/// Return the actual relative file so indexing, frame seeks and the audio decoder
/// all refer to the same replacement container (their byte offsets must agree).
pub(crate) fn movie_name(root: &Path, original: &[u8]) -> Result<Vec<u8>> {
    if original.is_ascii() {
        let name = basename(original)?;
        let replacement = format!("Pk/{name}").into_bytes();
        if present(root, &replacement)?.is_some() {
            return Ok(replacement);
        }
    }
    Ok(original.to_vec())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn archive() -> Vec<u8> {
        let mut b = vec![0; 190];
        b[..4].copy_from_slice(b"PKP\0");
        b[8..12].copy_from_slice(&1u32.to_le_bytes());
        b[12..16].copy_from_slice(&188u32.to_le_bytes());
        b[20..24].copy_from_slice(&2u32.to_le_bytes());
        for (i, c) in "test.ps3".encode_utf16().enumerate() {
            b[60 + i * 2..62 + i * 2].copy_from_slice(&c.to_le_bytes());
        }
        b
    }
    #[test]
    fn index_rejects_unbounded_truncated_and_escaping_entries() {
        let b = archive();
        assert!(read_index(&mut std::io::Cursor::new(&b)).is_ok());
        for (at, value) in [(8, u32::MAX), (12, 0), (16, u32::MAX), (20, 3)] {
            let mut bad = b.clone();
            bad[at..at + 4].copy_from_slice(&value.to_le_bytes());
            assert!(read_index(&mut std::io::Cursor::new(bad)).is_err());
        }
        for name in [b"../x".as_slice(), b"/x", b"a\\..\\x", b"a:\\x", b"x\0"] {
            assert!(basename(name).is_err());
        }
        assert_eq!(basename(b"Images\\Title.PB3").unwrap(), "title.pb3");
    }
    #[test]
    fn payload_requires_exact_bounded_raw_deflate() {
        let payload = b"synthetic translation resource";
        let packed = miniz_oxide::deflate::compress_to_vec(payload, 6);
        assert_eq!(unpack(&packed, payload.len()).unwrap(), payload);
        assert!(unpack(&packed, payload.len() - 1).is_err());
        assert!(unpack(&packed, payload.len() + 1).is_err());
        assert!(unpack(b"invalid", payload.len()).is_err());
    }
}

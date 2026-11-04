//! Password protection: a locked notebook's (or folder's) content is
//! encrypted on disk, in backups and in Drive sync.
//!
//! Each lock has a random 256-bit content key. The key is stored wrapped
//! (encrypted) by a key derived from the password with Argon2id, in the
//! lock's description (a small JSON kept on the notebook's or folder's row,
//! so it travels with sync and backups). Content is encrypted with
//! XChaCha20-Poly1305 (random 192-bit nonces; tampering is detected).
//! There is no way back without the password.

use argon2::{Algorithm, Argon2, Params, Version};
use base64::Engine as _;
use chacha20poly1305::aead::{Aead, KeyInit};
use chacha20poly1305::{Key, XChaCha20Poly1305, XNonce};
use serde::{Deserialize, Serialize};

use crate::error::{AppError, AppResult};

/// Text values (element data, thumbnails, settings) start with this when encrypted.
const TEXT_PREFIX: &str = "enc1:";
/// Encrypted blobs (imported PDFs) start with this.
const BLOB_PREFIX: &[u8] = b"INKENC1\0";

const B64: base64::engine::GeneralPurpose = base64::engine::general_purpose::STANDARD;

/// A content key.
pub type ContentKey = [u8; 32];

/// A lock's description, stored on the notebook's or folder's row.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct LockInfo {
    /// Notebooks and folders sharing a lock have the same id (and key).
    pub id: String,
    pub v: u32,
    /// Argon2id: memory (KiB), passes, lanes; and the salt.
    pub m: u32,
    pub t: u32,
    pub p: u32,
    pub salt: String,
    /// The content key, encrypted with the password's key.
    pub nonce: String,
    pub wrapped: String,
}

impl LockInfo {
    pub fn parse(json: &str) -> AppResult<Self> {
        serde_json::from_str(json).map_err(|e| AppError::Invalid(format!("damaged lock: {e}")))
    }
    pub fn to_json(&self) -> String {
        serde_json::to_string(self).expect("lock serialises")
    }
}

/// Only the id of a lock column's JSON (without checking the rest).
pub fn lock_id(json: &str) -> Option<String> {
    LockInfo::parse(json).ok().map(|l| l.id)
}

fn random<const N: usize>() -> [u8; N] {
    let mut b = [0u8; N];
    getrandom::getrandom(&mut b).expect("the system's random numbers");
    b
}

/// The password's key (Argon2id with the lock's settings).
fn password_key(password: &str, info: &LockInfo) -> AppResult<ContentKey> {
    let salt = B64.decode(&info.salt).map_err(|_| AppError::Invalid("damaged lock".into()))?;
    let params = Params::new(info.m, info.t, info.p, Some(32)).map_err(|e| AppError::Invalid(format!("damaged lock: {e}")))?;
    let mut out = [0u8; 32];
    Argon2::new(Algorithm::Argon2id, Version::V0x13, params)
        .hash_password_into(password.as_bytes(), &salt, &mut out)
        .map_err(|e| AppError::Invalid(format!("couldn't use that password: {e}")))?;
    Ok(out)
}

fn check_password(password: &str) -> AppResult<()> {
    if password.chars().count() < 4 {
        return Err(AppError::Invalid("Use a password of at least 4 characters.".into()));
    }
    if password.len() > 1024 {
        return Err(AppError::Invalid("That password is too long.".into()));
    }
    Ok(())
}

/// A lock for `password` around `key` (a new key when none), under `id`.
fn make(password: &str, key: &ContentKey, id: String) -> AppResult<LockInfo> {
    check_password(password)?;
    // OWASP's Argon2id settings: 19 MiB, 2 passes (quick on phones too).
    let mut info = LockInfo { id, v: 1, m: 19 * 1024, t: 2, p: 1, salt: B64.encode(random::<16>()), nonce: String::new(), wrapped: String::new() };
    let pk = password_key(password, &info)?;
    let nonce = random::<24>();
    let wrapped = XChaCha20Poly1305::new(Key::from_slice(&pk))
        .encrypt(XNonce::from_slice(&nonce), key.as_slice())
        .map_err(|_| AppError::Invalid("couldn't lock".into()))?;
    info.nonce = B64.encode(nonce);
    info.wrapped = B64.encode(wrapped);
    Ok(info)
}

/// A new lock: its description and its content key.
pub fn new_lock(password: &str) -> AppResult<(LockInfo, ContentKey)> {
    let key = random::<32>();
    Ok((make(password, &key, crate::db::new_id())?, key))
}

/// The same lock (same key and id) under a new password.
pub fn rewrap(info: &LockInfo, key: &ContentKey, password: &str) -> AppResult<LockInfo> {
    make(password, key, info.id.clone())
}

/// The content key, if the password is right.
pub fn unlock(info: &LockInfo, password: &str) -> AppResult<ContentKey> {
    let pk = password_key(password, info)?;
    let bad = || AppError::Invalid("damaged lock".into());
    let nonce = B64.decode(&info.nonce).map_err(|_| bad())?;
    let wrapped = B64.decode(&info.wrapped).map_err(|_| bad())?;
    if nonce.len() != 24 {
        return Err(bad());
    }
    let key = XChaCha20Poly1305::new(Key::from_slice(&pk))
        .decrypt(XNonce::from_slice(&nonce), wrapped.as_slice())
        .map_err(|_| AppError::Invalid("Wrong password.".into()))?;
    key.try_into().map_err(|_| bad())
}

fn seal(key: &ContentKey, plain: &[u8]) -> Vec<u8> {
    let nonce = random::<24>();
    let mut out = nonce.to_vec();
    out.extend(XChaCha20Poly1305::new(Key::from_slice(key)).encrypt(XNonce::from_slice(&nonce), plain).expect("encrypting in memory"));
    out
}

fn open(key: &ContentKey, sealed: &[u8]) -> AppResult<Vec<u8>> {
    if sealed.len() < 24 {
        return Err(AppError::Invalid("damaged encrypted data".into()));
    }
    XChaCha20Poly1305::new(Key::from_slice(key))
        .decrypt(XNonce::from_slice(&sealed[..24]), &sealed[24..])
        .map_err(|_| AppError::Invalid("encrypted data that doesn't match its key (damaged?)".into()))
}

pub fn is_encrypted_text(s: &str) -> bool {
    s.starts_with(TEXT_PREFIX)
}

pub fn encrypt_text(key: &ContentKey, plain: &str) -> String {
    format!("{TEXT_PREFIX}{}", B64.encode(seal(key, plain.as_bytes())))
}

/// Decrypts an encrypted text value; plain values come back as they are.
pub fn decrypt_text(key: &ContentKey, s: &str) -> AppResult<String> {
    let Some(body) = s.strip_prefix(TEXT_PREFIX) else { return Ok(s.to_string()) };
    let sealed = B64.decode(body).map_err(|_| AppError::Invalid("damaged encrypted data".into()))?;
    String::from_utf8(open(key, &sealed)?).map_err(|_| AppError::Invalid("damaged encrypted data".into()))
}

pub fn is_encrypted_blob(b: &[u8]) -> bool {
    b.starts_with(BLOB_PREFIX)
}

pub fn encrypt_blob(key: &ContentKey, plain: &[u8]) -> Vec<u8> {
    let mut out = BLOB_PREFIX.to_vec();
    out.extend(seal(key, plain));
    out
}

/// Decrypts an encrypted blob; plain blobs come back as they are.
pub fn decrypt_blob(key: &ContentKey, b: &[u8]) -> AppResult<Vec<u8>> {
    match b.strip_prefix(BLOB_PREFIX) {
        Some(sealed) => open(key, sealed),
        None => Ok(b.to_vec()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_right_password_opens_and_a_wrong_one_does_not() {
        let (info, key) = new_lock("correct horse").unwrap();
        assert_eq!(unlock(&info, "correct horse").unwrap(), key);
        assert!(unlock(&info, "wrong horse").unwrap_err().to_string().contains("Wrong password"));
        // Stored and read back.
        let again = LockInfo::parse(&info.to_json()).unwrap();
        assert_eq!(unlock(&again, "correct horse").unwrap(), key);
    }

    #[test]
    fn a_new_password_keeps_the_key() {
        let (info, key) = new_lock("first one").unwrap();
        let moved = rewrap(&info, &key, "second one").unwrap();
        assert_eq!(moved.id, info.id);
        assert_eq!(unlock(&moved, "second one").unwrap(), key);
        assert!(unlock(&moved, "first one").is_err());
    }

    #[test]
    fn text_and_blobs_round_trip_and_tampering_is_caught() {
        let (_, key) = new_lock("secret").unwrap();
        let t = encrypt_text(&key, "{\"text\":\"مرحبا\"}");
        assert!(is_encrypted_text(&t) && !t.contains("مرحبا"));
        assert_eq!(decrypt_text(&key, &t).unwrap(), "{\"text\":\"مرحبا\"}");
        assert_eq!(decrypt_text(&key, "plain").unwrap(), "plain");
        let b = encrypt_blob(&key, b"%PDF-1.7 ...");
        assert!(is_encrypted_blob(&b));
        assert_eq!(decrypt_blob(&key, &b).unwrap(), b"%PDF-1.7 ...");
        let mut bad = b.clone();
        let n = bad.len() - 1;
        bad[n] ^= 1;
        assert!(decrypt_blob(&key, &bad).is_err());
        let (_, other) = new_lock("other").unwrap();
        assert!(decrypt_text(&other, &t).is_err());
    }

    #[test]
    fn short_passwords_are_refused() {
        assert!(new_lock("abc").is_err());
    }
}

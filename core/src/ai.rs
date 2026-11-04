//! AI assistance: a model the user picks (an API key for Claude, OpenAI or
//! Gemini, or a local model through Ollama or any OpenAI-compatible
//! server), asked with text and pictures (handwriting, selections, pages).
//!
//! The settings, API key included, are kept in `ai.json` in the app's data
//! folder: not in the library, so not in backups or Drive sync.

use std::path::PathBuf;
use std::sync::Mutex;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::error::{AppError, AppResult};

/// Where requests go and with what.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct AiSettings {
    /// "" (off), "anthropic", "openai", "gemini", "ollama" or "custom" (OpenAI-compatible).
    pub provider: String,
    /// The server (empty: the provider's usual one).
    pub base_url: String,
    /// The model (empty: the provider's default, where it has one).
    pub model: String,
    pub api_key: String,
    /// Read handwritten pages in the background so search finds handwriting.
    pub search_handwriting: bool,
}

/// What the settings screen shows (the key only as its last characters).
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AiStatus {
    pub provider: String,
    pub base_url: String,
    pub model: String,
    pub has_key: bool,
    pub key_hint: String,
    pub search_handwriting: bool,
    /// Set up enough to ask.
    pub ready: bool,
    /// Where it runs: "cloud" or "local" (on this computer or network).
    pub location: String,
    pub effective_url: String,
    pub effective_model: String,
}

/// A changed setting (missing fields keep their value; an empty key removes it).
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct AiChange {
    pub provider: Option<String>,
    pub base_url: Option<String>,
    pub model: Option<String>,
    pub api_key: Option<String>,
    pub search_handwriting: Option<bool>,
}

/// A request: instructions, then the conversation so far.
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct AskRequest {
    pub system: String,
    pub messages: Vec<AskMessage>,
    /// A short, simple job (reading handwriting): less thinking, fewer tokens.
    pub quick: bool,
    pub max_tokens: Option<u32>,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct AskMessage {
    /// "user" or "assistant".
    pub role: String,
    pub text: String,
    /// PNG pictures, base64 (a data: URL is fine too).
    pub images: Vec<String>,
}

pub const DEFAULT_CLAUDE_MODEL: &str = "claude-opus-5-5";
/// The Messages API version this code is written for.
const ANTHROPIC_VERSION: &str = "2026-10-21";

fn default_url(provider: &str) -> &'static str {
    match provider {
        "anthropic" => "https://api.anthropic.com",
        "openai" => "https://api.openai.com/v1",
        "gemini" => "https://generativelanguage.googleapis.com/v1beta/openai",
        "ollama" => "http://localhost:11434/v1",
        _ => "",
    }
}

fn default_model(provider: &str) -> &'static str {
    match provider {
        "anthropic" => DEFAULT_CLAUDE_MODEL,
        _ => "",
    }
}

fn is_local(url: &str) -> bool {
    let host = url.split("://").nth(1).unwrap_or(url).split(['/', ':']).next().unwrap_or("");
    host == "localhost"
        || host.starts_with("127.")
        || host == "[::1]"
        || host.starts_with("192.168.")
        || host.starts_with("10.")
        || host.ends_with(".local")
}

impl AiSettings {
    pub fn url(&self) -> String {
        let u = if self.base_url.trim().is_empty() { default_url(&self.provider) } else { self.base_url.trim() };
        u.trim_end_matches('/').to_string()
    }
    pub fn model_name(&self) -> String {
        if self.model.trim().is_empty() { default_model(&self.provider).to_string() } else { self.model.trim().to_string() }
    }
    fn needs_key(&self) -> bool {
        matches!(self.provider.as_str(), "anthropic" | "openai" | "gemini")
    }
    /// Why it can't ask yet ("" when it can).
    pub fn problem(&self) -> String {
        if self.provider.is_empty() {
            return "Choose an AI provider in Settings → AI first.".into();
        }
        if !matches!(self.provider.as_str(), "anthropic" | "openai" | "gemini" | "ollama" | "custom") {
            return format!("Unknown AI provider '{}'.", self.provider);
        }
        if self.needs_key() && self.api_key.trim().is_empty() {
            return "Add your API key in Settings → AI first.".into();
        }
        if self.url().is_empty() {
            return "Enter the server's address in Settings → AI first.".into();
        }
        if self.model_name().is_empty() {
            return "Enter a model name in Settings → AI first.".into();
        }
        String::new()
    }
    pub fn status(&self) -> AiStatus {
        let key = self.api_key.trim();
        let url = self.url();
        AiStatus {
            provider: self.provider.clone(),
            base_url: self.base_url.clone(),
            model: self.model.clone(),
            has_key: !key.is_empty(),
            key_hint: if key.chars().count() > 8 { format!("…{}", key.chars().rev().take(4).collect::<Vec<_>>().into_iter().rev().collect::<String>()) } else { String::new() },
            search_handwriting: self.search_handwriting,
            ready: self.problem().is_empty(),
            location: if is_local(&url) { "local".into() } else { "cloud".into() },
            effective_url: url,
            effective_model: self.model_name(),
        }
    }
}

/// The settings, kept in a file of their own.
pub struct Ai {
    path: PathBuf,
    settings: Mutex<AiSettings>,
}

impl Ai {
    pub fn load(path: PathBuf) -> Ai {
        let settings = std::fs::read(&path).ok().and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default();
        Ai { path, settings: Mutex::new(settings) }
    }

    pub fn settings(&self) -> AiSettings {
        self.settings.lock().unwrap_or_else(|e| e.into_inner()).clone()
    }

    pub fn status(&self) -> AiStatus {
        self.settings().status()
    }

    pub fn change(&self, c: AiChange) -> AppResult<AiStatus> {
        let mut s = self.settings();
        if let Some(p) = c.provider {
            if p != s.provider {
                // Another provider: its own server and model (and key).
                s.base_url.clear();
                s.model.clear();
                if c.api_key.is_none() {
                    s.api_key.clear();
                }
            }
            s.provider = p;
        }
        if let Some(u) = c.base_url {
            s.base_url = u.trim().to_string();
        }
        if let Some(m) = c.model {
            s.model = m.trim().to_string();
        }
        if let Some(k) = c.api_key {
            s.api_key = k.trim().to_string();
        }
        if let Some(h) = c.search_handwriting {
            s.search_handwriting = h;
        }
        let bytes = serde_json::to_vec_pretty(&s).map_err(|e| AppError::Invalid(e.to_string()))?;
        let tmp = self.path.with_extension("json.tmp");
        std::fs::write(&tmp, bytes)?;
        restrict(&tmp);
        std::fs::rename(&tmp, &self.path)?;
        *self.settings.lock().unwrap_or_else(|e| e.into_inner()) = s.clone();
        Ok(s.status())
    }

    /// Asks the model; its answer's text.
    pub fn ask(&self, req: &AskRequest) -> AppResult<String> {
        ask(&self.settings(), req)
    }

    /// A tiny request, to check the settings work.
    pub fn test(&self) -> AppResult<String> {
        let req = AskRequest {
            system: String::new(),
            messages: vec![AskMessage { role: "user".into(), text: "Reply with the single word: ready".into(), images: vec![] }],
            quick: true,
            max_tokens: Some(2000),
        };
        self.ask(&req)
    }
}

/// Only this user may read the file with the key (where that's a thing).
fn restrict(path: &std::path::Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600));
    }
    #[cfg(not(unix))]
    let _ = path;
}

fn agent(local: bool) -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout_connect(Duration::from_secs(15))
        // Local models can be slow to load and to answer.
        .timeout(Duration::from_secs(if local { 600 } else { 300 }))
        // A server on this computer or network is reached directly, not through a proxy.
        .try_proxy_from_env(!local)
        .build()
}

/// A picture's base64 (without a data: URL's start).
fn b64_of(img: &str) -> &str {
    match img.find(";base64,") {
        Some(i) if img.starts_with("data:") => &img[i + 8..],
        _ => img,
    }
}

fn media_type(img: &str) -> &str {
    if img.starts_with("data:image/jpeg") {
        "image/jpeg"
    } else {
        "image/png"
    }
}

/// Claude models that take an effort setting.
fn claude_takes_effort(model: &str) -> bool {
    ["claude-opus-5", "claude-sonnet-5", "claude-haiku-5", "claude-fable", "claude-mythos", "claude-opus-4-6", "claude-opus-4-7", "claude-opus-4-8", "claude-sonnet-4-6"]
        .iter()
        .any(|p| model.starts_with(p))
}

/// Claude models whose refusals can be retried on another model by the server.
fn claude_has_fallbacks(model: &str) -> bool {
    ["claude-opus-5-5", "claude-opus-5", "claude-fable-5-1", "claude-sonnet-5-5"].contains(&model)
}

/// The request body for Claude's Messages API.
pub fn anthropic_body(model: &str, req: &AskRequest, official: bool) -> Value {
    let messages: Vec<Value> = req
        .messages
        .iter()
        .map(|m| {
            let mut content: Vec<Value> = m
                .images
                .iter()
                .map(|i| json!({ "type": "image", "source": { "type": "base64", "media_type": media_type(i), "data": b64_of(i) } }))
                .collect();
            content.push(json!({ "type": "text", "text": m.text }));
            json!({ "role": if m.role == "assistant" { "assistant" } else { "user" }, "content": content })
        })
        .collect();
    let mut body = json!({
        "model": model,
        "max_tokens": req.max_tokens.unwrap_or(if req.quick { 4000 } else { 16000 }),
        "messages": messages,
    });
    if !req.system.is_empty() {
        body["system"] = json!(req.system);
    }
    if claude_takes_effort(model) {
        body["output_config"] = json!({ "effort": if req.quick { "low" } else { "medium" } });
    }
    if official && claude_has_fallbacks(model) {
        body["fallbacks"] = json!("default");
    }
    body
}

/// The request body for an OpenAI-compatible chat completions API.
pub fn openai_body(provider: &str, model: &str, req: &AskRequest) -> Value {
    let mut messages = Vec::new();
    if !req.system.is_empty() {
        messages.push(json!({ "role": "system", "content": req.system }));
    }
    for m in &req.messages {
        let role = if m.role == "assistant" { "assistant" } else { "user" };
        if m.images.is_empty() {
            messages.push(json!({ "role": role, "content": m.text }));
        } else {
            let mut content = vec![json!({ "type": "text", "text": m.text })];
            for i in &m.images {
                let url = if i.starts_with("data:") { i.clone() } else { format!("data:image/png;base64,{i}") };
                content.push(json!({ "type": "image_url", "image_url": { "url": url } }));
            }
            messages.push(json!({ "role": role, "content": content }));
        }
    }
    let mut body = json!({ "model": model, "messages": messages });
    let max = req.max_tokens.unwrap_or(if req.quick { 2000 } else { 8000 });
    // OpenAI's own API moved to max_completion_tokens; the rest still take max_tokens.
    body[if provider == "openai" { "max_completion_tokens" } else { "max_tokens" }] = json!(max);
    body
}

/// The text of a Messages API answer.
pub fn anthropic_text(v: &Value) -> AppResult<String> {
    if v.get("stop_reason").and_then(Value::as_str) == Some("refusal") {
        return Err(AppError::Invalid("The model declined to answer that.".into()));
    }
    let text: String = v
        .get("content")
        .and_then(Value::as_array)
        .map(|blocks| blocks.iter().filter(|b| b.get("type").and_then(Value::as_str) == Some("text")).filter_map(|b| b.get("text").and_then(Value::as_str)).collect::<Vec<_>>().join(""))
        .unwrap_or_default();
    Ok(text.trim().to_string())
}

/// The text of a chat completions answer.
pub fn openai_text(v: &Value) -> AppResult<String> {
    let msg = v.pointer("/choices/0/message").ok_or_else(|| AppError::Invalid("The AI server's answer wasn't understood.".into()))?;
    let text = match msg.get("content") {
        Some(Value::String(s)) => s.clone(),
        Some(Value::Array(parts)) => parts.iter().filter_map(|p| p.get("text").and_then(Value::as_str)).collect::<Vec<_>>().join(""),
        _ => String::new(),
    };
    if text.is_empty() && msg.get("refusal").and_then(Value::as_str).is_some() {
        return Err(AppError::Invalid("The model declined to answer that.".into()));
    }
    Ok(strip_thinking(&text).trim().to_string())
}

/// Local reasoning models put their thinking in <think>…</think>: not part of the answer.
fn strip_thinking(s: &str) -> String {
    match (s.find("<think>"), s.find("</think>")) {
        (Some(a), Some(b)) if a < b => format!("{}{}", &s[..a], &s[b + 8..]),
        _ => s.to_string(),
    }
}

/// An error answer's message, made readable.
fn error_text(code: u16, body: &str) -> String {
    let detail = serde_json::from_str::<Value>(body)
        .ok()
        .and_then(|v| v.pointer("/error/message").and_then(Value::as_str).map(str::to_string).or_else(|| v.get("error").and_then(Value::as_str).map(str::to_string)))
        .unwrap_or_else(|| body.chars().take(200).collect());
    let what = match code {
        401 | 403 => "The API key was refused",
        404 => "Not found (check the model name and the server's address)",
        413 => "That's too much to send at once",
        429 => "Too many requests or out of credit; try again later",
        500..=599 => "The AI service had a problem; try again later",
        _ => "The AI request failed",
    };
    if detail.is_empty() { format!("{what} ({code}).") } else { format!("{what} ({code}): {detail}") }
}

pub fn ask(s: &AiSettings, req: &AskRequest) -> AppResult<String> {
    let problem = s.problem();
    if !problem.is_empty() {
        return Err(AppError::Invalid(problem));
    }
    if req.messages.is_empty() {
        return Err(AppError::Invalid("Nothing to ask.".into()));
    }
    let url = s.url();
    let model = s.model_name();
    let local = is_local(&url);
    let agent = agent(local);
    let (request, body) = if s.provider == "anthropic" {
        let official = s.base_url.trim().is_empty();
        let mut r = agent
            .post(&format!("{url}/v1/messages"))
            .set("x-api-key", s.api_key.trim())
            .set("anthropic-version", ANTHROPIC_VERSION)
            .set("content-type", "application/json");
        if official && claude_has_fallbacks(&model) {
            r = r.set("anthropic-beta", "server-side-fallback-2026-07-01");
        }
        (r, anthropic_body(&model, req, official))
    } else {
        let mut r = agent.post(&format!("{url}/chat/completions")).set("content-type", "application/json");
        if !s.api_key.trim().is_empty() {
            r = r.set("authorization", &format!("Bearer {}", s.api_key.trim()));
        }
        (r, openai_body(&s.provider, &model, req))
    };
    let resp = match request.send_string(&body.to_string()) {
        Ok(r) => r,
        Err(ureq::Error::Status(code, resp)) => {
            let text = resp.into_string().unwrap_or_default();
            return Err(AppError::Invalid(error_text(code, &text)));
        }
        Err(ureq::Error::Transport(t)) => {
            return Err(AppError::Invalid(if local {
                format!("Couldn't reach the local AI server at {url}. Is it running? ({t})")
            } else {
                format!("Couldn't reach the AI service; check the internet connection. ({t})")
            }))
        }
    };
    let v: Value = serde_json::from_str(&resp.into_string()?).map_err(|_| AppError::Invalid("The AI server's answer wasn't understood.".into()))?;
    if s.provider == "anthropic" { anthropic_text(&v) } else { openai_text(&v) }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn req(images: Vec<String>) -> AskRequest {
        AskRequest { system: "Be brief.".into(), messages: vec![AskMessage { role: "user".into(), text: "What is this?".into(), images }], quick: false, max_tokens: None }
    }

    #[test]
    fn claude_requests_carry_pictures_and_settings() {
        let b = anthropic_body("claude-opus-5-5", &req(vec!["data:image/png;base64,AAAA".into()]), true);
        assert_eq!(b["system"], "Be brief.");
        assert_eq!(b["messages"][0]["content"][0]["source"]["data"], "AAAA");
        assert_eq!(b["messages"][0]["content"][0]["source"]["media_type"], "image/png");
        assert_eq!(b["messages"][0]["content"][1]["text"], "What is this?");
        assert_eq!(b["output_config"]["effort"], "medium");
        assert_eq!(b["fallbacks"], "default");
        assert!(b.get("temperature").is_none() && b.get("thinking").is_none());
        // Older models: no effort; through another server: no fallbacks.
        let old = anthropic_body("claude-haiku-4-5", &req(vec![]), false);
        assert!(old.get("output_config").is_none() && old.get("fallbacks").is_none());
    }

    #[test]
    fn openai_compatible_requests() {
        let b = openai_body("ollama", "llava", &req(vec!["QUJD".into()]));
        assert_eq!(b["messages"][0]["role"], "system");
        assert_eq!(b["messages"][1]["content"][1]["image_url"]["url"], "data:image/png;base64,QUJD");
        assert_eq!(b["max_tokens"], 8000);
        let o = openai_body("openai", "m", &req(vec![]));
        assert_eq!(o["messages"][1]["content"], "What is this?");
        assert!(o.get("max_completion_tokens").is_some());
    }

    #[test]
    fn answers_are_read() {
        let a = json!({ "stop_reason": "end_turn", "content": [{ "type": "thinking", "thinking": "" }, { "type": "text", "text": " Hello " }] });
        assert_eq!(anthropic_text(&a).unwrap(), "Hello");
        assert!(anthropic_text(&json!({ "stop_reason": "refusal", "content": [] })).is_err());
        let o = json!({ "choices": [{ "message": { "content": "<think>hmm</think>Hi" } }] });
        assert_eq!(openai_text(&o).unwrap(), "Hi");
    }

    #[test]
    fn settings_need_what_the_provider_needs() {
        let mut s = AiSettings { provider: "anthropic".into(), ..Default::default() };
        assert!(s.problem().contains("API key"));
        s.api_key = "sk-ant-123456789".into();
        assert!(s.problem().is_empty());
        assert_eq!(s.model_name(), DEFAULT_CLAUDE_MODEL);
        assert_eq!(s.status().key_hint, "…6789");
        assert_eq!(s.status().location, "cloud");
        let o = AiSettings { provider: "ollama".into(), ..Default::default() };
        assert!(o.problem().contains("model"));
        let o = AiSettings { provider: "ollama".into(), model: "llava".into(), ..Default::default() };
        assert!(o.problem().is_empty());
        assert_eq!(o.status().location, "local");
    }

    #[test]
    fn settings_are_kept_and_a_new_provider_starts_clean() {
        let dir = std::env::temp_dir().join(format!("inkleaf-ai-{}", crate::db::new_id()));
        std::fs::create_dir_all(&dir).unwrap();
        let ai = Ai::load(dir.join("ai.json"));
        ai.change(AiChange { provider: Some("anthropic".into()), api_key: Some("sk-ant-abcdefghij".into()), ..Default::default() }).unwrap();
        let again = Ai::load(dir.join("ai.json"));
        assert_eq!(again.settings().api_key, "sk-ant-abcdefghij");
        let st = again.change(AiChange { provider: Some("ollama".into()), ..Default::default() }).unwrap();
        assert!(!st.has_key);
        assert_eq!(st.effective_url, "http://localhost:11434/v1");
        let _ = std::fs::remove_dir_all(dir);
    }
}

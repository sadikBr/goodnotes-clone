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

    /// Asks with the answer streamed (see `ask_stream`).
    pub fn ask_stream(&self, req: &AskRequest, on: &mut dyn FnMut(&str) -> bool) -> AppResult<String> {
        ask_stream(&self.settings(), req, on)
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

/// Hides a reasoning model's <think>…</think> from text that arrives in pieces.
#[derive(Default)]
pub struct ThinkFilter {
    inside: bool,
    held: String,
}

impl ThinkFilter {
    /// The part of `piece` to show (a tag split across pieces is held back until whole).
    pub fn push(&mut self, piece: &str) -> String {
        self.held.push_str(piece);
        let mut out = String::new();
        loop {
            let tag = if self.inside { "</think>" } else { "<think>" };
            if let Some(i) = self.held.find(tag) {
                if !self.inside {
                    out.push_str(&self.held[..i]);
                }
                self.held = self.held[i + tag.len()..].to_string();
                self.inside = !self.inside;
                continue;
            }
            // Keep what could be the start of the tag; the rest is settled.
            let keep = (1..tag.len()).rev().find(|&n| self.held.ends_with(&tag[..n])).unwrap_or(0);
            let cut = self.held.len() - keep;
            if !self.inside {
                out.push_str(&self.held[..cut]);
            }
            self.held = self.held[cut..].to_string();
            return out;
        }
    }
    /// What's left at the end.
    pub fn finish(&mut self) -> String {
        if self.inside { String::new() } else { std::mem::take(&mut self.held) }
    }
}

/// One server-sent event's data, read from a streamed answer: its text, or the end.
enum Piece {
    Text(String),
    Done,
    Nothing,
}

fn anthropic_piece(v: &Value) -> AppResult<Piece> {
    match v.get("type").and_then(Value::as_str) {
        Some("content_block_delta") if v.pointer("/delta/type").and_then(Value::as_str) == Some("text_delta") => {
            Ok(Piece::Text(v.pointer("/delta/text").and_then(Value::as_str).unwrap_or("").to_string()))
        }
        Some("message_delta") if v.pointer("/delta/stop_reason").and_then(Value::as_str) == Some("refusal") => {
            Err(AppError::Invalid("The model declined to answer that.".into()))
        }
        Some("message_stop") => Ok(Piece::Done),
        Some("error") => Err(AppError::Invalid(format!(
            "The AI service had a problem: {}",
            v.pointer("/error/message").and_then(Value::as_str).unwrap_or("unknown error")
        ))),
        _ => Ok(Piece::Nothing),
    }
}

fn openai_piece(v: &Value) -> AppResult<Piece> {
    if let Some(e) = v.get("error") {
        return Err(AppError::Invalid(format!("The AI service had a problem: {}", e.get("message").and_then(Value::as_str).unwrap_or("unknown error"))));
    }
    Ok(match v.pointer("/choices/0/delta/content").and_then(Value::as_str) {
        Some(t) => Piece::Text(t.to_string()),
        None => Piece::Nothing,
    })
}

/// Asks with the answer streamed: `on` gets each new piece of text as it
/// arrives (returning false stops). The whole answer comes back at the end.
/// Servers that don't stream are read whole (one piece).
pub fn ask_stream(s: &AiSettings, req: &AskRequest, on: &mut dyn FnMut(&str) -> bool) -> AppResult<String> {
    use std::io::BufRead;
    let resp = send(s, req, true)?;
    let anthropic = s.provider == "anthropic";
    if !resp.content_type().contains("event-stream") {
        // Not streamed after all: the whole answer at once.
        let v: Value = serde_json::from_str(&resp.into_string()?).map_err(|_| AppError::Invalid("The AI server's answer wasn't understood.".into()))?;
        let text = if anthropic { anthropic_text(&v)? } else { openai_text(&v)? };
        on(&text);
        return Ok(text);
    }
    let mut all = String::new();
    let mut think = ThinkFilter::default();
    let reader = std::io::BufReader::new(resp.into_reader());
    for line in reader.lines() {
        let line = line.map_err(|e| AppError::Invalid(format!("The answer was cut off: {e}")))?;
        let Some(data) = line.strip_prefix("data:") else { continue };
        let data = data.trim();
        if data == "[DONE]" {
            break;
        }
        let Ok(v) = serde_json::from_str::<Value>(data) else { continue };
        let piece = if anthropic { anthropic_piece(&v)? } else { openai_piece(&v)? };
        match piece {
            Piece::Text(t) => {
                let shown = think.push(&t);
                if !shown.is_empty() {
                    all.push_str(&shown);
                    if !on(&shown) {
                        return Ok(all);
                    }
                }
            }
            Piece::Done => break,
            Piece::Nothing => {}
        }
    }
    let rest = think.finish();
    if !rest.is_empty() {
        all.push_str(&rest);
        on(&rest);
    }
    Ok(all.trim().to_string())
}

/// Sends the request (streamed or not); the server's response, or a readable error.
fn send(s: &AiSettings, req: &AskRequest, stream: bool) -> AppResult<ureq::Response> {
    if req.messages.is_empty() {
        return Err(AppError::Invalid("Nothing to ask.".into()));
    }
    let official = s.base_url.trim().is_empty();
    let model = s.model_name();
    let body = if s.provider == "anthropic" { anthropic_body(&model, req, official) } else { openai_body(&s.provider, &model, req) };
    post(s, body, stream)
}

/// Sends a request body to the provider (its URL, key and headers); the response, or a readable error.
fn post(s: &AiSettings, mut body: Value, stream: bool) -> AppResult<ureq::Response> {
    let problem = s.problem();
    if !problem.is_empty() {
        return Err(AppError::Invalid(problem));
    }
    let url = s.url();
    let model = s.model_name();
    let local = is_local(&url);
    let agent = agent(local);
    let request = if s.provider == "anthropic" {
        let official = s.base_url.trim().is_empty();
        let mut r = agent
            .post(&format!("{url}/v1/messages"))
            .set("x-api-key", s.api_key.trim())
            .set("anthropic-version", ANTHROPIC_VERSION)
            .set("content-type", "application/json");
        if official && claude_has_fallbacks(&model) {
            r = r.set("anthropic-beta", "server-side-fallback-2026-07-01");
        }
        r
    } else {
        let mut r = agent.post(&format!("{url}/chat/completions")).set("content-type", "application/json");
        if !s.api_key.trim().is_empty() {
            r = r.set("authorization", &format!("Bearer {}", s.api_key.trim()));
        }
        r
    };
    if stream {
        body["stream"] = json!(true);
    }
    match request.send_string(&body.to_string()) {
        Ok(r) => Ok(r),
        Err(ureq::Error::Status(code, resp)) => {
            let text = resp.into_string().unwrap_or_default();
            Err(AppError::Invalid(error_text(code, &text)))
        }
        Err(ureq::Error::Transport(t)) => Err(AppError::Invalid(if local {
            format!("Couldn't reach the local AI server at {url}. Is it running? ({t})")
        } else {
            format!("Couldn't reach the AI service; check the internet connection. ({t})")
        })),
    }
}

pub fn ask(s: &AiSettings, req: &AskRequest) -> AppResult<String> {
    let resp = send(s, req, false)?;
    let v: Value = serde_json::from_str(&resp.into_string()?).map_err(|_| AppError::Invalid("The AI server's answer wasn't understood.".into()))?;
    if s.provider == "anthropic" { anthropic_text(&v) } else { openai_text(&v) }
}

// ------------------------------------------------------------ tools (the assistant)

/// A tool the model may use: its name, what it does, and its input (a JSON schema).
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct Tool {
    pub name: String,
    pub description: String,
    pub schema: Value,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct ToolCall {
    pub id: String,
    pub name: String,
    pub input: Value,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct ToolResult {
    pub id: String,
    pub content: String,
    pub error: bool,
}

/// A turn of a conversation with tools: the user's words, the model's (with the
/// tools it called), or the results of those calls ("tool").
#[derive(Debug, Clone, Default, Deserialize)]
#[serde(default)]
pub struct StepMessage {
    pub role: String,
    pub text: String,
    pub images: Vec<String>,
    pub calls: Vec<ToolCall>,
    pub results: Vec<ToolResult>,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct StepRequest {
    pub system: String,
    pub messages: Vec<StepMessage>,
    pub tools: Vec<Tool>,
    pub max_tokens: Option<u32>,
}

/// The model's turn: what it says and the tools it wants used (none: it's done).
#[derive(Debug, Clone, Default, Serialize)]
pub struct StepAnswer {
    pub text: String,
    pub calls: Vec<ToolCall>,
}

pub fn anthropic_step_body(model: &str, req: &StepRequest, official: bool) -> Value {
    let mut messages: Vec<Value> = Vec::new();
    for m in &req.messages {
        match m.role.as_str() {
            "assistant" => {
                let mut content = Vec::new();
                if !m.text.trim().is_empty() {
                    content.push(json!({ "type": "text", "text": m.text }));
                }
                for c in &m.calls {
                    content.push(json!({ "type": "tool_use", "id": c.id, "name": c.name, "input": if c.input.is_object() { c.input.clone() } else { json!({}) } }));
                }
                messages.push(json!({ "role": "assistant", "content": content }));
            }
            "tool" => {
                let content: Vec<Value> = m.results.iter().map(|r| json!({ "type": "tool_result", "tool_use_id": r.id, "content": r.content, "is_error": r.error })).collect();
                messages.push(json!({ "role": "user", "content": content }));
            }
            _ => {
                let mut content: Vec<Value> = m.images.iter().map(|i| json!({ "type": "image", "source": { "type": "base64", "media_type": media_type(i), "data": b64_of(i) } })).collect();
                content.push(json!({ "type": "text", "text": m.text }));
                messages.push(json!({ "role": "user", "content": content }));
            }
        }
    }
    let tools: Vec<Value> = req.tools.iter().map(|t| json!({ "name": t.name, "description": t.description, "input_schema": t.schema })).collect();
    let mut body = json!({ "model": model, "max_tokens": req.max_tokens.unwrap_or(16000), "messages": messages, "tools": tools });
    if !req.system.is_empty() {
        body["system"] = json!(req.system);
    }
    if claude_takes_effort(model) {
        body["output_config"] = json!({ "effort": "medium" });
    }
    if official && claude_has_fallbacks(model) {
        body["fallbacks"] = json!("default");
    }
    body
}

pub fn openai_step_body(provider: &str, model: &str, req: &StepRequest) -> Value {
    let mut messages = Vec::new();
    if !req.system.is_empty() {
        messages.push(json!({ "role": "system", "content": req.system }));
    }
    for m in &req.messages {
        match m.role.as_str() {
            "assistant" => {
                let mut msg = json!({ "role": "assistant", "content": if m.text.is_empty() { Value::Null } else { json!(m.text) } });
                if !m.calls.is_empty() {
                    msg["tool_calls"] = json!(m
                        .calls
                        .iter()
                        .map(|c| json!({ "id": c.id, "type": "function", "function": { "name": c.name, "arguments": c.input.to_string() } }))
                        .collect::<Vec<_>>());
                }
                messages.push(msg);
            }
            "tool" => {
                for r in &m.results {
                    messages.push(json!({ "role": "tool", "tool_call_id": r.id, "content": r.content }));
                }
            }
            _ => {
                if m.images.is_empty() {
                    messages.push(json!({ "role": "user", "content": m.text }));
                } else {
                    let mut content = vec![json!({ "type": "text", "text": m.text })];
                    for i in &m.images {
                        let url = if i.starts_with("data:") { i.clone() } else { format!("data:image/png;base64,{i}") };
                        content.push(json!({ "type": "image_url", "image_url": { "url": url } }));
                    }
                    messages.push(json!({ "role": "user", "content": content }));
                }
            }
        }
    }
    let tools: Vec<Value> = req
        .tools
        .iter()
        .map(|t| json!({ "type": "function", "function": { "name": t.name, "description": t.description, "parameters": t.schema } }))
        .collect();
    let mut body = json!({ "model": model, "messages": messages, "tools": tools });
    body[if provider == "openai" { "max_completion_tokens" } else { "max_tokens" }] = json!(req.max_tokens.unwrap_or(8000));
    body
}

pub fn anthropic_step_answer(v: &Value) -> AppResult<StepAnswer> {
    if v.get("stop_reason").and_then(Value::as_str) == Some("refusal") {
        return Err(AppError::Invalid("The model declined to do that.".into()));
    }
    let mut out = StepAnswer::default();
    for b in v.get("content").and_then(Value::as_array).into_iter().flatten() {
        match b.get("type").and_then(Value::as_str) {
            Some("text") => out.text.push_str(b.get("text").and_then(Value::as_str).unwrap_or("")),
            Some("tool_use") => out.calls.push(ToolCall {
                id: b.get("id").and_then(Value::as_str).unwrap_or("").to_string(),
                name: b.get("name").and_then(Value::as_str).unwrap_or("").to_string(),
                input: b.get("input").cloned().unwrap_or(json!({})),
            }),
            _ => {}
        }
    }
    out.text = out.text.trim().to_string();
    Ok(out)
}

pub fn openai_step_answer(v: &Value) -> AppResult<StepAnswer> {
    let msg = v.pointer("/choices/0/message").ok_or_else(|| AppError::Invalid("The AI server's answer wasn't understood.".into()))?;
    let mut out = StepAnswer::default();
    out.text = match msg.get("content") {
        Some(Value::String(s)) => strip_thinking(s).trim().to_string(),
        Some(Value::Array(parts)) => parts.iter().filter_map(|p| p.get("text").and_then(Value::as_str)).collect::<Vec<_>>().join("").trim().to_string(),
        _ => String::new(),
    };
    for (k, c) in msg.get("tool_calls").and_then(Value::as_array).into_iter().flatten().enumerate() {
        let args = c.pointer("/function/arguments");
        // Arguments come as a JSON string (some servers send the object itself).
        let input = match args {
            Some(Value::String(s)) => serde_json::from_str(s).unwrap_or(json!({})),
            Some(o @ Value::Object(_)) => o.clone(),
            _ => json!({}),
        };
        out.calls.push(ToolCall {
            id: c.get("id").and_then(Value::as_str).map(str::to_string).unwrap_or_else(|| format!("call_{k}")),
            name: c.pointer("/function/name").and_then(Value::as_str).unwrap_or("").to_string(),
            input,
        });
    }
    if out.text.is_empty() && out.calls.is_empty() && msg.get("refusal").and_then(Value::as_str).is_some() {
        return Err(AppError::Invalid("The model declined to do that.".into()));
    }
    Ok(out)
}

/// One turn of the model, with tools: what it says and the tools it calls.
pub fn step(s: &AiSettings, req: &StepRequest) -> AppResult<StepAnswer> {
    if req.messages.is_empty() {
        return Err(AppError::Invalid("Nothing to ask.".into()));
    }
    let model = s.model_name();
    let anthropic = s.provider == "anthropic";
    let body = if anthropic { anthropic_step_body(&model, req, s.base_url.trim().is_empty()) } else { openai_step_body(&s.provider, &model, req) };
    let resp = post(s, body, false)?;
    let v: Value = serde_json::from_str(&resp.into_string()?).map_err(|_| AppError::Invalid("The AI server's answer wasn't understood.".into()))?;
    if anthropic { anthropic_step_answer(&v) } else { openai_step_answer(&v) }
}

impl Ai {
    pub fn step(&self, req: &StepRequest) -> AppResult<StepAnswer> {
        step(&self.settings(), req)
    }
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
    fn tool_turns_round_trip_for_both_kinds_of_server() {
        let req = StepRequest {
            system: "Help.".into(),
            tools: vec![Tool { name: "create_folder".into(), description: "Makes a folder".into(), schema: json!({ "type": "object", "properties": { "name": { "type": "string" } } }) }],
            messages: vec![
                StepMessage { role: "user".into(), text: "Make a Biology folder".into(), ..Default::default() },
                StepMessage { role: "assistant".into(), text: "".into(), calls: vec![ToolCall { id: "t1".into(), name: "create_folder".into(), input: json!({ "name": "Biology" }) }], ..Default::default() },
                StepMessage { role: "tool".into(), results: vec![ToolResult { id: "t1".into(), content: "ok".into(), error: false }], ..Default::default() },
            ],
            max_tokens: None,
        };
        let a = anthropic_step_body("claude-opus-5-5", &req, true);
        assert_eq!(a["tools"][0]["input_schema"]["type"], "object");
        assert_eq!(a["messages"][1]["content"][0]["type"], "tool_use");
        assert_eq!(a["messages"][2]["role"], "user");
        assert_eq!(a["messages"][2]["content"][0]["tool_use_id"], "t1");
        let o = openai_step_body("openai", "gpt", &req);
        assert_eq!(o["tools"][0]["function"]["name"], "create_folder");
        assert_eq!(o["messages"][2]["tool_calls"][0]["function"]["arguments"], "{\"name\":\"Biology\"}");
        assert_eq!(o["messages"][3]["role"], "tool");
        // Answers with calls.
        let ca = anthropic_step_answer(&json!({ "stop_reason": "tool_use", "content": [{ "type": "text", "text": "Sure." }, { "type": "tool_use", "id": "x", "name": "create_folder", "input": { "name": "Bio" } }] })).unwrap();
        assert_eq!(ca.calls[0].input["name"], "Bio");
        assert_eq!(ca.text, "Sure.");
        let oa = openai_step_answer(&json!({ "choices": [{ "message": { "content": null, "tool_calls": [{ "id": "y", "type": "function", "function": { "name": "create_folder", "arguments": "{\"name\":\"Bio\"}" } }] } }] })).unwrap();
        assert_eq!(oa.calls[0].name, "create_folder");
        assert_eq!(oa.calls[0].input["name"], "Bio");
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
    fn thinking_is_hidden_even_split_across_pieces() {
        let mut f = ThinkFilter::default();
        let mut out = String::new();
        for p in ["Hel", "lo <thi", "nk>secret", " stuff</th", "ink> world", " <", "b>"] {
            out += &f.push(p);
        }
        out += &f.finish();
        assert_eq!(out, "Hello  world <b>");
    }

    #[test]
    fn streamed_pieces_are_read() {
        let d = json!({ "type": "content_block_delta", "index": 0, "delta": { "type": "text_delta", "text": "Hi" } });
        assert!(matches!(anthropic_piece(&d).unwrap(), Piece::Text(t) if t == "Hi"));
        let r = json!({ "type": "message_delta", "delta": { "stop_reason": "refusal" } });
        assert!(anthropic_piece(&r).is_err());
        let o = json!({ "choices": [{ "delta": { "content": "Yo" } }] });
        assert!(matches!(openai_piece(&o).unwrap(), Piece::Text(t) if t == "Yo"));
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

use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MatchKey {
    Action,
    Subsystem,
    DevPath,
    Env(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MatchOp {
    Equal,
    NotEqual,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MatchCondition {
    pub key: MatchKey,
    pub op: MatchOp,
    pub value: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Rule {
    pub conditions: Vec<MatchCondition>,
    pub run: Option<String>,
}

/// Matches string with wildcard '*'
pub fn glob_match(pattern: &str, text: &str) -> bool {
    let p_bytes = pattern.as_bytes();
    let t_bytes = text.as_bytes();
    let mut p_idx = 0;
    let mut t_idx = 0;
    let mut star_idx = None;
    let mut match_idx = 0;

    while t_idx < t_bytes.len() {
        if p_idx < p_bytes.len() && p_bytes[p_idx] == t_bytes[t_idx] {
            p_idx += 1;
            t_idx += 1;
        } else if p_idx < p_bytes.len() && p_bytes[p_idx] == b'*' {
            star_idx = Some(p_idx);
            p_idx += 1;
            match_idx = t_idx;
        } else if let Some(star) = star_idx {
            p_idx = star + 1;
            match_idx += 1;
            t_idx = match_idx;
        } else {
            return false;
        }
    }

    while p_idx < p_bytes.len() && p_bytes[p_idx] == b'*' {
        p_idx += 1;
    }

    p_idx == p_bytes.len()
}

fn unquote(s: &str) -> String {
    let s = s.trim();
    if (s.starts_with('"') && s.ends_with('"')) || (s.starts_with('\'') && s.ends_with('\'')) {
        if s.len() >= 2 {
            return s[1..s.len() - 1].to_string();
        }
    }
    s.to_string()
}

fn parse_match_key(key_str: &str) -> MatchKey {
    let trimmed = key_str.trim();
    if trimmed.eq_ignore_ascii_case("ACTION") {
        MatchKey::Action
    } else if trimmed.eq_ignore_ascii_case("SUBSYSTEM") {
        MatchKey::Subsystem
    } else if trimmed.eq_ignore_ascii_case("DEVPATH") {
        MatchKey::DevPath
    } else if (trimmed.starts_with("ENV{") || trimmed.starts_with("env{")) && trimmed.ends_with('}') {
        let inside = &trimmed[4..trimmed.len() - 1];
        MatchKey::Env(inside.trim().to_string())
    } else {
        MatchKey::Env(trimmed.to_string())
    }
}

/// Splits a line into tokens separated by commas, respecting quotes.
fn split_tokens(line: &str) -> Vec<String> {
    let mut tokens = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;
    let mut quote_char = '"';

    for c in line.chars() {
        match c {
            '"' | '\'' => {
                if in_quotes && c == quote_char {
                    in_quotes = false;
                } else if !in_quotes {
                    in_quotes = true;
                    quote_char = c;
                }
                current.push(c);
            }
            ',' if !in_quotes => {
                let trimmed = current.trim();
                if !trimmed.is_empty() {
                    tokens.push(trimmed.to_string());
                }
                current.clear();
            }
            _ => {
                current.push(c);
            }
        }
    }
    let trimmed = current.trim();
    if !trimmed.is_empty() {
        tokens.push(trimmed.to_string());
    }
    tokens
}

/// Parses a single rule line into a `Rule`.
pub fn parse_rule_line(line: &str) -> Option<Rule> {
    let line = line.trim();
    if line.is_empty() || line.starts_with('#') {
        return None;
    }

    let tokens = split_tokens(line);
    if tokens.is_empty() {
        return None;
    }

    let mut conditions = Vec::new();
    let mut run = None;

    for token in tokens {
        if let Some(idx) = token.find("!=") {
            let key_str = token[..idx].trim();
            let val_str = token[idx + 2..].trim();
            conditions.push(MatchCondition {
                key: parse_match_key(key_str),
                op: MatchOp::NotEqual,
                value: unquote(val_str),
            });
        } else if let Some(idx) = token.find("==") {
            let key_str = token[..idx].trim();
            let val_str = token[idx + 2..].trim();
            conditions.push(MatchCondition {
                key: parse_match_key(key_str),
                op: MatchOp::Equal,
                value: unquote(val_str),
            });
        } else if let Some(idx) = token.find("+=") {
            let key_str = token[..idx].trim();
            let val_str = token[idx + 2..].trim();
            if key_str.eq_ignore_ascii_case("RUN") {
                run = Some(unquote(val_str));
            }
        } else if let Some(idx) = token.find('=') {
            let key_str = token[..idx].trim();
            let val_str = token[idx + 1..].trim();
            if key_str.eq_ignore_ascii_case("RUN") {
                run = Some(unquote(val_str));
            }
        }
    }

    Some(Rule { conditions, run })
}

impl Rule {
    pub fn matches(&self, event: &HashMap<String, String>) -> bool {
        for cond in &self.conditions {
            let val = match &cond.key {
                MatchKey::Action => event.get("ACTION"),
                MatchKey::Subsystem => event.get("SUBSYSTEM"),
                MatchKey::DevPath => event.get("DEVPATH"),
                MatchKey::Env(k) => event.get(k),
            };

            match cond.op {
                MatchOp::Equal => match val {
                    Some(v) => {
                        if !glob_match(&cond.value, v) {
                            return false;
                        }
                    }
                    None => return false,
                },
                MatchOp::NotEqual => match val {
                    Some(v) => {
                        if glob_match(&cond.value, v) {
                            return false;
                        }
                    }
                    None => {} // Not present satisfies !=
                },
            }
        }
        true
    }
}

/// Variable substitution: `$env{KEY}` expands to event key, `%k` expands to the basename / leaf of `DEVPATH`.
pub fn substitute_vars(command: &str, event: &HashMap<String, String>) -> String {
    let devpath = event.get("DEVPATH").map(|s| s.as_str()).unwrap_or("");
    let trimmed = devpath.trim_end_matches('/');
    let leaf = match trimmed.rfind('/') {
        Some(idx) => &trimmed[idx + 1..],
        None => trimmed,
    };

    let mut result = String::with_capacity(command.len());
    let mut chars = command.char_indices().peekable();

    while let Some((i, c)) = chars.next() {
        if c == '%' {
            if let Some(&(_, next_c)) = chars.peek() {
                if next_c == 'k' {
                    chars.next();
                    result.push_str(leaf);
                    continue;
                } else if next_c == '%' {
                    chars.next();
                    result.push('%');
                    continue;
                }
            }
            result.push('%');
        } else if c == '$' {
            let rem = &command[i..];
            if rem.starts_with("$env{") || rem.starts_with("$ENV{") {
                if let Some(close_idx) = rem.find('}') {
                    let key = &rem[5..close_idx];
                    let val = event.get(key).map(|s| s.as_str()).unwrap_or("");
                    result.push_str(val);
                    for _ in 0..close_idx {
                        chars.next();
                    }
                    continue;
                }
            }
            result.push('$');
        } else {
            result.push(c);
        }
    }

    result
}

/// Parses a command line string into an argument vector, respecting quotes.
pub fn split_command(cmd: &str) -> Vec<String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut in_quote: Option<char> = None;

    for c in cmd.chars() {
        match in_quote {
            Some(q) if c == q => {
                in_quote = None;
            }
            Some(_) => {
                current.push(c);
            }
            None if c == '"' || c == '\'' => {
                in_quote = Some(c);
            }
            None if c.is_whitespace() => {
                if !current.is_empty() {
                    args.push(current);
                    current = String::new();
                }
            }
            None => {
                current.push(c);
            }
        }
    }
    if !current.is_empty() {
        args.push(current);
    }
    args
}

pub fn load_rules() -> Vec<Rule> {
    let mut rules = Vec::new();

    // 1. Read /etc/devd.rules if present
    if let Ok(content) = std::fs::read_to_string("/etc/devd.rules") {
        for line in content.lines() {
            if let Some(rule) = parse_rule_line(line) {
                rules.push(rule);
            }
        }
        println!("loaded {} rules from /etc/devd.rules", rules.len());
    } else {
        println!("Could not read /etc/devd.rules");
    }
    rules
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_glob_match() {
        assert!(glob_match("*", "anything"));
        assert!(glob_match("*", ""));
        assert!(glob_match("3/*", "3/1/2"));
        assert!(glob_match("3/*", "3/"));
        assert!(!glob_match("3/*", "2/1/2"));
        assert!(glob_match("usb*", "usb"));
        assert!(glob_match("usb*", "usb1"));
        assert!(glob_match("*/hid", "/bin/hid"));
    }

    #[test]
    fn test_parse_rule() {
        let line = r#"ACTION=="add", SUBSYSTEM=="usb", ENV{TYPE}=="3/*", RUN+="/bin/hid $env{DEVPATH}""#;
        let rule = parse_rule_line(line).unwrap();
        assert_eq!(rule.conditions.len(), 3);
        assert_eq!(
            rule.conditions[0],
            MatchCondition {
                key: MatchKey::Action,
                op: MatchOp::Equal,
                value: "add".to_string(),
            }
        );
        assert_eq!(
            rule.conditions[1],
            MatchCondition {
                key: MatchKey::Subsystem,
                op: MatchOp::Equal,
                value: "usb".to_string(),
            }
        );
        assert_eq!(
            rule.conditions[2],
            MatchCondition {
                key: MatchKey::Env("TYPE".to_string()),
                op: MatchOp::Equal,
                value: "3/*".to_string(),
            }
        );
        assert_eq!(rule.run, Some("/bin/hid $env{DEVPATH}".to_string()));
    }

    #[test]
    fn test_substitute_vars() {
        let mut event = HashMap::new();
        event.insert("DEVPATH".to_string(), "/devices/pci/0:14.0/usb1/1-1".to_string());
        event.insert("TYPE".to_string(), "3/1/2".to_string());

        let cmd = substitute_vars("/bin/hid $env{DEVPATH} %k $env{TYPE}", &event);
        assert_eq!(cmd, "/bin/hid /devices/pci/0:14.0/usb1/1-1 1-1 3/1/2");
    }

    #[test]
    fn test_split_command() {
        let cmd = r#"/bin/hid "/devices/pci/0:14.0/usb1/1-1" arg2"#;
        let args = split_command(cmd);
        assert_eq!(args, vec!["/bin/hid", "/devices/pci/0:14.0/usb1/1-1", "arg2"]);
    }
}

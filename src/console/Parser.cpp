#include "Parser.h"

#include <cstdio>
#include <cstring>

namespace mm {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

ParseResult refuse(const char* fmt, const char* a = "", const char* b = "") {
  ParseResult r;
  r.ok = false;
  std::snprintf(r.error, sizeof r.error, fmt, a, b);
  return r;
}

ParseResult accept(Verb verb) {
  ParseResult r;
  r.ok = true;
  r.command.verb = verb;
  return r;
}

}  // namespace

ParseResult parseLine(const char* line) {
  if (line == nullptr) return accept(Verb::Empty);

  // Bound the line first, then copy it so the tokens can be cut in place.
  const size_t rawLen = std::strlen(line);
  if (rawLen > kMaxLineLen + 2) {  // + a CR LF that a terminal may append
    return refuse("line too long (%s characters at most)", "200");
  }
  char buf[kMaxLineLen + 3];
  std::memcpy(buf, line, rawLen + 1);

  for (const char* p = buf; *p; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 && !isSpace(*p)) return refuse("control characters are not accepted");
    if (c > 0x7e) return refuse("printable ASCII only");
  }

  // Trim both ends.
  char* start = buf;
  while (*start && isSpace(*start)) start++;
  char* end = start + std::strlen(start);
  while (end > start && isSpace(end[-1])) end--;
  *end = '\0';
  if (*start == '\0') return accept(Verb::Empty);
  if (static_cast<size_t>(end - start) > kMaxLineLen) return refuse("line too long (%s characters at most)", "200");

  // First token: the verb.
  char* verb = start;
  char* rest = verb;
  while (*rest && !isSpace(*rest)) rest++;
  if (*rest) {
    *rest++ = '\0';
    while (*rest && isSpace(*rest)) rest++;
  }
  const bool hasRest = (*rest != '\0');

  if (std::strcmp(verb, "help") == 0) {
    if (hasRest) return refuse("help takes no argument");
    return accept(Verb::Help);
  }
  if (std::strcmp(verb, "status") == 0) {
    if (hasRest) return refuse("status takes no argument");
    return accept(Verb::Status);
  }
  if (std::strcmp(verb, "wifi") == 0) {
    if (hasRest) return refuse("wifi takes no argument");
    return accept(Verb::Wifi);
  }
  if (std::strcmp(verb, "reboot") == 0) {
    if (hasRest) return refuse("reboot takes no argument");
    return accept(Verb::Reboot);
  }
  if (std::strcmp(verb, "erase") == 0) {
    if (!hasRest) return refuse("erase forgets every setting; to do it, type: erase %s", kEraseWord);
    if (std::strcmp(rest, kEraseWord) != 0) return refuse("erase: the word is '%s'", kEraseWord);
    return accept(Verb::Erase);
  }
  if (std::strcmp(verb, "get") == 0) {
    if (!hasRest) return accept(Verb::GetAll);
    for (const char* p = rest; *p; p++) {
      if (isSpace(*p)) return refuse("get takes one key: get <key>");
    }
    const KeyDef* key = findKey(rest);
    if (key == nullptr) return refuse("unknown key: %s (try 'help')", rest);
    ParseResult r = accept(Verb::Get);
    r.command.key = key;
    return r;
  }
  if (std::strcmp(verb, "set") == 0) {
    if (!hasRest) return refuse("set needs a key and a value: set <key> <value>");
    char* keyName = rest;
    char* value = keyName;
    while (*value && !isSpace(*value)) value++;
    if (*value) {
      *value++ = '\0';
      while (*value && isSpace(*value)) value++;
    }
    const KeyDef* key = findKey(keyName);
    if (key == nullptr) return refuse("unknown key: %s (try 'help')", keyName);
    if (*value == '\0') return refuse("set %s needs a value: set %s <value>", key->name, key->name);
    const size_t valueLen = std::strlen(value);
    if (valueLen > kMaxValueLen) return refuse("%s: the value is too long", key->name);
    ParseResult r = accept(Verb::Set);
    r.command.key = key;
    if (!validateValue(*key, value, r.error, sizeof r.error)) {
      r.ok = false;
      r.command.verb = Verb::Empty;
      r.command.key = nullptr;
      return r;
    }
    std::memcpy(r.command.value, value, valueLen);
    r.command.value[valueLen] = '\0';
    return r;
  }
  return refuse("unknown command: %s (try 'help')", verb);
}

}  // namespace mm

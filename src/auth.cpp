#include "auth.h"
#include "config.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

namespace {

Preferences prefs;
uint8_t  g_salt[16];
uint8_t  g_hash[32];
String   g_fresh;                 // generated-at-first-boot password, RAM only
bool     g_isFresh = false;

struct Session { char token[33]; uint32_t expires; };
Session g_sessions[WUI_MAX_SESSIONS] = {};

uint8_t  g_fails = 0;
uint32_t g_lockUntil = 0;

void hashPassword(const String &pw, const uint8_t salt[16], uint8_t out[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, salt, 16);
  mbedtls_sha256_update(&ctx, (const uint8_t *)pw.c_str(), pw.length());
  mbedtls_sha256_finish(&ctx, out);
  mbedtls_sha256_free(&ctx);
}

// Comparison whose duration does not depend on where the first difference is.
bool constEq(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; ++i) d |= a[i] ^ b[i];
  return d == 0;
}
bool constEqStr(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t d = (la == lb) ? 0 : 1;
  size_t n = la < lb ? la : lb;
  for (size_t i = 0; i < n; ++i) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

String randomString(size_t n, const char *alphabet) {
  size_t m = strlen(alphabet);
  String s;
  s.reserve(n);
  for (size_t i = 0; i < n; ++i) s += alphabet[esp_random() % m];
  return s;
}

void persist(const String &pw) {
  esp_fill_random(g_salt, sizeof(g_salt));
  hashPassword(pw, g_salt, g_hash);
  prefs.putBytes("salt", g_salt, sizeof(g_salt));
  prefs.putBytes("hash", g_hash, sizeof(g_hash));
}

}  // namespace

namespace auth {

void begin() {
  prefs.begin("wui-auth", false);
  size_t haveSalt = prefs.getBytesLength("salt");
  size_t haveHash = prefs.getBytesLength("hash");
  if (haveSalt == sizeof(g_salt) && haveHash == sizeof(g_hash)) {
    prefs.getBytes("salt", g_salt, sizeof(g_salt));
    prefs.getBytes("hash", g_hash, sizeof(g_hash));
    g_isFresh = prefs.getBool("fresh", false);
    if (g_isFresh) g_fresh = prefs.getString("freshpw", "");
    if (g_fresh.isEmpty()) g_isFresh = false;   // shown once, then forgotten
    return;
  }
  // First boot: no baked-in default — generate one and show it on the screen.
  g_fresh = randomString(10, "abcdefghijkmnopqrstuvwxyz23456789");
  g_isFresh = true;
  persist(g_fresh);
  prefs.putBool("fresh", true);
  prefs.putString("freshpw", g_fresh);
}

bool   isFreshPassword() { return g_isFresh; }
String freshPassword()   { return g_fresh; }

bool checkPassword(const String &pw) {
  uint8_t h[32];
  hashPassword(pw, g_salt, h);
  return constEq(h, g_hash, sizeof(h));
}

bool setPassword(const String &pw, String &err) {
  if (pw.length() < WUI_MIN_PASSWORD) {
    err = "password must be at least " + String(WUI_MIN_PASSWORD) + " characters";
    return false;
  }
  if (pw.length() > 96) { err = "password too long"; return false; }
  persist(pw);
  prefs.putBool("fresh", false);
  prefs.remove("freshpw");
  g_isFresh = false;
  g_fresh = "";
  dropAllSessions();
  return true;
}

String newSession() {
  String tok = randomString(32, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
  int slot = 0;
  uint32_t oldest = UINT32_MAX;
  uint32_t now = millis() / 1000;
  for (int i = 0; i < WUI_MAX_SESSIONS; ++i) {
    if (g_sessions[i].token[0] == 0 || g_sessions[i].expires <= now) { slot = i; break; }
    if (g_sessions[i].expires < oldest) { oldest = g_sessions[i].expires; slot = i; }
  }
  strlcpy(g_sessions[slot].token, tok.c_str(), sizeof(g_sessions[slot].token));
  g_sessions[slot].expires = now + WUI_SESSION_TTL_S;
  return tok;
}

bool validSession(const String &t) {
  if (t.length() != 32) return false;
  uint32_t now = millis() / 1000;
  for (auto &s : g_sessions) {
    if (s.token[0] == 0 || s.expires <= now) continue;
    if (constEqStr(s.token, t.c_str())) { s.expires = now + WUI_SESSION_TTL_S; return true; }
  }
  return false;
}

void dropSession(const String &t) {
  for (auto &s : g_sessions)
    if (s.token[0] && constEqStr(s.token, t.c_str())) { memset(&s, 0, sizeof(s)); return; }
}

void dropAllSessions() { memset(g_sessions, 0, sizeof(g_sessions)); }

bool lockedOut() { return g_lockUntil && (int32_t)(millis() - g_lockUntil) < 0; }
void noteFailure() {
  if (++g_fails >= WUI_MAX_FAILS) { g_lockUntil = millis() + WUI_LOCKOUT_MS; g_fails = 0; }
}
void noteSuccess() { g_fails = 0; g_lockUntil = 0; }

}  // namespace auth

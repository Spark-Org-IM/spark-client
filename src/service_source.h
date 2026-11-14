#pragma once

#include <optional>
#include <string>

#include "config.h"

namespace spark {

// Values advertised by the static service-source document.
struct ServiceSource {
  std::optional<std::string> home;
  std::optional<std::string> status;
  std::optional<std::string> forum;
};

struct ServiceSourceResult {
  bool ok = false;
  ServiceSource value;
  std::string error;
};

// Parses either JSON (`home_domain` + `services.HOME` / `services.STATUS` /
// `services.FORUM`) or a plain-text document whose first non-blank line is a
// bare domain. This mirrors InstanceSyncService.parse in the Android client.
ServiceSourceResult ParseServiceSource(const std::string& body);

// Fetches and parses a sync URL.
ServiceSourceResult FetchServiceSource(const std::string& url, bool insecure);

// Probes a personal token against `baseUrl` via GET /api/v2/me.json.
// Returns 1 = accepted, 0 = rejected, -1 = unreachable/server error.
int ProbeToken(const std::string& baseUrl, bool insecure,
               const std::string& token);

struct SyncOutcome {
  bool success = false;
  std::string error;
  bool mainChanged = false;
  bool statusChanged = false;
  bool forumChanged = false;
  bool mainHeld = false;
  bool statusHeld = false;
  bool signedOut = false;
  std::string mainUrl;
  std::string statusUrl;
  std::string forumUrl;
};

// Applies the sync source to `cfg` with the Android client's precedence:
// manual override > last synced value > built-in default.
//
// Automatic sync never overwrites a manual override; it only refreshes the
// cached auto value. If the effective main server changes while a token is
// stored, the token is probed against the new server first:
//  - accepted    -> apply the new address and keep the login;
//  - rejected    -> revoke on the old server, clear the local token, apply;
//  - unreachable -> hold the old address and retry on the next sync.
SyncOutcome SyncInstanceConfig(Config& cfg);

}  // namespace spark

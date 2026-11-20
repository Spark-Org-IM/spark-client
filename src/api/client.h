#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "api/models.h"

namespace spark {

// Thread-safe client: every request builds its own httplib connection, and the
// bearer token is read under a mutex. One instance may be shared by the worker
// pool.
class ApiClient {
 public:
  ApiClient(std::string baseUrl, bool insecure, std::string token = {});

  void SetToken(std::string token);
  std::string Token() const;

  // The base URL may change at runtime when a service-source sync selects a
  // different main server. Safe to call while worker threads are requesting.
  void SetBaseUrl(std::string baseUrl);
  std::string BaseUrl() const;
  std::string Origin() const;
  bool Insecure() const { return insecure_; }

  Result<SiteInfo> Site();
  Result<ListPage> List(const std::string& kind, const std::string& lang,
                        int limit, int offset, const std::string& q = {},
                        std::optional<bool> active = std::nullopt);
  Result<nlohmann::json> Item(const std::string& kind, const std::string& lang,
                              const std::string& slug);
  Result<SearchResult> Search(const std::string& lang, const std::string& q,
                              int limit = 20, int offset = 0);
  // Public home-page picks: {max, picks:[slug], items:[{slug,title}]}.
  // With a token that has news.edit it also returns `candidates`.
  Result<nlohmann::json> HomeNews();
  Result<nlohmann::json> HomeNewsAdd(const std::string& slug);
  Result<nlohmann::json> HomeNewsRemove(const std::string& slug);
  Result<nlohmann::json> HomeNewsReorder(const std::vector<std::string>& picks);
  Result<nlohmann::json> SaveNews(const std::string& slug,
                                  const nlohmann::json& data);
  Result<nlohmann::json> DeleteNews(const std::string& slug);
  Result<nlohmann::json> History(const std::string& entity,
                                 const std::string& slug);
  Result<nlohmann::json> RevertHistory(const std::string& id);
  Result<nlohmann::json> Me();

  // RFC 8628 device authorisation.
  Result<DeviceCode> StartDeviceCode(const std::vector<std::string>& scopes = {});
  Result<DeviceToken> PollDeviceToken(const std::string& deviceCode);

  Result<nlohmann::json> RevokeToken(const std::string& id);

  // Generic authenticated JSON request used by plugins and the extension
  // console. `path` is an absolute API path such as "/api/v2/news/save".
  Result<nlohmann::json> RawRequest(const std::string& method,
                                    const std::string& path,
                                    const nlohmann::json& body =
                                        nlohmann::json::object());

 private:
  using Query = std::vector<std::pair<std::string, std::string>>;

  Result<nlohmann::json> GetJson(const std::string& path,
                                 const Query& query = {});
  Result<nlohmann::json> PostJson(const std::string& path,
                                  const nlohmann::json& body);
  Result<nlohmann::json> HandleResponse(int status, const std::string& body);
  void ApplyBaseUrlLocked();

  std::string baseUrl_;
  std::string schemeHostPort_;  // e.g. https://127.0.0.1:8096
  std::string prefix_;          // optional path prefix, no trailing slash
  bool insecure_ = false;

  // Protects baseUrl_ / schemeHostPort_ / prefix_ / token_.
  mutable std::mutex mutex_;
  std::string token_;};

}  // namespace spark

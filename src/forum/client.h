#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "api/models.h"
#include "forum/models.h"

namespace spark {

// Read-only Flarum client plus the username/password token exchange. It is
// deliberately separate from ApiClient: Flarum has its own token scheme
// (`Authorization: Token ...`) and JSON:API error format.
class ForumClient {
 public:
  ForumClient(std::string baseUrl, bool insecure, std::string token = {});

  void SetToken(std::string token);
  std::string Token() const;

  void SetBaseUrl(std::string baseUrl);
  std::string BaseUrl() const;
  std::string Origin() const;
  bool Insecure() const { return insecure_; }

  // First page when `next` is empty; otherwise follows links.next.
  Result<ForumPage> Discussions(int limit = 20, const std::string& next = {});
  Result<ForumThread> Discussion(const std::string& id);
  Result<ForumUser> User(const std::string& id);
  Result<ForumAuth> Login(const std::string& identification,
                          const std::string& password);

 private:
  using Query = std::vector<std::pair<std::string, std::string>>;

  Result<nlohmann::json> GetJson(const std::string& path,
                                 const Query& query = {});
  Result<nlohmann::json> PostJson(const std::string& path,
                                  const nlohmann::json& body);
  Result<nlohmann::json> HandleResponse(int status, const std::string& body);
  std::string PathFor(const std::string& next) const;
  void ApplyBaseUrlLocked();

  std::string baseUrl_;
  std::string schemeHostPort_;
  std::string prefix_;
  bool insecure_ = false;

  mutable std::mutex mutex_;
  std::string token_;
};

}  // namespace spark

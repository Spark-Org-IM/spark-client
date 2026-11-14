#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/models.h"

namespace spark {

struct ForumTag {
  std::string id;
  std::string name;
  std::string slug;
  std::string color;
  std::string icon;
  int position = 999;
  int discussionCount = 0;
};

struct ForumUser {
  std::string id;
  std::string username;
  std::string displayName;
  std::string avatarUrl;
  std::string slug;
};

struct ForumDiscussion {
  std::string id;
  std::string title;
  std::string slug;
  std::string url;
  std::string createdAt;
  std::string lastPostedAt;
  std::string author;
  std::string firstPostId;
  std::vector<std::string> tagNames;
  int commentCount = 0;
  bool sticky = false;
  bool locked = false;
  bool approved = true;
};

struct ForumPost {
  std::string id;
  std::string createdAt;
  std::string contentType;
  std::string contentHtml;
  std::string author;
  int number = 0;
  bool edited = false;
};

struct ForumThread {
  ForumDiscussion discussion;
  std::vector<ForumTag> tags;
  std::vector<ForumPost> posts;
};

struct ForumPage {
  std::vector<ForumDiscussion> items;
  std::string next;  // links.next (absolute URL); empty means the last page
  bool hasMore = false;
};

struct ForumAuth {
  std::string token;
  std::string userId;
  ForumUser user;
};

// Parsers for Flarum's JSON:API responses. All properties are optional on
// purpose: included resources may be missing or reduced to a few fields.
ForumPage ParseForumDiscussions(const nlohmann::json& json);
ForumThread ParseForumThread(const nlohmann::json& json);
ForumUser ParseForumUser(const nlohmann::json& json);
ForumAuth ParseForumAuth(const nlohmann::json& json);

// Compact author label: displayName, then username.
std::string ForumDisplayName(const ForumUser& user);

// Turns Flarum's ISO 8601 timestamps into a compact "YYYY-MM-DD HH:MM" string.
std::string FormatForumDate(const std::string& iso);

}  // namespace spark

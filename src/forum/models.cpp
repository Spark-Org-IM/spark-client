#include "forum/models.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "util.h"

namespace spark {
namespace {

using Included = std::map<std::string, const nlohmann::json*>;

std::string Key(const std::string& type, const std::string& id) {
  return type + ":" + id;
}

std::string JsonString(const nlohmann::json& object, const char* key) {
  if (!object.is_object() || !object.contains(key) ||
      !object.at(key).is_string()) {
    return {};
  }
  return object.at(key).get<std::string>();
}

std::string JsonId(const nlohmann::json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (value.is_number_integer()) return std::to_string(value.get<long long>());
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<unsigned long long>());
  }
  return {};
}

int JsonInt(const nlohmann::json& object, const char* key, int fallback = 0) {
  if (!object.is_object() || !object.contains(key)) return fallback;
  const auto& value = object.at(key);
  if (value.is_number_integer()) return value.get<int>();
  if (value.is_string()) {
    try {
      return std::stoi(value.get<std::string>());
    } catch (const std::exception&) {
      return fallback;
    }
  }
  return fallback;
}

bool JsonBool(const nlohmann::json& object, const char* key, bool fallback) {
  if (!object.is_object() || !object.contains(key)) return fallback;
  const auto& value = object.at(key);
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number_integer()) return value.get<int>() != 0;
  return fallback;
}

Included BuildIncluded(const nlohmann::json& json) {
  Included included;
  if (!json.is_object() || !json.contains("included") ||
      !json.at("included").is_array()) {
    return included;
  }
  for (const auto& entry : json.at("included")) {
    if (!entry.is_object()) continue;
    const std::string type = JsonString(entry, "type");
    const std::string id = JsonString(entry, "id");
    if (!type.empty() && !id.empty()) included[Key(type, id)] = &entry;
  }
  return included;
}

const nlohmann::json* Find(const Included& included, const std::string& type,
                           const std::string& id) {
  if (id.empty()) return nullptr;
  const auto it = included.find(Key(type, id));
  return it == included.end() ? nullptr : it->second;
}

const nlohmann::json* Relationship(const nlohmann::json& object,
                                   const char* name) {
  if (!object.is_object() || !object.contains("relationships") ||
      !object.at("relationships").is_object()) {
    return nullptr;
  }
  const auto& relationships = object.at("relationships");
  if (!relationships.contains(name) || !relationships.at(name).is_object()) {
    return nullptr;
  }
  const auto& relationship = relationships.at(name);
  if (!relationship.contains("data") || !relationship.at("data").is_object()) {
    return nullptr;
  }
  return &relationship.at("data");
}

std::string RelatedId(const nlohmann::json& object, const char* name) {
  const nlohmann::json* data = Relationship(object, name);
  return data == nullptr ? std::string{} : JsonString(*data, "id");
}

std::vector<std::string> RelatedIds(const nlohmann::json& object,
                                    const char* name) {
  std::vector<std::string> ids;
  if (!object.is_object() || !object.contains("relationships") ||
      !object.at("relationships").is_object()) {
    return ids;
  }
  const auto& relationships = object.at("relationships");
  if (!relationships.contains(name) || !relationships.at(name).is_object()) {
    return ids;
  }
  const auto& relationship = relationships.at(name);
  if (!relationship.contains("data")) return ids;
  const auto& data = relationship.at("data");
  if (!data.is_array()) return ids;
  for (const auto& entry : data) {
    if (!entry.is_object()) continue;
    const std::string id = JsonString(entry, "id");
    if (!id.empty()) ids.push_back(id);
  }
  return ids;
}

std::string AuthorName(const Included& included, const nlohmann::json& object,
                       const char* relationship) {
  const std::string id = RelatedId(object, relationship);
  const nlohmann::json* user = Find(included, "users", id);
  if (user == nullptr) return {};
  const nlohmann::json* attributes = user;
  if (user->contains("attributes") && user->at("attributes").is_object()) {
    attributes = &user->at("attributes");
  }
  std::string name = JsonString(*attributes, "displayName");
  if (name.empty()) name = JsonString(*attributes, "username");
  return name;
}

ForumTag ParseTag(const nlohmann::json& entry) {
  ForumTag tag;
  tag.id = JsonString(entry, "id");
  if (entry.contains("attributes") && entry.at("attributes").is_object()) {
    const auto& attributes = entry.at("attributes");
    tag.name = JsonString(attributes, "name");
    tag.slug = JsonString(attributes, "slug");
    tag.color = JsonString(attributes, "color");
    tag.icon = JsonString(attributes, "icon");
    tag.position = JsonInt(attributes, "position", 999);
    tag.discussionCount = JsonInt(attributes, "discussionCount");
  }
  return tag;
}

ForumDiscussion ParseDiscussion(const nlohmann::json& entry,
                                const Included& included) {
  ForumDiscussion discussion;
  discussion.id = JsonString(entry, "id");
  if (entry.contains("attributes") && entry.at("attributes").is_object()) {
    const auto& attributes = entry.at("attributes");
    discussion.title = JsonString(attributes, "title");
    discussion.slug = JsonString(attributes, "slug");
    discussion.createdAt = JsonString(attributes, "createdAt");
    discussion.lastPostedAt = JsonString(attributes, "lastPostedAt");
    discussion.commentCount = JsonInt(attributes, "commentCount");
    discussion.sticky = JsonBool(attributes, "isSticky", false);
    discussion.locked = JsonBool(attributes, "isLocked", false);
    discussion.approved = JsonBool(attributes, "isApproved", true);
    discussion.canSticky = JsonBool(attributes, "canSticky", false);
    discussion.canLock = JsonBool(attributes, "canLock", false);
    discussion.canHide = JsonBool(attributes, "canHide", false);
    discussion.canDelete = JsonBool(attributes, "canDelete", false);
    discussion.canRename = JsonBool(attributes, "canRename", false);
    discussion.canTag = JsonBool(attributes, "canTag", false);
  }

  discussion.author = AuthorName(included, entry, "lastPostedUser");
  if (discussion.author.empty()) {
    discussion.author = AuthorName(included, entry, "user");
  }
  discussion.firstPostId = RelatedId(entry, "firstPost");
  for (const std::string& id : RelatedIds(entry, "tags")) {
    if (const nlohmann::json* tag = Find(included, "tags", id)) {
      ForumTag parsed = ParseTag(*tag);
      if (!parsed.name.empty()) discussion.tagNames.push_back(parsed.name);
      if (!parsed.slug.empty()) discussion.tagSlugs.push_back(parsed.slug);
    }
  }
  return discussion;
}

ForumPost ParsePost(const nlohmann::json& entry, const Included& included) {
  ForumPost post;
  post.id = JsonString(entry, "id");
  if (entry.contains("attributes") && entry.at("attributes").is_object()) {
    const auto& attributes = entry.at("attributes");
    post.createdAt = JsonString(attributes, "createdAt");
    post.contentType = JsonString(attributes, "contentType");
    post.contentHtml = JsonString(attributes, "contentHtml");
    post.number = JsonInt(attributes, "number");
    post.edited = !JsonString(attributes, "editedAt").empty();
  }
  post.author = AuthorName(included, entry, "user");
  return post;
}

}  // namespace

ForumPage ParseForumDiscussions(const nlohmann::json& json) {
  ForumPage page;
  const Included included = BuildIncluded(json);

  if (json.is_object() && json.contains("links") &&
      json.at("links").is_object()) {
    const auto& links = json.at("links");
    if (links.contains("next") && links.at("next").is_string()) {
      page.next = links.at("next").get<std::string>();
      page.hasMore = !page.next.empty();
    }
  }

  if (!json.is_object() || !json.contains("data") ||
      !json.at("data").is_array()) {
    return page;
  }
  for (const auto& entry : json.at("data")) {
    if (!entry.is_object()) continue;
    page.items.push_back(ParseDiscussion(entry, included));
  }
  return page;
}

ForumThread ParseForumThread(const nlohmann::json& json) {
  ForumThread thread;
  const Included included = BuildIncluded(json);

  if (!json.is_object() || !json.contains("data") ||
      !json.at("data").is_object()) {
    return thread;
  }

  const nlohmann::json& data = json.at("data");
  thread.discussion = ParseDiscussion(data, included);

  for (const std::string& id : RelatedIds(data, "tags")) {
    if (const nlohmann::json* tag = Find(included, "tags", id)) {
      thread.tags.push_back(ParseTag(*tag));
    }
  }
  std::sort(thread.tags.begin(), thread.tags.end(),
            [](const ForumTag& left, const ForumTag& right) {
              if (left.position != right.position) {
                return left.position < right.position;
              }
              return left.id < right.id;
            });

  std::vector<std::string> postIds = RelatedIds(data, "posts");
  if (postIds.empty()) {
    for (const auto& [key, entry] : included) {
      (void)key;
      if (entry->value("type", std::string{}) == "posts") {
        postIds.push_back(JsonString(*entry, "id"));
      }
    }
  }

  for (const std::string& id : postIds) {
    if (const nlohmann::json* post = Find(included, "posts", id)) {
      thread.posts.push_back(ParsePost(*post, included));
    }
  }
  std::sort(thread.posts.begin(), thread.posts.end(),
            [](const ForumPost& left, const ForumPost& right) {
              return left.number < right.number;
            });
  return thread;
}

ForumUser ParseForumUser(const nlohmann::json& json) {
  ForumUser user;
  const nlohmann::json* data = &json;
  if (json.is_object() && json.contains("data") &&
      json.at("data").is_object()) {
    data = &json.at("data");
  }
  if (!data->is_object()) return user;

  user.id = JsonString(*data, "id");
  if (data->contains("attributes") && data->at("attributes").is_object()) {
    const auto& attributes = data->at("attributes");
    user.username = JsonString(attributes, "username");
    user.displayName = JsonString(attributes, "displayName");
    user.avatarUrl = JsonString(attributes, "avatarUrl");
    user.slug = JsonString(attributes, "slug");
  }
  return user;
}

ForumAuth ParseForumAuth(const nlohmann::json& json) {
  ForumAuth auth;
  if (!json.is_object()) return auth;
  auth.token = JsonString(json, "token");
  if (json.contains("userId")) auth.userId = JsonId(json.at("userId"));
  if (json.contains("user") && json.at("user").is_object()) {
    auth.user = ParseForumUser(json.at("user"));
  }
  return auth;
}

ForumTag ParseForumTag(const nlohmann::json& json) { return ParseTag(json); }

std::string ForumDisplayName(const ForumUser& user) {
  if (!user.displayName.empty()) return user.displayName;
  if (!user.username.empty()) return user.username;
  return {};
}

std::string FormatForumDate(const std::string& iso) {
  std::string value = Trim(iso);
  if (value.empty()) return value;
  const std::size_t t = value.find('T');
  if (t != std::string::npos) value[t] = ' ';
  if (value.size() > 16) value.resize(16);
  return value;
}

}  // namespace spark

#pragma once

#include <string>
#include <vector>

#include "forum/models.h"

namespace spark {

// Renders a discussion and its comment posts as wrapped, human-readable lines.
// System event posts (contentType != "comment") are skipped.
std::vector<std::string> RenderForumThreadLines(const ForumThread& thread,
                                                int width,
                                                const std::string& lang,
                                                bool include_title = true);

}  // namespace spark

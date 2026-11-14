#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace spark {

// Turns arbitrary JSON into indented, wrapped text lines. Used by the "raw
// data" view and by any caller that needs a faithful dump.
std::vector<std::string> BuildJsonLines(const nlohmann::json& json, int width);

// Renders a news / contact / platform detail. When `raw` is true the result is
// identical to BuildJsonLines(); otherwise it is a human-readable summary.
// `lang` selects the label language ("zh-cn" or "en-us").
std::vector<std::string> RenderDetailLines(const std::string& kind,
                                           const nlohmann::json& data,
                                           int width, bool raw,
                                           const std::string& lang = "zh-cn");

// Renders /api/v2/me.json. Accepts both the envelope (`user` object) and a bare
// user object.
std::vector<std::string> RenderMeLines(const nlohmann::json& data, int width,
                                       bool raw,
                                       const std::string& lang = "zh-cn");

}  // namespace spark

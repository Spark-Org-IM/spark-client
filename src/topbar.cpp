#include "topbar.h"

#include <algorithm>
#include <unordered_set>

namespace spark {

std::vector<TopBarState> MergeTopBarStates(
    const std::vector<TopBarSpec>& specs,
    const std::vector<TopBarPref>& prefs) {
  std::vector<TopBarState> states;
  std::unordered_set<std::string> used;
  states.reserve(specs.size());

  for (const TopBarPref& pref : prefs) {
    const auto spec = std::find_if(
        specs.begin(), specs.end(),
        [&](const TopBarSpec& candidate) { return candidate.id == pref.id; });
    if (spec == specs.end() || used.count(pref.id) != 0) continue;
    states.push_back({pref.id, pref.visible, pref.collapsed});
    used.insert(pref.id);
  }

  for (const TopBarSpec& spec : specs) {
    if (used.count(spec.id) != 0) continue;
    states.push_back({spec.id, spec.visible, spec.collapsed});
  }
  return states;
}

std::vector<TopBarPref> TopBarPrefsFromStates(
    const std::vector<TopBarState>& states) {
  std::vector<TopBarPref> prefs;
  prefs.reserve(states.size());
  for (const TopBarState& state : states) {
    prefs.push_back({state.id, state.visible, state.collapsed});
  }
  return prefs;
}

}  // namespace spark

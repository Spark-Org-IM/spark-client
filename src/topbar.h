#pragma once

#include <string>
#include <vector>

namespace spark {

// One configurable entry in the frontends' top bar / navigation sidebar.
// Only the presentation flags are persisted; titles and actions are rebuilt
// from the built-in pages and the currently loaded plugins.
struct TopBarPref {
  std::string id;
  bool visible = true;
  bool collapsed = false;
};

// Default state for an entry that is discovered at runtime.
struct TopBarSpec {
  std::string id;
  bool visible = true;
  bool collapsed = false;
};

struct TopBarState {
  std::string id;
  bool visible = true;
  bool collapsed = false;
};

// Applies saved preferences to the current set of entries. Saved order wins;
// entries that do not have a preference yet (new pages or plugins) are
// appended with their defaults. Unknown saved ids are dropped.
std::vector<TopBarState> MergeTopBarStates(
    const std::vector<TopBarSpec>& specs,
    const std::vector<TopBarPref>& prefs);

std::vector<TopBarPref> TopBarPrefsFromStates(
    const std::vector<TopBarState>& states);

}  // namespace spark

#pragma once

#include <string>

namespace spark {

// Which frontend should run. `Auto` means the caller still needs to inspect the
// environment; `DetectUiMode` resolves it.
enum class UiMode { Auto, Tui, Gtk };

UiMode ParseUiMode(const std::string& value);
const char* UiModeName(UiMode mode);

bool IsTerminalSession();
bool HasGraphicalSession();

// Explicit choice > SPARK_UI > .desktop launch > SSH > tty > graphical session.
// Returns UiMode::Auto when neither a terminal nor a graphical session exists.
// Used by the `spark-client` dispatcher; the frontends themselves never guess.
UiMode DetectUiMode(UiMode requested);

}  // namespace spark

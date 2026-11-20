#pragma once

#include <glib.h>

#include <functional>
#include <utility>

namespace spark::gtk {

// Runs `fn` on the GTK main loop. Network and other blocking spark_core calls
// must run on a worker thread and marshal their UI update through this helper.
inline void PostToMainThread(std::function<void()> fn) {
  auto* task = new std::function<void()>(std::move(fn));
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      +[](gpointer data) -> gboolean {
        auto* function = static_cast<std::function<void()>*>(data);
        (*function)();
        delete function;
        return G_SOURCE_REMOVE;
      },
      task, nullptr);
}

}  // namespace spark::gtk

#pragma once

#include <pybind11/pybind11.h>

#include <string>

namespace Slic3r {

// Binds the `orca.host.ui` submodule: native message boxes, progress dialogs,
// and interactive HTML windows for plugins. All calls run on the main/UI thread
// (marshaled from the plugin worker thread) and the host owns every window.
class PluginHostUi
{
public:
    static void RegisterBindings(pybind11::module_& host);

    // Lifecycle hook: close and tear down every UI window owned by a plugin.
    // Registered via PluginLoader::subscribe_on_unload_callback so UI windows
    // are destroyed on plugin unload/reload and at app shutdown (before the
    // Python interpreter is finalized). Matches PluginLifecycleCompleteFn.
    static void close_windows_for_plugin(const std::string& plugin_key);

    // Dock any panels a plugin requested from on_load before the main window
    // existed. Call once, on the main thread, after the main window is ready.
    static void flush_pending_panels();
};

} // namespace Slic3r

#ifndef slic3r_GUI_PluginWebSurface_hpp_
#define slic3r_GUI_PluginWebSurface_hpp_

#include <string>

#include <wx/string.h>

namespace Slic3r { namespace GUI { namespace plugin_web {

// Shared building blocks for plugin-facing web surfaces (the floating
// PluginWebDialog and the docked PluginWebPanel): the theme user-script that
// makes an unstyled plugin page match the host theme, the injected
// window.orca bridge, and the file:// base URL plugin HTML is loaded against.
// Implemented in PluginWebDialog.cpp.
std::string host_theme_user_script();
const char* bridge_js();
wxString    web_base_url();

}}} // namespace Slic3r::GUI::plugin_web

#endif // slic3r_GUI_PluginWebSurface_hpp_

#ifndef slic3r_GUI_PluginWebPanel_hpp_
#define slic3r_GUI_PluginWebPanel_hpp_

#include <functional>
#include <string>

#include <nlohmann/json.hpp>
#include <wx/panel.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

// A host-owned webview surface docked as a main-window tab — the "panel"
// contribution type of the plugin extension surface, and the docked
// counterpart of PluginWebDialog. Same content pipeline (raw plugin HTML with
// the injected host theme and window.orca bridge) and the same GIL-safety
// contract: the std::function hooks must NOT capture bare pybind11 objects;
// the plugin layer wraps Python callables in a GIL-safe holder.
class PluginWebPanel : public wxPanel
{
public:
    using MessageHandler = std::function<void(const nlohmann::json& data)>;
    using CloseHandler   = std::function<void()>;

    // MAIN-THREAD ONLY. `parent` must be the main tab book
    // (MainFrame::plugin_page_parent()); the caller docks the created panel via
    // MainFrame::add_plugin_page(). on_close fires only on a page-initiated
    // close (orca.close()), not on forced teardown; on_destroyed runs from the
    // destructor on every path and must touch host-side state only (no Python).
    PluginWebPanel(wxWindow*          parent,
                   const std::string& html,
                   MessageHandler     on_message,
                   CloseHandler       on_close,
                   CloseHandler       on_destroyed);
    ~PluginWebPanel() override;

    // Detach the page from the tab book before the window goes away, so the
    // generic plugin teardown path (close_windows_for_plugin -> Destroy())
    // leaves no orphaned tab button behind.
    bool Destroy() override;

    // Convenience helpers for plugin-host callers. MAIN-THREAD ONLY.
    static void post_message(PluginWebPanel* panel, const nlohmann::json& data);
    static void request_close(PluginWebPanel* panel);

    // Push a payload to the page; delivered to handlers registered via
    // window.orca.onMessage(). MAIN-THREAD ONLY (the plugin layer marshals).
    void push_message(const nlohmann::json& data);

    bool is_open() const { return m_open; }

private:
    void on_bootstrap_event(wxWebViewEvent& event);
    void on_script_message_event(wxWebViewEvent& event);
    void load_plugin_content();
    void fire_close();

    wxWebView*     m_browser{nullptr};
    std::string    m_html;
    bool           m_content_loaded{false};
    bool           m_open{true};
    bool           m_close_fired{false};
    MessageHandler m_on_message;
    CloseHandler   m_on_close;
    CloseHandler   m_on_destroyed;
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_PluginWebPanel_hpp_

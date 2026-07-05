#include "PluginWebPanel.hpp"

#include "PluginWebSurface.hpp"
#include "MainFrame.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"

#include <boost/log/trivial.hpp>

#include <wx/sizer.h>

#include <utility>

namespace Slic3r { namespace GUI {

PluginWebPanel::PluginWebPanel(wxWindow*          parent,
                               const std::string& html,
                               MessageHandler     on_message,
                               CloseHandler       on_close,
                               CloseHandler       on_destroyed)
    : wxPanel(parent, wxID_ANY)
    , m_html(html)
    , m_on_message(std::move(on_message))
    , m_on_close(std::move(on_close))
    , m_on_destroyed(std::move(on_destroyed))
{
    // Themed background so there is no white flash before the (transparent)
    // bootstrap page and plugin HTML render — same as PluginWebDialog.
    SetBackgroundColour(wxGetApp().get_window_default_clr());

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Same bootstrap trick as PluginWebDialog: bring the webview up on a tiny
    // bundled page, then swap in the plugin HTML via SetPage once it settles.
    const wxString bootstrap_url = plugin_web::web_base_url() + wxT("dialog/PluginWebDialog/blank.html");
    m_browser = WebView::CreateWebView(this, bootstrap_url);
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "PluginWebPanel: could not create webview";
        SetSizer(sizer);
        return;
    }

    m_browser->SetBackgroundColour(wxGetApp().get_window_default_clr());
    // Inject the host theme first so its <style> sits ahead of any plugin CSS
    // in the document (later same-specificity rules win).
    m_browser->AddUserScript(wxString::FromUTF8(plugin_web::host_theme_user_script()));
    m_browser->AddUserScript(wxString::FromUTF8(plugin_web::bridge_js()));

    sizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
    SetSizer(sizer);

    // Swap in the plugin HTML once the bootstrap page settles. Bind ERROR too
    // so a missing/blocked bootstrap resource still triggers the swap.
    Bind(wxEVT_WEBVIEW_LOADED, &PluginWebPanel::on_bootstrap_event, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &PluginWebPanel::on_bootstrap_event, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PluginWebPanel::on_script_message_event, this, m_browser->GetId());
}

PluginWebPanel::~PluginWebPanel()
{
    // Runs on every destruction path; the callback only touches the host-side
    // registry (no Python), mirroring ~PluginWebDialog.
    if (m_on_destroyed)
        m_on_destroyed();
}

bool PluginWebPanel::Destroy()
{
    m_open = false;
    if (MainFrame* mainframe = wxGetApp().mainframe)
        mainframe->remove_plugin_page(this);
    return wxPanel::Destroy();
}

void PluginWebPanel::post_message(PluginWebPanel* panel, const nlohmann::json& data)
{
    if (panel != nullptr && panel->is_open())
        panel->push_message(data);
}

void PluginWebPanel::request_close(PluginWebPanel* panel)
{
    if (panel == nullptr || !panel->m_open)
        return;
    // Page/JS-initiated close: fire the plugin's on_close while the window is
    // fully alive, then tear the page down (Destroy() also undocks the tab).
    panel->m_open = false;
    panel->fire_close();
    panel->Destroy();
}

void PluginWebPanel::push_message(const nlohmann::json& data)
{
    if (!m_open || m_browser == nullptr)
        return;
    nlohmann::json envelope;
    envelope["data"] = data;
    const wxString payload = wxString::FromUTF8(
        envelope.dump(-1, ' ', false, nlohmann::json::error_handler_t::ignore));
    WebView::RunScript(m_browser, wxT("__orcaDispatch(") + payload + wxT(")"));
}

void PluginWebPanel::on_bootstrap_event(wxWebViewEvent& event)
{
    load_plugin_content();
    event.Skip();
}

void PluginWebPanel::load_plugin_content()
{
    if (m_content_loaded)
        return;
    m_content_loaded = true;
    if (m_browser != nullptr)
        m_browser->SetPage(wxString::FromUTF8(m_html), plugin_web::web_base_url());
}

void PluginWebPanel::on_script_message_event(wxWebViewEvent& event)
{
    try {
        const nlohmann::json payload = nlohmann::json::parse(into_u8(event.GetString()));
        if (payload.value("channel", std::string()) != "orca")
            return;
        const std::string    kind = payload.value("kind", std::string());
        const nlohmann::json data = payload.contains("data") ? payload["data"] : nlohmann::json();
        if (kind == "message") {
            if (m_on_message)
                m_on_message(data);
        } else if (kind == "submit" || kind == "close") {
            // A docked page has no modal result; both submit and close undock it.
            request_close(this);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << "PluginWebPanel: unparsable script message: " << e.what();
    }
}

void PluginWebPanel::fire_close()
{
    if (m_close_fired)
        return;
    m_close_fired = true;
    if (m_on_close) {
        CloseHandler cb = m_on_close;
        m_on_close     = nullptr;
        cb();
    }
}

}} // namespace Slic3r::GUI

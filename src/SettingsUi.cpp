#include "SecretStore.h"
#include "SettingsUiLabels.h"
#include "SettingsStore.h"
#include "SettingsUiModel.h"
#include "ToolLauncher.h"
#include "account/AccountPanel.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace metasequoia::linux_ime;

namespace
{
enum class Page
{
    Account,
    Appearance,
    Input,
    Helpcode,
    Shortcuts,
    Dictionary,
    Skin,
    Voice,
    ScreenKeyboard,
    Handwriting,
    Utility,
    Online,
    FloatingToolbar,
    Help,
    About,
    Feedback,
};

struct AppState
{
    SettingsStore store;
    LibsecretSecretStore secrets;
    SettingsUiModel model;
    GtkWidget *window = nullptr;
    GtkWidget *content = nullptr;
    GtkWidget *sidebar = nullptr;
    GtkWidget *account_panel = nullptr;
    Page page = Page::Appearance;
    std::unordered_map<std::string, GtkWidget *> editors;
    // A pending rebuild of the current page, queued when a control that decides another row's visibility changes. Held
    // so a burst of changes collapses into one rebuild instead of queueing an idle callback per toggle.
    guint rebuild_source = 0;
    bool dirty = false;
};

struct PageInfo
{
    Page page;
    const char *title;
};

constexpr PageInfo kPages[] = {
    {Page::Account, "账号"},
    {Page::Appearance, "外观"},
    {Page::Input, "输入"},
    {Page::Helpcode, "辅助码"},
    {Page::Shortcuts, "快捷键"},
    {Page::Dictionary, "词库"},
    {Page::Skin, "皮肤"},
    {Page::Voice, "语音输入"},
    {Page::ScreenKeyboard, "屏幕键盘"},
    {Page::Handwriting, "手写识别板"},
    {Page::Utility, "实用功能"},
    {Page::Online, "AI 辅助"},
    {Page::FloatingToolbar, "悬浮工具栏"},
    {Page::Help, "帮助"},
    {Page::About, "关于"},
    {Page::Feedback, "反馈"},
};

const char *page_title(Page page)
{
    for (const auto &info : kPages)
    {
        if (info.page == page)
        {
            return info.title;
        }
    }
    return "设置";
}

bool page_is_model_section(Page page)
{
    return page == Page::Appearance || page == Page::Input || page == Page::Helpcode || page == Page::Shortcuts ||
           page == Page::Dictionary || page == Page::Voice || page == Page::Utility || page == Page::Online;
}

SettingsUiSection model_section_for_page(Page page)
{
    switch (page)
    {
    case Page::Appearance:
        return SettingsUiSection::Appearance;
    case Page::Input:
        return SettingsUiSection::Input;
    case Page::Helpcode:
        return SettingsUiSection::Helpcode;
    case Page::Shortcuts:
        return SettingsUiSection::Shortcuts;
    case Page::Dictionary:
        return SettingsUiSection::Dictionary;
    case Page::Voice:
        return SettingsUiSection::Voice;
    case Page::Online:
        return SettingsUiSection::Online;
    case Page::Utility:
        return SettingsUiSection::DesktopTools;
    default:
        return SettingsUiSection::Appearance;
    }
}

const char *choice_label(const std::string &value)
{
    if (value == "ime")
        return "中文输入";
    if (value == "direct")
        return "直接输入";
    if (value == "quanpin")
        return "全拼";
    if (value == "shuangpin")
        return "双拼";
    if (value == "wubi")
        return "五笔";
    if (value == "japanese")
        return "日文罗马字";
    if (value == "follow")
        return "跟随中英文";
    if (value == "chinese")
        return "中文";
    if (value == "english")
        return "英文";
    if (value == "half")
        return "半角";
    if (value == "full")
        return "全角";
    if (value == "raw")
        return "原始拼音";
    if (value == "pinyin")
        return "分词拼音";
    if (value == "hidden")
        return "隐藏";
    if (value == "vertical")
        return "竖排";
    if (value == "horizontal")
        return "横排";
    if (value == "lantian")
        return "蓝天";
    if (value == "ziranma")
        return "自然码";
    if (value == "shouyou2_0")
        return "搜狗双拼 2.0";
    if (value == "shouyouplus")
        return "搜狗双拼 Plus";
    if (value == "xiaohe")
        return "小鹤";
    if (value == "disabled")
        return "关闭";
    if (value == "pin")
        return "固定";
    if (value == "halve")
        return "减半";
    if (value == "linear")
        return "线性";
    if (value == "promote")
        return "提升";
    if (value == "deepseek")
        return "DeepSeek";
    if (value == "openai")
        return "OpenAI";
    if (value == "siliconflow")
        return "SiliconFlow";
    if (value == "groq")
        return "Groq";
    if (value == "custom")
        return "自定义";
    if (value == "local")
        return "本地";
    if (value == "deeplx")
        return "DeepLX";
    return value.c_str();
}

// What the window is allowed to say about a credential. The row carries a presence marker rather than the credential,
// so the placeholder is the only feedback there is, and both texts have to say that an empty field changes nothing:
// leaving it alone is how the user edits anything else on the page without touching the stored credential. The second
// text does not claim that no credential exists -- a credential belonging to a disabled feature is never loaded, so
// this window cannot know.
const char *credential_placeholder(const SettingsUiRow &row)
{
    return row.value == kSettingsCredentialStored ? "已保存凭据，留空则不修改" : "留空则不修改已保存的凭据";
}

GtkWidget *make_editor(const SettingsUiRow &row)
{
    switch (row.control)
    {
    case SettingsControl::Boolean: {
        GtkWidget *button = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), row.value == "true");
        return button;
    }
    case SettingsControl::Choice: {
        GtkWidget *combo = gtk_combo_box_text_new();
        for (const auto &choice : row.choices)
        {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), choice.c_str(), choice_label(choice));
        }
        // A value hand-written into config.ini can sit outside the offered choices; without an entry for it the combo
        // renders blank and reports an empty value, hiding from the user which setting has to be corrected.
        if (!row.value.empty() && std::find(row.choices.begin(), row.choices.end(), row.value) == row.choices.end())
        {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), row.value.c_str(), choice_label(row.value));
        }
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), row.value.c_str());
        return combo;
    }
    case SettingsControl::Integer: {
        double minimum = 1;
        double maximum = 10;
        if (row.id == "page-size")
        {
            minimum = 3;
            maximum = 9;
        }
        else if (row.id == "connect-timeout-ms")
        {
            minimum = 100;
            maximum = 10000;
        }
        else if (row.id == "total-timeout-ms")
        {
            minimum = 500;
            maximum = 30000;
        }
        else if (row.id == "mixed-english-minimum-prefix")
        {
            minimum = 1;
            maximum = 8;
        }
        else if (row.id == "ai-candidate-limit")
        {
            minimum = 1;
            maximum = 10;
        }
        GtkWidget *spin = gtk_spin_button_new_with_range(minimum, maximum, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), std::strtod(row.value.c_str(), nullptr));
        return spin;
    }
    case SettingsControl::Secret: {
        // A credential in a visible entry is a credential X11 and every accessibility bus message can read. Turning
        // visibility off puts the entry in GTK's password mode, where the accessible object reports the invisible
        // character instead of the text, and the password purpose keeps input methods and completion from keeping a
        // copy.
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
        gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), credential_placeholder(row));
        return entry;
    }
    case SettingsControl::Text:
        return gtk_entry_new();
    }
    return gtk_entry_new();
}

std::string editor_value(const SettingsUiRow &row, GtkWidget *editor)
{
    switch (row.control)
    {
    case SettingsControl::Boolean:
        return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(editor)) ? "true" : "false";
    case SettingsControl::Choice: {
        const gchar *value = gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor));
        return value == nullptr ? std::string{} : value;
    }
    case SettingsControl::Integer:
        return std::to_string(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editor)));
    // An untouched credential entry is empty, and the model reads that as "keep the stored credential", so a page flush
    // costs a credential nothing.
    case SettingsControl::Secret:
    case SettingsControl::Text:
        return gtk_entry_get_text(GTK_ENTRY(editor));
    }
    return {};
}

void show_message(GtkWindow *parent, GtkMessageType type, const std::string &message)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", message.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void clear_container(GtkWidget *container)
{
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *node = children; node != nullptr; node = node->next)
        gtk_widget_destroy(GTK_WIDGET(node->data));
    g_list_free(children);
}

bool flush_editors(AppState &state)
{
    std::string error;
    // SettingsUiModel::set rebuilds its row vector, so iterate over a snapshot
    // rather than invalidating the range-for iterator on every edit.
    const auto rows = state.model.rows();
    for (const auto &row : rows)
    {
        const auto editor = state.editors.find(row.id);
        if (editor != state.editors.end())
        {
            const auto value = editor_value(row, editor->second);
            if (row.control == SettingsControl::Secret ? !value.empty() : value != row.value)
                state.dirty = true;
        }
        if (editor != state.editors.end() && !state.model.set(row.id, editor_value(row, editor->second), &error))
        {
            // The model reports why a value was refused but not which row produced it, and a page can hold dozens of
            // editors, so name the offending row in the dialog.
            show_message(GTK_WINDOW(state.window), GTK_MESSAGE_ERROR, std::string(row_label(row)) + "：" + error);
            return false;
        }
    }
    return true;
}

GtkWidget *make_card(const char *title, const char *body)
{
    GtkWidget *frame = gtk_frame_new(title);
    GtkWidget *label = gtk_label_new(body);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 10);
    gtk_widget_set_margin_bottom(label, 10);
    gtk_container_add(GTK_CONTAINER(frame), label);
    return frame;
}

void launch_program(AppState &state, const char *command)
{
    // Same defect the floating toolbar had: a bare name only resolves when
    // ~/.local/bin is in PATH, which it is not in a desktop session.
    const std::string executable = metasequoia::linux_ime::tool_path(command);
    gchar *argv[] = {const_cast<gchar *>(executable.c_str()), nullptr};
    GError *error = nullptr;
    if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, &error))
    {
        show_message(GTK_WINDOW(state.window), GTK_MESSAGE_ERROR,
                     error == nullptr ? "无法启动桌面工具。" : error->message);
        g_clear_error(&error);
    }
}

void launch_tools_clicked(GtkButton *, gpointer data)
{
    launch_program(*static_cast<AppState *>(data), "metasequoia-ime-tools");
}

void launch_toolbar_clicked(GtkButton *, gpointer data)
{
    launch_program(*static_cast<AppState *>(data), "metasequoia-ime-toolbar");
}

void show_page(AppState &state, Page page);

// The controls that decide whether another row on the same page is shown. Changing one has to rebuild the page, or the
// credential row it gates stays on screen until the user navigates away and back.
bool row_gates_visibility(const std::string &id)
{
    return id == "ai-enabled" || id == "voice-enabled" || id == "translation-enabled" || id == "translation-provider";
}

gboolean rebuild_page(gpointer data)
{
    auto &state = *static_cast<AppState *>(data);
    state.rebuild_source = 0;
    show_page(state, state.page);
    return G_SOURCE_REMOVE;
}

// Queued rather than rebuilt here: show_page destroys every editor on the page, including the widget whose signal is
// still being emitted.
void gating_editor_changed(GtkWidget *, gpointer data)
{
    auto &state = *static_cast<AppState *>(data);
    if (state.rebuild_source == 0)
    {
        state.rebuild_source = g_idle_add(rebuild_page, &state);
    }
}

void build_model_page(AppState &state, GtkWidget *container)
{
    const SettingsUiSection section = model_section_for_page(state.page);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);

    if (state.page == Page::Appearance)
    {
        gtk_box_pack_start(GTK_BOX(box), make_card("候选窗口预览", "ni'mf\n▌1 你们   2 你   3 泥   4 呢   5 拟"), FALSE,
                           FALSE, 0);
    }
    else if (state.page == Page::Utility)
    {
        gtk_box_pack_start(GTK_BOX(box), make_card("实用功能", "剪贴板历史等桌面工具可在独立窗口中使用。"), FALSE,
                           FALSE, 0);
    }

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gint row_index = 0;
    for (const auto &row : state.model.rows())
    {
        if (settings_section_for_id(row.id) != section || !row.visible)
            continue;
        GtkWidget *label = gtk_label_new(row_label(row));
        gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
        GtkWidget *editor = make_editor(row);
        if (row.control == SettingsControl::Text)
        {
            gtk_entry_set_text(GTK_ENTRY(editor), row.value.c_str());
            gtk_widget_set_hexpand(editor, TRUE);
        }
        else if (row.control == SettingsControl::Secret)
        {
            // Deliberately no gtk_entry_set_text: load() hydrates the stored credential into the settings struct, and
            // this is the point where it has to stop. The row carries a presence marker, make_editor turned it into a
            // placeholder, and the entry stays empty until the user types a replacement.
            gtk_widget_set_hexpand(editor, TRUE);
        }
        if (row_gates_visibility(row.id))
        {
            g_signal_connect(editor, row.control == SettingsControl::Boolean ? "toggled" : "changed",
                             G_CALLBACK(gating_editor_changed), &state);
        }
        gtk_grid_attach(GTK_GRID(grid), label, 0, row_index, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), editor, 1, row_index, 1, 1);
        state.editors.emplace(row.id, editor);
        ++row_index;
    }
    if (row_index == 0)
    {
        gtk_box_pack_start(GTK_BOX(box), make_card(page_title(state.page), "此页面暂无可编辑设置。"), FALSE, FALSE, 0);
    }
    else
    {
        gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
    }
    if (state.page == Page::Utility)
    {
        // 实用功能 is a model page because it owns the desktop-tool toggles, so the launcher has to be emitted here;
        // build_static_page never sees this page.
        GtkWidget *button = gtk_button_new_with_label("打开桌面工具");
        g_signal_connect(button, "clicked", G_CALLBACK(launch_tools_clicked), &state);
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(container), box);
}

void build_static_page(AppState &state, GtkWidget *container)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    switch (state.page)
    {
    case Page::Skin:
        gtk_box_pack_start(GTK_BOX(box), make_card("皮肤", "候选窗口颜色和字体当前跟随 GTK 主题。"), FALSE, FALSE, 0);
        break;
    case Page::ScreenKeyboard:
        gtk_box_pack_start(GTK_BOX(box), make_card("屏幕键盘", "使用桌面工具打开可点击的屏幕键盘。"), FALSE, FALSE, 0);
        break;
    case Page::Handwriting:
        gtk_box_pack_start(GTK_BOX(box), make_card("手写识别板", "使用桌面工具打开手写识别板。"), FALSE, FALSE, 0);
        break;
    case Page::FloatingToolbar:
        gtk_box_pack_start(GTK_BOX(box), make_card("悬浮工具栏", "打开置顶悬浮工具栏，快速切换输入状态。"), FALSE,
                           FALSE, 0);
        break;
    case Page::Help:
        gtk_box_pack_start(GTK_BOX(box), make_card("帮助", "在输入法菜单中切换方案；候选窗口支持数字选择和翻页。"),
                           FALSE, FALSE, 0);
        break;
    case Page::About:
        gtk_box_pack_start(GTK_BOX(box),
                           make_card("关于水杉 IME", "水杉 IME Linux：全拼、双拼、五笔、日文及在线候选支持。"), FALSE,
                           FALSE, 0);
        break;
    case Page::Feedback:
        gtk_box_pack_start(GTK_BOX(box), make_card("反馈", "欢迎通过项目仓库提交问题和功能建议。"), FALSE, FALSE, 0);
        break;
    default:
        break;
    }
    if (state.page == Page::ScreenKeyboard || state.page == Page::Handwriting)
    {
        GtkWidget *button = gtk_button_new_with_label("打开桌面工具");
        g_signal_connect(button, "clicked", G_CALLBACK(launch_tools_clicked), &state);
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    }
    if (state.page == Page::FloatingToolbar)
    {
        GtkWidget *button = gtk_button_new_with_label("打开悬浮工具栏");
        g_signal_connect(button, "clicked", G_CALLBACK(launch_toolbar_clicked), &state);
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(container), box);
}

void detach_account_panel(AppState &state)
{
    if (state.account_panel && gtk_widget_get_parent(state.account_panel))
        gtk_container_remove(GTK_CONTAINER(state.content), state.account_panel);
}

void attach_account_panel(AppState &state)
{
    if (!state.account_panel)
    {
        account::SettingsSyncHooks sync;
        sync.store = std::make_shared<SettingsStore>(state.store);
        sync.allow = [&state] { return flush_editors(state) && !state.dirty; };
        sync.busy = [&state](bool busy) { gtk_widget_set_sensitive(state.window, !busy); };
        sync.applied = [&state](const InputSettings &settings) {
            state.model = SettingsUiModel(settings);
            state.dirty = false;
        };
        state.account_panel = account::create_account_panel(
            std::make_shared<LibsecretSecretStore>(), std::make_shared<online::CurlHttpTransport>(), std::move(sync));
        g_object_ref_sink(state.account_panel);
    }
    gtk_container_add(GTK_CONTAINER(state.content), state.account_panel);
}

void show_page(AppState &state, Page page)
{
    if (!flush_editors(state))
        return;
    state.page = page;
    state.editors.clear();
    detach_account_panel(state);
    clear_container(state.content);
    if (page == Page::Account)
        attach_account_panel(state);
    else if (page_is_model_section(page))
        build_model_page(state, state.content);
    else
        build_static_page(state, state.content);
    gtk_widget_show_all(state.content);
}

void sidebar_clicked(GtkButton *button, gpointer data)
{
    const auto page = static_cast<Page>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "settings-page")));
    show_page(*static_cast<AppState *>(data), page);
}

// The provider a credential is filed under, taken from the row the model already builds for it rather than from a
// second copy of the enum-to-name mapping that could drift from the store's.
std::string model_row_value(const AppState &state, const std::string &id)
{
    for (const auto &row : state.model.rows())
    {
        if (row.id == id)
            return row.value;
    }
    return {};
}

// SettingsStore::save is the authority on whether an enabled provider has a credential and refuses the save without
// one, but its answer arrives in English and only after the whole form has been rejected, so ask the same question
// first and name the field the user has to fill in. Only "the service answered and holds nothing" counts as missing,
// which is the line save() draws too: an unreachable keyring is not proof that a credential is absent, and refusing the
// save there would be this window inventing a failure the store does not have.
bool credential_missing(const AppState &state, SecretKind kind, const std::string &provider, const std::string &token)
{
    return token.empty() && !provider.empty() && state.secrets.lookup(kind, provider).status == SecretStatus::NotFound;
}

// Rebuilds the window from what is on disk and in Secret Service, discarding whatever the editors hold. Both the reload
// button and a completed save go through here. After a save it is the only honest state to show: a credential that has
// reached Secret Service has no reason to stay in a widget for the rest of the session and would be re-stored on every
// later save, the clear rows are one-shot requests that have now been carried out, and load() is the authority on
// whether a credential row may still claim that one exists.
void reload_from_store(AppState &state, std::string *warning)
{
    state.model = SettingsUiModel(state.store.load(state.secrets, warning));
    state.dirty = false;
    state.editors.clear();
    detach_account_panel(state);
    clear_container(state.content);
    if (state.page == Page::Account)
        attach_account_panel(state);
    else if (page_is_model_section(state.page))
        build_model_page(state, state.content);
    else
        build_static_page(state, state.content);
    gtk_widget_show_all(state.content);
}

void save_clicked(GtkButton *, gpointer data)
{
    auto &state = *static_cast<AppState *>(data);
    if (!flush_editors(state))
        return;
    const InputSettings &settings = state.model.settings();
    // A pending clear request is not an unfilled field. The user has asked for exactly the state this check exists to
    // prevent being reached by accident, and SettingsStore::save accepts it for the same reason, so refusing here would
    // leave the request with no way through.
    if (settings.online.ai.enabled && !settings.online.ai_credential_cleared &&
        credential_missing(state, SecretKind::AiApiToken, model_row_value(state, "ai-provider"),
                           settings.online.ai.token))
    {
        show_message(GTK_WINDOW(state.window), GTK_MESSAGE_ERROR,
                     "尚未提供 AI API 令牌：请在「AI 辅助」页填写该服务商的令牌，或取消勾选 AI 联想。");
        return;
    }
    if (settings.voice.enabled && !settings.voice_credential_cleared &&
        credential_missing(state, SecretKind::VoiceApiToken, settings.voice.provider, settings.voice.token))
    {
        show_message(GTK_WINDOW(state.window), GTK_MESSAGE_ERROR,
                     "尚未提供语音 API 令牌：请在「语音输入」页填写该服务商的令牌，或取消勾选启用语音输入。");
        return;
    }
    std::string error;
    if (!state.store.save(settings, state.secrets, &error))
    {
        show_message(GTK_WINDOW(state.window), GTK_MESSAGE_ERROR, error);
        return;
    }
    std::string warning;
    reload_from_store(state, &warning);
    show_message(GTK_WINDOW(state.window), GTK_MESSAGE_INFO,
                 "设置已保存；运行中的引擎会自动重新加载，未生效时请重启 IBus。");
    // Reported after the confirmation rather than instead of it: the save did happen, and this says what the saved
    // configuration cannot do yet -- a credential that was just forgotten leaves its feature enabled and inactive,
    // which is the state a clear request asks for and the only place the user is told about it.
    if (!warning.empty())
        show_message(GTK_WINDOW(state.window), GTK_MESSAGE_WARNING, warning);
}

void reset_clicked(GtkButton *, gpointer data)
{
    auto &state = *static_cast<AppState *>(data);
    // Reloading deliberately skips flush_editors: the flushed values would be thrown away by the load below anyway, and
    // flushing first let a single rejected editor abort the reload, which is the user's only way back to the settings
    // on disk.
    std::string warning;
    reload_from_store(state, &warning);
    if (!warning.empty())
        show_message(GTK_WINDOW(state.window), GTK_MESSAGE_WARNING, warning);
}

GtkWidget *build_window(AppState &state)
{
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    state.window = window;
    gtk_window_set_title(GTK_WINDOW(window), "水杉 IME 设置");
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 680);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_label_new("水杉 IME 设置");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_widget_set_margin_start(header, 20);
    gtk_widget_set_margin_top(header, 14);
    gtk_widget_set_margin_bottom(header, 14);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    state.sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_size_request(state.sidebar, 170, -1);
    gtk_widget_set_margin_start(state.sidebar, 10);
    gtk_widget_set_margin_end(state.sidebar, 10);
    for (const auto &info : kPages)
    {
        GtkWidget *button = gtk_button_new_with_label(info.title);
        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        g_object_set_data(G_OBJECT(button), "settings-page", GINT_TO_POINTER(static_cast<int>(info.page)));
        g_signal_connect(button, "clicked", G_CALLBACK(sidebar_clicked), &state);
        gtk_box_pack_start(GTK_BOX(state.sidebar), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(body), state.sidebar, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    state.content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(scroll), state.content);
    gtk_box_pack_start(GTK_BOX(body), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(outer), body, TRUE, TRUE, 0);

    GtkWidget *buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
    GtkWidget *reset = gtk_button_new_with_label("重新加载");
    GtkWidget *save = gtk_button_new_with_label("保存");
    gtk_container_add(GTK_CONTAINER(buttons), reset);
    gtk_container_add(GTK_CONTAINER(buttons), save);
    gtk_box_pack_end(GTK_BOX(outer), buttons, FALSE, FALSE, 12);
    g_signal_connect(reset, "clicked", G_CALLBACK(reset_clicked), &state);
    g_signal_connect(save, "clicked", G_CALLBACK(save_clicked), &state);
    gtk_container_add(GTK_CONTAINER(window), outer);
    show_page(state, Page::Appearance);
    return window;
}

int check_settings()
{
    SettingsStore store;
    std::string warning;
    SettingsUiModel model(store.load(&warning));
    return model.rows().empty() ? 1 : 0;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--check")
        return check_settings();
    if (!gtk_init_check(&argc, &argv))
    {
        g_printerr("Unable to initialize GTK; use --check for a headless configuration check.\n");
        return 1;
    }
    AppState state;
    std::string warning;
    state.model = SettingsUiModel(state.store.load(state.secrets, &warning));
    GtkWidget *window = build_window(state);
    gtk_widget_show_all(window);
    if (!warning.empty())
        show_message(GTK_WINDOW(window), GTK_MESSAGE_WARNING, warning);
    gtk_main();
    if (state.account_panel)
    {
        gtk_widget_destroy(state.account_panel);
        g_object_unref(state.account_panel);
    }
    return 0;
}

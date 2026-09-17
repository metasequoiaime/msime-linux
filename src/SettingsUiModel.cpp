#include "SettingsUiModel.h"

#include "online/EndpointPolicy.h"

#include <glib.h>

#include <charconv>
#include <cctype>
#include <limits>
#include <string_view>

namespace metasequoia::linux_ime
{
namespace
{
void set_error(std::string *error, const char *message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

const char *bool_value(bool value)
{
    return value ? "true" : "false";
}

bool parse_bool(std::string_view value, bool &result)
{
    if (value == "true" || value == "1" || value == "yes")
    {
        result = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no")
    {
        result = false;
        return true;
    }
    return false;
}

bool parse_integer(std::string_view value, int minimum, int maximum, int &result)
{
    if (value.empty())
    {
        return false;
    }
    int parsed = 0;
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size() || parsed < minimum ||
        parsed > maximum)
    {
        return false;
    }
    result = parsed;
    return true;
}

bool valid_text(std::string_view value, std::size_t maximum, bool allow_newlines = false)
{
    if (value.size() > maximum || !g_utf8_validate(value.data(), static_cast<gssize>(value.size()), nullptr))
    {
        return false;
    }
    for (const unsigned char character : value)
    {
        if (character == 0 || character == 0x7fU ||
            (character < 0x20U && !(allow_newlines && (character == '\n' || character == '\r' || character == '\t'))))
        {
            return false;
        }
    }
    return true;
}

void add(std::vector<SettingsUiRow> &rows, const char *id, const char *label, std::string value,
         SettingsControl control, std::vector<std::string> choices = {}, bool visible = true)
{
    rows.push_back({id, label, std::move(value), control, std::move(choices), visible});
}

std::string mode_value(InputMode value)
{
    return value == InputMode::Ime ? "ime" : "direct";
}

std::string scheme_value(SchemeType value)
{
    switch (value)
    {
    case SchemeType::Quanpin:
        return "quanpin";
    case SchemeType::Shuangpin:
        return "shuangpin";
    case SchemeType::Wubi:
        return "wubi";
    case SchemeType::JapaneseRomaji:
        return "japanese";
    }
    return "quanpin";
}

std::string punctuation_value(PunctuationMode value)
{
    return value == PunctuationMode::Chinese ? "chinese" : "english";
}

std::string punctuation_lock_value(PunctuationLock value)
{
    switch (value)
    {
    case PunctuationLock::Chinese:
        return "chinese";
    case PunctuationLock::English:
        return "english";
    case PunctuationLock::Follow:
        break;
    }
    return "follow";
}

std::string width_value(CharacterWidth value)
{
    return value == CharacterWidth::Full ? "full" : "half";
}

std::string preedit_value(PreeditStyle value)
{
    switch (value)
    {
    case PreeditStyle::Raw:
        return "raw";
    case PreeditStyle::Pinyin:
        return "pinyin";
    case PreeditStyle::Hidden:
        return "hidden";
    }
    return "raw";
}

std::string candidate_window_layout_value(CandidateWindowLayout value)
{
    return value == CandidateWindowLayout::Horizontal ? "horizontal" : "vertical";
}

std::string frequency_value(FrequencyAdjustmentMode value)
{
    switch (value)
    {
    case FrequencyAdjustmentMode::Disabled:
        return "disabled";
    case FrequencyAdjustmentMode::Pin:
        return "pin";
    case FrequencyAdjustmentMode::Halve:
        return "halve";
    case FrequencyAdjustmentMode::Linear:
        return "linear";
    case FrequencyAdjustmentMode::Promote:
        return "promote";
    }
    return "disabled";
}

std::string ai_provider_value(online::AiProvider value)
{
    switch (value)
    {
    case online::AiProvider::DeepSeek:
        return "deepseek";
    case online::AiProvider::OpenAI:
        return "openai";
    case online::AiProvider::SiliconFlow:
        return "siliconflow";
    case online::AiProvider::Groq:
        return "groq";
    case online::AiProvider::Custom:
        return "custom";
    }
    return "deepseek";
}

std::string translation_provider_value(TranslationProvider value)
{
    return value == TranslationProvider::DeepLX ? "deeplx" : "local";
}

// What a credential row is allowed to say about a credential: that this run has one, never which one. load() hydrates
// the token into the struct, so the row has to reduce it to a flag here rather than pass the string on to a widget.
std::string credential_value(const std::string &token)
{
    return token.empty() ? std::string{} : std::string(kSettingsCredentialStored);
}

// A credential row and its clear companion are always shown or hidden together: a request to forget a credential that
// the configuration has no use for would name a provider the user is not looking at.
void add_credential(std::vector<SettingsUiRow> &rows, const char *id, const char *label, const char *clear_label,
                    const std::string &token, bool cleared, bool visible)
{
    // The clear row comes first so that a credential entered in the same flush is applied after it and unticks it. The
    // reverse order would let a tick discard a credential the user typed on the same visit with nothing said about it.
    add(rows, settings_credential_clear_id(id).c_str(), clear_label, bool_value(cleared), SettingsControl::Boolean, {},
        visible);
    // A ticked clear row already dropped the in-memory token, so the presence marker follows from the token alone.
    add(rows, id, label, credential_value(token), SettingsControl::Secret, {}, visible);
}

bool set_choice(std::string_view value, std::initializer_list<std::string_view> choices, std::size_t &index)
{
    std::size_t current = 0;
    for (const auto choice : choices)
    {
        if (value == choice)
        {
            index = current;
            return true;
        }
        ++current;
    }
    return false;
}
} // namespace

std::string settings_credential_clear_id(const std::string &credential_id)
{
    return credential_id + "-clear";
}

SettingsUiSection settings_section_for_id(const std::string &id)
{
    if (id == "page-size" || id == "candidate-window-layout" || id == "punctuation" || id == "punctuation-lock" ||
        id == "width" || id == "preedit-style" || id == "smart-punctuation" ||
        id == "smart-punctuation-repeat-to-chinese" || id == "paired-punctuation" || id == "bracket-paging" ||
        id == "word-to-character")
    {
        return SettingsUiSection::Appearance;
    }
    if (id == "mode" || id == "default-mode" || id == "scheme" || id == "comma-period-paging" ||
        id == "mixed-english-candidates" || id == "mixed-english-minimum-prefix" || id == "mixed-emoji-candidates" ||
        id == "mixed-kaomoji-candidates")
    {
        return SettingsUiSection::Input;
    }
    if (id == "quanpin-helpcode" || id == "quanpin-helpcode-schema" || id == "shuangpin-helpcode" ||
        id == "shuangpin-helpcode-schema" || id == "show-quanpin-helpcode" || id == "show-shuangpin-helpcode")
    {
        return SettingsUiSection::Helpcode;
    }
    if (id == "unicode-mode" || id == "super-jianpin-mode" || id == "temporary-english-mode" ||
        id == "temporary-japanese-mode" || id.rfind("switch-language-", 0) == 0)
    {
        return SettingsUiSection::Shortcuts;
    }
    if (id == "frequency-adjustment" || id == "frequency-trigger-count" || id == "frequency-linear-step")
    {
        return SettingsUiSection::Dictionary;
    }
    if (id.rfind("voice-", 0) == 0)
    {
        return SettingsUiSection::Voice;
    }
    if (id == "clipboard-history" || id == "floating-toolbar" || id == "quick-phrase-mode" || id == "date-time-mode" ||
        id == "emoji-mode" || id == "kaomoji-mode")
    {
        return SettingsUiSection::DesktopTools;
    }
    return SettingsUiSection::Online;
}

const char *settings_section_title(SettingsUiSection section)
{
    switch (section)
    {
    case SettingsUiSection::Appearance:
        return "外观";
    case SettingsUiSection::Input:
        return "输入";
    case SettingsUiSection::Helpcode:
        return "辅助码";
    case SettingsUiSection::Shortcuts:
        return "快捷功能";
    case SettingsUiSection::Dictionary:
        return "词库与调频";
    case SettingsUiSection::Voice:
        return "语音输入";
    case SettingsUiSection::DesktopTools:
        return "实用工具";
    case SettingsUiSection::Online:
        return "AI 与在线服务";
    }
    return "设置";
}

SettingsUiModel::SettingsUiModel(InputSettings settings) : settings_(std::move(settings))
{
    rebuild_rows();
}

const InputSettings &SettingsUiModel::settings() const
{
    return settings_;
}

const std::vector<SettingsUiRow> &SettingsUiModel::rows() const
{
    return rows_;
}

void SettingsUiModel::rebuild_rows()
{
    rows_.clear();
    add(rows_, "mode", "Input mode", mode_value(settings_.mode), SettingsControl::Choice, {"ime", "direct"});
    add(rows_, "default-mode", "Mode on activation", mode_value(settings_.default_mode), SettingsControl::Choice,
        {"ime", "direct"});
    add(rows_, "scheme", "Input scheme", scheme_value(settings_.scheme), SettingsControl::Choice,
        {"quanpin", "shuangpin", "wubi", "japanese"});
    add(rows_, "page-size", "Candidates per page", std::to_string(settings_.page_size), SettingsControl::Integer);
    add(rows_, "candidate-window-layout", "Candidate window layout",
        candidate_window_layout_value(settings_.candidate_window_layout), SettingsControl::Choice,
        {"vertical", "horizontal"});
    add(rows_, "punctuation", "Punctuation", punctuation_value(settings_.punctuation_mode), SettingsControl::Choice,
        {"chinese", "english"});
    add(rows_, "punctuation-lock", "Punctuation lock", punctuation_lock_value(settings_.punctuation_lock),
        SettingsControl::Choice, {"follow", "chinese", "english"});
    add(rows_, "width", "Character width", width_value(settings_.character_width), SettingsControl::Choice,
        {"half", "full"});
    add(rows_, "preedit-style", "Preedit style", preedit_value(settings_.preedit_style), SettingsControl::Choice,
        {"raw", "pinyin", "hidden"});
    add(rows_, "comma-period-paging", "Comma/period paging", bool_value(settings_.comma_period_paging),
        SettingsControl::Boolean);
    add(rows_, "word-to-character", "Word to character", bool_value(settings_.word_to_character),
        SettingsControl::Boolean);
    add(rows_, "bracket-paging", "Bracket paging", bool_value(settings_.bracket_paging), SettingsControl::Boolean);
    add(rows_, "smart-punctuation", "Smart punctuation", bool_value(settings_.smart_punctuation),
        SettingsControl::Boolean);
    add(rows_, "smart-punctuation-repeat-to-chinese", "Repeat punctuation to Chinese",
        bool_value(settings_.smart_punctuation_repeat_to_chinese), SettingsControl::Boolean);
    add(rows_, "paired-punctuation", "Paired punctuation", bool_value(settings_.paired_punctuation),
        SettingsControl::Boolean);
    add(rows_, "quanpin-helpcode", "Quanpin helpcode", bool_value(settings_.quanpin_helpcode_enabled),
        SettingsControl::Boolean);
    add(rows_, "quanpin-helpcode-schema", "Quanpin helpcode schema", settings_.quanpin_helpcode_schema,
        SettingsControl::Choice, {"lantian", "ziranma", "shouyou2_0", "shouyouplus", "xiaohe"});
    add(rows_, "shuangpin-helpcode", "Shuangpin helpcode", bool_value(settings_.shuangpin_helpcode_enabled),
        SettingsControl::Boolean);
    add(rows_, "shuangpin-helpcode-schema", "Shuangpin helpcode schema", settings_.shuangpin_helpcode_schema,
        SettingsControl::Choice, {"lantian", "ziranma", "shouyou2_0", "shouyouplus", "xiaohe"});
    add(rows_, "show-quanpin-helpcode", "Show Quanpin helpcode in candidates",
        bool_value(settings_.show_quanpin_helpcode_in_candidates), SettingsControl::Boolean);
    add(rows_, "show-shuangpin-helpcode", "Show Shuangpin helpcode in candidates",
        bool_value(settings_.show_shuangpin_helpcode_in_candidates), SettingsControl::Boolean);
    add(rows_, "frequency-adjustment", "Frequency adjustment", frequency_value(settings_.frequency_adjustment_mode),
        SettingsControl::Choice, {"disabled", "pin", "halve", "linear", "promote"});
    add(rows_, "frequency-trigger-count", "Frequency trigger count", std::to_string(settings_.frequency_trigger_count),
        SettingsControl::Integer);
    add(rows_, "frequency-linear-step", "Frequency linear step", std::to_string(settings_.frequency_linear_step),
        SettingsControl::Integer);
    add(rows_, "unicode-mode", "Unicode mode", bool_value(settings_.unicode_mode_enabled), SettingsControl::Boolean);
    add(rows_, "super-jianpin-mode", "Super jianpin mode", bool_value(settings_.super_jianpin_mode_enabled),
        SettingsControl::Boolean);
    add(rows_, "temporary-english-mode", "Temporary English mode", bool_value(settings_.temporary_english_mode_enabled),
        SettingsControl::Boolean);
    add(rows_, "temporary-japanese-mode", "Temporary Japanese mode",
        bool_value(settings_.temporary_japanese_mode_enabled), SettingsControl::Boolean);
    add(rows_, "switch-language-shift", "Switch language with Shift", bool_value(settings_.switch_language_shift),
        SettingsControl::Boolean);
    add(rows_, "switch-language-ctrl", "Switch language with Control", bool_value(settings_.switch_language_ctrl),
        SettingsControl::Boolean);
    add(rows_, "switch-language-ctrl-alt-space", "Switch language with Control+Alt+Space",
        bool_value(settings_.switch_language_ctrl_alt_space), SettingsControl::Boolean);
    add(rows_, "mixed-english-candidates", "Mixed English candidates",
        bool_value(settings_.mixed_english_candidates_enabled), SettingsControl::Boolean);
    add(rows_, "mixed-english-minimum-prefix", "English minimum prefix",
        std::to_string(settings_.mixed_english_minimum_prefix), SettingsControl::Integer);
    add(rows_, "mixed-emoji-candidates", "Mixed Emoji candidates", bool_value(settings_.mixed_emoji_candidates_enabled),
        SettingsControl::Boolean);
    add(rows_, "mixed-kaomoji-candidates", "Mixed kaomoji candidates",
        bool_value(settings_.mixed_kaomoji_candidates_enabled), SettingsControl::Boolean);
    add(rows_, "clipboard-history", "Clipboard history", bool_value(settings_.clipboard_history_enabled),
        SettingsControl::Boolean);
    add(rows_, "quick-phrase-mode", "Quick phrase mode (Shift+K)", bool_value(settings_.quick_phrase_mode_enabled),
        SettingsControl::Boolean);
    add(rows_, "date-time-mode", "Date and time mode (Shift+T)", bool_value(settings_.date_time_mode_enabled),
        SettingsControl::Boolean);
    add(rows_, "emoji-mode", "Emoji mode (Shift+E)", bool_value(settings_.emoji_mode_enabled),
        SettingsControl::Boolean);
    add(rows_, "kaomoji-mode", "Kaomoji mode (Shift+M)", bool_value(settings_.kaomoji_mode_enabled),
        SettingsControl::Boolean);
    add(rows_, "floating-toolbar", "Floating toolbar", bool_value(settings_.floating_toolbar_enabled),
        SettingsControl::Boolean);
    add(rows_, "voice-enabled", "Voice input", bool_value(settings_.voice.enabled), SettingsControl::Boolean);
    add(rows_, "voice-provider", "Voice provider", settings_.voice.provider, SettingsControl::Choice,
        {"openai", "siliconflow", "groq", "custom"});
    // Each credential row follows the provider row it belongs to, and the order matters: set() clears the in-memory
    // credential when the provider changes, so a credential typed in the same session as a provider switch has to be
    // applied after that switch rather than before it.
    add_credential(rows_, "voice-credential", "Voice credential", "Forget the stored voice credential",
                   settings_.voice.token, settings_.voice_credential_cleared, settings_.voice.enabled);
    add(rows_, "voice-endpoint", "Voice endpoint", settings_.voice.endpoint, SettingsControl::Text);
    add(rows_, "voice-model", "Voice model", settings_.voice.model, SettingsControl::Text);
    add(rows_, "voice-language", "Voice language", settings_.voice.language, SettingsControl::Text);
    add(rows_, "voice-polish-enabled", "Voice polish", bool_value(settings_.voice.polish_enabled),
        SettingsControl::Boolean);
    add(rows_, "voice-polish-endpoint", "Voice polish endpoint", settings_.voice.polish_endpoint,
        SettingsControl::Text);
    add(rows_, "voice-polish-model", "Voice polish model", settings_.voice.polish_model, SettingsControl::Text);
    add(rows_, "voice-polish-prompt", "Voice polish prompt", settings_.voice.polish_prompt, SettingsControl::Text);
    add(rows_, "cloud-enabled", "Cloud candidates", bool_value(settings_.online.cloud_candidates_enabled),
        SettingsControl::Boolean);
    add(rows_, "connect-timeout-ms", "Connect timeout (ms)", std::to_string(settings_.online.connect_timeout.count()),
        SettingsControl::Integer);
    add(rows_, "total-timeout-ms", "Total timeout (ms)", std::to_string(settings_.online.total_timeout.count()),
        SettingsControl::Integer);
    add(rows_, "ai-enabled", "AI suggestions", bool_value(settings_.online.ai.enabled), SettingsControl::Boolean);
    add(rows_, "ai-provider", "AI provider", ai_provider_value(settings_.online.ai.provider), SettingsControl::Choice,
        {"deepseek", "openai", "siliconflow", "groq", "custom"});
    add_credential(rows_, "ai-credential", "AI credential", "Forget the stored AI credential",
                   settings_.online.ai.token, settings_.online.ai_credential_cleared, settings_.online.ai.enabled);
    add(rows_, "ai-endpoint", "AI endpoint", settings_.online.ai.endpoint, SettingsControl::Text);
    add(rows_, "ai-model", "AI model", settings_.online.ai.model, SettingsControl::Text);
    add(rows_, "ai-prompt", "AI prompt", settings_.online.ai.prompt, SettingsControl::Text);
    add(rows_, "ai-candidate-limit", "AI candidate limit", std::to_string(settings_.online.ai.candidate_limit),
        SettingsControl::Integer);
    add(rows_, "translation-enabled", "Candidate translations",
        bool_value(settings_.online.candidate_translations_enabled), SettingsControl::Boolean);
    add(rows_, "translation-provider", "Translation provider",
        translation_provider_value(settings_.online.translation_provider), SettingsControl::Choice,
        {"local", "deeplx"});
    // Only DeepLX authenticates: the local backend never reads this token, and the store never files one under it.
    add_credential(rows_, "translation-credential", "Translation credential",
                   "Forget the stored translation credential", settings_.online.translation_token,
                   settings_.online.translation_credential_cleared,
                   settings_.online.candidate_translations_enabled &&
                       settings_.online.translation_provider == TranslationProvider::DeepLX);
    add(rows_, "translation-target-language", "Translation target language",
        settings_.online.translation_target_language, SettingsControl::Choice,
        {"en", "fr", "ja", "es", "ru", "de", "ko"});
    add(rows_, "translation-endpoint", "Translation endpoint", settings_.online.translation_endpoint,
        SettingsControl::Text);
}

bool SettingsUiModel::set(const std::string &id, const std::string &value, std::string *error)
{
    InputSettings candidate = settings_;
    bool parsed = true;
    const char *message = "Invalid value for setting.";
    if (id == "mode")
    {
        if (value == "ime")
            candidate.mode = InputMode::Ime;
        else if (value == "direct")
            candidate.mode = InputMode::Direct;
        else
            parsed = false;
    }
    else if (id == "default-mode")
    {
        if (value == "ime")
            candidate.default_mode = InputMode::Ime;
        else if (value == "direct")
            candidate.default_mode = InputMode::Direct;
        else
            parsed = false;
    }
    else if (id == "scheme")
    {
        if (value == "quanpin")
            candidate.scheme = SchemeType::Quanpin;
        else if (value == "shuangpin")
            candidate.scheme = SchemeType::Shuangpin;
        else if (value == "wubi")
            candidate.scheme = SchemeType::Wubi;
        else if (value == "japanese")
            candidate.scheme = SchemeType::JapaneseRomaji;
        else
            parsed = false;
    }
    else if (id == "page-size")
    {
        int number = 0;
        parsed = parse_integer(value, 3, 9, number);
        candidate.page_size = static_cast<std::size_t>(number);
    }
    else if (id == "punctuation")
    {
        if (value == "chinese")
            candidate.punctuation_mode = PunctuationMode::Chinese;
        else if (value == "english")
            candidate.punctuation_mode = PunctuationMode::English;
        else
            parsed = false;
    }
    else if (id == "punctuation-lock")
    {
        if (value == "follow")
            candidate.punctuation_lock = PunctuationLock::Follow;
        else if (value == "chinese")
            candidate.punctuation_lock = PunctuationLock::Chinese;
        else if (value == "english")
            candidate.punctuation_lock = PunctuationLock::English;
        else
            parsed = false;
    }
    else if (id == "width")
    {
        if (value == "half")
            candidate.character_width = CharacterWidth::Half;
        else if (value == "full")
            candidate.character_width = CharacterWidth::Full;
        else
            parsed = false;
    }
    else if (id == "preedit-style")
    {
        if (value == "raw")
            candidate.preedit_style = PreeditStyle::Raw;
        else if (value == "pinyin")
            candidate.preedit_style = PreeditStyle::Pinyin;
        else if (value == "hidden")
            candidate.preedit_style = PreeditStyle::Hidden;
        else
            parsed = false;
    }
    else if (id == "candidate-window-layout")
    {
        if (value == "vertical")
            candidate.candidate_window_layout = CandidateWindowLayout::Vertical;
        else if (value == "horizontal")
            candidate.candidate_window_layout = CandidateWindowLayout::Horizontal;
        else
            parsed = false;
    }
    else if (id == "comma-period-paging" || id == "word-to-character" || id == "bracket-paging" ||
             id == "smart-punctuation" || id == "smart-punctuation-repeat-to-chinese" || id == "paired-punctuation" ||
             id == "quanpin-helpcode" || id == "shuangpin-helpcode" || id == "unicode-mode" ||
             id == "super-jianpin-mode" || id == "temporary-english-mode" || id == "temporary-japanese-mode" ||
             id == "mixed-english-candidates" || id == "mixed-emoji-candidates" || id == "mixed-kaomoji-candidates" ||
             id == "clipboard-history" || id == "floating-toolbar" || id == "voice-enabled" ||
             id == "quick-phrase-mode" || id == "date-time-mode" || id == "emoji-mode" || id == "kaomoji-mode" ||
             id == "show-quanpin-helpcode" || id == "show-shuangpin-helpcode" || id.rfind("switch-language-", 0) == 0 ||
             id == "voice-polish-enabled" || id == "cloud-enabled" || id == "ai-enabled" || id == "translation-enabled")
    {
        bool value_as_bool = false;
        parsed = parse_bool(value, value_as_bool);
        if (parsed)
        {
            if (id == "comma-period-paging")
                candidate.comma_period_paging = value_as_bool;
            else if (id == "word-to-character")
                candidate.word_to_character = value_as_bool;
            else if (id == "bracket-paging")
                candidate.bracket_paging = value_as_bool;
            else if (id == "smart-punctuation")
                candidate.smart_punctuation = value_as_bool;
            else if (id == "smart-punctuation-repeat-to-chinese")
                candidate.smart_punctuation_repeat_to_chinese = value_as_bool;
            else if (id == "paired-punctuation")
                candidate.paired_punctuation = value_as_bool;
            else if (id == "quanpin-helpcode")
                candidate.quanpin_helpcode_enabled = value_as_bool;
            else if (id == "shuangpin-helpcode")
                candidate.shuangpin_helpcode_enabled = value_as_bool;
            else if (id == "unicode-mode")
                candidate.unicode_mode_enabled = value_as_bool;
            else if (id == "super-jianpin-mode")
                candidate.super_jianpin_mode_enabled = value_as_bool;
            else if (id == "temporary-english-mode")
                candidate.temporary_english_mode_enabled = value_as_bool;
            else if (id == "temporary-japanese-mode")
                candidate.temporary_japanese_mode_enabled = value_as_bool;
            else if (id == "switch-language-shift")
                candidate.switch_language_shift = value_as_bool;
            else if (id == "switch-language-ctrl")
                candidate.switch_language_ctrl = value_as_bool;
            else if (id == "switch-language-ctrl-alt-space")
                candidate.switch_language_ctrl_alt_space = value_as_bool;
            else if (id == "mixed-english-candidates")
                candidate.mixed_english_candidates_enabled = value_as_bool;
            else if (id == "mixed-emoji-candidates")
                candidate.mixed_emoji_candidates_enabled = value_as_bool;
            else if (id == "mixed-kaomoji-candidates")
                candidate.mixed_kaomoji_candidates_enabled = value_as_bool;
            else if (id == "show-quanpin-helpcode")
                candidate.show_quanpin_helpcode_in_candidates = value_as_bool;
            else if (id == "show-shuangpin-helpcode")
                candidate.show_shuangpin_helpcode_in_candidates = value_as_bool;
            else if (id == "quick-phrase-mode")
                candidate.quick_phrase_mode_enabled = value_as_bool;
            else if (id == "date-time-mode")
                candidate.date_time_mode_enabled = value_as_bool;
            else if (id == "emoji-mode")
                candidate.emoji_mode_enabled = value_as_bool;
            else if (id == "kaomoji-mode")
                candidate.kaomoji_mode_enabled = value_as_bool;
            else if (id == "clipboard-history")
                candidate.clipboard_history_enabled = value_as_bool;
            else if (id == "floating-toolbar")
                candidate.floating_toolbar_enabled = value_as_bool;
            else if (id == "voice-enabled")
                candidate.voice.enabled = value_as_bool;
            else if (id == "voice-polish-enabled")
                candidate.voice.polish_enabled = value_as_bool;
            else if (id == "cloud-enabled")
                candidate.online.cloud_candidates_enabled = value_as_bool;
            else if (id == "ai-enabled")
                candidate.online.ai.enabled = value_as_bool;
            else if (id == "translation-enabled")
                candidate.online.candidate_translations_enabled = value_as_bool;
        }
    }
    else if (id == "quanpin-helpcode-schema" || id == "shuangpin-helpcode-schema")
    {
        std::size_t index = 0;
        parsed = set_choice(value, {"lantian", "ziranma", "shouyou2_0", "shouyouplus", "xiaohe"}, index);
        if (parsed)
        {
            if (id == "quanpin-helpcode-schema")
                candidate.quanpin_helpcode_schema = value;
            else
                candidate.shuangpin_helpcode_schema = value;
        }
    }
    else if (id == "frequency-adjustment")
    {
        std::size_t index = 0;
        parsed = set_choice(value, {"disabled", "pin", "halve", "linear", "promote"}, index);
        if (parsed)
            candidate.frequency_adjustment_mode = static_cast<FrequencyAdjustmentMode>(index);
    }
    else if (id == "frequency-trigger-count" || id == "frequency-linear-step")
    {
        int number = 0;
        parsed = parse_integer(value, 1, 10, number);
        if (parsed)
        {
            if (id == "frequency-trigger-count")
                candidate.frequency_trigger_count = number;
            else
                candidate.frequency_linear_step = number;
        }
    }
    else if (id == "mixed-english-minimum-prefix")
    {
        int number = 0;
        parsed = parse_integer(value, 1, 8, number);
        candidate.mixed_english_minimum_prefix = static_cast<std::size_t>(number);
    }
    else if (id == "connect-timeout-ms" || id == "total-timeout-ms")
    {
        int number = 0;
        parsed = id == "connect-timeout-ms" ? parse_integer(value, 100, 10000, number)
                                            : parse_integer(value, 500, 30000, number);
        if (parsed)
        {
            if (id == "connect-timeout-ms")
                candidate.online.connect_timeout = std::chrono::milliseconds(number);
            else
                candidate.online.total_timeout = std::chrono::milliseconds(number);
        }
    }
    else if (id == "ai-provider")
    {
        std::size_t index = 0;
        parsed = set_choice(value, {"deepseek", "openai", "siliconflow", "groq", "custom"}, index);
        if (parsed)
        {
            candidate.online.ai.provider = static_cast<online::AiProvider>(index);
            // A credential belongs to the provider it was filed under. The token in memory was hydrated for the
            // provider that was selected until now, and SettingsStore::save refuses to re-file it under a different
            // provider's name, so carrying it across a switch can only turn into a save failure. Dropping it leaves an
            // empty token, which the store reads as "keep whatever is stored for the newly selected provider" -- and if
            // the user types one in the same session it is applied after this row, because the credential row follows
            // the provider row.
            if (candidate.online.ai.provider != settings_.online.ai.provider)
                candidate.online.ai.token.clear();
        }
    }
    else if (id == "voice-provider")
    {
        std::size_t index = 0;
        parsed = set_choice(value, {"openai", "siliconflow", "groq", "custom"}, index);
        if (parsed)
        {
            candidate.voice.provider = value;
            // The voice twin of the AI switch above: the store files a voice credential under the provider name too,
            // and refuses to move one.
            if (candidate.voice.provider != settings_.voice.provider)
                candidate.voice.token.clear();
        }
    }
    else if (id == "ai-candidate-limit")
    {
        int number = 0;
        parsed = parse_integer(value, 1, 10, number);
        candidate.online.ai.candidate_limit = static_cast<std::size_t>(number);
    }
    else if (id == "translation-provider")
    {
        if (value == "local")
            candidate.online.translation_provider = TranslationProvider::Local;
        else if (value == "deeplx")
            candidate.online.translation_provider = TranslationProvider::DeepLX;
        else
            parsed = false;
        // Only DeepLX authenticates, so this drops a hydrated DeepLX token when the user goes back to the local backend
        // rather than letting it be filed under a provider that never reads it.
        if (parsed && candidate.online.translation_provider != settings_.online.translation_provider)
            candidate.online.translation_token.clear();
    }
    else if (id == "translation-target-language")
    {
        std::size_t index = 0;
        parsed = set_choice(value, {"en", "fr", "ja", "es", "ru", "de", "ko"}, index);
        if (parsed)
            candidate.online.translation_target_language = value;
    }
    else if (id == settings_credential_clear_id("ai-credential") ||
             id == settings_credential_clear_id("voice-credential") ||
             id == settings_credential_clear_id("translation-credential"))
    {
        bool value_as_bool = false;
        parsed = parse_bool(value, value_as_bool);
        if (parsed)
        {
            // The in-memory credential goes with the request. load() hydrates the stored one into this struct, so
            // leaving it in place would have SettingsStore::save re-file the very credential the user asked it to
            // forget; the store reads a cleared flag as authoritative for the same reason.
            if (id == settings_credential_clear_id("ai-credential"))
            {
                candidate.online.ai_credential_cleared = value_as_bool;
                if (value_as_bool)
                    candidate.online.ai.token.clear();
            }
            else if (id == settings_credential_clear_id("voice-credential"))
            {
                candidate.voice_credential_cleared = value_as_bool;
                if (value_as_bool)
                    candidate.voice.token.clear();
            }
            else
            {
                candidate.online.translation_credential_cleared = value_as_bool;
                if (value_as_bool)
                    candidate.online.translation_token.clear();
            }
        }
    }
    else if (id == "ai-credential" || id == "voice-credential" || id == "translation-credential")
    {
        // An empty entry is the untouched one: the window never renders a stored credential back into the widget, so
        // "nothing typed" has to mean "keep whatever Secret Service already holds" rather than "this provider has no
        // credential". SettingsStore::save reads an empty token the same way, which is what makes leaving the field
        // alone a safe way to edit anything else on the page.
        parsed = online::token_allowed(value);
        if (!parsed)
        {
            // Deliberately says nothing about the value it rejected: this message is interpolated into a dialog by the
            // caller, and a credential must not reach a diagnostic.
            message = "This credential contains characters that cannot be sent in a request header.";
        }
        else if (!value.empty())
        {
            // A credential entered here unticks the clear row that precedes it. Storing a replacement already forgets
            // whatever was there, so the removal the flag asks for would only undo the save the user just made, and the
            // two rows would otherwise disagree with no way for the window to say which one won.
            if (id == "ai-credential")
            {
                candidate.online.ai.token = value;
                candidate.online.ai_credential_cleared = false;
            }
            else if (id == "voice-credential")
            {
                candidate.voice.token = value;
                candidate.voice_credential_cleared = false;
            }
            else
            {
                candidate.online.translation_token = value;
                candidate.online.translation_credential_cleared = false;
            }
        }
    }
    else if (id == "ai-endpoint" || id == "ai-model" || id == "ai-prompt" || id == "translation-endpoint" ||
             id == "voice-endpoint" || id == "voice-model" || id == "voice-language" || id == "voice-polish-endpoint" ||
             id == "voice-polish-model" || id == "voice-polish-prompt")
    {
        const bool is_endpoint = id == "ai-endpoint" || id == "translation-endpoint" || id == "voice-endpoint" ||
                                 id == "voice-polish-endpoint";
        const bool is_model = id == "ai-model" || id == "voice-model" || id == "voice-polish-model";
        const std::size_t maximum = is_endpoint ? 2048 : (is_model ? 256 : (id == "voice-language" ? 32 : 8192));
        // SettingsStore::valid_voice_settings refuses to persist an empty voice model, language, polish model or polish
        // prompt, and refuses an empty endpoint while the matching feature is enabled. Accepting those values here
        // would move the failure to save time, where the store can only report an unattributable "Input settings were
        // outside the supported range." and drops every other pending edit with it.
        const bool required =
            id == "voice-model" || id == "voice-language" || id == "voice-polish-model" || id == "voice-polish-prompt";
        // The enable toggles are flushed before their endpoints, so checking the already applied toggle attributes the
        // failure to whichever of the two rows the user actually left inconsistent.
        const bool required_while_enabled = (id == "voice-endpoint" && candidate.voice.enabled) ||
                                            (id == "voice-polish-endpoint" && candidate.voice.polish_enabled);
        parsed = valid_text(value, maximum, id == "ai-prompt" || id == "voice-polish-prompt") &&
                 (!is_endpoint || value.empty() || value.rfind("https://", 0) == 0);
        if (parsed && value.empty() && (required || required_while_enabled))
        {
            parsed = false;
            message = required_while_enabled ? "This setting cannot be empty while the feature is enabled."
                                             : "This setting cannot be empty.";
        }
        if (parsed)
        {
            if (id == "ai-endpoint")
                candidate.online.ai.endpoint = value;
            else if (id == "ai-model")
                candidate.online.ai.model = value;
            else if (id == "ai-prompt")
                candidate.online.ai.prompt = value;
            else if (id == "translation-endpoint")
                candidate.online.translation_endpoint = value;
            else if (id == "voice-endpoint")
                candidate.voice.endpoint = value;
            else if (id == "voice-model")
                candidate.voice.model = value;
            else if (id == "voice-language")
                candidate.voice.language = value;
            else if (id == "voice-polish-endpoint")
                candidate.voice.polish_endpoint = value;
            else if (id == "voice-polish-model")
                candidate.voice.polish_model = value;
            else
                candidate.voice.polish_prompt = value;
        }
    }
    else
    {
        parsed = false;
    }

    if (!parsed)
    {
        set_error(error, message);
        return false;
    }
    settings_ = std::move(candidate);
    rebuild_rows();
    if (error != nullptr)
        error->clear();
    return true;
}
} // namespace metasequoia::linux_ime

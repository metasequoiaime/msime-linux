#include "CloudSettingsMapper.h"
#include "SettingsUiLabels.h"
#include <algorithm>
#include <charconv>

namespace metasequoia::linux_ime::account
{
namespace
{
struct Binding
{
    const char *local;
    const char *cloud;
};
// Explicit shared semantics only. No credentials, endpoints, paths, clipboard
// consent, or transient input mode is eligible for automatic serialization.
constexpr Binding bindings[] = {
    {"page-size", "appearance.page_size"},
    {"candidate-window-layout", "appearance.candidate_window_layout"},
    {"comma-period-paging", "general.paging_comma_period"},
    {"bracket-paging", "general.paging_brackets"},
    {"word-to-character", "input.word_to_character"},
    {"smart-punctuation", "input.smart_punctuation"},
    {"smart-punctuation-repeat-to-chinese", "input.smart_punctuation_repeat_to_chinese"},
    {"paired-punctuation", "input.paired_punctuation"},
    {"quanpin-helpcode", "helpcode.quanpin_helpcode"},
    {"quanpin-helpcode-schema", "helpcode.quanpin_helpcode_schema"},
    {"shuangpin-helpcode", "helpcode.shuangpin_helpcode"},
    {"shuangpin-helpcode-schema", "helpcode.shuangpin_helpcode_schema"},
    {"show-quanpin-helpcode", "helpcode.show_qp_helpcode_in_candidate_window"},
    {"show-shuangpin-helpcode", "helpcode.show_sp_helpcode_in_candidate_window"},
    {"frequency-trigger-count", "frequency_adjustment.trigger_count"},
    {"frequency-linear-step", "frequency_adjustment.linear_step"},
    {"unicode-mode", "utility.unicode_mode"},
    {"super-jianpin-mode", "utility.jianpin_mode"},
    {"switch-language-shift", "keybindings.switch_language_shift"},
    {"switch-language-ctrl", "keybindings.switch_language_ctrl"},
    {"switch-language-ctrl-alt-space", "keybindings.switch_language_ctrl_alt_space"},
    {"mixed-english-candidates", "general.cn_en_mixed_input"},
    {"mixed-english-minimum-prefix", "general.cn_en_mixed_input_min_chars"},
    {"mixed-emoji-candidates", "general.emoji_mixed_input"},
    {"mixed-kaomoji-candidates", "general.kaomoji_mixed_input"},
    {"quick-phrase-mode", "utility.quick_phrase"},
    {"date-time-mode", "utility.date_time_mode"},
    {"emoji-mode", "utility.emoji_mode"},
    {"kaomoji-mode", "utility.kaomoji_mode"},
    {"floating-toolbar", "general.floating_toolbar"},
    {"voice-enabled", "voice_input.voice_input"},
    {"voice-provider", "voice_input.asr_provider"},
    {"voice-model", "voice_input.asr_model"},
    {"voice-language", "voice_input.language"},
    {"voice-polish-enabled", "voice_input.polish_text"},
    {"voice-polish-prompt", "voice_input.polish_prompt"},
    {"cloud-enabled", "general.cloud_candidates"},
    {"ai-enabled", "ai_assistant.enabled"},
    {"ai-provider", "ai_assistant.provider"},
    {"ai-model", "ai_assistant.model"},
    {"ai-prompt", "ai_assistant.prompt"},
    {"ai-candidate-limit", "ai_assistant.candidate_limit"},
    {"translation-enabled", "general.candidate_translations"},
};
const SettingsUiRow &row(const SettingsUiModel &model, const char *id)
{
    const auto it = std::find_if(model.rows().begin(), model.rows().end(),
                                 [&](const SettingsUiRow &value) { return value.id == id; });
    if (it == model.rows().end())
        throw std::logic_error("Missing cloud settings binding");
    return *it;
}
PreferenceValue value(const SettingsUiRow &row)
{
    if (row.control == SettingsControl::Boolean)
        return row.value == "true";
    if (row.control == SettingsControl::Integer)
    {
        std::int64_t number = 0;
        const auto parsed = std::from_chars(row.value.data(), row.value.data() + row.value.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != row.value.data() + row.value.size())
            throw Failure(0);
        return number;
    }
    if (row.control == SettingsControl::Text || row.control == SettingsControl::Choice)
        return row.value;
    throw std::logic_error("Credential row cannot be synchronized");
}
std::string local_value(const SettingsUiRow &row, const PreferenceValue &value)
{
    if (row.control == SettingsControl::Boolean && std::holds_alternative<bool>(value))
        return std::get<bool>(value) ? "true" : "false";
    if (row.control == SettingsControl::Integer && std::holds_alternative<std::int64_t>(value))
        return std::to_string(std::get<std::int64_t>(value));
    if ((row.control == SettingsControl::Text || row.control == SettingsControl::Choice) &&
        std::holds_alternative<std::string>(value))
        return std::get<std::string>(value);
    throw Failure(400);
}
} // namespace
std::string CloudSettingsMapper::field_label(const std::string &field)
{
    static const SettingsUiModel model;
    for (const auto &binding : bindings)
        if (field == binding.cloud)
            return row_label(row(model, binding.local));
    return field;
}
std::map<std::string, PreferenceValue> CloudSettingsMapper::export_settings(const InputSettings &local)
{
    const SettingsUiModel model(local);
    std::map<std::string, PreferenceValue> result;
    for (const auto &binding : bindings)
        result.emplace(binding.cloud, value(row(model, binding.local)));
    return result;
}
Preferences CloudSettingsMapper::prepare_upload(const InputSettings &local, const Preferences &remote)
{
    auto result = remote;
    for (const auto &[key, value] : export_settings(local))
        result.settings[key] = value;
    return result;
}
SettingsPreview CloudSettingsMapper::prepare_download(const InputSettings &local, const Preferences &remote)
{
    const auto exported = export_settings(local);
    // Model IDs are provider-specific. A cloud import must never silently
    // redirect an existing local endpoint or move its credentials to a provider.
    for (const auto &pair : {std::pair{"ai_assistant.provider", "ai_assistant.model"},
                             std::pair{"voice_input.asr_provider", "voice_input.asr_model"}})
    {
        const auto provider = remote.settings.find(pair.first);
        const auto model = remote.settings.find(pair.second);
        if (provider != remote.settings.end() && provider->second != exported.at(pair.first))
            throw Failure(400);
        if (model != remote.settings.end() && model->second != exported.at(pair.second) &&
            provider == remote.settings.end())
            throw Failure(400);
    }
    SettingsUiModel candidate(local);
    SettingsPreview preview{local, {}, {}};
    for (const auto &[key, value] : remote.settings)
    {
        const auto binding = std::find_if(std::begin(bindings), std::end(bindings),
                                          [name = key](const Binding &item) { return name == item.cloud; });
        if (binding == std::end(bindings))
        {
            preview.unsupported_fields.push_back(key);
            continue;
        }
        const auto &current = row(candidate, binding->local);
        const auto before = account::value(current);
        const auto text = local_value(current, value);
        if (before != value)
        {
            if (!candidate.set(binding->local, text))
                throw Failure(400);
            preview.changes.push_back({key, before, value});
        }
    }
    preview.settings = candidate.settings();
    return preview;
}
} // namespace metasequoia::linux_ime::account

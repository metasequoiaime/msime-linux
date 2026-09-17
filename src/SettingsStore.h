#pragma once

#include "InputController.h"
#include "SecretStore.h"
#include "VoiceInput.h"
#include "online/AiSuggestionProvider.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <optional>

namespace metasequoia::linux_ime
{
enum class TranslationProvider
{
    Local,
    DeepLX,
};

enum class CandidateWindowLayout
{
    Vertical,
    Horizontal,
};

struct OnlineSettings
{
    bool cloud_candidates_enabled = true;
    online::AiSuggestionConfig ai;
    bool candidate_translations_enabled = true;
    TranslationProvider translation_provider = TranslationProvider::Local;
    std::string translation_target_language = "en";
    std::string translation_endpoint;
    std::string translation_token;
    std::chrono::milliseconds connect_timeout{2500};
    std::chrono::milliseconds total_timeout{8000};
    // Whether Secret Service handed out the credential an enabled provider needs. These are runtime state that save()
    // never writes to config.ini: a locked or absent keyring is not the user turning a provider off, and persisting it
    // as such silently loses the intent for good. `enabled` is the user's persisted intent and only the user changes
    // it; these flags are this run's answer about whether that intent can be acted on. Consumers gate a request on
    // `enabled && <provider>_credential_available`, and the credential-aware load() is the only thing that clears them.
    // AI needs a credential either way, so both a service that could not be reached (SecretStatus::Unavailable) and one
    // that answered with nothing stored (SecretStatus::NotFound) clear its flag; translation only clears on
    // Unavailable, because a self-hosted DeepLX endpoint may legitimately accept unauthenticated requests.
    bool ai_credential_available = true;
    bool translation_credential_available = true;
    // A request to forget the stored credential rather than a fact about it, and the one thing an empty token cannot
    // say: the token fields are write-only, so "nothing typed" has to mean "keep whatever Secret Service already
    // holds" or an untouched form would erase a credential on every save. Runtime only, like the flags above -- save()
    // acts on it and never writes it to config.ini, and load() never sets it. Acting on one removes the credential and
    // leaves `enabled` alone, which is the whole point: turning the feature off also removes the credential, but it
    // discards a configuration the user may still want. What is left is the state load() already reports when the
    // keyring answers NotFound -- enabled, configured, and inactive until a credential is added.
    bool ai_credential_cleared = false;
    bool translation_credential_cleared = false;
};

struct InputSettings
{
    InputMode mode = InputMode::Ime;
    // The mode the engine enters when the input method is activated, matching the Windows
    // default_ime_mode. `mode` above stays the running state within an activation.
    InputMode default_mode = InputMode::Ime;
    SchemeType scheme = SchemeType::Quanpin;
    std::size_t page_size = 9;
    // The candidate window orientation, matching the Windows appearance.candidate_window_layout.
    // Vertical is what IBus draws by default, so it is the default here too.
    CandidateWindowLayout candidate_window_layout = CandidateWindowLayout::Vertical;
    PunctuationMode punctuation_mode = PunctuationMode::Chinese;
    PunctuationLock punctuation_lock = PunctuationLock::Follow;
    CharacterWidth character_width = CharacterWidth::Half;
    bool comma_period_paging = true;
    bool word_to_character = true;
    bool bracket_paging = false;
    bool smart_punctuation = true;
    bool smart_punctuation_repeat_to_chinese = true;
    bool paired_punctuation = true;
    PreeditStyle preedit_style = PreeditStyle::Raw;
    bool quanpin_helpcode_enabled = true;
    std::string quanpin_helpcode_schema = "lantian";
    bool shuangpin_helpcode_enabled = true;
    std::string shuangpin_helpcode_schema = "lantian";
    // Whether the helpcode hint appears beside each candidate, matching the Windows
    // show_qp/sp_helpcode_in_candidate_window pair. Only meaningful when the matching helpcode
    // above is enabled.
    bool show_quanpin_helpcode_in_candidates = true;
    bool show_shuangpin_helpcode_in_candidates = true;
    FrequencyAdjustmentMode frequency_adjustment_mode = FrequencyAdjustmentMode::Promote;
    int frequency_trigger_count = 1;
    int frequency_linear_step = 1;
    bool unicode_mode_enabled = true;
    bool super_jianpin_mode_enabled = true;
    // The engine defaults every local mode on and Linux only overrode four of them, so these
    // four worked but could not be turned off. Names and defaults follow the Windows utility
    // section; these are the dedicated Shift+key modes, not the mixed candidates above.
    bool quick_phrase_mode_enabled = true;
    bool date_time_mode_enabled = true;
    bool emoji_mode_enabled = true;
    bool kaomoji_mode_enabled = true;
    bool temporary_english_mode_enabled = true;
    bool temporary_japanese_mode_enabled = true;
    bool mixed_english_candidates_enabled = true;
    std::size_t mixed_english_minimum_prefix = 2;
    bool mixed_emoji_candidates_enabled = true;
    bool mixed_kaomoji_candidates_enabled = false;
    bool clipboard_history_enabled = false;
    bool floating_toolbar_enabled = true;
    // Chinese/English toggles, matching the Windows keybindings section. Ctrl+Space is claimed by
    // the desktop rather than by the engine on both platforms, so it is not offered here either.
    bool switch_language_shift = true;
    bool switch_language_ctrl = false;
    bool switch_language_ctrl_alt_space = true;
    VoiceInputConfig voice;
    // The voice twin of the online credential flags above: runtime only, never persisted, and cleared by either
    // non-Found answer because the voice provider authenticates.
    bool voice_credential_available = true;
    // The voice twin of OnlineSettings::ai_credential_cleared.
    bool voice_credential_cleared = false;
    OnlineSettings online;
};

// The name a provider is known by: the value written to config.ini, and the name its credential is filed under in
// Secret Service. Null for a value outside the enum. Shared rather than mapped again by each caller, because a
// credential stored under one spelling and looked up under another simply is not found, which reads to the user as a
// provider they never configured. Returns a string literal, so the result outlives any caller.
const char *ai_provider_name(online::AiProvider provider);
const char *translation_provider_name(TranslationProvider provider);

// Content identity of config.ini as it is on disk right now, in the same form save() reports through its `digest`
// parameter. Empty when the file cannot be read, which never compares equal to a digest save() produced.
std::string settings_file_digest(const std::filesystem::path &path);

struct SettingsFileVersion
{
    bool missing = false;
    std::string digest;
    bool operator==(const SettingsFileVersion &other) const
    {
        return missing == other.missing && digest == other.digest;
    }
};

class SettingsStore
{
  public:
    SettingsStore();
    explicit SettingsStore(std::filesystem::path config_home);

    InputSettings load(std::string *warning = nullptr) const;
    // Credential overloads may block on Secret Service and are not main-loop APIs.
    InputSettings load(const SecretStore &secret_store, std::string *warning = nullptr) const;
    // `digest` receives the identity of the exact bytes this call serialized, so a caller watching config.ini can tell
    // its own write apart from somebody else's by content. Timestamp and size cannot: these settings are small, most
    // edits do not change the byte count, and the file is replaced by an atomic rename, so an external write can land
    // in the same filesystem timestamp tick at the same size. It is filled in as soon as the bytes exist, including on
    // the failure paths after that point -- the rename may already have replaced the file when one of them reports an
    // error -- and left untouched when the call gives up before serializing anything.
    bool save(const InputSettings &settings, std::string *error = nullptr, std::string *digest = nullptr) const;
    bool save(const InputSettings &settings, SecretStore &secret_store, std::string *error = nullptr,
              std::string *digest = nullptr) const;
    // Missing is a valid first-save baseline; unreadable is nullopt.
    std::optional<SettingsFileVersion> version() const;
    // Serializes with all SettingsStore saves; no credential changes on conflict.
    bool save_if_unchanged(const InputSettings &settings, SecretStore &secret_store,
                           const SettingsFileVersion &expected, std::string *error = nullptr) const;
    const std::filesystem::path &config_path() const;

  private:
    bool save_unlocked(const InputSettings &settings, std::string *error, std::string *digest) const;
    bool save_with_secrets_unlocked(const InputSettings &settings, SecretStore &secret_store, std::string *error,
                                    std::string *digest) const;
    std::filesystem::path config_path_;
};
} // namespace metasequoia::linux_ime

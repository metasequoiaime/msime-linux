# Metasequoia IME for Linux

> **This repository is archived and no longer accepts issues or pull requests.** The Linux frontend has been merged into [metasequoiaime/msime](https://github.com/metasequoiaime/msime); development, releases and bug reports all continue there. This repository stays read-only for history — existing release assets remain downloadable but will not be updated.

[中文 README](README.md) · [Website](https://msime.app) · [Docs](https://msime.app/docs/) · [Privacy](PRIVACY.md) · [Packaging](docs/packaging.md)

<!-- badges:start -->
[![CI](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Linux/ci.yml?branch=develop&label=CI)](https://github.com/metasequoiaime/MSIME-Linux/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Linux/codeql.yml?branch=develop&label=CodeQL)](https://github.com/metasequoiaime/MSIME-Linux/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/metasequoiaime/MSIME-Linux?include_prereleases&label=release)](https://github.com/metasequoiaime/MSIME-Linux/releases)
[![License](https://img.shields.io/github/license/metasequoiaime/MSIME-Linux)](LICENSE)
[![Stars](https://img.shields.io/github/stars/metasequoiaime/MSIME-Linux?style=flat)](https://github.com/metasequoiaime/MSIME-Linux/stargazers)
<!-- badges:end -->

An IBus input method for Chinese and Japanese, sharing its C++ conversion engine with the Windows, macOS and iOS frontends. The engine lives in [MSIME-Engine](https://github.com/metasequoiaime/MSIME-Engine) and is pinned here as a submodule.

**This is a public beta.** Interfaces and configuration keys can still change between minor versions.

## Install

Each release attaches `.deb`, `.rpm` and `.tar.gz` packages for amd64 and arm64: [Releases](https://github.com/metasequoiaime/MSIME-Linux/releases). Install the one matching your distribution, restart IBus, then add "Metasequoia IME" in your desktop's input source settings.

Packages are not signed. Every release asset shows its SHA256 on the release page; verify with `sha256sum` before installing.

Note the two channels. Automatic per-merge builds are marked **Pre-release**; deliberately curated ones are ordinary releases. Prefer the latter.

**Not yet in any distribution repository.** If you want to package it for yours, [docs/packaging.md](docs/packaging.md) has the dependency list, the offline build path and the licensing situation for the dictionary data.

## What it does

- Chinese input: full pinyin, double pinyin, Wubi 86, with live scheme switching
- Japanese input: romaji, as a scheme and as a temporary mode from Chinese
- Helpcode (形码) filtering on pinyin schemes
- Mixed Chinese-English input, emoji and kaomoji candidates, a dedicated English candidate mode
- English-Chinese glosses from the local dictionaries
- GTK settings application, clipboard history with a panel, on-screen keyboard, handwriting workspace
- Speech-to-text through the standalone `metasequoia-ime-voice` command, which also records directly with `arecord` or `pw-record`

## Privacy

An input method sees every keystroke, so the boundaries are stated explicitly in [PRIVACY.md](PRIVACY.md): which features reach the network, what each sends, what the defaults are, and how to turn each off.

The one to know up front: **cloud candidates are on by default**, sending the composition spelling to Google's input-tools service. It is the only network feature that works without you supplying a credential — AI suggestions are off by default, candidate translation defaults to a local provider that makes no request, and voice transcription runs only in a separate command. API tokens are stored in the desktop Secret Service, never in `config.ini`.

There is no telemetry, analytics or crash reporting.

## Building from source

```sh
sudo apt install build-essential cmake pkg-config libibus-1.0-dev libboost-dev \
  libboost-json-dev libfmt-dev libspdlog-dev libsqlite3-dev libcurl4-openssl-dev \
  nlohmann-json3-dev libsecret-1-dev libgtk-3-dev python3
git submodule update --init --recursive
./scripts/build.sh
```

`libboost-json-dev` is required in addition to `libboost-dev`: Boost.JSON is a compiled component and the header metapackage alone makes CMake fail at configure time.

`scripts/build.sh` downloads the dictionaries named in `product-lock.json` and verifies them against the digests committed there. For an offline or sandboxed build, supply them from disk instead — see [docs/packaging.md](docs/packaging.md).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) covers the build, the test suites and what to run before opening a pull request. Contributions do not have to be code; the [recruiting page](https://github.com/metasequoiaime/.github/blob/main/RECRUITING.md) lists open areas.

Most issues, documentation and code comments are in Chinese. English pull requests and issues are welcome, and packaging questions in particular are best asked in English on the issue tracker.

Suspected vulnerabilities go through the organization [SECURITY.md](https://github.com/metasequoiaime/.github/blob/main/SECURITY.md), never a public issue.

## Licence

GPL-3.0-only for the code; see [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). The dictionary data has mixed provenance and is documented file by file in the engine's [`dictionary/NOTICE.md`](https://github.com/metasequoiaime/MSIME-Engine/blob/main/dictionary/NOTICE.md).

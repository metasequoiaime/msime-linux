# 水杉输入法 Linux 版

> **本仓库已归档，不再接受 issue 与 pull request。** Linux 前端已整合进 [metasequoiaime/msime](https://github.com/metasequoiaime/msime)，后续开发、发布与问题反馈都请到那里。本仓库保留为只读历史，已有的 Release 资产仍可下载，但不再更新。

[官网](https://msime.app) · [用户文档](https://msime.app/docs/) · [隐私说明](PRIVACY.md) · [打包指南](docs/packaging.md) · [English README](README.en.md)

<!-- badges:start -->
[![CI](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Linux/ci.yml?branch=develop&label=CI)](https://github.com/metasequoiaime/MSIME-Linux/actions/workflows/ci.yml)
[![CodeQL](https://img.shields.io/github/actions/workflow/status/metasequoiaime/MSIME-Linux/codeql.yml?branch=develop&label=CodeQL)](https://github.com/metasequoiaime/MSIME-Linux/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/metasequoiaime/MSIME-Linux?include_prereleases&label=release)](https://github.com/metasequoiaime/MSIME-Linux/releases)
[![Downloads](https://img.shields.io/github/downloads/metasequoiaime/MSIME-Linux/total?label=downloads)](https://github.com/metasequoiaime/MSIME-Linux/releases)
[![License](https://img.shields.io/github/license/metasequoiaime/MSIME-Linux)](LICENSE)
[![Stars](https://img.shields.io/github/stars/metasequoiaime/MSIME-Linux?style=flat)](https://github.com/metasequoiaime/MSIME-Linux/stargazers)
<!-- badges:end -->

水杉输入法（Metasequoia IME）的 Linux 前端。首个前端面向 IBus，复用 [`MSIME-Engine`](https://github.com/metasequoiaime/MSIME-Engine) 提供的共享 C++ 组词引擎。

引擎子模块固定在提供原生桌面前端 API 的上游修订版本，同时提供 `helpcode/helpcodes` 辅助码。公共词库源数据和构建器已合入 MSIME-Engine；发布词库按 `product-lock.json` 从 MSIME-Engine 下载，并核对来源提交、格式清单和摘要。历史 MSIME-Dict release 保留原版本和摘要。

当前桌面体验支持在全拼、双拼、五笔与日语罗马音之间实时切换，中文／直接输入切换，混合英文／Emoji／颜文字候选，专用英文候选，来自本地 SQLite 词库的实时候选，键盘与鼠标选择候选，翻页，中英文标点，半／全角输入，可配置的内联预编辑，全拼／双拼辅助码，以及按用户持久化的设置。

Linux 桌面工具包含一个 GTK 设置程序、一个剪贴板历史存储与面板，以及一个小型的屏幕键盘／手写工作区。语音转写通过独立的 `metasequoia-ime-voice` 命令使用配置好的 HTTPS 服务商完成；它接受已有的 WAV 文件，也可以在安装了 `arecord` 或 `pw-record` 时用 `--record 秒数` 直接从 Linux 音频录制。

## 依赖

Debian／Ubuntu：

```sh
sudo apt install build-essential cmake pkg-config libibus-1.0-dev libboost-dev libboost-json-dev libfmt-dev libspdlog-dev libsqlite3-dev libcurl4-openssl-dev nlohmann-json3-dev libsecret-1-dev libgtk-3-dev gnome-keyring python3 python3-gi python3-pypinyin gir1.2-ibus-1.0 ibus dbus-x11 iso-codes
```

`libboost-json-dev` 必须与 `libboost-dev` 一起装：Boost.JSON 是编译型组件，头文件元包不提供 `find_package(Boost REQUIRED COMPONENTS json)` 所需的配置文件与库，缺了它 CMake 配置阶段就会失败。

若需要本地中文手写识别，还需安装 `tesseract-ocr` 与 `tesseract-ocr-chi-sim`。缺少它们时桌面工具仍可使用，并会在状态栏说明缺失的后端。

## 安装

发行版包在 [Releases](https://github.com/metasequoiaime/MSIME-Linux/releases)，每个版本提供 x86_64 与 aarch64 的 `.deb`、`.rpm` 和 `.tar.gz`，每个包附一个 `.sha256`：

```sh
sha256sum -c metasequoia-ime-*.deb.sha256
sudo apt install ./metasequoia-ime-*.deb        # Debian / Ubuntu
sudo dnf install ./metasequoia-ime-*.rpm        # Fedora / RHEL
```

包未经签名，请核对上面的校验值。安装后重启 IBus，在桌面环境的输入源设置中添加「Metasequoia IME」。

要从源码构建（开发，或发行版包不适用时）看下面两节。

## 构建与测试

```sh
git clone --recursive https://github.com/metasequoiaime/MSIME-Linux.git
cd MSIME-Linux
python3 scripts/fetch_dictionary.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

若要在没有 IBus 头文件的环境下构建受测核心，使用 `-DMETASEQUOIA_IME_BUILD_IBUS=OFF`。

## 为当前用户安装

安装前请先停止正在运行的水杉输入法引擎，以便安全地重放它最新学习到的词频。当前用户的引擎仍在运行时，安装脚本会拒绝继续。

```sh
./scripts/install.sh
```

安装脚本把引擎放到 `~/.local/libexec`，把 IBus 组件描述文件与 desktop 入口放到 `${XDG_DATA_HOME:-$HOME/.local/share}` 下，并把三个词库、日语整句模型 `dict_japanese.dat` 及其必须随附的 Mozc 声明，以及五个辅助码数据文件装进引擎自己解析出的数据目录——绝对路径的 `METASEQUOIA_IME_DATA_DIR` 优先，其次是 `$XDG_DATA_HOME/metasequoiaime`，最后是 `$HOME/.local/share/metasequoiaime`。这个顺序与引擎的 `metasequoia::data_directory()` 完全一致，卸载脚本也按同样的顺序解析，否则设了该变量的用户会装进一个引擎根本不读的目录。它会为当前用户的前缀重新配置构建目录以生成这些文件，并在退出时把 `CMAKE_INSTALL_PREFIX` 还原成原值，因此同一个构建目录之后仍可直接用于 `cpack`。

IBus 只扫描自己的包数据目录（通常是 `/usr/share/ibus/component`）与 `IBUS_COMPONENT_PATH`，**不会**扫描 `XDG_DATA_HOME`。因此安装脚本还会写一个 `${XDG_CONFIG_HOME:-$HOME/.config}/environment.d/10-metasequoiaime.conf`，把系统目录和当前用户目录一并列进 `IBUS_COMPONENT_PATH`（该变量是替换而非追加，所以系统目录必须显式写上，否则其他输入法会消失）。**注销后重新登录**该文件才会生效，然后重启 IBus，在桌面的输入源设置中选择“Metasequoia IME”。

只想在当前会话里试一下、不注销，可以直接带着变量重启 IBus：

```sh
IBUS_COMPONENT_PATH=/usr/share/ibus/component:${XDG_DATA_HOME:-$HOME/.local/share}/ibus/component ibus-daemon -drx
```

要卸载当前用户的安装：

```sh
./scripts/uninstall.sh
```

该脚本移除安装脚本写入的每一项——引擎、`~/.local/bin` 下的四个工具、四个 desktop 入口、IBus 组件、词库、辅助码，以及 `environment.d` 配置——但**保留学习到的词频**（`msime_user.db`），因为那是唯一无法从仓库重建的数据。要连同词库、设置、剪贴板历史和学习数据一并清除，用 `./scripts/uninstall.sh --purge`。

卸载后重启 IBus，它才会停止提供“Metasequoia IME”。用发行版包安装的则用 `sudo apt remove metasequoia-ime-linux`（或对应的 rpm 命令）。

## 打包

CMake 配置内置 CPack，可生成 TGZ、DEB 与 RPM 三种包，它们共用同一份安装清单：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
cd build && cpack -G "TGZ;DEB"
```

`CMAKE_INSTALL_PREFIX` 必须显式写上：CPack 以它作为包内布局的根，缺省的 `/usr/local` 会让 IBus 组件描述文件里的 `<exec>` 指向发行版包不该使用的路径。CI 打包时用的也是 `/usr`。

DEB 生成需要 `dpkg-dev` 和 `file`（CPack 用它解析二进制以生成 shlibs 依赖），RPM 生成需要 `rpm`（Debian／Ubuntu 上是 `rpm` 包）。CI 在每次构建中验证 TGZ 与 DEB 生成。

发布由 release-please 驱动，与 Apple 前端保持一致：日常改动合进默认分支 `develop`，维护者把 `develop` 合进 `main` 时，其中的 conventional commits 会自动生成版本 PR，合并该 PR 即打标签并创建草稿 Release，随后发布流水线从被打标签的提交构建 TGZ／DEB／RPM，附加到 Release 并将其转正。版本号记录在 `version.txt` 与 `CMakeLists.txt` 中，由 release-please 统一维护，不要手改。

## 操作与设置

- 单独轻敲任一 Shift 键在中文转换与直接输入之间切换。与其他键组合使用的 Shift 不受影响。
- 按 `Ctrl+.` 在中英文标点之间切换。按 `Ctrl+Shift+Space` 在半角与全角输入之间切换。两个状态也可在 IBus 语言栏菜单中访问。
- 使用 IBus 语言栏菜单选择全拼、双拼、五笔或日语罗马音。
- 使用上／下键移动候选光标。PageUp／PageDown、`-`／`=` 以及 Shift+Tab／Tab 用于翻页。逗号／句号翻页默认开启，与 Windows 版一致；设置 `comma-period-paging=false` 可关闭，之后组词中的逗号与句号直接作为标点提交。
- 使用 `1`–`9` 或小键盘 `1`–`9` 从当前页选择。空格提交高亮候选，回车提交原始输入，Esc 取消。有活动组词时，标点会与高亮候选一并提交；单引号仍作为拼音分隔符。
- 全拼或双拼下，无活动组词时按 `Shift+U` 进入 Unicode 模式。输入十六进制标量，如 `4e00` 或 `+1f600`；空格提交高亮字符，`Shift+1`–`Shift+9` 选择其他可见候选。设置 `unicode-mode=false` 可禁用该模式。
- 全拼或双拼下，无活动组词时按 `Shift+T` 输出本地日期／时间。`rq`、`riqi` 或 `date` 取当前日期；`sj`、`shijian` 或 `time` 取当前时间；`xq`、`xingqi` 或 `week` 取当前星期。
- 全拼或双拼下，无活动组词时按 `Shift+K` 并输入小写字母码，可查询本地词库中的快捷短语。例如 `yyds` 会选中其内置短语；用户的新增与删除通过 XDG 日志在词库分阶段升级后依然保留。
- 全拼或双拼下，无活动组词时按 `Shift+E` 进入 Emoji、按 `Shift+M` 进入颜文字。可用全拼、简拼、受支持的双拼拼写或英文关键词搜索；空格提交高亮结果。
- 全拼或双拼下，无活动组词时按 `Shift+J` 进入超级简拼模式。之后的每个字母代表一个声母；双拼的声母键遵循当前方案。翻页与选择照常，或设置 `super-jianpin-mode=false` 禁用该模式。
- 全拼或双拼下且无活动组词时，按 `Shift+Y` 进行一次临时英文组词（先原始文本，后补全），或按 `Shift+R` 进行一次临时日语罗马音组词。提交、取消或删空前缀后即返回原来的中文方案。设置 `temporary-english-mode=false` 或 `temporary-japanese-mode=false` 可分别禁用这两个快捷键。
- 按 `Ctrl+Shift+E` 进入或退出专用英文模式。输入字母查询纯英文前缀候选；空格选择高亮候选，回车提交并学习原始字母输入且不退出该模式。
- `mixed-english-candidates`、`mixed-emoji-candidates` 与 `mixed-kaomoji-candidates` 控制是否把这些本地来源合并进普通的全拼与双拼候选。与 Windows 一致的优先级是：首个中文候选，然后是第一个英文、Emoji 与颜文字匹配，之后是其余本地候选与来源分组候选，并做稳定去重。Emoji 与颜文字从输入两个字符起生效；`mixed-english-minimum-prefix` 控制英文阈值，取值 1–8。与 Windows 版一致，混合英文与混合 Emoji 默认开启、混合颜文字默认关闭，英文阈值默认为 2。
- 在线候选默认开启，在 500 毫秒空闲后异步获取。Google 云输入建议占据第二个候选位。网络失败、超时、重置、失焦以及过期的请求代次都不会阻塞或改变本地输入。在 `[online]` 中设置 `cloud-enabled=false` 可禁用。
- AI 建议使用 OpenAI 兼容的服务商（`deepseek`、`openai`、`siliconflow`、`groq` 或 `custom`），占据第三个候选位。非机密项在 `[ai]` 中配置（`enabled`、`provider`、`endpoint`、`model`、`prompt`、`candidate-limit`）；API 令牌存放在桌面 Secret Service 中，绝不写入 `config.ini` 或诊断信息。运行时只接受 HTTPS 端点。
- 候选翻译作为展示元数据显示（`候选 · 释义`），绝不改变提交的候选文本。优先使用本地英汉释义；在 `[translation]` 中设置 `provider=deeplx` 可启用 HTTPS DeepLX 兼容的回退。在 `[translation]` 中配置 `target-language` 与 `endpoint`；其 Bearer 令牌存放在 Secret Service 中。翻译出错只会清空释义，候选选择仍然可用。
- 设置 `word-to-character=true` 后，`[` 提交高亮候选的第一个汉字，`]` 提交最后一个汉字。若 `bracket-paging=true`，方括号翻页优先，这两个键上的字词转单字选择会被禁用。
- 启用 `smart-punctuation=true` 后，逗号、句号与冒号在 ASCII 字母或数字之后保持 ASCII 形态。若 `smart-punctuation-repeat-to-chinese=true`，两秒内重复同一符号会将其替换为中文形态；无法获取周围文本时安全回退为中文标点。
- 启用 `paired-punctuation=true` 后，前引号、方括号、花括号、书名号与圆括号会成对插入，并把光标留在中间。
- 设置 `preedit-style=raw`、`pinyin` 或 `hidden`，分别显示所敲按键、切分后的拼音，或不显示内联预编辑。隐藏内联预编辑不会隐藏候选查找表。
- 全拼与双拼的辅助码分别由 `quanpin-helpcode` 与 `shuangpin-helpcode` 控制。其方案键接受 `lantian`、`ziranma`、`shouyou2_0`、`shouyouplus` 或 `xiaohe`；辅助码只在完整的基础拼写之后才生效。
- 本地候选学习使用 `frequency-adjustment=disabled|pin|halve|linear|promote`。`pin` 把选中的非首位候选移到最前，`halve` 将其名次减半，`linear` 按 `frequency-linear-step` 前进，`promote` 前进一位、位置较靠后时则前进到第五位。`frequency-trigger-count` 控制触发一次调整所需的选择次数；两个数值设置均取值 1–10。
- 启动 `metasequoia-ime-settings`（也可从桌面应用菜单打开）即可编辑同一套 XDG 设置，无需手改 `config.ini`。AI、翻译与语音的 API 令牌各有一个只写的密码框：已保存的凭据不会回填到表单里，留空即表示保持 Secret Service 中现有的凭据不变，填入新值才会覆盖。每个令牌框前面还有一个「清除已保存的…令牌」勾选框，勾上再保存就把该凭据从 Secret Service 中删掉，而该服务商的其余配置原样保留——功能仍是启用状态，只是在填入新令牌之前不会发出请求。令牌框只在配置真正会用到它时出现：翻译服务商选「本地」时不显示翻译令牌，AI 联想或语音输入未勾选时不显示对应令牌。启动 `metasequoia-ime-tools` 使用剪贴板历史、可把文本送入剪贴板的屏幕键盘，以及手写工作区。启动 `metasequoia-ime-toolbar` 获得一个置顶的快捷栏，用于打开上述桌面工具。
- 在设置程序中设置 `voice.enabled=true` 并配置 `[voice]` 的端点／模型，然后运行 `metasequoia-ime-voice --file recording.wav` 或 `metasequoia-ime-voice --record 5`。设置 `voice.polish-enabled=true` 可在打印前把转写文本送入配置好的 Chat Completions 端点润色。API 令牌按语音服务商存放在 Secret Service 中，绝不写入 `config.ini`；转写或可选润色失败都不会影响本地输入引擎。

设置存放在 `$XDG_CONFIG_HOME/metasequoiaime/config.ini`，回退到 `~/.config/metasequoiaime/config.ini`。`[input]` 组保存上面列出的本地输入设置。在线相关的非机密项分别保存在 `[online]`（`cloud-enabled`、`connect-timeout-ms`、`total-timeout-ms`）、`[ai]`（`enabled`、`provider`、`endpoint`、`model`、`prompt`、`candidate-limit`）与 `[translation]`（`enabled`、`provider`、`target-language`、`endpoint`）。工具显示状态保存在 `[utility]`（`clipboard-history`、`floating-toolbar`），语音选项保存在 `[voice]`（`enabled`、`provider`、`endpoint`、`model`、`language`、`polish-enabled`、`polish-endpoint`、`polish-model`、`polish-prompt`）。AI、翻译与语音的令牌按服务商隔离的属性存放在桌面 Secret Service 中，绝不写入本文件。运行中的引擎会监视该文件，设置窗口保存或手工编辑后都会重新加载并立即生效，包括超时和运行时启用 AI；引擎自己的写入是原子替换，并按内容识别，不会把自己刚写的内容当成外部改动重新加载。学习到的权重与英文原始条目记录在 `${XDG_DATA_HOME:-$HOME/.local/share}/metasequoiaime/msime_user.db` 中；重新运行 `scripts/install.sh` 会把该日志重放进暂存的 `msime.db`、`others.db` 与 `english.db`，再作为一个整体替换线上词库。

## 与桌面核心的能力对齐

| 能力 | 状态 | 依据 |
| --- | --- | --- |
| 全拼、双拼、五笔与日语切换 | 已支持 | 控制器测试与 IBus 属性冒烟测试 |
| 中文／直接模式与单 Shift 切换 | 已支持 | 控制器测试与真实 keyval 映射测试 |
| 候选光标、翻页、数字键与鼠标选择 | 已支持 | 控制器测试与 IBus 适配器测试 |
| XDG 配置与原子替换 | 已支持 | 文件系统与 IBus 生命周期测试 |
| IBus 注册与当前用户安装 | 已支持 | D-Bus 与安装 CI 冒烟门禁 |
| 中英文标点与全角模式 | 已支持 | 转换、控制器、映射、设置与真实 D-Bus 测试 |
| 智能标点与成对标点 | 已支持 | 周围文本、替换删除与光标前移的 D-Bus 冒烟测试 |
| 高亮候选的首／末汉字选择 | 已支持 | 引擎 Unicode 测试、控制器测试与真实 D-Bus 提交冒烟测试 |
| 原始／切分／隐藏预编辑与辅助码 | 已支持 | 引擎、控制器、设置、已安装数据与真实 D-Bus 查找冒烟测试 |
| 本地候选词频学习 | 已支持 | 引擎五种模式的持久化测试，加上控制器、设置与真实 D-Bus／XDG 日志冒烟测试 |
| 显式 Unicode 标量输入 | 已支持 | 引擎标量校验、控制器／映射／设置测试与真实 D-Bus 提交冒烟测试 |
| 本地日期、时间与星期候选 | 已支持 | 引擎确定性格式化测试，加上控制器与真实 D-Bus 提交冒烟测试 |
| XDG 快捷短语与升级重放 | 已支持 | 引擎查询／失败测试，控制器与真实 D-Bus 提交冒烟测试，事务性安装冒烟测试 |
| 显式 Emoji 与颜文字模式 | 已支持 | 引擎前缀／排序／失败测试，生成的 `others.db`，事务性安装与真实 D-Bus 提交冒烟测试 |
| 混合英文、Emoji 与颜文字候选 | 已支持 | 引擎候选位／去重／失败隔离测试，加上控制器／设置与真实 D-Bus 排序冒烟测试 |
| 超级简拼模式 | 已支持 | 引擎全拼／双拼查询与词频测试，加上控制器／设置与真实 D-Bus 翻页／选择冒烟测试 |
| 专用英文输入 | 已支持 | 引擎失败／学习测试，控制器／映射／设置测试，生成的 `english.db`，事务性安装与真实 D-Bus 提交冒烟测试 |
| 临时英文与日语输入模式 | 已支持 | 引擎生命周期测试，加上控制器／设置与真实 D-Bus 提交／恢复冒烟测试 |
| 云候选建议 | 已支持 | 异步服务商／服务测试，控制器代次测试，真实 IBus 冒烟测试，Ubuntu CI |
| OpenAI 兼容的 AI 建议 | 已支持 | 服务商契约／缓存测试，控制器代次测试，真实 IBus 冒烟测试，Ubuntu CI |
| 候选翻译展示 | 已支持 | 本地／DeepLX 解析、防抖／取消与 UTF-8 测试，真实 IBus 冒烟测试，Ubuntu CI |
| GTK 设置程序 | 已支持 | 设置模型测试，无头 `--check`，安装冒烟测试 |
| 剪贴板历史 | 已支持 | UTF-8／大小／去重／原子存储测试与 GTK 工具面板 |
| 屏幕键盘工作区 | 已支持 | GTK 桌面工具可执行文件与无头检查 |
| 从录制 WAV 的语音转写 | 已支持 | HTTPS multipart 服务商契约测试与独立 CLI |
| 麦克风语音采集 | 已支持 | 独立 CLI 通过 `arecord` 或 `pw-record` 采集；服务商转写仍为可选 |
| 手写识别 | 已支持 | GTK 笔画画布与 Tesseract `chi_sim+eng` 后端；不可用时给出明确的安装指引 |
| 浮动工具栏 | 已支持 | 置顶的 GTK 工具，带桌面工具启动入口与安装冒烟测试 |

## 范围

本仓库负责 Linux／IBus 适配层、Linux 安装、打包与 CI。Apple 前端在 [`MSIME-Apple`](https://github.com/metasequoiaime/MSIME-Apple)，输入引擎与词库则是共享依赖。

## 许可

水杉输入法 Linux 版依据 GNU 通用公共许可证第 3 版分发，见 [LICENSE](LICENSE)。本仓库的每个二进制产物都静态链接了 [`MSIME-Engine`](https://github.com/metasequoiaime/MSIME-Engine) 提供的共享引擎，而该引擎是 GPLv3，因此构建与再分发都受同样的条款约束。

被编入二进制的第三方代码（googlepinyinime-rev、utfcpp，以及以 header-only 方式使用的 fmt、spdlog、nlohmann/json 与 Boost 头文件部分）及其各自的许可全文见 [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)，该文件随包安装到 `share/licenses/metasequoia-ime-linux/`。只做动态链接的系统库（IBus、GTK、SQLite、libcurl、libsecret、Boost.JSON）保留发行版各自的许可，本仓库不对其进行再分发；手写识别调用的 Tesseract 是外部进程，不参与链接。

## 隐私与安全

转换、候选学习、剪贴板历史与手写识别都在本地完成。云候选默认开启，会把所敲的拼写发送给 Google 的输入工具服务；AI 建议、DeepLX 翻译与语音转写均为选择性开启，数据发往用户自行配置的端点。完整清单与各自的关闭方式见 [PRIVACY.md](PRIVACY.md)。疑似漏洞请按 [SECURITY.md](https://github.com/metasequoiaime/.github/blob/main/SECURITY.md) 的方式私下上报，不要通过公开 issue。

参与贡献请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

### 公共语音模块

语音转写的 multipart 请求与识别／润色结果解析共用 Engine `voice/` 公共实现。Linux 继续负责录音工具、HTTPS／令牌策略、网络取消及用户界面；现有 120 秒录音范围和 20 MiB 上传限制保留，不启用 Engine 的录音设备或 Whisper 依赖。

### 词库产品格式

Engine 词库发布除摘要外必须提供 `dictionary-manifest.json`，下载阶段使用与固定 Engine 一致的公共校验器检查格式版本、桌面 profile 和实际消费的载荷。桌面 profile 的载荷是三个数据库、日语整句模型 `dict_japanese.dat` 以及 `mozc_dictionary_oss_README.txt`；`product-lock.json` 锁定全部五个，校验器另外核对模型的 `MSJPDT1` 头。`dict-2026.09.05` 是唯一保留的无清单兼容版本，它早于该模型，因此只锁定三个数据库。清单随安装数据一起打包。

<!-- star-history:start -->
## Star History

<a href="https://star-history.com/#metasequoiaime/MSIME-Linux&Date">
  <img src="https://api.star-history.com/svg?repos=metasequoiaime/MSIME-Linux&type=Date" alt="Star History Chart" width="600">
</a>
<!-- star-history:end -->

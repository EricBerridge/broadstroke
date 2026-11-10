---
name: modernize
description: Check Polynary's dependencies for upgrades and apply the ones the user approves: llama.cpp's server build, the AI models, the .NET SDK and NuGet packages, the build tools. Use when asked to check for updates, upgrade a dependency, or modernize the stack; never upgrade without the user's go-ahead.
---

# Modernize

Keep Polynary's dependencies current on purpose, not by drift: find what is newer, judge whether it is worth it, and upgrade only what the user approves, with the measurements to show nothing got worse. Run commands from the repo root.

## Rules

- **Check first, then ask.** Report what is pinned, what is newer, and whether each is worth it (and why). Change nothing until the user says which to upgrade.
- **One kind at a time:** llama.cpp, the models and the rest of the stack each get their own change, so a regression has one cause.
- **Pins move together:** a version and its digest, in every place that holds them (below), in the same change.
- **Accept on measurements, not release notes.** A benchmark may drop by noise, 2-3 questions; anything more is a regression to explain or a reason to stay.
- **Stable releases only:** never a daily, nightly, preview, beta or release-candidate version while a stable one exists, for llama.cpp (below), NuGet packages (`dotnet list ... --outdated` lists stable versions; don't add `--include-prerelease`), the SDK and the tools. A prerelease the stack already depends on (such as SharpGen's, through Vortice) stays as its parent package brings it.

## Where things are pinned

| What | Pins | Fetched by |
| --- | --- | --- |
| llama.cpp, the Vulkan build the app ships | `src/Polynary.Assist/Polynary.Assist.csproj`: `LlamaBuild` (`bNNNNN`), `LlamaSha256`; the build number `HostTests` expects (`tests/Polynary.Assist.Tests/HostTests.cs`) | That project's `FetchLlamaServer` target, on any build or publish (so the pipeline's publish), into `artifacts/llama/<build>-vulkan`, checked against the SHA-256 |
| llama.cpp for the benchmarks | `tools/ai-benchmark.cs`: `LlamaBuild` | `ai-benchmark.cs setup`: CUDA and CPU builds into `artifacts/ai-bench/llama/{gpu,cpu}`, checked against GitHub's digests. The `vulkan` folder is copied by hand from `artifacts/llama/<build>-vulkan`, and `setup` skips a folder that already has `llama-server.exe`: delete the old ones first |
| The models | `src/Polynary.Presentation/Assist/ai-models.json`: each model's Hugging Face `repo`, `revision`, `file`, `sizeBytes`, `sha256`, and its per-model `review` values (D55, D61) | Settings' download (`Polynary.Assist/ModelStore`), checked against the SHA-256. The benchmark's own list (`models` in `tools/ai-benchmark.cs`) names the same repos and files |
| .NET SDK, and the runtime the app ships | `global.json` (`version`, `rollForward: patch`, so it stays in its feature band); the self-contained app ships the runtime that SDK carries | CI: `actions/setup-dotnet` reads `global.json` |
| NuGet packages | `Directory.Packages.props` (central versions, transitive pinning); the 15 committed `packages.lock.json` | Restore; CI restores in locked mode, so lock files must match |
| WiX and AzureSignTool | `.config/dotnet-tools.json` (`rollForward: false`) | The pipeline's `dotnet tool restore` |
| CI actions | `.github/workflows/ci.yml` (`uses: ...@vN`) | GitHub |
| Shipped versions, for users | `THIRD-PARTY-NOTICES.txt`'s table (every shipped component, llama.cpp included) | - |

## 1. llama.cpp

- **Compare with the stable releases, not the daily builds:** `gh api repos/ggml-org/llama.cpp/releases/latest --jq '.tag_name'` is the newest stable release (`vX.Y.Z`); its only asset, `nightly-tag.txt`, names the `bNNNNN` build it is, whose Windows zips the app uses, and its notes say what changed. The daily `bNNNNN` builds, several a day, are marked prereleases: don't move to one while a stable release exists, unless a fix we need is in no stable release yet, and then only with the user's go-ahead for that build in particular; move to the next stable release when it has the fix. List both with `gh api "repos/ggml-org/llama.cpp/releases?per_page=20" --jq '.[] | .tag_name + " " + .published_at + " " + (.prerelease|tostring)'`.
- **Worth it only for:** a fix we need, a measured speed gain, support for a model we want, or a security issue (the server listens on localhost with a key, §16.4, so weigh server-side issues by that). Not because a build is newer: there are several a day.
- **Upgrade, once approved:**
  1. The new zip's digest: `gh api repos/ggml-org/llama.cpp/releases/tags/<build> --jq '.assets[] | select(.name|test("win-vulkan-x64")) | .digest'`.
  2. Set `LlamaBuild` and `LlamaSha256` in `Polynary.Assist.csproj`, `LlamaBuild` in `tools/ai-benchmark.cs`, the build `HostTests` expects, and the llama.cpp row of `THIRD-PARTY-NOTICES.txt`; check its asset names (`llama-<build>-bin-win-vulkan-x64.zip`, and the CUDA and CPU zips `setup` names) still exist.
  3. Build once to fetch it; refresh the benchmark's builds (delete `artifacts/ai-bench/llama/*`, run `dotnet run -c Release tools/ai-benchmark.cs -- setup`, copy `artifacts/llama/<build>-vulkan` to `artifacts/ai-bench/llama/vulkan`).
  4. Rerun on the three current models (Gemma 4 E2B, Qwen3.5-4B, Gemma 4 12B), Vulkan, against the numbers in the spec:
     - the import trial, `-- run <model> vulkan`, then `-- report`;
     - `-- calibration`: if a model's probabilities move, recalibrate its `review` thresholds in `ai-models.json` (D55);
     - `-- import-checks <model>`: a model keeps `skipValues` only with no value where none is meant (D61);
     - `-- help <model> vulkan --priming whole` and `-- help-data <model> --passes two`.
  5. The guide's reading saved on disk (§16.6) must restore on the new build, or be deleted and rebuilt cleanly: run `artifacts/bin/Polynary.Assist.Tests/debug/Polynary.Assist.Tests.exe` with a model present (`HostTests` skips without one: `-- setup gemma4-e2b`, or `POLYNARY_TEST_MODEL`), then ask two questions in the app across a restart and check the second session's first question doesn't reread the guide.
  6. Accept only if nothing drops beyond noise; otherwise report and stay on the old build.

## 2. Models

- **New revisions:** for each `repo` in `ai-models.json`, `curl -s https://huggingface.co/api/models/<repo>` gives `sha` (the newest revision) and `lastModified`; compare with `revision`. A new revision is a new model: its file, size and SHA-256 come from `https://huggingface.co/api/models/<repo>/tree/<revision>` (`lfs.oid`, `size`), and it is benchmarked as a candidate.
- **New candidates for a tier:** add it to the benchmark's `models` list, `-- setup <id>`, and run the import trial, `calibration`, `import-checks`, `help` and `help-data`. It replaces the tier's model only if it beats it on the import trial, `help` and `help-data`, beyond noise, and fits the tier's memory (D60: one model per tier). The model it replaces becomes `"status": "older"`, behind Show more models; never remove an entry users may have downloaded.
- Add or change a model's `review` values from its own calibration, and `AiModelCatalogTests` and `AiSettingsTests` with the list.

## 3. The rest of the stack

- **Check:** `dotnet list Polynary.slnx package --outdated`, and the same for `build/Polynary.Pipeline` and `installer/Polynary.Installer` (not in the solution); the newest .NET 10 releases: `curl -s https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json` (`releases[].sdks`, `runtime.version`, `security`, `cve-list`); the shipped runtime: the version of `artifacts/bin/Polynary.App/release_win-x64/System.Private.CoreLib.dll`; the tools: `https://api.nuget.org/v3-flatcontainer/<tool>/index.json`.
- **A runtime security release is worth taking:** the app is self-contained, so its users run the runtime the SDK ships until a new release.
- **Upgrade, once approved:** change the versions, then `dotnet restore Polynary.slnx --force-evaluate` (and the pipeline and installer projects) to rewrite the lock files, build with no warnings (`dotnet build Polynary.slnx -c Debug`, and the app `-c Release`), run all five test suites (`AGENT_BOOTSTRAP.md`'s commands), and run the full pipeline: `dotnet run --project build/Polynary.Pipeline -c Release`. Update `THIRD-PARTY-NOTICES.txt` for every shipped component that changed.
- **Known traps:**
  - A new SDK feature band (`10.0.3xx` to `10.0.4xx`) needs `global.json` changed by hand, since `rollForward: patch` stays in the band; each SDK pins the implicit ILLink package, so the lock files change with it.
  - QuestPDF 2026.9 renamed `UseSystemFonts`, `RegisterFontFromStream` and `ThrowOnMissingTextGlyphs`; the old names are obsolete, and warnings fail the build. `ReportSetup.EnsureInitialized()` must stay the first QuestPDF call.
  - Windows App SDK is referenced as its components (`Microsoft.WindowsAppSDK.WinUI`, `.Foundation`), not the metapackage: keep it that way, and update the release number in `Directory.Packages.props`'s comment and the notices.
  - Vortice and SharpGen versions move together; the renderer's tests run on WARP, so also look at the app's graph.
  - TUnit runs on Microsoft.Testing.Platform (`global.json`'s `test.runner`); keep `TUnit` and `TUnit.FsCheck` on one version.
  - WiX 7 needs `-acceptEula wix7` (`build/Polynary.Pipeline/Modules.cs`); a new major version may need another.
  - Sylvan reads the importer's cells: run the Core suite's import tests and the benchmark data sets' import (`-- import-checks`) after an upgrade.

## 4. Docs

Follow the specification-stewardship skill (`.claude/skills/specification-stewardship/SKILL.md`):

- The spec where a version or a measured result changes: llama.cpp's build and its results in §16.3-16.4 (and §16.7's calibration table), the help benchmarks in §16.6, the stack in §4.1 and §4.5. Replace numbers with the new ones and their dates; don't keep the old ones as history.
- `AGENT_BOOTSTRAP.md`: its commands, gotchas and state, when they change.
- The change log, by `.github/copilot-instructions.md`'s rules: a line in both `ChangeLog.html` and `CHANGELOG.md` when users would notice (faster answers, a new model, a fix they could hit, a security update), none for build-only changes.

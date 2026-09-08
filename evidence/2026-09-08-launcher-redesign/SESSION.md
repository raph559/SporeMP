# Launcher redesign - 2026-09-08

Request: make the launcher substantially better, preserving the simple player flow and the latest changelogs. The user clarified that Escape came from their game and requested background code work without desktop control. The initial review saw the old 0.1.1 layout; an attempted evidence capture was interrupted. After the clarification, no Computer Use, input, activation, window closing or game launch was performed.

This increment adds launcher 0.1.2. Home now uses a dedicated artwork panel with a prominent Play action, a separate readiness strip and integrated release information. Permanent navigation exposes Home, What's new and Settings. Release history has a version selector, installed-version label, full reading pane, case-insensitive multi-word search, and a clear empty state. Settings groups installation selection and normal save behavior, with troubleshooting collapsed. Typography, control contrast and focus states are consistent, and keyboard navigation and preference-aware page fades are implemented.

Native launch, compatibility, the normal Windows account and existing-save behavior remain as accepted for M01. No multiplayer capabilities were added or represented as available. The original illustration is reused without editing; its provenance remains in src/launcher/Assets/README.md.

Status: IMPLEMENTED_NOT_RUN for the redesigned presentation. BUILD and the compiled release-note model checks below passed. The redesigned window, navigation, animations and layout have NOT RUN through visible UI. This is separate from M01, which remains VERIFIED. M02-M20 and the complete acceptance plan are unchanged.

## Build and artifact

The previous running launcher was not replaced. The new executable and its companion files are in build/launcher/0.1.2, including launcher.runtime.json pointing at the existing checkout and Python interpreter. Nothing was installed into the game. The existing build/launcher/Release output was not mutated.

Executed from C:\Users\Developer\Documents\ChatGPT\SporeMP in PowerShell:

```powershell
dotnet build src/launcher/SporeMP.Launcher.csproj -c Release -o build/launcher/0.1.2 --nologo -m:1
$launcherPython = & python -c 'import sys; print(sys.executable)'
@{ repo_root = (Get-Location).Path; python_executable = $launcherPython } | ConvertTo-Json | Set-Content -LiteralPath build/launcher/0.1.2/launcher.runtime.json -Encoding utf8NoBOM
```

Expected: successful WPF compilation and complete runtime configuration without showing the app. Observed: exit 0, zero warnings and zero errors. build.log records the first build; build-final.log records the final build after including ISO dates in release-note search. The final rebuild took 0.73 seconds. Python resolution/configuration returned 0. No full suite or native game test was repeated.

## Compiled release-note check

Loaded the built managed assembly in PowerShell and called only the release-note model. Application startup and WPF window construction were not called. The following command returned exit 0; its booleans and assembly version are recorded in release-notes-check.json.

```powershell
$launcherAssembly = [Reflection.Assembly]::LoadFrom((Join-Path (Get-Location) 'build/launcher/0.1.2/SporeMP.dll'))
$launcherNotes = [SporeMP.Launcher.ReleaseNotes]::Load()
$launcherChecks = [ordered]@{
  embedded_history = ($launcherNotes.Count -eq 3 -and $launcherNotes[0].Version -eq '0.1.2')
  version_search = (@($launcherNotes | Where-Object { $_.Matches('v0.1.2') }).Count -eq 1)
  mixed_case_words = (@($launcherNotes | Where-Object { $_.Matches('  SETTINGS  0.1.2 ') }).Count -eq 1)
  date_search = (@($launcherNotes | Where-Object { $_.Matches('2026-09-08') }).Count -eq 3)
  empty_search = (@($launcherNotes | Where-Object { $_.Matches('  ') }).Count -eq 3)
  no_match = (@($launcherNotes | Where-Object { $_.Matches('unmatched-release-xyz') }).Count -eq 0)
  installed_version = (@($launcherNotes | Where-Object IsCurrentVersion).Count -eq 1 -and $launcherNotes[0].IsCurrentVersion)
}
$launcherCheckReport = [ordered]@{
  evidence_class = 'HOST - release note model only; no window or application startup'
  assembly_version = $launcherAssembly.GetName().Version.ToString()
  checks = $launcherChecks
  ui_execution = 'NOT_RUN'
  native_execution = 'NOT_RUN'
}
$launcherCheckReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath evidence/2026-09-08-launcher-redesign/release-notes-check.json -Encoding utf8NoBOM
$launcherCheckReport | ConvertTo-Json -Depth 5
if ($launcherChecks.Values -contains $false) { exit 1 }
git diff --check
```

All seven checks matched their expected counts and version. git diff --check returned 0. These results establish compiled search/content behavior, not interactive binding or visual acceptance.

## Provenance and next step

Base source revision: 29ff2bc. The local main branch records this increment; no remote or push is involved. Files and artifact SHA-256 fingerprints are in files.json. Native dependencies were not rebuilt or fetched: SDK cbf9206b9a823f0911cd9be0217104a49d72380b; loader 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9. The previous supported executable SHA-256 is dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37. Game/content and environment provenance remain in ../2026-09-08-m01-native/environment.json and native-lifecycles.json; they were not re-inventoried for this UI-only increment. That same-day environment records Windows 11 Pro 10.0.26200, Ryzen 7 9700X, approximately 32 GB RAM and RTX 4080 SUPER. files.json pins the environment reference.

Next smallest presentation check, when desktop interaction is welcome: open the staged launcher once, inspect Home at normal/minimum sizes, select and search releases including an empty result, and inspect Settings. No game launch is required for that check. No such interaction is scheduled. M02 remains the next implementation milestone.
# Compatibility and save protection

## Observed candidate, not a supported release

| Item | Observed/pinned value |
|---|---|
| Installation | GOG SPORE Collection, `C:\Games\SPORE` |
| Candidate executable | `SporebinEP1\SporeApp.exe`, file/product version 3.1.0.29 |
| Architecture | PE32 / IMAGE_FILE_MACHINE_I386 (`0x014c`), executable rather than DLL |
| Size | 24,895,536 bytes |
| SHA-256 | `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37` |
| SDK commit | `cbf9206b9a823f0911cd9be0217104a49d72380b` |
| SDK binding family | `EXECUTABLE_TYPE=2`, March2017; the upstream combined selector lists this file size |
| Local SDK version | 2.5.0 development build (`SDK_BUILD_VER=0`), not a published release number |
| Native compatibility | M01 native lifecycle qualified for this exact local configuration; final player flow confirmed by user |
| Initial content | Exact observed trees for Data, DataEP1, bp1content and SporebinEP1 |
| Clean standard-content equivalence | **NOT VERIFIED**; a local inventory is not an official content certification |

The base-game executable is 1.3.0.29, 20,454,960 bytes, SHA-256 `25d42a7a5c4d438fb155233230f57d29e2849bfdff5c889a5d0847f0469d914e`. It is not the bridge target. GA is the runtime candidate supporting the campaign integration, not a scope change to adventure-only multiplayer.

`../config/compatibility.candidate.json` records exact PE fields and content hashes. Inspection is read-only. Validation rejects different executable bytes (including same-size changes), x64 images, DLLs, malformed/truncated PE headers, missing roots and added/changed/removed content. Reparse points are refused. No candidate field grants launch permission.

## Guard ordering and launch modes

The upstream SDK binds native addresses during DLL load. The implemented SporeMP.NativeHost validates compiled game/content/payload fingerprints before creating or injecting a game. It rejects reparse paths and unknown content entries, holds file read locks, starts the original process suspended, configures the pinned injector and resumes only after success. A late bridge check is secondary.

The loader source is pinned at 26adca9a2578b5bb32ba2eac90d96bd9ac7d48a9. This configuration loads the current SDK core and project bridge; legacy mods are not included. Three clean native runs and six rejection fixtures are recorded in ../evidence/2026-09-08-m01-native/SESSION.md.

Normal Play uses --play under the player's normal Windows account and existing save paths. There is no separate-account or backup gate and no profile redirection, following the user's explicit correction. Project DLLs stay in local/launcher/native-payload rather than being installed into SPORE.

Optional developer --probe/--launch modes retain the owned SporeMP-M01 account, SID/known-folder checks and protected-path OS denial probes. Earlier environment-variable-only isolation failed; real isolated native runs subsequently reported save/creation/temp paths under C:\Users\SporeMP-M01.

Native ETW traces include actual save/config writes in that test profile and shared NVIDIA driver writes in ProgramData. Some correlations remain unresolved. Complete personal-file manifests and OS denial probes independently establish preservation during those earlier tests. Normal Play intentionally uses normal saves.

The old Python preflight starts no process and does not govern the implemented player action. Other executable/content configurations and official clean-content equivalence remain unqualified.

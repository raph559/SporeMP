# Inspected original-game presentation — 2026-09-14

Evidence class: original SPORE pixels inspected through live Sky screenshots and finalized, exact-process WGC captures. Contact sheets are extracted from those recordings, not generated visuals. Raw clips and images remain ignored under `local/m05-ability-0914`; their SHA-256 identities are in `native-summary-02.json`.

## Active bite and native foreign-species UI, native-02 / 0.0.25

The startup was advanced to the fully visible original galaxy menu before private-IPC loading. The resulting Creature view is unobstructed original 3D, unlike the failed native-01 startup-overlay presentation. Capture PID 36040 / HWND 1378824 starts at request time 09:43:14.2225299 UTC and completes with recorder exit 0.

`bite-02.png` samples eight original frames from capture-02 at 28.6 s, 4 fps, scaled 512×288 in a 4×2 sheet. Inspection shows original creature motion/attack flash, the green foreign NPC and HUD DNA changing from 0 to the displayed integer 8. The trace supplies exact 8.75. Request timestamps are only approximate video alignment; per-frame causal timing is not inferred from them.

The original B bite entry is sequence 2134 / QPC 707646990. Arming is sequence 2205 / QPC 710425583, 0.2778593 s later. A sampled original context still has ability index 5 and animation 72212134 after arming at sequence 2265 / QPC 711563081. At sequence 2395 / QPC 715353450 both are idle (`UINT_MAX`); that readout is attached to a later denied challenge call, not another admitted native attack. This establishes an already-active bite and subsequent cleanup. It does not establish charge/spit behavior or complete effect parity.

UI actions below use fresh observed controls, with an immediate state refresh after each action:

| UTC | Action and observed result |
|---|---|
| 09:43:59 | Click at the prior green creature location hit the scene/ground after camera movement. Not counted as selecting the foreign NPC. |
| 09:44:17, 09:45:05, 09:45:21 | Actual own-species selections, including native B 8519 and noun 99; normal native creature card/name/health shown. |
| 09:45:37.286 | Select green **Payer**, diagnostic NPC 3 / native 9601. Selection sequence 5295; social-result denial sequence 5297; original UI readout sequence 5298, text IDs 1339603203 and 673168902, `award_unchanged:true`, `original_ui_tail:true`. Original species-introduction cinematic appears. |
| 09:46:14.954 | Continue the native introduction. Original Payer/Maxis card, neutral relationship marker and Hunt 3 Payer 0/3 mission description appear. The advertised 20 DNA is a mission description, **not** an awarded balance. |
| 09:46:27.331 | Dismiss the original stance tutorial; usable Creature HUD returns. |
| 09:46:39.756 | Click actual Bite Level 1 HUD control. NPC HP remains 6 and no admitted native hit follows; do not claim the click reached PlayAbility without a corresponding trace event. |

`social-02.png` samples the 45-second social-02 recording at 1 s and every 5 s, 512×288, 3×3. The sheet was inspected: species introduction → original stance explanation/mission/creature card → unobstructed 3D gameplay. Recorder exit 0. The last HUD click is after this clip; it has live observation and trace evidence only.

## Actual saved-world Play and Cancel, native-06 / 0.0.29

Capture-06 targets original game PID 19224 for 150 seconds, command start 10:12:00.535794 UTC, recorder completion 10:14:31.311810 UTC, exit 0. Original scene exit uses Escape at 10:12:30.289, Exit at 10:12:37.773, Don't Save at 10:12:45.176 and confirmation at 10:12:56.485; native epoch becomes 4 and A/B bindings clear.

At 10:13:36.226 the saved Satiria world is selected. At **10:13:53.137** the actual blue Play control is clicked. Native `menu_click` denial is sequence **1579**, QPC **1928832784**, epoch 4. The saved-world panel remains visible; no loading screen or local scene starts. At **10:14:04.320**, native Cancel returns to the complete galaxy view with Play/Create/Share/Galactic Adventures controls visible. The final build therefore preserves this tested menu interaction while denying local load after disconnection/scene exit.

`menu-06.png` samples capture-06 from 97 s every 4 s, 512×288, 3×3. Inspection shows the world panel before and after the denied Play, then the full galaxy menu after Cancel. Trace/UI correlation is necessary: a still image alone cannot prove a button was clicked or that load was denied.

## Failures retained

- Native-01 (.25): a private load was sent before startup presentation completed. The actual game retained the GA splash/ESRB overlay over 3D/HUD. Task Manager was also initially foreground. This is failed presentation, despite passing scalar challenges and clean exit. Task Manager was minimized at 09:34:29.621. The later focus click at 09:34:52 also moved the avatar; it is not called harmless focus.
- Native-03 (.26): filename Load denied, but native loading UI had already started and remained stuck.
- Native-04 (.27): outer countdown denied, but an earlier callback had already hidden the saved-world controls. Escape did not restore them.
- Native-05 (.28): the guarded alternative scheduling route was never called by this actual menu; fallback countdown denial again left controls hidden.
- Native-06 (.29) resolves that precise route by guarding the ordinary Play event **before** its hide/schedule calls. Earlier failed presentation runs are not relabeled as passes.

Prior `.23` process-audio recordings remain unprocessed original game audio; no new recording, listening response or perceptual acceptance is inferred here. Current screenshots/recordings do not supply audio evidence or general frame-time overhead measurements.

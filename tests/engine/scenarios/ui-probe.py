"""Diagnostic: read each player's original Creature HUD at rest, and what lies under its stance and ability places.

Nothing is pressed. The readings are `UI_READBACK` evidence of what each player's own game shows after a fresh
restore of the checkpoint sealed by trial146, and the source of the window identities other scenarios use.
"""

ARTIFACT = 'local/m11-resume-2026-09-27/trial146/sealed-checkpoint/checkpoint.smp'
AUTHORITY = {'schema': 7, 'experimental_foreign_species': 1}
DEADLINE_SECONDS = 120
# Places of the 1280x720 HUD read off a frame of trial229: the two stance buttons, then the ability bar left to right.
PLACES = [(608, 638), (680, 638), (592, 668), (618, 668), (645, 668), (672, 668), (700, 668), (733, 668), (760, 668), (790, 668), (818, 668)]


def drive(trial):
    trial.wait_ready(stable_seconds=3)
    trial.settle(3)
    for role in ('player-1', 'player-2'):
        windows = trial.ui(role)
        texts = [w['text'] for w in windows if w['text']]
        trial.expect(bool(texts), f"{role}'s interface can be read", windows=len(windows), texts=texts[:40])
        for place in PLACES:
            under = trial.ui(role, at=place)
            trial.expect(bool(under), f"{role}: a window lies under {place}", id=under[0]['id'] if under else None,
                         chain=[w['id'] for w in under][:5], box=under[0]['box'] if under else None)
    trial.settle(2)

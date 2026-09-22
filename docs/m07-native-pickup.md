# M07 original Creature corpse pickup

Bridge 0.0.41 introduced the bounded original corpse pickup path described below.
[Native18/.44](../evidence/2026-09-21-m07-completion/native18-encounter.md)
subsequently qualifies overlapping original feeding orders, one first-feed
beneficiary, positive original nutrition, exact client projections and consumed
corpse retirement for the pinned Creature fixture. The original feeding DNA call
returns zero; B's separate combat reward remains 7 DNA. This native evidence is
separate from BUILD and HOST ABI/protocol checks. The complete milestone criteria
and current acceptance remain in [M07](../tests/engine/M07.md).

## Resource and native ownership

SPORE keeps two different corpse values: the first-feeding flag at Animal
offset `0xB5F`, and remaining food at `0xB84`. The first-feeding claim
and original transfer of food are the contested result. The original DNA call
can legitimately return zero; it is not assumed to be a positive bonus. Remaining
nutrition can be eaten afterward, without replaying the first-feeding action. Native food
may become negative after the final bite, so finite signed values are preserved.

An authenticated pickup intention identifies one current dead NPC incarnation
with positive food. The coordinator replaces client actor fields with observed
authority state and checks ownership and generations. The engine thread then
resolves both original nouns again. Original `SetCreatureTarget` and the
actor-local order helper `0xC0C070` submit the original eating order. A successful
order readback is queued work, not a consumption or award result.

The original behavior callback `0xD70FD0` retains the native claim at `0xB50`,
movement, diet checks, animation, first-feed flag, food consumption and reward.
The bridge never writes those values on the authority. Two narrow classification
hooks permit a scoped owned B actor to reach the original first-feed branch and
prevent the original avatar priority rule from overriding another living owned
actor's existing native claim. They neither select the winner nor manufacture
a claim. Callback contexts are cleared on nested bypasses and restored by RAII.

## ABI, binding and reward scope

The pinned executable is the existing GA 3.1.0.29 PE32 profile; its SHA-256 is
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The SDK commit remains `cbf9206b9a823f0911cd9be0217104a49d72380b`.
`native_pickup.cpp` checks the original instruction prefixes, object layout,
readable ranges, current noun census and engine thread before its bindings are
admitted. Native pointers stay inside the bridge.

The callback uses eight caller-cleaned stack words and returns a boolean in AL.
The order helper uses the original eleven stack words and callee cleanup.
HOST Detours tests forward the actual typed calls and repeat 4,096 mixed calls
to check stack balance and exact original-call counts.

The first-feed DNA caller is RVA `0x9713B2`. For B, only that audited award
call receives the existing player `StageScope`; the entire AI callback does
not. The original award return is observed only for a finite nonnegative amount
and consistent finite DNA change, including unchanged DNA for a zero amount.
Positive resource benefit requires separate native food decrease and feeder
hunger or health increase under that original claim. The original EatMeat action caller RVA `0x9713DC`
has exactly three initialized payload words `{actor, corpse, 0}`. Its scoped
action observation records the original before/after progression values.

Optional EatMeat properties can also reach strategy progress, world unlock
objects and tutorial/UI globals. Current player scopes do not establish
independent ownership of all those globals. A native report must identify the
branches exercised by its fixture; this path cannot qualify full inventory,
content-unlock or campaign progression.

## Replica and reconnect state

Wire schema 4 remains 400 bytes. The trailing fields are `fed_on` at byte 388,
signed float32 `food` at 392, and `pickup_owner` at 396. The beneficiary is
metadata from the observed native first-feed flag transition, claim and returned
original reward call, never request arrival order. A zero DNA amount does not
imply zero food transfer, and food transfer is observed separately. Zero denotes unknown; a false first-feed flag requires beneficiary zero.
Clients apply the authoritative flag and food under the existing replica guard
and reread them without invoking consume or award routines. Fresh baselines
carry the same state and absolute owner balances. Engine epoch, incarnation and
source-tick checks continue to reject stale updates.

Native acceptance requires both independently authenticated requests to be
outstanding before the first observed grant, two original order returns, one
first-feed transition and original reward return, actual native food transfer
to one claimant, matching native client projections,
and a fresh reconnect without resurrection or duplicate reward. Original
remaining-food consumption is reported separately from the once-only first-feeding action.

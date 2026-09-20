# Authority matrix

Status: required global design with **bounded native implementation in M03–M05**. M05 is VERIFIED for the recorded living Creature fixture under its [four original acceptance clauses](../evidence/2026-09-14-m05-ability/acceptance.md); whole-domain/global authority separation is not qualified. The planned global ownership lease identifies resource ID, worker ID, universe/session epoch and monotonically increasing generation. The coordinator must reject old generations before committing or forwarding outcomes. A lease does not establish that native background mutation has been suppressed; that requires traces in M04/M05/M10.

| Mutable domain | Gameplay producer | Coordinator responsibility | Client/other-worker view | Required evidence |
|---|---|---|---|---|
| Creatures, cell actors, tribal units, vehicles, ships | One context's native simulator | Actor ID/owner/generation and routing | Native presentation; no duplicate decisions | M03/M05/M07; T01/T02/T04 |
| Native NPC AI, targeting, damage, death, rewards | Same authoritative native encounter | Attribution and durable outcome identity | Observed results only | M03/M07; T02/T03/T06 |
| Pickups, food, items, inventories, currency | Native owner of affected resource | Serialize contention and transaction IDs | No locally committed spend/grant | M07/M09/M11–M15; T03/T09 |
| Species, DNA, unlocks and stage eligibility | Native simulation/editor belonging to persistent species | Species ownership, immutable creation versions, lineage | Independent progress and editor UI | M08/M11–M17; T05/T11/T12 |
| Tribes, population, tools and tribal resources | Native tribe owner in its context | Command permissions and shared-tribe grants | Unauthorized orders denied | M13; T03/T11 |
| Cities, vehicles, buildings, conquest and purchases | One native world/city authority | Ownership transfer transaction | Replica representation of same city | M14; T03/T09/T11 |
| Terrain, planet records, ecology, terraforming | One canonical planet-resource owner | Versioned artifacts and projections | Same canonical terrain/ecology in each view | M08/M10/M15/M16; T07/T10/T12 |
| Empire relationships, diplomacy, missions | Designated native owner; partition unresolved | Relationship/event identity and cross-context routing | Read-only cache until ownership proven | M10/M15/M16; T10/T12 |
| Galaxy-wide AI, disasters, economy and inactive-area outcomes | Single proven owner for each global system | Fence and deduplicate observed global events | Suppress independent mutation | M04/M10/M15; T08/T10 |
| Editor transactions | Native editor/validator plus species permissions | Begin/commit/cancel, immutable content, conflicts | Editing player only; other worlds continue | M08/M17; T05/T11 |
| Location/stage transfers | Source then destination native owner, never both | Persist prepare/quiesce/fence/restore/commit/cleanup | Scene epochs and fresh baseline | M10/M17; T07/T08/T09 |
| Administrative access and invitations | Multiplayer policy | Auth, role checks, revocation and audit | No unauthenticated administrative endpoint | M06/M18; T13/T14 |

Native AI stays active for NPC tribes/civilizations/creatures as appropriate. Human ownership may change command authority, not native formulas. Changing avatar accessors temporarily does not prove correct actor identity for asynchronous actions, rewards, timers, UI or saving.

No complete global domain row is verified. `native-behavior-baseline.md` maps mechanic coverage to native entry-point research. `multiplayer-policy.md` identifies choices introduced specifically for multiplayer.

## Current M05 implementation boundary

| Domain in the recorded Creature fixture | Authority process | Armed replica process | Current evidence |
|---|---|---|---|
| Living A health, energy, hunger and A DNA scalar | Original combat and native state capture | Absolute values applied to existing A; pure DNA setter | Native DNA 0 → 8.75 once, no second award on fresh baseline, duplicate/disconnect/publication rejection. |
| NPC/avatar AI, target, selector, ability, strike and damage | Original simulator | Gameplay entries denied; main-avatar selection admitted as presentation only | Live challenges and natural AI denials; own/foreign-species UI works. Actual bite remains active after arming and cleans up. Charge/projectile parity remains unqualified. |
| Hunger/healing, cooldown, ability-use and herd/scene timers | Original native timers | Separable mutation callbacks denied; mixed presentation routines retained | Long disconnected native runs; .25 adds no-attack timer slices. Charm/no-attack temporary probes during original Update are explicitly field fixtures, not real casts. |
| Part unlock/lock, identified grant actions, death/revival/growth/brain/removal | Original simulation | Terminal mutations and seven known part-action IDs denied | Live native item/lifecycle calls, counts/points/fingerprint comparisons; .20 action dispatch and pending-world checks. Natural consumption and every progression write remain unqualified. |
| Creature creation and population | Original factory and native scheduling | Deny creature factory plus identified parent/herd/event paths | Direct creation returns null with unchanged noun count; corrected parent/timer boundary avoids observed .18 crash. Other factory callers remain an audit boundary. |
| Save, load and outbound publication | Native owner may save/load/publish | Save/persistence-message and filename Load guards; actual ordinary Play cut before UI hiding; role never publishes | Actual .22–.24 Save and .29 saved-world Play denial with unchanged closed hashes and usable Cancel. Other unlisted load/cache routes remain unqualified. |
| Binding/destruction/scene lifecycle | Original objects and worker generation | Existing-object binding, tombstones and baseline/entity fences | Native A/B adoption creates zero nouns; actual scene exit invalidates; stale/disconnected apply, publication and IPC load denied. Native reuse, death/respawn and network lifecycle remain M06/M07 work. |
| Camera, movement, animation, audio, UI and interpolation | Original engine | Original presentation paths; narrow selection and mutation slices; no prediction/motion transport | Inspected movement/jump/selection, actual foreign-species social UI, active-bite cleanup and usable menu denial. Descriptive .23 frame/audio capture retained; perceptual listening and full effects parity unqualified. |

The developer bootstrap is neither an authoritative baseline nor a safe player-facing connected mode. It loads the sealed fixture before arming and never reopens after disconnect. [M05 bindings, lifecycle and native evidence](m05-replicas.md) describes the exact coverage and next experiment. The current private IPC adapter is not global authority leasing, network authentication or multiplayer gameplay.

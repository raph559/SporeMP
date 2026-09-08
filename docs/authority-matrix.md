# Authority matrix

Status: required design, native implementation **TODO**. An ownership lease identifies resource ID, worker ID, universe/session epoch and monotonically increasing generation. The coordinator rejects old generations before committing or forwarding outcomes. A lease does not establish that native background mutation has been suppressed; that requires traces in M04/M05/M10.

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

All rows remain unverified. `native-behavior-baseline.md` maps mechanic coverage to native entry-point research. `multiplayer-policy.md` identifies choices introduced specifically for multiplayer.

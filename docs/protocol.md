# Protocol contract — planned

No network transport has been integrated in M01. These requirements constrain M04/M06 implementation; they are not functioning messages or a completed networking demo.

Every serialized envelope needs protocol/schema version, authenticated identity, universe/session epoch, command sequence, actor/resource ID, entity/resource generation, context/location ID, ownership generation and bounded payload length. Never send pointers, raw native object memory, STL/EASTL containers or native layout assumptions.

Message families: connect/authenticate; join/resume; content manifest/fetch; scene begin/readiness; entity create/update/remove; action request/accept/reject/result; native authoritative events; checkpoint/delta; editor begin/commit/cancel; transition prepare/commit/abort; resynchronization; shutdown/error. Each requires ownership and epoch validation appropriate to its domain.

Lifecycle, spending, rewards, progression, edits, ownership and transfers require reliable delivery with duplicate handling. Motion uses sequenced replaceable snapshots, baseline acknowledgments, entity tombstones and scene epochs; stale motion cannot recreate a deleted entity. Reconnect obtains a fresh authoritative baseline. Bulk content must not monopolize action traffic.

M06 selects numeric limits for every packet, decompression output, queue, timeout and rate from measured fixtures. Parser tests cover truncation, overflow, oversized fields, unknown versions, replay, reordering and exhausted queues. Do not implement bespoke cryptography or reliable UDP. GameNetworkingSockets is a candidate pending its exact build/authentication/deployment check; no standalone Steam relay entitlement is assumed.

Acceptance means a request is validated/queued, application means the native engine produced its outcome, and durable acknowledgment means recoverable committed state. M09 must define which actions use which acknowledgments and prove crash behavior. A journal or database transaction alone is not native crash atomicity.

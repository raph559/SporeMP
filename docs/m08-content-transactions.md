# M08 content transfer and editor transactions

Bridge/NativeHost **0.0.50**, network schema **7**, 600-byte packets, peer/server
config schema **2**. Qualified for the pinned native Creature fixture; see
[acceptance](../evidence/2026-09-22-m08-completion/SESSION.md).

## Ownership and publication

An authenticated player begins against their current immutable version. The
coordinator fences owner connection, base version, authority connection and scene
generation. One draft per player, two drafts total, expires after five minutes.
Disconnect/authority replacement cannot inherit readiness. Cancellation and
expiry do not publish; an uncertain RPC is never automatically repeated.

The editor remains original SPORE. Passive listeners observe native request and
accepted/cancelled results; their routing field is not a transaction nonce.
`tools/native/m08-editor-artifact.py` requires the healthy closed native save
witness, a later inspection, clean process exits, matching bridge/config identity,
and unchanged canonical world files before extracting the original PNG and copied
native observation. Source acceptance alone does not authorize publication.

`SporeMP.ContentClient.exe` owns authenticated Windows TLS and exchanges bounded
value frames over private standard input/output. `tools/native/m08-content-session.py`
provides developer begin/upload/ready/commit/cancel controls through an atomic
private file queue. It is not a native gameplay client. The explicit
`--fixture-authority` option exists for labeled adversarial HOST tests; the real
positive acceptance uses original SPORE as authority.

The coordinator quarantines PNG bytes, validates their hash and envelope, then
lets only the authority fetch the sealed candidate. The authority's original
`ImportPNG` and `LoadCreatureData` calls derive the native resource key, rigblocks,
capability entries and part roots. Values are copied on the engine thread; owned
loader results are released once. Local keys differ between profiles and are
mapped separately from the global PNG/version identities.

After authority attestation, each peer fetches the immutable dependency manifest.
Its installed-content identity, six world hashes, native-property digest, PNG hash
and size must agree. Original exact/mapped lookups through both qualified resource
manager getters resolve every required part **before PNG fetch/import**. A missing
key clears readiness and is reported to the owner. A matching peer imports once,
loads native data and acknowledges its actual observation. The owner also supplies
the closed source's native observation. Every live participant must be ready
before one immutable version is committed. Altered client numbers cannot replace
the authority's copied values. Duplicate/stale commits fail.

The manifest hashes a versioned explicit encoding of owner, revision, parent,
PNG digest/size, native properties, installed-content identity, six world hashes,
model type, counts and sorted native part keys. The registry retains old immutable
versions and per-connection native mappings. It is bounded to 32 versions and
16MiB cached PNG data; it is not durable across coordinator restart (M09).

## Native intake boundary

Native content receive is enabled only for explicit supervised developer workers
with a fixed local `m08-content-allowlist.txt`, containing 1–32 approved SHA-256
values. Approval is rechecked before import. A network peer cannot extend this
file or supply an import path. Normal Play leaves the receiver disabled.

Candidates are at most 4MiB; uploads are ordered, bounded and hash/CRC checked.
The native receiver creates `content-<sha256>.png` exclusively in its private run,
flushes it, and uses the original import only after regular-file/link/reparse,
size, PNG envelope and hash checks. A false original import result is never
promoted to success even if it returns an existing key. Successfully imported
same-process content may be reinspected after a readiness fence; the cache is
cleared at disposal. Jobs are bounded to two with a 120-second deadline.

This is locally approved native-produced content. It is not a promise that the
original decoder safely handles arbitrary Internet PNGs. No installed game asset
is transferred. The pinned full installed-content inventory covers dependencies
outside the nine directly observed part roots; arbitrary mod dependency discovery
is unqualified.

## Value protocol

ContentWire schema1 occupies 512 bytes within the existing 600-byte packet.
It has a 92-byte explicit little-endian header and at most 420 payload bytes;
unused bytes must be zero. No pointers or native containers cross processes.

| Operations | Meaning |
|---|---|
| 1–5 | begin, offer, chunk, seal, fetch |
| 6–11 | observation_begin, observation_chunk, observation_end, commit, cancel, current |
| 12–15 | validate_event, load_event, published_event, cancelled_event |
| 16–18 | dependencies, dependency_failure, dependency_event |

Native observation encoding has a 292-byte header, 40-byte rigblocks, 8-byte
capability entries and 12-byte resolved part keys. The coordinator bounds it to
64KiB and validates model type, hierarchy, capability ranges, sorted/unique roots,
world/content identities and exact digests. Dependency encoding has a 304-byte
header plus 1–512 sorted unique 12-byte `.prop` keys, total316–6448 bytes. Chunks
carry the whole dependency-manifest digest, total size and ordered offset.

Coordinator logs record operation/request/transaction, result, content identity
and exact mismatch key/index, never PNG bodies or credentials. A queued dispatch
is not native application: correlate native import return, copied observation,
readiness and published-version events. Native low-level observations deliberately
retain `readiness:false`; only the separate transaction establishes publication.

## Terrain and limits

The fixed [canonical world bundle](m08-world-identity.md) is provisioned from the
complete closed native fixture, then checked in the actual account before game
creation and during TLS admission. Runtime terrain maps to the persisted property
record in `PlanetScripts.pld`; a same-size changed record blocks startup with the
exact filename. This is not seed-based world regeneration or dynamic planet
streaming. M09 adds durability; later stage/location milestones retain their full
native requirements. Imported model availability does not authorize campaign
species replacement through ordinary movement packets.

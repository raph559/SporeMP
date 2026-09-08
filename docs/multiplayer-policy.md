# Multiplayer policy — initial defaults

These choices address multiplayer ownership and coexistence. They do not change native combat, production, resource formulas or progression requirements. Implementation is TODO beyond M01 diagnostics.

- Private invite-based universe; authenticated membership and operator roles. Initial PvP default: disabled, with an explicit server configuration to enable it and a tested enforcement path. This is a project policy default, not native engine behavior.
- Separate persistent player, species and faction identities by default. Optional shared-faction play needs explicit permissions for unit control, resources, edits and progression consent; connection to a faction is not unrestricted ownership.
- Concurrent actions are serialized/fenced by the owner of the affected resource. Native validation determines outcomes. A duplicate pickup/spend request cannot grant duplicate results.
- Editing isolates the player's participation; unaffected players and workers keep progressing. Native pause/editor behavior must be adapted and tested rather than globally propagated.
- Disconnect relinquishes live input rights, preserves persistent identity and follows an explicit stage-specific avatar/AI policy once tested. No disconnected-avatar immortality/despawn behavior is assumed yet.
- Transitions affect the owning species/faction only. A Space visitor must not force a lower-stage player to advance. Applicable original cross-stage consequences remain required.
- Publish supported content, player counts and worker requirements only after real-game tests. Invite-only scope does not waive compatibility, authentication, ownership or parser checks.

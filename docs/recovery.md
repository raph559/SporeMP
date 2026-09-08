# Save protection, rollback and recovery

M01 provides quiescent **file backups**, verified by SHA-256. It does not provide a native checkpoint manager or crash-atomic universe persistence. M09 owns those requirements.

## Before native testing

Close SPORE normally. `backup` checks the process list before and after copying, rejects overlapping destinations and reparse points, captures file/directory manifests before and after, verifies copied files and writes its commit manifest only if all comparisons agree. It never overwrites an existing destination. Failure leaves an incomplete directory without a committed manifest. Do not start the game while this operation runs.

```powershell
python tools/diagnostics/sporemp_diag.py backup --source 'C:\Users\Developer\AppData\Roaming\Spore' --source 'C:\Users\Developer\Documents\My Spore Creations' --destination local/backups/unique-run-name
python tools/diagnostics/sporemp_diag.py verify-backup --destination local/backups/unique-run-name
```

The first session's backup is `local/backups/m01-before-native`. Game data and backups are excluded from Git. File-integrity verification is not native save/load verification. Add any additional native/launcher/configuration locations discovered by access tracing before treating save protection as complete.

## Player flow and rollback

The player launcher now uses the normal Windows account and existing saves. It does not call the backup/profile tools. This follows the user's explicit 2026-09-08 correction. Earlier backup instructions above remain optional developer utilities, not player prerequisites.

Player settings, diagnostics and staged project DLLs reside under local/launcher. The game directory receives no project DLL installation. Close SPORE normally before changing staged loader files. Removing or disabling the launcher does not require replacing game files or restoring personal saves.

The earlier developer probes created the standard account SporeMP-M01, C:\Users\SporeMP-M01 and C:\ProgramData\SporeMP\M01, with ownership metadata and a DPAPI-protected credential in local/native-account. They are not used by normal Play. Preserve their evidence/backups unless explicitly choosing to remove them.

For developer setup rollback: first stop its game/host, verify the account SID against local/native-account/account.json, then disable that exact account. Remove only the explicit game-root deny ACE belonging to that recorded test SID if retiring the developer arrangement. Do not restore the entire original ACL blindly or change other users' entries. The original SDDL is retained in local/native-account/game-original-acl.txt. Profile/account deletion is optional and was not performed. Any later deletion must target the verified owned paths, never the player's profile.

Personal-save restoration remains an optional future recovery operation: retain the live/damaged state, verify a complete backup, restore to a separate target first and validate before replacement. No automatic restore or native checkpoint manager is provided by M01.

## Future persistent universe contract

M09 must coordinate safe native checkpoint boundaries, completed artifacts, ID/content/ownership metadata and atomic manifest publication, with the last good checkpoint retained. Test kills between native effect, captured event, database transaction, artifact write and durable acknowledgment. Do not replay arbitrary inputs to a non-deterministic engine or treat an event journal as a complete save. M10 adds a recoverable prepare/fence/restore/commit transfer protocol; stale source workers lose commit permission.

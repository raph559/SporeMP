# Public evidence and privacy

This repository is a reviewed public export of the development history. The original history, source brief and unmodified test artifacts are retained in a separate private archive. Public publication does not add native acceptance or change the M00–M20 plan.

Before publication, all reachable text history was reviewed for credential patterns and personal identifiers. Personal commit email addresses were replaced with the maintainer's GitHub noreply address. Recorded developer account paths, computer names and machine/account SID prefixes were replaced with stable generic values. Native actor IDs, behavior, timestamps, test outcomes and limitations remain part of the recorded evidence.

Historical screenshots and photos under `evidence/` were omitted from public history because they can contain personal desktop or account information. The original project illustration under `src/launcher/Assets/` and its website copy remain published with provenance. The omitted captures remain available to the maintainer in the private archive. Historical links or image paths in reports refer to those original captures; their absence here must not be interpreted as a newly repeated visual test.

## Hashes and reproducibility

Redaction changes bytes. Historical source revisions, SHA-256 values and artifact manifests describe the private original runs unless a report explicitly labels them as public-export hashes. A mismatch against a redacted export is expected and does not establish tampering with the original artifact. The historical acceptance decisions remain bounded to their stated executable, content, SDK, fixture and execution conditions.

The public Git history has different commit identifiers after privacy filtering. Do not merge old private branches into the public repository. Recover a needed implementation change by reviewing and reapplying its code, followed by the appropriate checks.

## Future reports

Keep raw captures, full logs, invitations, account inventories and save data under ignored `local/`. Publish the smallest reviewed reproduction and relevant redacted output. Never publish credentials, private certificates, personal saves or extracted game assets. New raw evidence captures and trace files are ignored by default; an explicit reviewed export is required to add one.

State whether evidence is BUILD, HOST, FIXTURE/MOCK, NATIVE or NETWORK-REAL. Report unavailable checks as NOT RUN with their prerequisite. Sanitized historical evidence is not a substitute for new native acceptance of a changed implementation.

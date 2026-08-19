# Security and Threat Model

- **Status:** Proposed; review required before Python, plugins, AI, or cloud release
- **Requirement prefix:** `SEC`
- **Depends on:** [Processes and IPC](../foundations/07-processes-and-ipc.md), [Engineering API](../foundations/08-engineering-api.md), [persistence](../foundations/06-persistence-and-recovery.md)
- **Unblocks:** automation, plugins, AI, collaboration, distribution

## 1. Assets

Protect:

- proprietary geometry, requirements, drawings, scripts, metadata, and history;
- integrity of dimensions, references, simulation assumptions, releases, and exports;
- credentials, provider tokens, signing keys, and organization policy;
- local filesystem/network and other projects;
- application availability and compute/storage budgets;
- provenance/audit evidence and update chain.

## 2. Adversaries and failures

Plan for malicious or malformed project/import/plugin/script content; prompt injection in engineering data; compromised AI/provider/plugin/update service; hostile collaborator; untrusted local user context; dependency vulnerability; and accidental defects that behave like attacks through memory exhaustion, parser crashes, or silent engineering changes.

Kearne does not initially defend project confidentiality from an administrator controlling the local OS. This boundary must remain explicit.

## 3. Trust boundaries

```text
QML/user input -> Engineering API
project/import bytes -> parser/migration
coordinator <-> workers/shared artifacts
Python/plugin code -> OS/files/network/project API
AI context/tools <-> model provider
desktop <-> collaboration/update/crash services
desktop observation -> screenshots/semantic UI/input actions
Codex app-server <-> Kearne Agent Bridge/provider/auth
package/update metadata -> installed binaries
```

Each boundary has authentication where applicable, version negotiation, size/depth/count limits, typed validation, time/resource budgets, structured failure, and fuzz/fault coverage.

### SEC-001 — Treat parsers as untrusted

File extensions, local origin, child-process origin, signatures, and content digests do not make decoded data safe. Validate before allocation/use and isolate complex parsers.

### SEC-002 — Least privilege capabilities

Filesystem read/write, network destination, project read/write, export, Python, native execution, AI disclosure, simulation compute, and administrative repair are separate unforgeable capabilities scoped by project, operation, resource, and lifetime.

### SEC-003 — Recheck at execution

UI hiding, model instructions, plugin manifests, and client-side checks are not authorization. The coordinator validates actor capability on every query, command, artifact grant, and operation.

## 4. Threat controls

| Threat | Required control |
|---|---|
| Project/import memory corruption or denial | isolated parser/worker, bounded decoder, fuzzing, quotas, coordinator-safe failure |
| Path traversal/symlink/device path | brokered handles, normalized destination policy, no worker-chosen host paths |
| Malicious Python/plugin | denied-by-default capabilities, OS isolation where proven, process kill, signed/pinned runtime |
| Worker impersonation/stale publish | per-launch authentication, worker-instance/job IDs, artifact broker, generation checks |
| Prompt injection/data exfiltration | untrusted-content delimiting, deterministic policy, query disclosure classes, destination allowlist, confirmation |
| Command replay/duplication | authenticated context, request idempotency, base revision, expiry where needed |
| Project tampering/corrupt cache | integrity digests, structural/domain validation, cache treated untrusted, recovery copy |
| Dependency/update compromise | locked sources/digests, SBOM, signing, isolated release builders, verified updater/rollback |
| Resource exhaustion | per-role CPU/memory/time/process/output quotas, bounded queues, circuit breakers |
| Sensitive logs/crash dumps | structured redaction, no secrets/prompts/source bytes by default, opt-in upload |
| Screenshot disclosure | Kearne-session scope by default, separate display-capture grant, private temporary artifacts, expiry, provider disclosure check |
| Agent UI control abuse | visible revocable session capability, semantic action allowlist, normal UI/controller path, audit correlation |
| Compromised or incompatible Codex runtime | verified path/version, generated-schema handshake, explicit environment/tools/sandbox/network, kill/restart, `No AI` fallback |
| Confused deputy across projects | project-scoped handles and actor context; no ambient active-project authority |
| Silent engineering manipulation | command-only writes, revision/provenance, semantic diff, no ambiguous topology guesses |

## 5. Project and artifact handling

- Never execute embedded scripts/plugins on open without policy and user intent.
- Unknown entities remain inert opaque data.
- Project thumbnails/metadata are parsed with the same caution as geometry.
- External links do not auto-fetch from network in local-only mode.
- Temporary files use private directories/permissions and atomic publication.
- Sensitive memory is minimized; secrets use OS credential stores and never enter project/history schemas.

### SEC-004 — Safe open mode

Kearne provides a mode that opens semantic metadata/read-only retained artifacts without executing plugins, Python, AI, external links, migrations with code from unknown providers, or untrusted evaluators.

## 6. AI-specific controls

- Provider choice and project disclosure permission are visible.
- Retrieved document text is data, never authority.
- Tool policy is local and deterministic.
- Tool results cannot grant new capabilities.
- Export/network/Python/plugin installation require separate policy and confirmation.
- Context and results are bounded; provider responses are untrusted schema input.
- Model/provider identity and tool provenance are retained without secrets.
- App-server runtime approvals and Kearne engineering/disclosure approvals remain independent.
- Screenshot paths are brokered and short-lived; adding an image to a turn requires the same disclosure policy as project data.

## 7. Desktop observation controls

Complete Kearne-session capture excludes unrelated applications. Full-display capture is disabled by default and requires a separate capability because it may expose other projects, applications, notifications, or credentials. OS secure desktops and permission surfaces are never bypassed.

The observation driver accepts authenticated local sessions, bounded requests, stable control IDs, and allowed public actions. It cannot call private QML mutation functions. Captures and semantic snapshots do not enter project history, telemetry, or crash reports by default and are cleaned under quota/expiry policy.

## 8. Plugin and supply-chain controls

Packages are signed/content-addressed, declare capabilities, pin dependencies, and execute out of process by default. Permission expansion on update requires review. Revoked/vulnerable plugins may be disabled while opaque project data and source artifacts remain recoverable.

Release signing keys use restricted hardware/service-backed storage, separated roles, auditable access, and recovery/rotation procedures.

## 9. Collaboration controls

Authentication, tenant/project authorization, branch compare-and-swap, encryption in transit, storage isolation, audit, rate/quota limits, backups, deletion/retention, and regional policy require independent review. The client validates server objects and does not delete local data because a remote project disappeared.

## 10. Security verification

- Threat-boundary fuzz targets and hostile schema generators.
- Capability state machines proving denied operations cannot succeed through alternate adapters.
- Worker sandbox/escape probes on each OS release baseline.
- Dependency/SBOM/advisory and secret scanning.
- Update signature, downgrade, rollback, mirror, and tamper tests.
- Prompt-injection/adversarial tool-loop suites.
- Capture-scope tests proving application capture excludes unrelated windows and display capture requires its distinct grant.
- Codex protocol/version corruption, approval-confusion, brokered-image, and hostile-tool-sequence tests.
- Static analysis and sanitizers on supported toolchains; memory-safe parser components for new untrusted formats unless a reviewed dependency prevents it.
- Periodic external review before enabling untrusted plugins, cloud AI, or collaboration.

Security regressions receive permanent generator/fuzz/property coverage when possible, not only one fixed exploit file.

## 11. Incident behavior

Kearne can disable a vulnerable network/provider/plugin integration without blocking local safe-open access to projects. Advisories identify affected versions, risk, workaround, update, data exposure, and artifact/plugin compatibility. Audit and crash evidence follow retention/privacy policy.

## 12. Open decisions

- **SEC-OPEN-001:** Platform sandbox guarantees and fallback wording.
- **SEC-OPEN-002:** Local project encryption and OS credential integration.
- **SEC-OPEN-003:** Code-signing identities, key custody, and update service.
- **SEC-OPEN-004:** Cloud provider/data residency/retention contracts.
- **SEC-OPEN-005:** Security response and supported-version policy.
- **SEC-OPEN-006:** Agent-access enablement, consent indicator, and organization policy.

## 13. Definition of done

A high-risk capability ships only after its trust boundary, capabilities, limits, adversarial tests, dependency chain, recovery behavior, product wording, and independent review findings meet its release gate.

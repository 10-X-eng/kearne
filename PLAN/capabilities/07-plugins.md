# Plugin System

- **Status:** Proposed; post-MVP implementation
- **Requirement prefix:** `PLG`
- **Depends on:** [Engineering API](../foundations/08-engineering-api.md), [processes](../foundations/07-processes-and-ipc.md), [security](../delivery/06-security-threat-model.md)
- **Unblocks:** third-party source helpers, graphical operations, solvers, formats, tools, and UI extensions

## 1. Purpose

Extend Kearne without making project safety, API compatibility, determinism, or application stability depend on an unrestricted in-process binary ABI.

## 2. Plugin classes

```text
Command/query plugin
Model-function environment/helper package
Graphical-operation tooling
Import/export adapter
Analysis/simulation backend
Material/standards data pack
AI tool/provider adapter
Declarative UI contribution
Trusted native integration (exception)
```

### PLG-001 — Out-of-process default

Third-party executable code runs through the worker and Engineering API protocols by default. A stable wire/API contract is the public compatibility boundary; Kearne does not promise C++ ABI stability across releases.

### PLG-002 — Trusted-native exception

In-process native plugins require explicit trusted installation, exact ABI/build compatibility, signing policy, and restart. They are intended only where measured latency or GPU integration cannot be met out of process.

## 3. Package and manifest

A plugin package declares:

```text
globally unique plugin ID and version
publisher/signature identity
minimum/maximum Kearne API and schema versions
entry points and worker roles
record/function-contract/operation/command schemas
requested permissions
resource budgets
dependencies and platform artifacts
migration/evaluator fingerprints
license and update source
UI contributions
```

### PLG-003 — No undeclared capability

Filesystem, network, project read/write, geometry evaluation, Python, AI, UI, export, and solver access are separately declared and granted. Runtime enforcement does not trust the manifest alone.

### PLG-004 — Stable schema ownership

Plugin-defined record, function-contract, and graphical-operation kinds are namespace-qualified by immutable plugin ID. Removing or renaming the plugin cannot collide with another plugin's schemas.

## 4. Lifecycle

Install, enable, upgrade, downgrade, disable, and uninstall are explicit operations. Project-required plugin versions are resolved before writable open.

### PLG-005 — Missing plugin preservation

Projects retain source, opaque plugin records/contracts, requirements, and last-known-good derived artifacts when the plugin is missing. Dependent function calls and records report `UnavailableEvaluator`; Kearne MUST NOT drop unknown fields on save.

### PLG-006 — Upgrade migration

Plugin schema migrations execute against a copy/transactional project migration under resource and permission bounds. Upgrade failure leaves the prior project unchanged; incompatibility may require the prior plugin version or safe-open mode.

### PLG-007 — Version pinning

A project's evaluated plugin results record the exact compatibility fingerprint. Automatic plugin updates do not silently recompute released or opened documents under new behavior.

## 5. Declarative extension points

To minimize duplicated code, command, function-contract, and graphical-operation descriptors can supply property fields, command-palette metadata, icons, documentation, AI tool metadata, and generic inspectors. Complex UI contributions use a constrained declarative surface; arbitrary QML injection into the main engine is not the default.

Descriptors reduce repeated adapter code but do not replace domain validators or specialized interaction tools.

## 6. Distribution and trust

- Packages are content-addressed and signature-verifiable.
- Publisher trust, package permission changes, and unsigned-development mode are visible.
- Dependency resolution is deterministic and lockable per project/environment.
- Revocation and vulnerability advisories do not destroy local project access; they may disable execution while preserving data/read-only artifacts.
- Enterprise policy may allowlist publishers, plugin IDs, versions, and permissions.

## 7. Verification strategy

The plugin SDK ships executable conformance suites for every extension port. Generated synthetic plugins exercise:

- valid/invalid manifests and dependency graphs;
- schema collisions and evolution;
- permission denial and escalation attempts;
- install/upgrade/downgrade/remove with projects open and closed;
- crash, hang, malformed messages, oversized artifacts, and nondeterminism;
- missing-plugin load/save preservation;
- API minor/major compatibility matrices.

Kearne CI runs representative plugins built against oldest-supported SDK versions. Plugin authors run the same suite locally.

## 8. Open decisions

- **PLG-OPEN-001:** Package/archive format, signature ecosystem, and marketplace policy.
- **PLG-OPEN-002:** Declarative UI technology and isolation.
- **PLG-OPEN-003:** Dependency/runtime strategy for Python versus native worker plugins.
- **PLG-OPEN-004:** Public API compatibility window.
- **PLG-OPEN-005:** Trusted-native approval and crash-report policy.

## 9. Definition of done

The plugin system is implemented when an independently built sample for each supported class passes the SDK conformance suite, missing plugins never lose project data, permission faults are contained, and no plugin requires private headers or QML-to-core backdoors.

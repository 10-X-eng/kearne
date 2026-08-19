# TECH-001 — Schema and Binding Pipeline

- **Status:** In progress
- **Question:** Can one IDL generate bounded C++/Python/CLI/AI boundaries while the domain remains strongly typed?
- **Requirements:** `API-001` through `API-014`, `PLAN-001`, `PLAN-002`, `ARCH-001`, `ARCH-002`, `TST-002`, `TST-004`
- **Decision:** [ADR-0004](../../PLAN/adr/0004-one-engineering-api.md)

The probe pins Protobuf 35.1 and Abseil 20250512.1 source archives by SHA-256. Protobuf Editions 2024 declarations generate C++ and Python types, a descriptor set, registry metadata, and bounded AI tool JSON. The executable converts wire values into strong domain wrappers before one validator and reducer. Binary framing is the compatibility path; ProtoJSON is limited to CLI/tool edges.

The same rename/query scenario runs in-process, over framed local IPC, from generated Python, through CLI JSON, and from generated AI-tool arguments. Additional probes cover unknown entity payloads, additive unknown fields, JSON information loss, malformed/oversized frames, deterministic metadata, and a reusable parser fuzz target.

This prototype is not production code. Selection requires retained measurements, supported-platform evidence, an accepted schema ADR, and an upgrade policy.

Run it:

```sh
cmake -S prototype/001-schema-binding-pipeline -B build-prototype-001 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-prototype-001 --parallel
ctest --test-dir build-prototype-001 --output-on-failure
python3 -m venv build-prototype-001/venv
build-prototype-001/venv/bin/pip install protobuf==7.35.1
build-prototype-001/venv/bin/python prototype/001-schema-binding-pipeline/probe.py --build-dir build-prototype-001
```

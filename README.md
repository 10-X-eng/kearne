# Kearne

Kearne is a new local-first, AI-native mechanical CAD system for parametric and direct modeling, assemblies, simulation, versioned design, drawings, and automation. It is not a FreeCAD rewrite. Native Python/build123d functions are canonical part geometry; C++ owns the project, product semantics, jobs, persistence, and Qt/QML desktop; OCCT evaluates geometry; Codex app-server supplies the AI harness without owning project state.

The project is building the production desktop frontend first. Start with:

1. [Product and architecture specification](SPEC.md)
2. [Engineering plan and document map](PLAN/README.md)

Production code lives outside [`prototype/`](prototype/README.md). Prototypes are permitted only for unresolved feasibility risks and are excluded from the product build. The [implementation sequence](PLAN/delivery/01-implementation-sequence.md) defines frontend, backend, and release gates.

Desktop work is accepted only when the agent harness can launch the build, inspect semantic UI state, and return a lossless capture of the complete visible Kearne session.

```sh
# Debian-family development host: install native prerequisites once.
tools/install-dev-deps-debian.sh

# Existing provisioned host:
python3 tools/bootstrap.py
ctest --preset dev
```

The host installer installs the compiler, Qt, yaml-cpp, SpaceMouse headers, and build tools, then runs the bootstrap. The bootstrap creates `.venv`, installs pinned Python tooling, and invokes the selected CMake preset. CMake resolves checksum-pinned native dependencies. The installer refuses a distribution whose Qt is older than 6.8.

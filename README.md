# Kearne

Kearne is a new local-first, AI-native mechanical CAD system for parametric and direct modeling, assemblies, simulation, versioned design, drawings, and automation. It is not a FreeCAD rewrite. Qt/QML provides the Windows and Linux desktop shell; C++ and OCCT provide the engineering core; Python and build123d provide procedural modeling; Codex app-server provides the AI harness without owning CAD state.

The project is in architecture definition. Start with:

1. [Product and architecture specification](SPEC.md)
2. [Engineering plan and document map](PLAN/README.md)

Implementation begins with the risk-retiring spikes and stage gates in the engineering plan. Unresolved decisions marked `OPEN` must be settled before they affect persisted data or public interfaces.

Desktop work is accepted only when the agent harness can launch the build, inspect semantic UI state, and return a lossless capture of the complete visible Kearne session.

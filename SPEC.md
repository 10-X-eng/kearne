# Kearne — AI-Native Mechanical CAD Platform

## Product & System Architecture Specification

**Status:** Architecture baseline
**Implementation contracts:** [`PLAN/`](PLAN/README.md)
**Target platforms:** Windows 11+, modern Linux distributions
**Primary domain:** Mechanical CAD, assemblies, engineering simulation, technical documentation
**Core geometry kernel:** Open CASCADE Technology (OCCT)
**Primary application language:** C++23
**UI:** Qt 6 / Qt Quick / QML
**Automation and AI geometry:** Python 3 + build123d
**AI harness:** Codex app-server behind a Kearne-owned adapter and tool policy
**Architecture principle:** Humans, AI agents, scripts, plugins, and external APIs operate on the same document and command model.

---

# 1. Product Vision

Kearne is a new product, not a FreeCAD rewrite, fork, workbench replacement, or compatibility clone. Technology and architecture are selected from Kearne's requirements and measured evidence.

The product shall be a high-performance, AI-native mechanical engineering environment combining:

* professional parametric solid modeling;
* direct modeling;
* surface modeling;
* mechanical assemblies;
* engineering simulation;
* drawing/documentation workflows;
* built-in version history;
* design branching and merging;
* configuration management;
* scripting and extensibility;
* local-first performance;
* optional cloud collaboration and compute;
* AI capable of understanding and modifying the complete engineering model.

The product should feel less like a traditional CAD program with an AI chat panel added to it and more like an engineering operating environment in which AI is another first-class participant.

A human should be able to create an extrusion by clicking a toolbar.

A Python plugin should be able to create the same extrusion through an API.

An AI agent should be able to create the same extrusion through a structured command.

All three shall result in the same feature node in the same document graph.

The central architecture is therefore:

```text
                   USER INTENT
                       │
       ┌───────────────┼────────────────┐
       │               │                │
       ▼               ▼                ▼
   GUI Tools          AI             Python
       │               │                │
       └───────────────┼────────────────┘
                       ▼
                COMMAND SYSTEM
                       │
                       ▼
                DOCUMENT GRAPH
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
        CAD        ASSEMBLY     SIMULATION
          │            │            │
          └────────────┼────────────┘
                       ▼
                      OCCT
```

---

# 2. Competitive Product Principles

The product shall incorporate several ideas demonstrated successfully by Autodesk Fusion and Onshape, while implementing them in a local-first, AI-native architecture.

Fusion demonstrates the value of keeping parametric modeling, assemblies, simulation, drawings, manufacturing-related workflows, and centralized product data connected rather than treating them as unrelated applications. Fusion supports parametric, direct, surface, freeform and mesh workflows and uses design data downstream in assemblies, simulation, drawings and manufacturing.

Fusion's assembly model also demonstrates the usefulness of motion-aware joints: a joint describes component relationship and allowed degrees of freedom rather than requiring the user to accumulate numerous independent constraints.

Onshape demonstrates several data-model features that should be foundational rather than optional: immutable versions, document history, branches, merging, configurations, linked references, release management and built-in PDM.

Onshape also demonstrates the value of associating simulation directly with assemblies, materials and mechanical relationships instead of forcing users into a disconnected analysis application. Its simulation system supports structural and modal analysis while keeping studies associated with the underlying design.

The proposed system shall combine these concepts with a stronger local desktop architecture and substantially deeper AI integration.

---

# 3. Product Principles

## 3.1 Local-first

Core CAD operations must work without an internet connection.

The following shall be available offline:

* sketching;
* parametric modeling;
* direct modeling;
* assembly editing;
* local simulation where an installed solver supports it;
* drawings;
* import/export;
* scripting;
* project history;
* local AI models where configured.

Cloud services may enhance the experience but shall not be required to open or edit a user's design.

---

## 3.2 GPU-smooth interaction

The viewport shall be treated as a real-time application.

Targets:

* 60 FPS normal interactive manipulation;
* 120 FPS where hardware allows;
* camera movement must never wait for CAD recomputation;
* selection highlighting should generally appear within one frame;
* geometry recomputation must never execute synchronously on the UI thread;
* simulation jobs must never block editing;
* large assembly loading shall be incremental.

---

## 3.3 Non-destructive parametric editing

Every normal modeling operation shall remain editable unless explicitly converted into a dumb/imported body.

Users must be able to change:

* dimensions;
* sketch constraints;
* feature parameters;
* references;
* material;
* component configuration;
* assembly relationships.

The system shall recompute downstream dependencies automatically.

---

## 3.4 AI actions are real CAD actions

AI shall not modify opaque geometry blobs whenever a semantic operation can be represented.

For example:

```text
Bad:

AI creates arbitrary modified BREP.

Preferred:

AI creates HoleFeature
    diameter = 6 mm
    type = clearance
    pattern = rectangular
    referenceFace = ...
```

The user must subsequently be able to edit the resulting hole through ordinary CAD tools.

---

# 4. Technology Stack

## Desktop application

```text
C++23
Qt 6
Qt Quick / QML
OCCT
```

## Scripting

```text
Python 3
build123d
OCP
```

## Optional future scripting environments

* Lua for lightweight macros;
* JavaScript/TypeScript for UI extensions;
* WASM for sandboxed portable plugins.

Python remains the primary engineering scripting language.

---

# 5. Process Architecture

The application shall be multi-process, not merely multi-threaded.

```text
┌───────────────────────────────────────────────┐
│                 APPLICATION                   │
│                                               │
│ UI Thread                                     │
│ QML / commands / interaction                  │
│                                               │
│ Render Thread                                 │
│ viewport / GPU / picking                      │
│                                               │
│ Core Worker Pool                              │
│ lightweight parallel work                    │
└────────────────┬──────────────────────────────┘
                 │
        IPC / shared memory
                 │
   ┌─────────────┼─────────────┐
   ▼             ▼             ▼
Geometry      Python       Simulation
Workers       Workers       Workers
   │             │             │
  OCCT       build123d        solver
```

---

# 6. Core Services

The application core shall consist of independently testable services.

```text
ApplicationCore
│
├── DocumentService
├── CommandService
├── FeatureService
├── GeometryService
├── SketchService
├── AssemblyService
├── ConstraintService
├── SimulationService
├── RenderService
├── SelectionService
├── MaterialService
├── VersionService
├── ConfigurationService
├── ImportExportService
├── AIService
├── PluginService
├── JobService
└── PersistenceService
```

No QML object should directly own engineering state.

QML shall be a presentation layer over application models.

---

# 7. Document Architecture

The document graph is the heart of the application.

A document shall contain semantic objects rather than merely a sequence of OCCT shapes.

Example:

```text
Document
│
├── Metadata
├── Variables
├── Configurations
│
├── Component: BasePlate
│   ├── Origin
│   ├── Sketch001
│   ├── Extrude001
│   ├── HolePattern001
│   └── Fillet001
│
├── Component: Motor
│
├── Assembly
│   ├── Instance001
│   ├── Instance002
│   └── Joint001
│
├── Simulation
│   └── StaticStudy001
│
└── Drawing
    └── Sheet001
```

Every node shall possess a persistent UUID.

Example:

```cpp
using ObjectId = UUID;
```

---

# 8. Dependency Graph

Features shall form a directed dependency graph.

Example:

```text
Sketch001
   ↓
Extrude001
   ↓
Fillet001
   ↓
Hole001
```

But arbitrary references are possible:

```text
Parameter: thickness ─────┐
                          ▼
Sketch001 → Extrude001 → Shell001
                          ▲
ExternalGeometry ─────────┘
```

Each node shall report:

* inputs;
* output objects;
* dependencies;
* dirty state;
* compute state;
* compute duration;
* errors;
* warnings.

---

# 9. Incremental Recompute

Changing one parameter must not cause the entire document to rebuild.

Example:

```text
A → B → C → D

X → Y
```

Changing `B` should invalidate:

```text
B
C
D
```

while leaving:

```text
A
X
Y
```

unchanged.

Node states:

```text
Clean
Dirty
Queued
Computing
Complete
Warning
Failed
Suppressed
```

Recompute must execute asynchronously.

---

# 10. Feature Evaluation Contract

Every feature shall implement approximately:

```cpp
class Feature {
public:
    FeatureResult evaluate(
        const EvaluationContext& context,
        CancellationToken cancellation
    );

    DependencySet dependencies() const;

    ParameterSchema parameterSchema() const;
};
```

A result contains:

```text
Geometry
Topology identity mapping
Metadata
Warnings
Errors
Mass properties
Bounding box
Render invalidation information
```

---

# 11. Persistent Topology Identity

Topological naming is one of the most important architectural problems in a parametric CAD system.

Users and downstream features need references such as:

```text
"the top face of Extrude001"
```

to survive reasonable upstream modifications.

Raw OCCT face indices shall never become persistent user references.

The application shall maintain semantic topology identifiers.

Example:

```text
Extrude001:
    generatedFace(profileEdge=A)
    generatedFace(profileEdge=B)
    capFace(start)
    capFace(end)
```

Persistent references should combine:

* feature ancestry;
* operation history;
* geometric signatures;
* orientation;
* adjacency;
* creation history;
* fallback similarity matching.

Topology resolution shall have confidence levels.

```text
Exact
Historical
GeometricMatch
Ambiguous
Broken
```

Ambiguous references must be surfaced rather than silently guessed.

---

# 12. Undo and Redo

All persistent changes shall pass through a command system.

```cpp
class Command {
    execute();
    undo();
    describe();
};
```

Examples:

```text
CreateSketchCommand
SetParameterCommand
DeleteFeatureCommand
CreateJointCommand
ApplyMaterialCommand
CreateSimulationCommand
```

Undo/redo therefore applies equally to:

* mouse actions;
* keyboard actions;
* scripts;
* AI actions.

---

# 13. Transactions

Multi-step actions shall support atomic transactions.

Example AI operation:

```text
Create sketch
Add four circles
Dimension holes
Extrude cut
Create pattern
```

may execute as:

```text
BeginTransaction
...
CommitTransaction
```

If any critical operation fails:

```text
RollbackTransaction
```

The history should optionally expose the transaction as one user-facing operation while retaining its internal subcommands for auditing.

---

# 14. Parametric Variables

The application shall have a first-class expression system.

Example:

```text
width = 100 mm
height = width / 2
wall = 2.5 mm
holeDiameter = M5_clearance
```

Expressions support:

* mathematical operators;
* units;
* functions;
* document variables;
* configuration values;
* component properties.

Units must be dimensional quantities, not plain floating-point values.

Invalid operations such as:

```text
5 mm + 3 degrees
```

must fail at evaluation time.

---

# 15. Configuration System

Configurations shall be fundamental to the document architecture.

A single design may represent:

```text
Bracket
├── Small
├── Medium
└── Large
```

Configurations may control:

* dimensions;
* expressions;
* suppression;
* materials;
* component selections;
* assembly joints;
* component quantities;
* feature parameters;
* simulation parameters;
* metadata.

Onshape currently allows configurations to drive feature parameters, dimensions, part properties and assembly relationships; that level of flexibility should be considered the minimum target.

Configurations should support tables:

| Configuration | Width | Thickness | Hole |
| ------------- | ----: | --------: | ---: |
| Small         |    40 |         3 |    4 |
| Medium        |    60 |         4 |    6 |
| Large         |    90 |         6 |    8 |

---

# 16. Sketcher

The sketcher shall be a dedicated geometric constraint environment.

Supported geometry:

* point;
* line;
* polyline;
* circle;
* arc;
* ellipse;
* elliptical arc;
* rectangle;
* polygon;
* slot;
* spline;
* construction geometry;
* projected geometry.

Constraints:

* coincident;
* horizontal;
* vertical;
* parallel;
* perpendicular;
* tangent;
* concentric;
* equal;
* midpoint;
* symmetric;
* fixed;
* collinear;
* distance;
* horizontal distance;
* vertical distance;
* radius;
* diameter;
* angle.

The solver must distinguish:

```text
Under-constrained
Fully constrained
Over-constrained
Conflicting
Redundant
```

Conflicting constraints must identify a minimal conflicting set where practical.

---

# 17. AI-Assisted Sketching

AI should assist without hiding constraint behavior.

Functions:

* auto-constrain;
* infer symmetry;
* infer design intent;
* identify duplicated dimensions;
* suggest missing constraints;
* convert rough geometry into constrained geometry;
* detect likely hole patterns;
* convert images or diagrams into editable sketches.

AI-generated constraints shall be visually distinguishable before confirmation when appropriate.

---

# 18. Solid Modeling

Initial professional feature set:

* extrusion;
* revolve;
* sweep;
* loft;
* hole;
* pocket/cut;
* boolean union;
* boolean subtract;
* boolean intersection;
* fillet;
* chamfer;
* shell;
* draft;
* rib;
* web;
* split body;
* split face;
* mirror;
* linear pattern;
* circular pattern;
* path pattern;
* face offset;
* replace face;
* move face;
* delete face;
* thicken;
* scale;
* transform;
* derived body.

---

# 19. Hole Feature

Holes should be engineering-aware.

Support:

* simple;
* counterbore;
* countersink;
* tapped/threaded;
* clearance;
* tapered.

Fastener databases should allow:

```text
M2
M2.5
M3
M4
M5
...
```

and common imperial standards.

Thread representation shall support:

```text
Cosmetic
Modeled
```

Cosmetic threads shall be preferred for performance.

---

# 20. Direct Modeling

Imported STEP or legacy solids shall remain editable.

Direct operations:

* press/pull;
* move face;
* rotate face;
* delete face;
* offset face;
* replace face;
* resize cylindrical feature;
* recognize holes;
* recognize fillets;
* recognize pockets;
* recognize patterns.

Feature recognition may optionally convert dumb geometry into editable semantic features.

---

# 21. Surface Modeling

Professional surfacing features shall include:

* surface extrude;
* surface revolve;
* surface sweep;
* surface loft;
* boundary surface;
* fill surface;
* offset surface;
* ruled surface;
* trim;
* extend;
* split;
* stitch;
* unstitch;
* thicken;
* face blend.

Analysis:

* curvature;
* zebra analysis;
* draft analysis;
* surface continuity;
* minimum radius;
* deviation analysis.

Onshape currently emphasizes boundary surfaces, face blends and continuity analysis through G3 for advanced surfacing. This represents a useful benchmark for long-term surface tooling.

---

# 22. Sheet Metal

Long-term professional scope shall include:

* sheet metal rules;
* flange;
* contour flange;
* bend;
* unfold;
* refold;
* hem;
* jog;
* corner relief;
* bend relief;
* rip;
* flat pattern;
* DXF export;
* bend table.

---

# 23. Frames and Weldments

The system shall support structural-member workflows.

Features:

* path-based structural members;
* standard profile libraries;
* custom profiles;
* corner trimming;
* miters;
* end caps;
* gussets;
* cut lists.

Onshape's current Frames implementation supports standard profile libraries, custom profiles, trimming and generated cut lists; these are appropriate parity targets.

---

# 24. Assembly Architecture

Assemblies shall reference components rather than duplicate their geometry.

```text
Assembly
│
├── ComponentDefinition A
│
├── ComponentDefinition B
│
├── Instance A1 → Definition A
├── Instance A2 → Definition A
└── Instance B1 → Definition B
```

Each instance stores:

```text
ComponentReference
ConfigurationReference
Transform
Visibility
AppearanceOverride
Metadata
AssemblyState
```

Repeated components shall share geometry data.

---

# 25. Lightweight Assemblies

Large assemblies require multiple representation levels.

```text
Level 0: Bounding box
Level 1: Coarse mesh
Level 2: Production mesh
Level 3: BREP loaded
```

Not every component needs BREP loaded merely to display an assembly.

The application may render thousands of components using cached tessellation while loading exact OCCT topology only when required.

---

# 26. Joints and Mates

The system shall support both intuitive joints and lower-level constraints.

Primary workflow:

```text
Fastened
Revolute
Slider
Cylindrical
Planar
Ball
Pin-slot
```

A joint describes allowed component motion.

This adopts the useful Fusion concept that a mechanical relationship can directly represent degrees of freedom.

Advanced users may additionally use primitive constraints:

```text
Coincident
Parallel
Distance
Angle
Concentric
Tangent
```

---

# 27. Assembly Solver

Assembly solving must be independent from OCCT geometry evaluation.

Inputs:

```text
Bodies
Reference frames
Constraints
Current transforms
```

Outputs:

```text
Solved transforms
Degrees of freedom
Conflicts
Redundancies
```

The solver shall detect:

* under-constrained assemblies;
* fully constrained assemblies;
* over-constrained systems;
* circular dependency;
* impossible constraint sets.

---

# 28. Motion

Assemblies should allow interactive motion.

Examples:

* rotate hinge;
* slide carriage;
* move piston;
* animate linkage.

Dragging a component must solve assembly constraints interactively.

A joint inspector shall expose remaining degrees of freedom.

---

# 29. Interference Detection

Assembly analysis shall include:

* static interference;
* clearance analysis;
* contact detection;
* minimum-distance query;
* motion envelope.

Users should be able to run:

```text
Analyze → Interference
```

and receive a navigable list of collisions.

---

# 30. Exploded Views

Assemblies shall support persistent exploded states.

Exploded states may feed:

* drawings;
* assembly instructions;
* animations;
* AI explanations;
* manufacturing documentation.

---

# 31. Bill of Materials

Assemblies shall generate structured BOM data.

Fields:

```text
Part number
Revision
Description
Quantity
Material
Mass
Supplier
Configuration
Make/buy
Cost
Custom properties
```

BOM data must be accessible through:

* drawings;
* scripting;
* AI;
* CSV export;
* future ERP integrations.

---

# 32. Version History

Every persistent mutation shall create an append-only history event.

History should behave conceptually like source-control history rather than file copies.

```text
Workspace
  │
  ● edit sketch
  │
  ● extrude changed
  │
  ● add motor
  │
  ● checkpoint v1
```

Users must be able to inspect and restore previous states.

---

# 33. Immutable Versions

A version represents an immutable snapshot.

Example:

```text
V1 — initial prototype
V2 — motor redesign
V3 — production release
```

Released artifacts should reference immutable versions, not mutable workspaces.

This follows the successful separation of editable workspaces from immutable versions used by Onshape.

---

# 34. Branching

Users shall be able to create a branch from any version.

```text
               ┌── lightweight-concept
main ──────────┤
               └── reinforced-design
```

Branches allow experimentation without copying entire projects.

Use cases:

* competing concepts;
* supplier-specific modifications;
* AI design alternatives;
* optimization experiments;
* production fixes.

---

# 35. Merge

Changes from one branch may be merged into another.

Merging shall occur at semantic document-object level when possible, not raw binary file level.

Example:

```text
Branch A:
changes Fillet001 radius

Branch B:
adds Hole002
```

Both may merge automatically.

Conflict:

```text
Branch A:
deletes Extrude001

Branch B:
modifies Extrude001
```

requires resolution.

Onshape's existing branch/merge workflow validates this general model for CAD exploration and collaboration.

---

# 36. AI Branching

AI should strongly leverage branches.

When a user asks:

> Make this bracket 30% lighter but keep the current mounting interfaces.

the system may create:

```text
ai/lightweight-bracket
```

and perform modifications there.

The user can:

```text
Compare
Accept
Reject
Merge
```

This makes aggressive AI assistance safe.

---

# 37. Semantic Diff

Users must be able to compare versions or branches semantically.

Example:

```text
Changed:
    wallThickness
       3.0 mm → 2.5 mm

Added:
    Rib004

Removed:
    Fillet002

Assembly:
    Motor configuration
       24V → 48V

Mass:
    2.84 kg → 2.51 kg
```

A 3D geometry difference visualization shall also be available.

---

# 38. Release Management

Optional PDM functionality shall support lifecycle states:

```text
In Work
Review
Approved
Released
Obsolete
```

Objects may have:

```text
Part Number
Revision
Owner
Approver
Release Date
```

Release workflows should support approval rules and immutable released references.

Onshape currently integrates release state and revision management directly with CAD/PDM objects, providing an appropriate product benchmark.

---

# 39. File and Project Storage

Project storage should use a structured container.

Example:

```text
project.kearne
```

internally containing:

```text
manifest
document database
feature graph
parameters
materials
history
configuration definitions
BREP cache
mesh cache
thumbnails
simulation metadata
drawing metadata
```

The authoritative source is the semantic document data.

BREPs and meshes are caches where practical.

---

# 40. Crash Recovery

The command journal shall permit recovery after unexpected termination.

The system should periodically checkpoint state without forcing the user to manually save.

A crash must not normally destroy work completed since the previous explicit save.

---

# 41. AI Architecture

AI shall have several capability levels.

```text
Level 1 — Read
Level 2 — Suggest
Level 3 — Preview
Level 4 — Modify
Level 5 — Run tools
Level 6 — Autonomous bounded task
```

Users and organizations shall control permitted levels.

Codex app-server shall provide the AI thread, turn, streamed-event, approval, and authentication harness. Kearne shall remain the authority for engineering permissions, tools, commands, revisions, and evidence. Conversational state shall not become canonical CAD state.

---

# 42. AI Context Model

AI should not primarily consume screenshots.

It should receive structured engineering context.

Example:

```json
{
  "selection": {
    "type": "face",
    "surface": "plane",
    "area": "4300 mm^2",
    "normal": [0, 0, 1],
    "ownerFeature": "Extrude003"
  }
}
```

It may query:

* feature graph;
* dimensions;
* materials;
* configurations;
* assembly hierarchy;
* joint state;
* BOM;
* simulation studies;
* selected topology;
* warnings;
* geometry measurements.

---

# 43. AI Command API

The AI shall have a typed tool API.

Example tools:

```text
inspect_document
inspect_selection
measure
create_sketch
add_sketch_geometry
add_constraint
create_extrude
create_hole
create_pattern
create_fillet
set_parameter
create_component
insert_component
create_joint
assign_material
create_simulation
run_simulation
compare_versions
create_branch
merge_branch
export
```

Every tool shall have a machine-readable schema.

---

# 44. AI Planning

For non-trivial requests AI should be able to build an explicit engineering plan.

Example:

```text
Goal:
Reduce bracket mass 20%

Constraints:
Mounting holes cannot move
Maximum stress < 120 MPa
Minimum wall = 2 mm

Plan:
1. Inspect geometry.
2. Identify non-critical material.
3. Create branch.
4. Add lightening pockets.
5. Run simulation.
6. Compare mass and stress.
7. Present candidate.
```

The user should be able to inspect this plan.

---

# 45. AI Preview

Potentially destructive or extensive AI modifications shall have preview support.

```text
AI Proposal

+ Create Rib003
- Remove Fillet001
~ wallThickness 4 → 3 mm

Predicted mass:
2.8 kg → 2.4 kg
```

The viewport may show:

```text
green = added geometry
red = removed geometry
```

---

# 46. AI + build123d

build123d shall be a first-class procedural geometry environment.

It is particularly useful for:

* generative geometry;
* mathematical shapes;
* repetitive structures;
* fixtures;
* templates;
* AI-created prototypes;
* geometry transformations difficult to express through existing semantic features.

Architecture:

```text
AI
 ↓
build123d script
 ↓
Python sandbox
 ↓
OCP
 ↓
TopoDS_Shape
```

The resulting shape may be:

1. inserted as a procedural feature;
2. converted into an imported body;
3. translated into native semantic features when recognizable.

---

# 47. Python Sandbox

Generated Python must not execute directly inside the main application process.

```text
Main CAD Process
       │
       │ RPC
       ▼
Python Worker
       │
       ├── restricted environment
       ├── build123d
       ├── application Python SDK
       └── user plugin
```

The worker process shall support:

* execution timeout;
* cancellation;
* memory limit;
* CPU quota where possible;
* filesystem capability controls;
* network permissions;
* crash isolation.

---

# 48. Python SDK

Users should have access to a stable native application API.

Example:

```python
doc = app.active_document

plate = doc.create_component("Base Plate")

sketch = plate.create_sketch("XY")
sketch.rectangle(100, 60)

body = plate.extrude(sketch, 8)

plate.hole(
    diameter=6,
    position=(10, 10)
)
```

Anything accessible through normal GUI tools should eventually be accessible through Python.

---

# 49. Plugins

Plugins may add:

* commands;
* feature types;
* import/export formats;
* analysis tools;
* simulation backends;
* materials databases;
* AI tools;
* UI panels.

Plugins must declare permissions.

Example:

```text
Geometry read
Geometry write
Filesystem
Network
AI
Simulation
UI
```

---

# 50. AI Feature Generation

The application should allow AI to create reusable feature definitions.

Example user request:

> Make me a configurable snap-fit tab feature.

AI may produce:

```text
SnapFitFeature
Parameters:
    length
    width
    hookDepth
    clearance
```

That feature becomes available in the toolbar or feature library.

This combines the extensibility idea of systems such as Onshape Custom Features with AI-generated tooling. Onshape's Custom Features/FeatureScript system demonstrates the value of making reusable user-defined CAD features a first-class capability.

---

# 51. Simulation Architecture

Simulation is a document subsystem, not a separate application.

```text
CAD Model
   │
   ▼
Simulation Study
   │
   ├── Geometry
   ├── Materials
   ├── Contacts
   ├── Loads
   ├── Constraints
   └── Mesh Settings
            │
            ▼
          Solver
            │
            ▼
          Results
```

---

# 52. Solver Abstraction

Simulation shall not depend permanently on one solver.

```cpp
class SimulationBackend {
public:
    MeshResult mesh(...);
    SolveResult solve(...);
};
```

Possible backends include:

* CalculiX;
* Code_Aster;
* future commercial solver;
* cloud solver;
* custom GPU solver.

---

# 53. Initial Simulation Types

Phase 1:

* linear static structural;
* modal.

Phase 2:

* steady-state thermal;
* thermal stress;
* buckling.

Phase 3:

* nonlinear structural;
* transient thermal;
* dynamic/event simulation;
* fatigue.

Fusion currently offers workflows spanning static, thermal and more advanced simulation types, while Onshape integrates linear static and modal analysis directly with assemblies. These provide useful coverage targets without requiring identical solver implementations.

---

# 54. Simulation Study

Example:

```text
StaticStudy001
│
├── GeometrySet
├── MaterialAssignments
├── Contacts
├── Fixtures
│   └── Fixed001
├── Loads
│   └── Force001
├── MeshSettings
└── Results
```

The study references semantic geometry rather than copying geometry.

---

# 55. Automatic Simulation Updating

When relevant CAD features change, the study becomes:

```text
Results Out of Date
```

The user may choose:

```text
Auto solve
Manual solve
Solve on idle
Cloud solve
```

Small studies may update automatically.

Expensive studies should remain explicit.

---

# 56. Assembly Simulation

Assembly relationships should assist simulation setup.

For example:

```text
Fastened joint
    ↓
Bonded contact candidate

Revolute joint
    ↓
Pin/cylindrical contact candidate
```

The simulation environment should reuse mechanical intent rather than forcing the engineer to completely recreate component relationships.

Onshape uses assembly mates to inform its simulation kinematics, demonstrating the practical value of this integration.

---

# 57. Simulation Results

Visualization:

* displacement;
* von Mises stress;
* principal stress;
* strain;
* factor of safety;
* reaction forces;
* temperature;
* mode shapes.

Users must be able to probe:

```text
point
face
component
maximum
minimum
```

Results shall support animated deformation.

---

# 58. Design Optimization

Optimization should eventually become a major AI feature.

Example:

> Reduce mass while keeping maximum displacement under 0.5 mm.

AI can vary:

```text
thickness
rib dimensions
pocket dimensions
material
feature suppression
```

and use simulation results as objectives.

```text
Generate → Simulate → Evaluate → Iterate
```

This workflow should operate on dedicated branches or experiment spaces.

---

# 59. AI Engineering Loop

A long-term differentiating workflow:

```text
User Goal
   ↓
AI proposes requirements
   ↓
Generate candidates
   ↓
CAD evaluation
   ↓
Simulation
   ↓
Scoring
   ↓
Optimization
   ↓
Human review
```

Example:

> Design a motor bracket for this motor and these four mounting points. Aluminum 6061, factor of safety at least 2, minimize mass.

The AI should be capable of producing validated design candidates rather than merely geometry.

---

# 60. Materials

Materials shall be centralized engineering assets.

Material record:

```text
Name
Density
Elastic modulus
Poisson ratio
Yield strength
Ultimate strength
Thermal conductivity
Specific heat
Expansion coefficient
Appearance
Cost
Custom properties
```

Users may create:

* personal libraries;
* team libraries;
* project libraries.

---

# 61. Rendering

The CAD viewport should be a dedicated rendering subsystem.

Initial architecture:

```text
OCCT BREP
   ↓
OCCT tessellation
   ↓
Render mesh cache
   ↓
GPU
```

OCCT AIS may initially handle substantial portions of:

* display;
* selection;
* topology highlighting;
* clipping;
* hidden-line behavior.

The architecture must permit replacement by a custom renderer later.

---

# 62. Renderer Abstraction

```cpp
class RenderScene {
    addObject(...)
    updateObject(...)
    removeObject(...)
    setSelection(...)
};
```

The document model shall never depend directly on AIS objects.

This prevents the rendering implementation from becoming inseparable from the modeling architecture.

---

# 63. Mesh Cache

Rendering must not repeatedly tessellate unchanged geometry.

Cache key should include:

```text
Shape hash
Tessellation tolerance
Angular tolerance
Display representation
```

Meshes shall be stored in GPU-friendly vertex/index formats.

---

# 64. Large Assembly Rendering

Performance techniques:

* instanced rendering;
* level of detail;
* frustum culling;
* occlusion culling;
* mesh deduplication;
* background loading;
* compressed mesh cache;
* bounding-box placeholders;
* deferred BREP loading.

Repeated bolts should ideally reference one GPU mesh.

---

# 65. Picking

The selection system must support:

```text
Object
Component
Body
Face
Edge
Vertex
Sketch
Sketch entity
Joint
Simulation entity
```

GPU picking should eventually be preferred for large scenes.

Selections must resolve back to persistent document IDs.

---

# 66. Visual Styles

Viewport modes:

* shaded;
* shaded with edges;
* wireframe;
* hidden line;
* transparent;
* x-ray;
* section view;
* curvature;
* analysis overlay.

---

# 67. Modern UI Design

The interface shall use Qt Quick/QML rather than traditional Qt Widgets for the primary shell.

Visual goals:

* clean;
* minimal;
* dense without being cluttered;
* dark and light themes;
* fluid transitions;
* high-DPI;
* keyboard-first capable;
* touchpad friendly;
* modern iconography.

Every desktop workflow shall be agent-observable. An authorized harness must be able to launch the application, await typed UI and frame state without sleeps, inspect semantic controls, operate public input paths, and return a lossless image containing every visible Kearne-owned surface. Capturing unrelated applications or OS-owned secure surfaces remains subject to platform permission.

---

# 68. Primary Layout

Default workspace:

```text
┌─────────────────────────────────────────────────────────┐
│ Application / document bar                              │
├─────────────────────────────────────────────────────────┤
│ Contextual modeling toolbar                             │
├──────────────┬─────────────────────────┬────────────────┤
│              │                         │                │
│ Model Tree   │                         │ Properties     │
│              │       3D VIEWPORT       │                │
│              │                         │ Parameters     │
│              │                         │                │
├──────────────┴─────────────────────────┴────────────────┤
│ Command / AI / Jobs / diagnostics                      │
└─────────────────────────────────────────────────────────┘
```

Panels should be dockable but docking should not visually resemble legacy MDI applications.

---

# 69. Contextual Tools

Toolbars should react to context.

Selecting a planar face may present:

```text
Sketch
Hole
Extrude
Offset
Measure
AI
```

Selecting an assembly component may present:

```text
Move
Joint
Replace
Configure
Properties
Suppress
```

---

# 70. Command Palette

A universal command palette should be first-class.

Keyboard example:

```text
Ctrl+K
```

Then type:

```text
fillet
simulation
export STEP
create branch
change material
```

AI commands may also appear.

---

# 71. Search

Universal search shall index:

* features;
* components;
* parameters;
* materials;
* commands;
* versions;
* branches;
* drawings;
* simulation studies;
* documentation;
* plugins.

Search should understand engineering synonyms where possible.

---

# 72. Properties Panel

Every selected object exposes structured editable properties.

Example:

```text
Extrude

Profile       Sketch003
Distance      25 mm
Direction     One Side
Operation     Join
Draft         0 deg
```

Parameter editing should update preview interactively where feasible.

---

# 73. Feature Timeline vs Tree

The application should avoid requiring users to choose between a pure chronological timeline and a pure hierarchical model tree.

Provide:

## Structure view

```text
Assembly
 ├─ Base
 └─ Motor
```

## Feature history

```text
Sketch
Extrude
Fillet
Hole
```

Both represent the same document graph.

Users can switch views.

---

# 74. Drawings

The system shall support associative engineering drawings.

Capabilities:

* base view;
* projected view;
* section view;
* detail view;
* auxiliary view;
* exploded assembly view;
* hidden lines;
* center marks;
* centerlines;
* dimensions;
* tolerances;
* GD&T;
* surface finish;
* weld symbols;
* balloons;
* parts lists;
* BOM tables.

---

# 75. Drawing Associativity

Drawings reference CAD objects semantically.

If geometry changes:

```text
CAD
 ↓
Drawing marked stale
 ↓
Drawing regenerates
```

Dimensions associated with preserved topology should remain attached.

Onshape currently keeps drawings linked with associated parts, assemblies and BOM information; this level of associativity should be considered a baseline requirement.

---

# 76. Standards

Initial standards:

* ISO;
* ANSI.

Later:

* DIN;
* JIS;
* BS.

Drawing templates should support company branding and custom title blocks.

---

# 77. Import

Primary formats:

* STEP;
* IGES;
* BREP;
* STL;
* OBJ;
* 3MF;
* DXF;
* DWG where licensing permits.

Later:

* Parasolid;
* JT;
* glTF;
* native competitor formats through licensed translators where economically justified.

---

# 78. Export

Primary outputs:

* STEP;
* IGES;
* STL;
* 3MF;
* DXF;
* PDF drawing;
* OBJ/glTF for visualization;
* simulation result formats.

---

# 79. Healing

Imported BREP should pass through configurable healing.

Diagnostics:

* invalid shells;
* gaps;
* self-intersections;
* tolerance problems;
* open edges;
* duplicate faces.

Users should be able to inspect healing actions.

---

# 80. Job System

Every expensive operation shall be represented as a job.

```text
Job
├── ID
├── priority
├── dependencies
├── status
├── progress
├── cancellation token
└── result
```

Job types:

```text
Geometry recompute
Tessellation
STEP import
STEP export
Simulation mesh
Simulation solve
Python execution
AI request
Drawing regeneration
Thumbnail generation
```

---

# 81. Scheduling

Priority classes:

```text
Interactive
High
Normal
Background
Idle
```

Example:

A user changes a parameter.

```text
Feature recompute     HIGH
Viewport mesh         HIGH
Mass property update  NORMAL
Thumbnail generation  IDLE
```

---

# 82. Cancellation

Long operations must support cancellation where the underlying algorithm allows it.

If cancellation cannot safely interrupt a kernel call, that work shall execute in a worker process that can be terminated when necessary.

---

# 83. Memory Architecture

Large BREP objects and meshes must not be copied unnecessarily.

Use:

* shared ownership;
* immutable geometry results;
* copy-on-write document structures where useful;
* shared memory for large inter-process mesh transfer;
* memory-mapped caches for very large models.

---

# 84. Geometry Worker Isolation

For especially unstable or expensive geometry operations, optional isolated OCCT workers should be supported.

```text
Main Process
     ↓
Geometry Worker
     ↓
OCCT boolean
```

If the worker crashes:

```text
Main application survives.
```

This capability can initially be reserved for imports and risky operations.

---

# 85. Collaboration

Local desktop operation is mandatory.

Cloud collaboration should be additive.

Potential architecture:

```text
Local Document
      │
Operation Log
      │
Sync Service
      │
Cloud Project
```

Synchronization should operate on semantic operations rather than repeatedly uploading monolithic CAD files.

---

# 86. Real-Time Collaboration

Future collaborative editing should support:

* user presence;
* visible selections;
* comments;
* concurrent editing;
* operation-level synchronization.

Onshape currently supports simultaneous editing with presence information, illustrating the user experience benchmark for eventual collaborative workflows.

This capability need not be part of the first desktop release, but the document architecture must not preclude it.

---

# 87. Comments and Review

Comments may attach to:

* document;
* feature;
* body;
* face;
* edge;
* assembly instance;
* joint;
* drawing;
* simulation result.

Example:

```text
@alex Can this wall drop to 2 mm?
```

The reference should remain associated with the underlying semantic object when possible.

---

# 88. Security

Projects may contain proprietary engineering data.

Requirements:

* local projects never uploaded implicitly;
* telemetry must be explicit and documented;
* cloud synchronization optional;
* AI provider selection visible;
* per-project AI network permission;
* encryption in transit;
* encrypted cloud storage;
* enterprise SSO later;
* audit logs for enterprise deployments.

---

# 89. AI Privacy Modes

Provide explicit modes:

```text
Local Only
Approved Cloud AI
No AI
```

Enterprise administrators may restrict models/providers.

---

# 90. Performance Targets

Initial targets on recommended hardware:

## Startup

```text
Cold start < 3 seconds desirable
Warm start < 1.5 seconds desirable
```

## Interaction

```text
Viewport camera input → visible frame:
< 16.7 ms target at 60 Hz

Selection highlight:
< 50 ms typical
```

## Modeling

Simple feature updates:

```text
< 100 ms whenever kernel operation permits
```

Complex operations run asynchronously.

## Assemblies

Initial target:

```text
10,000 visible component instances
```

with responsive navigation using lightweight representations.

Long-term:

```text
100,000+ instance visualization
```

through aggressive instancing and LOD.

---

# 91. Responsiveness Rule

No code running on the UI thread may perform:

* OCCT booleans;
* filleting;
* STEP parsing;
* meshing;
* simulation;
* Python;
* AI inference;
* heavy database operations.

The UI thread orchestrates, it does not compute.

---

# 92. Observability

Development builds shall expose:

```text
frame time
GPU time
feature recompute time
OCCT operation duration
mesh generation time
memory consumption
job queue
Python workers
solver utilization
AI latency
```

A built-in performance inspector will significantly accelerate development.

---

# 93. Diagnostics

Feature errors should be actionable.

Bad:

```text
BRep_API: command not done
```

Good:

```text
Fillet failed.

Radius 12 mm is too large for Edge 14.

Largest successful radius found:
8.43 mm

[Use 8.4 mm]
[Inspect Edge]
```

Raw OCCT diagnostics may be accessible in developer mode.

---

# 94. AI Error Recovery

When a command fails, AI may inspect:

```text
operation
kernel error
affected topology
feature parameters
```

and propose alternatives.

Example:

```text
12 mm fillet failed.
The local curvature permits approximately 8.4 mm.

Try 8 mm?
```

---

# 95. Command History

The command history should be searchable.

Example:

```text
10:43 Create Sketch003
10:45 Extrude Sketch003 25 mm
10:46 AI: Create mounting pattern
10:48 Set material Aluminum 6061
```

Users can inspect which commands were AI-generated.

---

# 96. Provenance

Every generated object may record:

```text
CreatedBy:
    Human
    AI
    Script
    Plugin
    Import

Timestamp
Command
Parent transaction
```

This becomes particularly valuable in regulated engineering environments.

---

# 97. Design Requirements

The document may optionally contain explicit engineering requirements.

Example:

```text
REQ-001
Mass < 2.5 kg

REQ-002
Safety factor > 2

REQ-003
Motor mounting holes fixed

REQ-004
Fits within 150 × 100 × 80 mm envelope
```

AI and optimization workflows can operate against these requirements.

---

# 98. AI Design Review

The AI should eventually perform engineering review tasks such as:

* detect thin walls;
* detect unreachable fasteners;
* detect assembly interference;
* detect suspicious unconstrained sketches;
* detect missing materials;
* detect impossible manufacturing geometry;
* identify over-complex feature trees;
* flag simulation assumptions;
* detect changed released interfaces.

The result should link directly to model objects.

---

# 99. Geometry Inspection API

AI and plugins shall have efficient query APIs.

Examples:

```text
find_planar_faces
find_cylindrical_faces
find_holes
measure_distance
measure_angle
compute_bbox
compute_mass
compute_center_of_mass
find_interference
find_thickness
find_symmetry
query_adjacency
```

This prevents agents from repeatedly processing raw tessellated meshes just to understand basic geometry.

---

# 100. Semantic Selection

Selections should carry meaning.

Instead of:

```text
triangle #13991
```

the AI receives:

```text
Face:
    Feature = Extrude004
    Surface = Cylinder
    Radius = 3 mm
    Axis = ...
```

This is a critical AI-native requirement.

---

# 101. Natural-Language Selection

Users should eventually be able to say:

> Select all 6 mm holes on the top plate.

AI translates the query into semantic geometry filters.

Other examples:

> Select every fillet smaller than 2 mm.

> Find fasteners touching this bracket.

> Show components made from steel.

---

# 102. AI Command Bar

AI should not be confined to a chatbot panel.

A command bar can support:

```text
"Make this 20 mm taller"

"Pattern this hole around the shaft"

"Replace these M4 screws with M5"

"Run a stress study with 500 N here"

"Why did this fillet fail?"

"Make a drawing of this component"

"Compare this branch with production"
```

Selection context is automatically supplied.

---

# 103. Agent Workspaces

Long AI operations should operate in an isolated workspace.

Example:

```text
Agent Task
"Optimize bracket"
│
├── Candidate 1
├── Candidate 2
├── Candidate 3
└── Recommendation
```

The user's active design remains unaffected until a result is accepted.

---

# 104. AI Design Alternatives

The system should make parallel AI exploration easy.

```text
Original
│
├── AI Concept A
├── AI Concept B
└── AI Concept C
```

Comparison metrics:

```text
Mass
Volume
Cost
Max stress
Displacement
Manufacturability
Part count
```

---

# 105. Manufacturing Awareness

Although full CAM may be a later product phase, CAD features should contain manufacturing semantics where helpful.

AI/design analysis may detect:

* impossible internal corners;
* insufficient draft;
* minimum tool diameter;
* unreachable machining areas;
* undercuts;
* thin walls;
* poor additive orientation;
* sheet metal rule violations.

Fusion's product strategy demonstrates the value of connecting CAD, engineering validation and downstream manufacturing rather than treating CAD as an isolated geometry editor.

---

# 106. Future CAM

The architecture should reserve a Manufacturing document subsystem:

```text
Manufacturing
├── Setup
├── Stock
├── Machine
├── Tool Library
├── Operations
├── Toolpaths
└── Simulation
```

Do not attempt full CAM during the first CAD release.

---

# 107. Drawing AI

AI can assist in documentation:

> Create a manufacturing drawing for this bracket.

The system may:

1. choose views;
2. generate section/detail views;
3. dimension critical features;
4. add hole notes;
5. populate title block;
6. generate BOM;
7. flag missing tolerances.

AI-generated drawings must remain fully editable.

---

# 108. User Experience Modes

The same application should support different proficiency levels.

## Beginner

Simplified toolbar and guided commands.

## Professional

Dense CAD tooling, shortcuts and advanced properties.

## AI-first

Command bar and conversational manipulation prominent.

These are layouts over the same command system, not different CAD engines.

---

# 109. Keyboard-First Engineering

Every command shall have a searchable command identifier.

Users should be able to bind shortcuts.

Example:

```text
S       Sketch
E       Extrude
H       Hole
F       Fillet
M       Measure
Ctrl+K  Command palette
```

Exact defaults can be determined during UX testing.

---

# 110. Selection UX

Selection should support:

* preselection highlight;
* cycling overlapping entities;
* filters;
* selection sets;
* select similar;
* semantic filters;
* box select;
* lasso select.

Example:

```text
Select Similar → Cylindrical faces → Same radius
```

---

# 111. Measure Tool

The measure tool shall automatically infer useful measurements:

* point-point;
* edge length;
* radius;
* diameter;
* face area;
* angle;
* minimum distance;
* center distance;
* mass;
* volume.

Measurements may be pinned to the viewport.

---

# 112. Section Analysis

Interactive section planes:

* XY;
* YZ;
* XZ;
* arbitrary plane;
* selected face.

Multiple section planes should eventually be supported.

---

# 113. Design Health Panel

A persistent model-health system should report:

```text
2 under-constrained sketches
1 broken reference
3 outdated simulations
1 missing material
0 assembly conflicts
```

This becomes both a user feature and AI context source.

---

# 114. Extensible Feature Registry

Native and plugin features shall register through a common schema.

```text
FeatureType
ParameterSchema
Icon
Category
Evaluator
SerializationVersion
MigrationHandler
```

Document loading must tolerate missing plugins by retaining opaque feature parameters and reporting unresolved dependencies.

---

# 115. API Versioning

The application SDK must be versioned independently of internal implementation.

Example:

```text
API v1
API v2
```

Breaking internal changes must not immediately break plugins.

---

# 116. Document Schema Migration

Every document object includes a schema version.

```text
ExtrudeFeature schemaVersion=3
```

Loaders must migrate older schemas forward.

Backward compatibility should be treated as a core engineering responsibility.

---

# 117. Automated Testing

Testing layers:

```text
Unit
Geometry regression
Document graph
Assembly solver
Simulation integration
Rendering
Persistence
API
AI tools
UI automation
```

Geometry regression tests should store expected invariants instead of relying exclusively on byte-identical BREP output.

Examples:

```text
volume
surface area
face count
bounding box
topology relationships
```

Regression assurance shall scale through shared contract suites, schema-aware generators and shrinkers, model-based state machines, metamorphic relations, deterministic replay, fuzzing, and fault injection. Registering a public command, entity, feature, adapter, worker, or format shall enroll it in applicable conformance suites. Manual exploration shall produce reusable seeds, scenarios, models, or properties rather than a growing set of hand-rewritten procedural tests.

UI automation shall combine semantic control state with complete Kearne-session captures. Broad screenshot goldens, widget coordinates, wall-clock sleeps, private implementation calls, and exact BREP bytes shall not be primary correctness oracles.

---

# 118. Determinism

Given identical:

```text
document
application version
kernel version
configuration
```

feature evaluation should be as deterministic as practical.

This matters for:

* collaboration;
* version comparison;
* cache validity;
* CI;
* AI experimentation.

---

# 119. Headless Mode

The CAD engine must run without the GUI.

Example:

```bash
kearne project.kearne \
  --configuration production \
  --export output.step
```

or:

```bash
kearne --run script.py
```

Headless operation enables:

* CI;
* automated exports;
* simulation farms;
* server workloads;
* batch AI;
* cloud compute.

---

# 120. Server Architecture Compatibility

Although desktop-first, core services should avoid assumptions that prevent server execution.

Ideal separation:

```text
kearne_core
kearne_geometry
kearne_assembly
kearne_simulation
kearne_python
kearne_render
kearne_ui
```

Only:

```text
kearne_ui
kearne_render
```

should fundamentally require a desktop graphical environment.

---

# 121. Initial Repository Structure

```text
/src
    /app
    /core
    /document
    /commands
    /geometry
    /features
    /sketch
    /assembly
    /simulation
    /render
    /versioning
    /persistence
    /plugins
    /ai
    /ipc
    /ui

/python
    /sdk
    /workers
    /build123d_bridge

/tests

/resources
    /qml
    /icons
    /materials
    /standards

/tools
```

---

# 122. Core Libraries

Logical dependencies:

```text
kearne_document
    ↓
kearne_commands
    ↓
kearne_features
    ↓
kearne_geometry
    ↓
OCCT
```

Assembly:

```text
kearne_assembly
    ↓
kearne_document
```

Simulation:

```text
kearne_simulation
    ↓
kearne_document
    ↓
solver adapters
```

AI:

```text
kearne_ai
    ↓
public command API
```

AI should not directly reach arbitrary internal implementation details.

---

# 123. Public Internal API Boundary

A strong architectural boundary shall exist between:

```text
Engineering API
```

and:

```text
Implementation
```

The GUI itself should preferentially use the Engineering API.

This ensures that if an AI command can call:

```text
create_hole(...)
```

the normal Hole dialog is effectively calling the same service.

---

# 124. Initial MVP

The first usable alpha should intentionally be narrower.

## Required

* modern QML UI;
* OCCT viewport;
* document graph;
* command system;
* undo/redo;
* sketcher;
* constraints;
* extrusion;
* revolve;
* boolean;
* fillet;
* chamfer;
* hole;
* patterns;
* parameters;
* STEP import/export;
* STL export;
* Python worker;
* build123d integration;
* AI structured command API;
* project persistence;
* background job system.

Do not attempt simulation and full assemblies before the document and topology architecture are reliable.

---

# 125. Phase 2 — Serious CAD

Add:

* robust persistent topology;
* direct editing;
* surface modeling;
* configurations;
* advanced patterns;
* shell/draft/rib;
* drawing foundations;
* material library;
* performance profiling;
* large model rendering.

---

# 126. Phase 3 — Assemblies

Add:

* component instances;
* external references;
* joints;
* assembly constraint solver;
* motion;
* interference;
* BOM;
* exploded views;
* lightweight representations.

---

# 127. Phase 4 — Versioning and AI Workflows

Add:

* immutable versions;
* branches;
* semantic diff;
* merge;
* AI branch workflows;
* AI design alternatives;
* agent workspace;
* stronger build123d translation.

---

# 128. Phase 5 — Simulation

Add:

* meshing;
* linear static;
* modal;
* assembly contacts;
* simulation visualization;
* solver workers;
* AI simulation setup;
* design-validation loops.

---

# 129. Phase 6 — Documentation and Release

Add:

* professional drawings;
* GD&T;
* BOM tables;
* release lifecycle;
* revision management;
* approval flow;
* PDF/DXF output.

---

# 130. Phase 7 — Collaboration

Add:

* cloud projects;
* synchronization;
* comments;
* presence;
* team permissions;
* concurrent editing;
* shared branches.

---

# 131. Architectural Non-Goals

The initial architecture should explicitly avoid:

## A monolithic Python application

Python remains essential but is not responsible for the core UI and engineering-runtime lifecycle.

## Direct QML → OCCT calls

All modeling flows through engineering services.

## build123d as the document model

build123d is an excellent procedural geometry interface, not the canonical application data model.

## Raw BREP as project state

BREP is an evaluated geometry result.

## AI writing arbitrary document internals

AI operates through validated commands and tools.

## Simulation on the UI thread

Never.

## File-copy versioning

Versioning is semantic and history-aware.

---

# 132. The Most Important Data Flow

Human-created feature:

```text
User clicks Extrude
        ↓
Extrude dialog
        ↓
CreateExtrudeCommand
        ↓
Document transaction
        ↓
Feature graph
        ↓
Geometry job
        ↓
OCCT
        ↓
TopoDS_Shape
        ↓
Mesh job
        ↓
Renderer
```

AI-created feature:

```text
User:
"Extrude this sketch 20 mm"
        ↓
AI
        ↓
CreateExtrude tool
        ↓
CreateExtrudeCommand
        ↓
Document transaction
        ↓
Feature graph
        ↓
Geometry job
        ↓
OCCT
        ↓
TopoDS_Shape
        ↓
Renderer
```

Python-created feature:

```text
Python SDK
        ↓
create_extrude(...)
        ↓
CreateExtrudeCommand
        ↓
same pipeline
```

There is one CAD system.

There are multiple ways to control it.

---

# 133. build123d Data Flow

Procedural geometry is the exception:

```text
User / AI
    ↓
Python build123d
    ↓
OCP
    ↓
TopoDS_Shape
    ↓
ProceduralFeature
    ↓
Document
```

The feature retains:

* script;
* dependencies;
* parameters;
* resulting geometry;
* execution logs.

Changing an exposed script parameter triggers asynchronous re-evaluation.

---

# 134. Example AI Workflow

User selects a bracket and says:

> Make this 25% lighter without moving any mounting holes. Use 6061 aluminum and keep factor of safety above 2 under this 1 kN load.

System:

```text
1. AI reads selected component.
2. AI identifies mounting interfaces.
3. AI reads material.
4. AI creates optimization branch.
5. AI creates simulation study.
6. AI measures baseline mass.
7. AI creates candidate geometry.
8. Geometry workers evaluate candidate.
9. Solver evaluates candidate.
10. AI iterates.
11. Candidates appear in comparison view.
```

Result:

```text
Original
Mass: 1.84 kg
FOS: 3.1

Candidate A
Mass: 1.42 kg
FOS: 2.3

Candidate B
Mass: 1.36 kg
FOS: 1.8  ✕
```

The user chooses Candidate A.

```text
Merge Candidate A
```

This workflow should represent the long-term identity of the product.

---

# 135. Core Product Differentiator

The principal differentiator should not simply be:

> CAD with AI.

It should be:

> A programmable engineering system in which geometry, product structure, simulation, history, requirements and AI all share the same semantic model.

Traditional CAD tends to expose UI operations to humans and APIs to programmers as separate layers.

This product should instead center everything around:

```text
Semantic Document
+
Command API
+
Engineering Graph
```

Humans, scripts and AI then become peers.

---

# 136. Final Architecture

```text
                         ┌──────────────────┐
                         │       AI         │
                         │ agents / models  │
                         └────────┬─────────┘
                                  │
                ┌─────────────────┴────────────────┐
                │        ENGINEERING API           │
                │ typed commands / queries / tools │
                └─────────────────┬────────────────┘
                                  │
          ┌───────────────────────┼──────────────────────┐
          │                       │                      │
          ▼                       ▼                      ▼
      QML / GUI             Python SDK              Plugins
          │                       │                      │
          └───────────────────────┼──────────────────────┘
                                  ▼
                         COMMAND / TRANSACTION
                                SYSTEM
                                  │
                                  ▼
                           DOCUMENT GRAPH
                                  │
             ┌────────────────────┼────────────────────┐
             │                    │                    │
             ▼                    ▼                    ▼
          FEATURES            ASSEMBLIES          SIMULATION
             │                    │                    │
             └────────────┬───────┴──────────┬─────────┘
                          ▼                  ▼
                        OCCT             SOLVERS
                          │
                          ▼
                    TopoDS_Shapes
                          │
                  ┌───────┴────────┐
                  ▼                ▼
              BREP Cache       Tessellation
                                   │
                                   ▼
                              GPU Renderer

         separate worker processes where appropriate

      ┌─────────────┬──────────────┬───────────────┐
      ▼             ▼              ▼               ▼
    Python        Geometry       Simulation       Import
    Workers       Workers         Workers         Workers
```

---

# 137. Architectural Decisions to Freeze Early

Several decisions should be treated as foundational and changed only with strong justification.

**1. OCCT is the canonical geometry kernel.**

**2. The document graph, not Python code or BREP, is canonical product state.**

**3. C++ owns the application runtime.**

**4. QML owns the primary user-interface presentation layer.**

**5. Python executes outside the UI process.**

**6. build123d is a first-class procedural modeling environment, not the document engine.**

**7. Every persistent mutation goes through commands and transactions.**

**8. AI uses the same command API available to human-facing tools and scripting.**

**9. Geometry computation is asynchronous.**

**10. Simulation is asynchronous and solver-independent.**

**11. Assemblies reference components through instances rather than copying geometry.**

**12. Versions are immutable; workspaces are mutable.**

**13. Branching and semantic merging are built into the document architecture rather than added later.**

**14. Persistent topology identity is treated as core infrastructure from the beginning.**

**15. Rendering is isolated behind an abstraction so OCCT AIS can eventually be supplemented or replaced without replacing the CAD engine.**

---

# 138. Product North Star

The ideal interaction is eventually:

> Design a compact gearbox housing around these components. Use 6061-T6 aluminum. These faces are mounting interfaces and cannot move. Keep the first natural frequency above 400 Hz. Make the design machinable on a 3-axis mill and target less than $90 of material and machining time.

The software should understand that request as a collection of engineering requirements rather than simply a geometry prompt.

It should be able to:

```text
inspect
design
model
assemble
simulate
compare
explain
document
revise
```

while every result remains accessible to the engineer through conventional professional CAD tools.

That combination—**high-performance native CAD for humans plus a semantic engineering platform for AI**—should drive every architectural decision.

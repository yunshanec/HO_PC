🎯 3-Layer Canvas Architecture

Context

We are refactoring an existing HarmonyOS (ArkUI) paper-cut editor page.
The current implementation mixes input, rendering, preview, and state logic in a single Canvas.

Goal:
Refactor the editor into a strict 3-layer Canvas architecture, inspired by professional drawing apps (e.g. Procreate).

This is not a simple drawing canvas.
It is a command-driven, non-destructive editor with fold-based symmetry.

⸻

High-Level Architecture

The editor MUST be split into three independent Canvas layers with clear responsibility boundaries.

UI (Buttons / Gestures)
↓
① InputCanvas        (Interaction Layer)
↓  emits Commands
② OffscreenCanvas    (Data / Truth Layer)
↓  rendered output
③ PreviewCanvas      (Presentation Layer)

Absolutely no layer may take responsibilities from another layer.

⸻

① InputCanvas — Interaction Layer

Responsibility (ONLY)
•	Receive user input (touch / pen / mouse)
•	Restrict drawing to the current sector (fan-shaped) area
•	Draw temporary, in-progress paths only
•	Convert user input into Command objects

Rules
•	Must use ctx.clip(sectorPath) to limit interaction
•	Must clear itself immediately after pointer end
•	Must NOT:
•	store final strokes
•	modify real image data
•	perform undo / redo
•	mirror or rotate content

Output

On pointer end, InputCanvas MUST emit a Command:

onPointerEnd(path) {
emit(new PencilCommand(path))
}

InputCanvas never directly draws to the final result.

⸻

② OffscreenCanvas — Data / Truth Layer

Responsibility (ONLY)
•	Store the true editable result
•	Apply and revert Commands
•	Serve as the single source of truth for the editor

Stored Content
•	Pencil strokes
•	Cut paths (destination-out)
•	Eraser operations
•	Clear operations

Rules
•	Never receives user input
•	Never clips to sector (already constrained by InputCanvas)
•	Only modified via:

command.apply(ctx)
command.revert(ctx)

History System
•	Must use a Command pattern
•	Supports:
•	undo
•	redo
•	clear

Fold Mode Rule (CRITICAL)

Changing foldMode MUST:
•	❌ NOT clear OffscreenCanvas
•	❌ NOT clear history
•	✅ ONLY update sector geometry
•	✅ Trigger preview re-render

⸻

③ PreviewCanvas — Presentation Layer

Responsibility (ONLY)
•	Render the OffscreenCanvas result into a full paper-cut preview
•	Apply:
•	rotation
•	mirroring
•	symmetry expansion

Core Logic

for (let i = 0; i < foldMode; i++) {
ctx.save()
ctx.rotate(i * sectorAngle)

if (i % 2 === 1) {
ctx.scale(-1, 1)
}

ctx.drawImage(offscreenCanvas, ...)
ctx.restore()
}

Rules
•	Never receives input
•	Never modifies data
•	Never participates in undo / redo
•	Resolution may be lower than OffscreenCanvas for performance

⸻

Data Flow (STRICT)

Pointer Event
↓
InputCanvas
↓   (emit Command)
Command
↓
OffscreenCanvas.apply()
↓
PreviewCanvas.render()

Reverse dependencies are forbidden.

⸻

Fold Mode Responsibilities

Layer	Aware of FoldMode	Behavior
InputCanvas	YES	clip sector
OffscreenCanvas	NO	unaffected
PreviewCanvas	YES	rotate / mirror


⸻

Real-Time Preview
•	Preview rendering MUST be decoupled from input
•	Use requestAnimationFrame when preview is enabled
•	Do NOT re-render preview on every pointer move
•	Preview can be toggled on/off

⸻

Refactor Constraints (MANDATORY)
1.	Split existing single-canvas logic into:
•	InputCanvas
•	OffscreenCanvas
•	PreviewCanvas
2.	Remove all direct drawing from InputCanvas to final image
3.	All editing operations MUST be Command-based
4.	Fold mode changes MUST NOT destroy edit history
5.	PreviewCanvas MUST be read-only
6.	This editor is non-destructive

⸻

Final Note

This is not a generic drawing board.

It is a command-driven, geometry-constrained, multi-layer paper-cut editor.

Refactor existing code to match this architecture exactly.

⸻

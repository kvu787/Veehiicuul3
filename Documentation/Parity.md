# Migration and parity inventory

This is a real runnable first milestone, not complete workflow parity. Veehiicuul2 and Win32_DemoApp remain stopped read-only references. No history, build output or old snapshot was copied. Explicit domain/runtime dependencies and regression tests were ported; native application/track UI files were excluded.

| Area                           | First milestone implementation                                              | Evidence / remaining work                                                  |
| ------------------------------ | --------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| Platform and presentation      | DX12 only, actual 2560x1440 target, borderless centered 1:1 output          | 3840x2400 NE18NZ2 pixel comparison; startup bounds/DPI/margin state tests  |
| Minimal shared custom UI       | Buttons, menus, lists, numeric fields, sliders, shared clip/scroll state    | Pointer routing, selection/edit/cancel, scrollbar and content-shrink tests |
| Source SlopeCar / SimplePaint  | Original embedded geometry, five independently painted source regions       | SHA-256 source fingerprint, exact shader contract/hardware/WARP tests      |
| Model cage workflows           | Box/plane/cylinder, picking, vertex drag, transforms, levels/refine/extrude | Domain and shared-controller tests; physical picking/drag feel pending     |
| Selected-face paint slots      | Copy, assign, paint, inheritance, undo/cancel, save/export                  | Full ported face workflow and compiled export tests; displayed capture     |
| Numeric / infinite paint drag  | Full SimplePaint domains, raw relative capture guard, numeric-only speed    | Precision tests, fake-platform cleanup, integrated cancel; physical gap    |
| Track authoring                | NURBS outline drafts, X/Y/weight, surfaces, start, gates, decorations       | Persistence/generation/placement/controller tests; displayed circuit       |
| Shared assets / authored state | Embedded model scene as vehicle/decor; independent histories and modes      | Material/budget/persistence and model-track-drive preservation tests       |
| Driving                        | 2D spatial/core stepping, GameInput, separate 3D presentation               | Simulated stepping, runtime/input edge tests; physical gamepad gap         |
| Persistence / export           | Current Version 2 policy, original asset ABI and source identities          | Scene/track round trips, fingerprint and compiled C++ consumers            |
| Latency association            | Consumed reading timestamps, exact Present submission and ETW decoder       | Clock/protocol/lifecycle tests; actual displayed/physical latency pending  |
| On-demand resource behavior    | Idle message wait, dirty-only DXGI pacing, active driving deadlines         | 3-second hardware/WARP idle measurements; longer-term power pending        |
| Immutable delivery             | Clean-source named snapshots, hashes, copied tests, verified ready pointer  | Each package documents copied tests and remaining manual limits            |

Application workflow gaps to close progressively: editable track surface colors/degree/knots, gate endpoint editing/reordering beyond placement/remove-last, camera-follow toggle, direct selected-part framing, and broader negative native input/capture/focus integration tests. Track point drags currently clear built surfaces; build again before Drive. Numeric edits commit on Enter or clicking elsewhere, rather than continuously previewing each partial string. History is bounded to 128 completed edits to keep resource growth bounded. Drafts and cameras remain authored mode state; per-mode sidebar scroll restoration is still pending. Existing generalized import, creases, limit-surface editing, symmetry, loop cuts and complete game features were already absent in Veehiicuul2 and remain outside this milestone.

The user deliberately excluded Tab navigation, accessibility providers/screen-reader integration and broad UI framework replication. Those are scope omissions, not open delivery gates. IME/rich text, access-key infrastructure, docking, elaborate themes/transitions and a general toolkit are also excluded. Numeric fields accept ASCII decimal/scientific forms; source names render through DirectWrite, but arbitrary name editing is not exposed.

Visible automated verification does not establish physical pointer/gamepad usability or real panel scanout timing. The background test owner never activates; cursor operations are simulated. No claim of faster UI or lower electrical power than the native reference is made.

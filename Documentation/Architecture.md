# Veehiicuul3 architecture and staged migration

Windows 11 x64, C++20, DX12. Veehiicuul2 is a read-only reference at 8352897; its selected-face material candidate was 533c1b9 and last visually verified scrollbar build was 48bad5c. Win32_DemoApp is also read-only. The existing Veehiicuul3 repository and AGENTS.md are preserved.

The planned application owns a single HWND, one DX12 swap chain and an actual 2560x1440 offscreen render target. The borderless window covers the selected monitor; its final pass centers the image unscaled and clears black margins. Pointer coordinates subtract the centered offset and black margins are inactive. Too-small displays are rejected without changing desktop resolution. Mode, commands, inspector controls, menus, scrollbars and text fields are application data, hit testing and GPU geometry. There are no Common Controls child widgets, native in-app menus, ImGui, Qt, WinUI or web runtime. This is a small application-specific layer, not a toolkit.

DirectWrite shapes/rasterizes text into a bounded coverage atlas; DX12 composites cached text and colored triangles. Win32 provides window/DPI/input messages, raw relative mouse input, cursor ownership, clipboard, standard file dialogs and monitor identity. The platform keeps these services because replacing them would add complexity with little application benefit. GPU buffers use fences before reuse. Static UI renders only on invalidation; active driving schedules bounded updates. Minimized/inactive driving suspends. No frame-latency handle is polled continuously when the editor is idle.

The preserved domain boundary includes Catmull-Clark cages, inherited face materials, serialization/export, exact embedded SlopeCar data, the original SimplePaint module, NURBS tessellation and planar Racing2D. Racing2D has no renderer, HWND or 3D spatial dependency. Track/vehicle presentation derives from that 2D state. GameInput reading timestamps and consumed-reading display correlation remain distinct from input-to-physical-display verification.

## Current scope

Pointer-driven buttons, lists, menus, toggle actions, clipping/scroll synchronization, captured drags, numeric text editing, cancellation and DPI layout are required. Relative paint dragging hides and restores the cursor and uses raw horizontal counts; drag speed is numeric only. Mode changes keep independently authored model and track state. A neutral UI palette keeps 3D semantic colors.

The user explicitly omitted Tab navigation and accessibility work. Do not add Tab/Shift+Tab traversal or UI Automation providers in this milestone. General keyboard navigation, access-key infrastructure, IME/rich text, docking, themes and animation frameworks are deliberately deferred. Numeric fields accept ASCII numbers and essential selection/clipboard/editing keys. Names remain source-provided in this milestone.

## Stages

1. Port tested domain/input/rendering modules and establish their regression tests.
2. Deliver a runnable custom DX12 UI connected to real model, track and driving operations. Keep every missing workflow visible in Parity.md.
3. Close application workflow gaps incrementally with domain and shared UI state tests. This does not authorize adding omitted framework features.
4. Verify displayed rendering and physical interaction exclusively on NE18NZ2. Locked sessions do not block stages 1-3 or hidden GPU checks. Candidate builds stay explicitly unverified until their displayed checks pass.

No dependency installation, security/power setting change or unlock is authorized. Effective 2026-10-09 17:40:31 UTC, never run git push. All commits stay local; the earlier public-push approval is revoked. Do not request push approval or substitute another publishing transport. Self-contained snapshots are Git-ignored, immutable, named by source commit, and retain earlier snapshots.

## Visibility and production scheduling

Run and automated bounded observation use the same Pump implementation. Its only finite deadline in static editing is an explicit test observation limit. DXGI pacing handles are included only when a frame is dirty and known visibility allows it. Active driving waits for a bounded update deadline; sub-step ticks do not invalidate a frame. Hidden/minimized, unavailable session/input desktop, console display-off, DWM-cloaked and suspended states block both ticking and drawing. Resumption starts without integrating the blocked interval.

The application registers DXGI visibility notifications, WTS session messages, console-display power notifications and process-local DWM cloaking events; show/minimize/activation messages also refresh observed state. Registrations are released before window destruction. Notifications trigger state queries, not polling. WM_PAINT never clears a blocking visibility state. Normal and instrumented Present share the same scheduler gates. DXGI_STATUS_OCCLUDED and DXGI_PRESENT_TEST are not used as flip-discard visibility guarantees: [Microsoft's DXGI status contract](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-status) explicitly excludes flip-model occlusion status.

These signals establish known platform visibility, not per-pixel compositor visibility. Arbitrary opaque/translucent overlapping windows are not exhaustively classified; inactive driving already pauses, and static editing has no permanent loop. The console power signal does not identify every individual panel's power state. DWM/DXGI notification refreshes do not turn backbuffer comparisons into physical scanout evidence. Automatic tests inject visibility messages/state only into this application's window and run both instrumented and ordinary Present through Pump; they do not minimize, lock, suspend or power off the user's environment.

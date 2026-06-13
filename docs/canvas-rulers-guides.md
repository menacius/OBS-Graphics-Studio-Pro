# Canvas rulers and Photoshop-style guides

This update adds a Photoshop-style ruler and guide workflow to the title editor canvas.

## View menu

The editor `View` menu now includes:

- **Rulers** — shows or hides horizontal and vertical pixel rulers around the canvas.
- **Guides** — shows or hides user-created guides.
- **Lock Guides** — prevents guide creation, movement, and deletion.
- **Show Guide Coordinates** — shows the live X/Y pixel coordinate while dragging a guide.
- **Clear Guides** — removes all user-created guides.

The ruler and guide options are persisted through `QSettings` so the editor restores the last-used ruler state.

## Ruler behavior

- Rulers reserve a top and left gutter so the canvas remains readable and centered inside the remaining canvas area.
- Tick marks scale with the current zoom level and use adaptive major/minor spacing.
- Coordinates are expressed in canvas pixels.

## Guide behavior

- Drag from the top ruler to create a vertical guide.
- Drag from the left ruler to create a horizontal guide.
- Drag an existing guide to reposition it.
- Drag an existing guide outside the canvas band to remove it.
- When guide coordinates are enabled, a small live readout appears while dragging.
- Lock Guides disables creating, moving, and removing guides.

## Snapping integration

User-created guides are included in the existing `View > Snap To > Guides` target set. When guide snapping is enabled, layer moves/resizes can snap to custom ruler guides in addition to the existing title/action safe guides.

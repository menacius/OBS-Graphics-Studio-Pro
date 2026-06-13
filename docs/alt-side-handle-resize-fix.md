# Alt side-handle resize fix

Side resize handles now preserve their single-axis behavior while Alt is held:

- Left/right handles perform centered horizontal resizing only.
- Top/bottom handles perform centered vertical resizing only.
- Corner handles keep the existing centered two-axis resize behavior.
- Shift/aspect-lock behavior remains routed through the existing modifier resize helper.

This prevents side handles from behaving like corner handles during Alt-drag manipulation on the canvas.

# OBS Theme GUI Audit

This change removes hardcoded dark styling from the Effects dock and the Text layer Type panel and derives their colors from the active Qt/OBS palette.

Updated areas:
- Effects dock root, effect list, toolbar buttons, add-effect menu, settings scroll area, effect setting groups, spin boxes, combo boxes, and effect color buttons.
- Text layer properties Type panel, including panel frame/title and type toggle buttons.
- Related popup/context UI used by properties, including keyframe context menus, stroke option popup, color/gradient editor popups, placeholder style tabs, color swatch panel labels/buttons, and live cue play button styling.

Notes:
- Intentional semantic colors remain unchanged where they represent state rather than theme, such as the unsaved-change red dot and warning yellow label.
- Color swatches and gradient ramps still display their actual color values by design, while their borders/text follow the active palette.

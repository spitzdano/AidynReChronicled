# Aidyn ReChronicled: button icons

The Xbox controller buttons Aidyn ReChronicled shows: inside the game's own text ("DETAILS [X]"), on the controls
card (walking and combat; View shows and hides it), beside the pause pages' arrows (LB, RB) and in the title screen's
version switch. All of them are made from the 15 icons here, so redrawing an icon changes it everywhere.

These are the port's own pictures (drawn from shapes and a font), not the game's art.

## What's here

| | |
|---|---|
| `current/` | The 15 icons as they are drawn now, 256x256: A, B, X, Y, LB, RB, LT, RT, View, Menu, DPad, LS, RS, L3, R3 |
| `redone/` | Empty: new versions go here, under the same file names |
| `in-game/` | Where they appear, at 8 times the game's size: every line of the controls cards (`ctl_*`), the page prompts (`prompt_lb`, `prompt_rb`), the version switch (`version_*`) and the icons in the game's text (`font/`) |
| `contact-cards.png` | The walking and combat cards as the game shows them |
| `contact-buttons.png` | The 15 icons at a glance |
| `manifest.json` | Each icon: what it is on an N64 controller, what it does in the game, and where it is drawn at what size |

## Rules for redone icons

1. **Same file name**, in `redone/` (`A.png`, `LB.png`, `L3.png`…). Leave out any you don't redo.
2. **Square, transparent background**, the button centred and filling most of the square, like the current ones.
   Wide buttons (LB, RB, LT, RT) are drawn inside the square too. 256x256 or larger.
3. **Readable small.** In the game an icon is 7 to 10 texels across: about 30 to 45 pixels on a 1080p screen. Bold
   shapes, strong contrast, the letter large; fine detail disappears.
4. **Keep the meanings recognisable**: A green, B red, X blue, Y yellow (as on the controller), the View button's two
   windows and the Menu button's three lines, L / R on the sticks.
5. **PNG**, 8-bit RGBA.

## Putting them back

Bring `redone/` back to the PC and ask for it to be built in. The files go into the project's `art/buttons/`; every
prompt and the HD menu pack's text icons are then redrawn from them, so nothing else needs doing.

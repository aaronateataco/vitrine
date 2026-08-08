# Reference screenshots

`switch2-home-menu.jpg` is a real Nintendo Switch 2 HOME menu, supplied by the
repository owner from their own console. It is kept here as the measurement
source for VITRINE's Console layout, which was previously guessed from written
descriptions and got several things wrong.

Measurements taken from it, at 1920x1080, converted into the 1280x720 design
space `ui.c` is authored in (divide by 1.5):

| Element | Screenshot (1080p) | Design space |
|---|---|---|
| User avatars | d=72, y=75, pitch=112 | d=48, y=50, pitch=75 |
| Clock / battery | y≈115, right edge 1860 | y=64, right=1228 |
| Selected title | x=270, y=253, **cyan**, above the row | x=180, y=155 |
| Game icons | 400px, first at x=152, pitch=416, y=293 | 267px, x=101, pitch=277, y=195 |
| Corner radius | ~48 on 400 (12%) | 12% of tile |
| Dock pill | x 345..1578, y 772..908 | x 230..1052, y 515..605 |
| Button hints | y≈1020, bottom-right | y=672 |

Two things the screenshot corrected outright:

- The background is a **charcoal grey (~38,38,38)**, not the near-black I had.
- The menu **is not monochrome** — the selected title is cyan and the dock icons
  are individually coloured. Reporting about the theme picker offering only
  "Basic Dark" and "Basic Light" had led me to strip all colour out.
- The selection sits at a **fixed left position** with the row sliding beneath
  it, rather than being centred.

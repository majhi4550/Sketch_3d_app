# Sketch3D 3.5

## New in 2.1
- **Trim (Q)** - AutoCAD style: every other entity in the sketch plane is a cutting edge. Click the part to remove (red preview).
  Works on lines, polylines, rectangles and circles (a trimmed circle becomes an arc). **Shift = Extend.**
- **Extend (Shift+X)** - click near the end of a line/polyline to run it to the next edge (green preview). **Shift = Trim.**
- **Mirror (Shift+M)** - pick objects, Enter, click two points of the mirror line; live ghost preview, `K` toggles keep-original, F8 ortho.
  Rectangles that stop being axis-aligned become polylines automatically.
- **Pattern (Shift+A)** - rectangular (rows x columns x spacing) and polar (count, total angle, centre, rotate items) dialog; stays open for repeated Apply.
- **Dynamic input (F12)** - size fields drawn in the viewport at the geometry: Length + Angle for lines, Width + Height for rectangles, Radius for circles.
  Type a number, `Tab` (or `,` / `<`) jumps to the next field, `Enter` accepts, `Esc` clears. Typed fields stay locked while the other follows the mouse.
- **Dimensions** now measure and offset inside the active sketch plane: Aligned / Horizontal / Vertical (`K` / `H` / `V` or the Dim box), with extension lines, arrows and a readable value.
- Esc on an idle command returns to Select.


CAD-style **3D sketch application** (Qt 6 Widgets, C++17, CMake). Everything you draw is true 3D geometry,
and you can draw **directly in the 3D view**.

## What's new in 2.0
**Viewport**
- Real camera: orthographic Top / Front / Left / Right / Back / Bottom, free **ISO 3D** orbit with optional **perspective** (field of view adjustable).
- Orientation **gizmo** (top-right): click an axis end to look along it. Ctrl+1..7 for the named views.
- Zoom to cursor, pan, orbit (turntable style), *Fit all* (Ctrl+0), *Fit selection* (F).
- Circles, arcs, rectangles and images are projected correctly (ellipses / parallelograms in 3D).
- Viewport dock: views, camera, sketch plane + offset, grid size / extent, axes, paper, plane sheet, gizmo, 4 themes (Dark, Graphite, Blueprint, Light), line weight, orbit speed. Settings are remembered.
- Grid with automatic level-of-detail, origin triad, hover highlight, selection glow.

**Drawing in 3D**
- Mouse is ray-cast onto the active **sketch plane** (XY / XZ / ZY / YX) with an adjustable **offset**.
- **Tab** cycles the plane; hover a point and press **O** to move the plane through it.
- **X / Y / Z** lock the cursor to a world axis after the first point -> draw vertical lines in the 3D view.
- Snaps: grid, endpoint, midpoint, centre, alignment guides, **Ortho** mode (F9 / F3 / F8).
- **Type a number** while drawing (`120`, or `80,40` for a rectangle) + Enter for exact sizes; live length / angle / radius readout.
- Top/Front/Side views automatically select the matching plane (can be switched off).

**Editing**
- Copy / Paste / Duplicate, Mirror (keep original or not), Array (count + vector), Move by vector, arrow-key nudge (Shift = x10, PgUp/PgDn along the plane normal).
- **Extrude**: sweeps rectangles, circles, polylines, arcs into a 3D wireframe.
- Trim / Join / Break / Offset now work in the entity's own plane (correct in any view). Trim removes the part you click.
- Box select: left-to-right = fully inside, right-to-left = touching.
- Right-click context menu, Objects list, selection info (length, radius, area...).
- Undo/redo keeps your selection and camera.

**Files**: Open / Save `.sk3`, recent files, export PNG / JPG / PDF and **DXF** (3D LINE entities).

## Controls
| Action | Input |
|---|---|
| Orbit | Right-drag, or Alt + left-drag |
| Pan | Middle-drag, Shift + left-drag, or Space + left-drag |
| Zoom | Wheel (towards the cursor) |
| Tools | S select, L line, P polyline, R rect, C circle, A arc, U curve, D dimension, T text, Q trim, J join, B break, W offset, E edit, M move, N dynamic move |
| Finish polyline | Enter / double-click |
| Cancel | Esc |
| Help | F1 |

## Build (Qt Creator)
1. *File > Open File or Project...* -> `CMakeLists.txt`
2. Configure with a **Qt 6.2+** kit (tested on Qt 6.4 and your 6.11 MinGW setup)
3. Build & run

Source layout: `canvas.cpp` (camera, snapping, input), `canvas_paint.cpp` (rendering), `canvas_ops.cpp` (editing, undo, files),
`mainwindow.cpp` (UI), `sketchitem.*` (entity data). Old `.sk3` files still open.

## 2.2 – Virtual Cutter & strong Trim

* **Trim (Q)** now works on lines, polylines, rectangles, circles, arcs and curves in any plane. Circles/arcs/curves stay true arcs/curves after cutting; rectangles become polylines. Drag a **fence** with Trim to trim many entities at once.
* **Cutter (Shift+C)**: click two blade points. *Blade: Split* slices every crossed shape into pieces; *Blade: Cut away side* uses an endless blade and a third click on the side to remove. Select objects first to restrict the cut.
* **Cut mode** toolbar combo: *View* (virtual – cuts what visibly crosses on screen, even across different planes, so the final look is right) or *True 3D* (only real 3D intersections).
* Every cut is a single undo step; Esc cancels a pending blade.

## 2.3 – Precision tracking

* **Ortho (F8)** now locks the direction from the last point *exactly* and keeps lengths grid-precise; **hold Shift** while drawing for temporary ortho (Shift+click draws instead of panning while a drawing tool is active). Works for drag-moves too.
* **Polar tracking (F10)** with a step selector (5°–90°): the direction snaps to the nearest angle step with a cyan guide ray and a live `POLAR 30°` / `ORTHO 0°` tag beside the cursor.
* Tracked direction + alignment with any existing point snaps to the exact crossing (`+ ALIGN`); length snaps to the grid relative to the start point.
* New object snaps: **Intersection** and **Perpendicular**, besides endpoint / midpoint / centre.
* Typed dynamic-input values keep the locked angle.

## 2.4 – Grip fixes when switching planes

* Rectangles, circles, text and images now snap **only to points on the current sketch plane** (a snap on another plane's geometry used to pull the corner away from the cursor). Line-type tools can still snap in 3D; such snaps are preferred on-plane and labelled "(off plane)".
* Every sketch plane now remembers its **own offset** (no stale offset carried to the next plane), and the cursor is re-snapped the moment the plane changes.
* Grazing views: when the view is almost edge-on to the sketch plane the cursor no longer shoots far away; a hint asks you to rotate the view or change plane.

## 2.5 – Precise move, groups, line types, text

* **Object snap restored** for Rect/Circle/Text across planes: points on other planes snap to their *projection* onto the sketch plane (labelled "projected"), so the shape stays under the cursor.
* **Move (M)** – base point → target with typed distance + angle (Tab), ortho/polar/object snaps, live ΔX ΔY ΔZ preview, **K / Copy** for multiple copies. Drag-moves snap any vertex of the moved set onto other geometry.
* **Group / Ungroup** (Ctrl+G / Ctrl+Shift+G); groups survive copy, paste, mirror, pattern, trim and save/load.
* **Line types** (solid, dash, dot, dash-dot, centre, hidden) that scale with the drawing; **true line weight** (mm × zoom, no 7 px cap); Style toolbar with colour palette. DXF export writes line type, weight and colour.
* **Text** – multi-line, font, bold/italic, rotation, height; edit via double-click / F2 / Properties panel.

## 2.5.1 / 2.6 – Easier work: transform size, shapes stay on the sheet

* Icons for every tool, **Ctrl+K command palette**, floating selection bar, **align / distribute** with smart guides, welcome overlay.
* **Transform size:** selecting objects shows a bounding box with 8 grips. Drag a corner to scale proportionally (Shift = free), an edge to stretch one direction; grips snap to geometry and the box shows the live W × H and %. The Properties panel has a **Size** group: type exact Width / Height (optionally keep proportions, choose which corner / centre stays fixed) and press Apply size.
* **Limit drawing to the sheet** (View menu, on by default): the cursor stops at the sketch-sheet edge, so shapes can no longer be created far away in space by accident. Raise *Grid half-size* in the Viewport panel for bigger drawings, or switch the option off.

## 2.6.1 - slanted lines fix
- Cause: line tools snapped to points lying at a different depth, so one end left the sketch plane (looks fine from the top, slanted from the side).
- Drawing tools now stay on the sketch plane. Snaps to off-plane points are projected onto it. View > "Snap to points off the plane (3D snap)" restores the old behaviour.
- Edit > "Flatten to sketch plane" (Ctrl+Shift+F) puts the selection (or the whole drawing) exactly on the current plane. Undoable.

## 2.7 - freedom of manipulation and look
- Rotation grip: a round handle above the selection box. Drag it to rotate freely around the selection centre; Shift = 15 deg steps. Rectangles turned at odd angles become closed polylines with true corners (the Rotate button does the same now).
- Flip horizontal / vertical in place (Alt+Shift+H / Alt+Shift+V, selection bar, Properties > Transform).
- Fill colour with transparency for closed shapes (rectangle, circle, closed polyline): Properties > Pen > Fill, selection bar, Edit menu. "None" removes it. Saved in the file.
- Draw order: Bring to front (Ctrl+]) / Send to back (Ctrl+[).

## 2.7.1 - object snap on any plane
- The first point of a shape snaps to the real 3D point of any object (end, mid, centre, intersection), whatever plane it was drawn on.
- The rest of that shape is drawn in the parallel plane through that first point, so side/top panels start exactly on the cabinet corner and never come out slanted.
- Move base/target points snap to true 3D points too.

## 2.8 - Model menu: solids, constraints, corners, visibility
- Snap "On line": a point can land anywhere on a line/edge (move a point onto another vector's line). Move keeps sizes; works across planes.
- Extrude solid (Ctrl+E, Model toolbar, selection bar, Properties > Model): closed shapes become shaded solid bodies. Bodies are never cut by Trim/Cutter/fence (unbreakable). Their far face snaps too.
- Sketch on top/bottom face of a body (sketch plane jumps onto the face).
- Split body at the sketch plane. Shell: rectangular body -> open box (base + 4 walls, grouped).
- Fillet / Chamfer between two selected lines (radius 0 = sharp corner).
- Constraints (one-shot, first selected line = reference): Horizontal, Vertical, Parallel, Perpendicular, Equal, Join ends; Fix (Ctrl+L) locks objects, Unfix all.
- Appearance: Fill colour = body colour (alpha = transparency).
- Hide (Ctrl+H), Isolate (Ctrl+Shift+I), Show all (Ctrl+Shift+H).

## 360° viewport, user view orientation (Fusion style)
- **Free orbit** (default, View menu / Viewport panel / cube menu, `Ctrl+Shift+O`): right-drag turns the model about **any axis**, straight over the top and upside down - no stops. **Ctrl + right-drag** rolls about the viewing axis.
  Switch it off for the **turntable** orbit (Top stays up; tilt stops just short of straight down/up without jamming).
- **Set current view as Top / Front / Left / Right / Back / Bottom** (right-click the view cube, View > View orientation, or the *View = Top / View = Front* buttons in the Viewport panel): whatever you are looking at becomes that view. The view cube, Ctrl+1..7, the view buttons and the turntable "up" all follow. *Reset view orientation* returns to the world axes. The small XYZ triad always shows the real world axes.
- **Home**: house button / Ctrl+R. *Set current view as Home* stores any view as Home; *Reset Home* goes back to the ISO corner.
- **Roll 90°**: the round arrows above the cube, or `Ctrl+Shift+Left / Right`.
- Face clicks on the cube now swing smoothly to the view. Orientation, Home and the 3D camera are saved in the `.sk3` file (older files still open).

## Offset (W) - reworked
- **Click the object, then click the side.** A green live preview follows the cursor and shows the distance.
- **Type the distance** at any time (e.g. `25.5`) + **Enter**. With an object picked and the cursor on a side, Enter makes the copy straight away; the value is remembered and shown in Properties > Offset distance.
- **T = Through**: the copy passes through the point you click (distance read live from the cursor).
- **Enter** with nothing picked offsets the copy just made again (fast concentric / nested outlines). **Esc** = pick another object.
- Polylines (open and closed) get true mitred corners at exactly the offset distance (the old version shortened the corners and always went to one side). Arcs and 3-point curves are supported now, as well as lines, rectangles and circles.

## View cube (replaces the axis gizmo)
- Top-right of the viewport: a labelled cube that turns with the camera. 26 click points:
  - **6 faces** -> Top / Bottom / Front / Back / Left / Right (the named orthographic views),
  - **12 edges** -> 45° views between two faces (e.g. TOP FRONT),
  - **8 corners** -> isometric views (e.g. TOP FRONT RIGHT).
- Hover lights up the spot (a corner lights on every visible face it touches). Edge and corner clicks animate smoothly and keep the zoom.
- **House button** (bottom-right of the cube) = home ISO view, same as *Reset ISO* but keeps the zoom. Small XYZ triad bottom-left.
- After orbiting anywhere, one click on a face, edge or corner returns to an exact view.

## 2.8.1 - explode, break rectangles, pen options
- Explode (Ctrl+Shift+X, Edit menu, right-click, selection bar): rectangles and polylines fall apart into single lines at every joint; groups are released. Solid bodies stay whole.
- Break now works on rectangles too; a closed rectangle/polyline opens at the clicked point into one open polyline.
- Line colour / thickness / type: right-click menu on the selection and two new selection-bar buttons (thickness, line type).

## 2.9 - big sheets, feet-inches, hatch and colour fill
**Bigger sheet.** *Grid half-size* now goes up to **50 000 mm** (50 m / 164 ft each way, 10x the old 5 000 mm). The grid only draws what is on screen, so it stays fine on a big sheet, and you can zoom out 25x further than before to see all of it.

**Units** (View > Units, or the *Units* group in the Viewport panel):
- Millimetres, Centimetres, Metres, decimal Inches, decimal Feet, **Architectural** `5'-6 1/2"` (rounded to 1/2 ... 1/64", *Fractions*) and **Engineering** `5'-6.50"` (*Decimals*).
- The drawing is always stored in exact mm, so you can switch at any time. Every size field (offset, size W/H, move vector, extrude, shell, fillet, plane offset, grid half-size, pattern spacing, page size ...), the dimension text, the cursor read-out, selection info, the grid list (1/16" ... 5' in feet-inches) and the on-screen labels follow. Line weights stay in mm.
- **Type lengths in any unit**, in any field, in the size boxes while drawing and in Offset: `4'-6 1/2"`, `4' 6"`, `4'6`, `54"`, `6-1/2"`, `3/8"`, `2ft 3in`, `1.2m`, `120cm`, `1200mm`. A bare number is in the current unit (inches for the feet-inches systems, like AutoCAD). While drawing, `'` `"` `/` and the space bar go into the size box.
- The units are saved in the `.sk3` file and come back when it is opened.

**Hatch / colour fill (H)** - like AutoCAD's "pick internal point":
- Choose the Hatch tool, hover inside any area that lines, polylines, rectangles, circles, arcs and curves close off on the sketch plane - they may cross each other and stick out past the corners - and click. A live preview shows the area before you click.
- Shapes inside the area are **islands** and stay open (a circle in a rectangle: click between them for a ring, inside the circle for a disc).
- Patterns: Solid fill, Diagonal (ANSI31), Diagonal reverse, Cross-hatch (ANSI37), Horizontal, Vertical, Square grid, Steel (ANSI32), Brick / block, Dots, Earth. Spacing (Auto = about 10 px at the zoom you hatch at, or a real length), angle and fill colour (with transparency) are in *Properties > Hatch / colour fill*. The lines use the pen colour; a fill colour sits under a pattern, or is the colour of a solid fill.
- A hatch is its own object: click inside it to select, move, copy, mirror, delete it. *Apply to selected* restyles selected hatches - and puts a pattern straight onto selected rectangles, circles and closed polylines.
- Exports: PNG / PDF as drawn; DXF gets the hatch lines on a layer `HATCH` (solid fills are not written to DXF).

**Separate drawing windows:**
- **File > New Window (Ctrl+Shift+N)** opens another drawing in its own window, with its own file, undo history, view and selection. **File > Open in New Window** opens a file without closing the current drawing.
- The **Window** menu lists every open drawing (`*` = unsaved) and has **Tile windows side by side** for comparing drawings or copying between them. **Ctrl+W** closes one window; **File > Exit** closes them all and asks about each unsaved drawing by name.
- **Copy / paste between windows:** Ctrl+C in one window, Ctrl+V in another. The first paste lands in the same place, and a paste in the same window steps aside as before. It uses the Windows clipboard, so it also works between two separately started copies of Sketch3D.
- Opening a file that is already open in another window brings that window to the front instead of opening it twice.
- Units are one setting for the whole app: switching to feet-inches in one window switches every window.

## 3.0 - 3D boards: Box, Press/Pull, cut list (the AutoCAD 3D modelling way)
Build furniture the way it is done in AutoCAD's 3D Modeling workspace: every board is a box, boards are snapped onto each other, faces are pulled until they meet.

**Box (Shift+B, Model > Box, or the Box button):** click the first corner, the opposite corner, then the height.
- **Start on a board's face and the new box grows straight out of that face** (the face lights up blue while you hover - AutoCAD's dynamic UCS). A top goes on top of a side, a shelf starts on the inside face of a side, and so on. Afterwards the sketch plane goes back to where it was.
- Corners, midpoints and centres of every board snap (both ends of the board), so boards meet exactly.
- Type the sizes: length, **Tab**, width, **Enter**, then the height + **Enter**. A typed height goes out of the face you started on; a negative one goes the other way.
- **Enter alone at the height step makes a board of the set thickness** (Properties > Model > *Board*, default 18 mm). Every size can be typed in feet-inches too.

**Press/Pull (Shift+P):** click a face of a board and move the mouse - the face slides along its normal and the board grows or shrinks (end faces change the thickness/length, side faces stretch the board, a circle's side changes its radius). Snap onto a corner of another board to stop exactly there, or type the distance + Enter. Click again to finish, or press-drag-release. **Esc** puts it back. Click inside a closed 2D shape and Press/Pull turns it into a board.

**Real 3D display:** boards are drawn with a depth buffer, so they hide each other correctly from every side (no more doors showing through the back panel), and only visible edges are drawn. Click a board's face to select it.

**Mirror and patterns keep a board's thickness on the right side** (before, a mirrored side panel came out shifted by its own thickness); scaling scales the thickness too.

**Part names and cut list:**
- Type a **Name** at the top of the Properties panel (Side, Top, Shelf ...) and press Enter. Names show in the Objects list.
- **Model > Cut list (Ctrl+Shift+B)** lists every board with length, width, thickness (L >= W >= T) and quantity - identical boards with the same name are counted together - plus the board area in m². **Copy** pastes into Excel as columns, **Save CSV** writes a file for Excel / the cost sheet, **Place on drawing** puts the table on the sheet.

**Self-test:** `Sketch3D.exe --selftest <folder>` builds a carcass with these tools through simulated mouse and keys and writes PASS/FAIL lines, the cut list and screenshots to the folder; `--render <file.sk3> <out.png>` saves a picture of a drawing.

## 3.1 - move / rotate gizmo, Union / Subtract / Intersect
**Gizmo** (the *Gizmo* box in the tools toolbar, or Model > Gizmo): in the 3D view, with the Select tool, the selection shows
- **Move**: three arrows (X red, Y green, Z blue) - move along that axis; three small squares - move in that plane;
- **Rotate**: three rings - turn about X / Y / Z around the selection's centre (Shift = 15° steps).

Click a handle, move the mouse, click again (or press - drag - release). Type a distance or angle + Enter for an exact value; Esc cancels. Moving along an arrow over a corner of another board stops the selection's near or far side exactly on it, so boards butt together. Boards keep their thickness the right way round, rectangles turned off the axes become closed polylines, and Top / Front / Side views keep the 2D resize and turn grips.

**Union (Ctrl+Alt+U), Subtract (Ctrl+Alt+S), Intersect (Ctrl+Alt+I)** - Model menu, Model toolbar, Properties > Model:
- **Union**: the selected boards become one (e.g. a stepped or L-shaped top).
- **Subtract**: select the board to cut **first**, then Ctrl+click the boards that cut it - toe-kick notches, grooves, rebates, cut-outs, holes. The cutters are removed; *Model > Subtract, keep the cutting boards* keeps them (for joints: cut a shelf's groove with the shelf itself).
- **Intersect**: only what the boards share is left.
- The boards must be square to each other. The result is one board where possible (a notch through a panel gives one L-shaped panel); when it needs several pieces they are grouped and count as **one part** in the cut list. Boards can now have holes.

## 3.2 - Rotate tool, quick turns, materials, sheet nesting
**Rotate (Shift+R)** - the AutoCAD / Fusion way, in 3D:
1. select the objects (or start the tool and click them / drag a box, then Enter);
2. click the **base point** - the pivot (corners and midpoints of boards snap);
3. move the mouse and click, or **type the angle + Enter**. The ghost shows the result, the arc shows the angle.
- **X / Y / Z** turn about that world axis through the pivot (the line through the pivot shows it in red / green / blue); the same key again goes back to the sketch plane's normal.
- **Shift** = 15° steps, **F8** (ortho) = 90° steps, polar tracking steps work too.
- **K** = copy: the original stays and the tool keeps making turned copies (a fan of shelves, a lazy-susan) until Esc.
- Boards keep their thickness; text turns with the drawing.

**Quick turns** - stand a board up or lay it down in one click: *Model > Rotate > Turn ±90° about X / Y / Z, 180° about Z*, or Properties > **Turn X 90° / Y 90° / Z 90°** (Shift+click = -90°). Properties > **Rotate** turns the selection by any angle about the plane normal or X / Y / Z, around its centre.

**Materials** - Properties > **Material**: type or pick one (18 / 12 / 9 / 6 mm ply, 18 mm MDF, 16 mm particle board, 25 mm solid wood, any name you type, or one already in the drawing) for the selected boards. Booleans keep it. The cut list has a **Material** column, sorts by material, thickness and name, and boards of different materials are never counted as the same part.

**Sheet nesting** - Cut list > **Sheets** tab: set the stock sheet (default 2440 x 1220), saw kerf (4 mm) and edge trim (10 mm), and whether parts may turn (untick to keep the grain along the sheet). Every material + thickness is packed onto its own sheets, drawn to scale with labels, with the number of sheets, the area used and any part larger than the sheet. *Save sheet layouts PNG* saves the pictures for the workshop. The settings are remembered.
- How it packs (MaxRects): 6 part orders x 4 fit rules x 2 sheet choices are tried and the fewest sheets win, then the emptiest last sheet (the biggest usable offcut). Measured on 300 random cut lists against one pass: 3054 -> 3031 sheets, 23 lists need one sheet less, none more, about 2 ms per cut list.

**Tests** - `--selftest <folder>` now also checks the Rotate tool (typed angle, X axis + copy), quick turns, materials in the cut list, a save / load round trip, union undo / redo and the nesting (known answers, no overlaps, kerf and trim kept, the 300-list study). `--bench <file.sk3> <out.txt>` times drawing a model from new and cached views and a save / load round trip (a 217-board kitchen: 6.7 ms per new view).

## 3.3 - Fillet, Chamfer, Shell, Revolve, Pipe, Extrude Join / Cut, Cut joints, calculator
**Calculator in every typed value** - while drawing, extruding, moving, turning or in the Properties boxes, type a sum: `800-2*18`, `(2400-3*18)/4`, `1.2m-18`, `360/7` (angles). The field shows the result as you type (`800-2*18 = 764`). Feet-inch values work inside sums: `4'-6"+3/4"`; with feet-inch units `6-1/2` stays six and a half inches.

**Sketch on a face, then extrude** - Rectangle, Circle and Polyline started on a board's face are drawn in that face (like Box). Press/Pull on such a shape (it wins over the face under it) makes a body that:
- **Auto** (default): **cuts** when you pull into a board (pockets, grooves, holes, rebates), otherwise is a **new body**;
- while pulling press **N** new body, **J** join (grows the board), **C** cut, **I** intersect, **A** auto; Model > Extrude a shape does sets the default.
One undo step each; a cut board stays one part in the cut list.

**Cut joints (Ctrl+Shift+J)** - sink a shelf, rail or tenon into the boards it meets (e.g. 6 mm into each side), select it, Cut joints: every board it goes into gets the housing / dado / mortise, the selected board stays whole.

**Fillet (Shift+F) and Chamfer (Shift+H) tools** - type the radius / distance, point at a corner or edge (it lights up) and click; keep clicking. They work on corners of 2D rectangles and polylines, two lines that meet, board edges along the thickness (any board: rounded table-top corners), and any edge of a box-shaped board (a rounded or bevelled front edge). Rounded faces are drawn smooth, with their outline where they turn away from you. (An edge across the thickness of a board that is not a plain box cannot be rounded; the tool shows it in red and says why.)

**Shell tool (Shift+S)** - type the wall thickness, click the face to leave open. A box becomes a carcass of separate, named boards: the two sides full size, top and bottom between them, the back inside all four (each its own part in the cut list, grouped). Any other board opened at an end becomes walls + an end plate. Model > Shell the selected boards opens them at the far end.

**Revolve tool (Shift+O)** - click a closed shape, then two points of the axis (in the shape's plane, e.g. a line's ends); type the angle first (Enter = 360). Legs, knobs, bowls, rings. Or select the shape and a line: Model > Revolve the selected shape. The cut list gives the turned blank (length x diameter x diameter).

**Pipe tool (Shift+I)** - click a line, polyline, arc or curve: a rod or tube along it with true bends. Type the diameter; Model > Pipe settings sets the wall (0 = solid rod) and the bend radius (default 1.5 x diameter, smaller where the segments are short). The cut list gives the developed (cut) length: an L of 500 + 400 with R 50 bends is 878.5. Pipes and turned parts are left out of the sheet nesting.

**Tests** - `--selftest` checks the calculator (metric and feet-inch), a box typed with a sum, sketching on a face, extrude cut / join by exact volume, cut joints, fillet and chamfer volumes (to the 0.05 mm chord error of the arcs), the shell carcass and its cut list, revolve and pipe cut data and a save / load of a pipe: 67 checks. Drawing speed unchanged (217-board kitchen: 7.5 - 7.7 ms per new view, 3.2: 7.3 - 7.9 ms).

## 3.4 - 3D Tools panel, Shift multi-pick, multi-face Press/Pull, up to a face, Combine dialog, Box follows the mouse
**3D Tools panel** (left side, View > 3D Tools): the modelling tools from the upgrades in one place - Create (Box, Press/Pull, Revolve, Pipe), Modify (Fillet, Chamfer, Shell, Rotate), Combine (Combine..., Union, Subtract, Intersect, Cut joints, Cut list), what an extruded shape does by default, and the sizes (board, fillet R, chamfer, shell wall, pipe diameter and wall) that follow what you type in the tools. Fillet, Chamfer, Shell, Revolve and Pipe left the main toolbar; their shortcuts stay.

**Shift + click picks several** - in Select, Shift+click (or Ctrl+click) adds or removes lines, shapes and 3D bodies; Shift+drag adds a box's worth. Pan is middle-drag or Space+drag (Shift+drag still pans in the drawing tools).

**Press/Pull on several faces** - Shift+click faces of different boards (or several shapes; again = take off), then click a face and pull or type: they all move by the same distance, each along its own direction.

**Up to a face** - while pulling, move over a face parallel to the one you pull (it lights up green): the face goes exactly to that face's plane; with several faces each one lands on that plane (shelves of different heights end level).

**Extrude cuts only the selected boards** - select the boards first (Shift+click), then pull a shape through them: only those are cut / joined (when the new body meets one of them; otherwise every board it meets). N / J / C / I while pulling now apply to that pull only; the default is set in the panel or Model > Extrude a shape does.

**Combine dialog** (Model > Combine, Ctrl+Alt+C, or the panel): Operation Cut / Join / Intersect, a Targets list and a Tools list filled from the selection (Add selected, Remove, Clear, Swap), Keep the tool bodies. Click a row to see that body. Opened with bodies selected: the first is the target, the others the tools. One undo step.

**Box follows the mouse** - after the first corner the box grows with the mouse even with Ortho or Polar on (they used to pin the opposite corner to an axis, so one side stayed 0), and in the Top view the height follows the mouse moving up the screen.

**Tests** - 77 checks, new: Box with Ortho + Polar, Box height in the Top view, Shift+click add / remove, two faces pulled together, two shelves pulled up to a taller board's top, an extrude through two boards cutting only the selected one, Combine Cut and Intersect by volume, the panel.

## 3.5 - object snap remade for 3D, Mirror about a body centre, Move point to point and up to a face
**Object snap (F3), remade** - every object gives its points from one place, so drawing, Move, Mirror, Rotate, Press/Pull and the gizmo all snap to the same things:
- boards: every corner at both ends, every edge midpoint (also half-way along the thickness), the **centre of every flat face**, the **body centre**;
- turned parts and pipes: the centres of their ends and creases, the pipe ends, the body centre;
- 2D shapes as before: endpoints, midpoints, centres, intersections, perpendicular, nearest point on a line;
- **On face**: in Move, Mirror and the Rotate pivot, with no point near, the point on the face under the cursor (not the sketch plane behind it).
New markers: a square with a dot = face centre, a circle with a cross = body centre, a diamond = on face. Points on top of each other on screen: the nearer one wins. **View > Object snap settings (Ctrl+F3)** (or the 3D Tools panel) switches each kind on or off.

**Mirror about a point** - in the Mirror tool, point at a snap (e.g. the top's Body centre) and press **X**, **Y** or **Z**: the selection is mirrored across the plane through that point square to that axis (a cabinet's left side to the right through the top's centre). Clicking a point first, then X / Y / Z, does the same; two clicks still give a mirror line.

**Move point to point, and up to a face** - the Move tool's base and target snap to the 3D points above (a block's corner straight onto another body's face centre). With the base point picked on a face of the moving body, a target over a **parallel face** (no point near) moves the selection straight along that face's normal until the faces meet - "Up to face", the face lights green.

**Grid while turning the view** - the grid disappears while you orbit (right mouse button held) and comes back when you let go.

**Keep the tool bodies?** - Union (Join), Subtract (Cut) and Intersect from the menu, a shortcut, the toolbar or the 3D Tools panel ask whether the tool bodies stay: **Keep the tools** / **Remove the tools** / Cancel. The first selected body is the target, the others are the tools. "Don't ask again" uses your last answer; switch the question back on with *Ask to keep tool bodies* in the 3D Tools panel. The Combine dialog's checkbox starts from the same answer.

**Speed** - a body's snap points are worked out once and kept while it is unchanged: on the 217-board kitchen a mouse move with a point tool takes 1.3 ms (2.7 ms without the cache).

**Tests** - 82 checks; new: mirror through a body centre with X, Move corner to face centre, Move up to a face, Subtract answering Keep / Remove, the Box in the Top view (base 200 x 100 and a height), the orbit pictures. `--bench` now also times object snap per mouse move.

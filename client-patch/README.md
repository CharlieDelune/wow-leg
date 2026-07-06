# client-patch — WoW 3.3.5a client MPQ patch (multiclass)

Client-side half of the multiclass fork: the files that ship in a patch MPQ so the
stock 3.3.5a client can display and drive off-class content. Kept in the server repo
so a server+client change lands in one history.

## Layout

- `overlay/` — **byte-mirror of the MPQ patch.** Assemble the patch MPQ from the
  contents of this directory (it maps 1:1 onto the client's data root):
  - `DBFilesClient/*.dbc` — broadened client DBCs (ARAC race/class unlock +
    `SkillLineAbility.ClassMask` widening so trainers/spellbook show off-class content).
    Generated from the stock DBCs by `dbc-scripts/` — see below.
  - `Interface/FrameXML/` — FrameXML overrides: the `MulticlassFrame` class-swap panel
    (`.lua`/`.xml`), its `Bindings.xml` keybind, and the `MainMenuBarMicroButtons` /
    `FrameXML.toc` deltas that add the micro-button and load the panel.
- `dbc-scripts/` — one-off Python tools that produced the DBCs in `overlay/` by
  reading the stock DBCs and writing the broadened copies.

## The byte-mirror contract

`overlay/` must match what goes into the MPQ **exactly** — nothing more, nothing less:

- FrameXML files are **ASCII, CRLF, no BOM** (the client's stock convention). The
  sibling `.gitattributes` sets `overlay/** -text` so git preserves these bytes verbatim
  instead of applying the repo-root `* text eol=lf` normalization (which would also
  corrupt the binary DBCs).
- Add **no stray files** under `overlay/` — every file here ships in the MPQ. Docs,
  tooling, and git metadata stay outside it (README and `dbc-scripts/` at this level;
  `.gitattributes` at this level, not inside `overlay/`).

## External reference (not in the repo)

The full extracted stock client (~26 GB) is **not** version-controlled — regenerate it
by extracting the retail 3.3.5a MPQs. On this workstation it lives at
`~/azerothcore/client/vanilla/`; the `dbc-scripts/` read stock DBCs from there.

## Building / running

Assemble a patch MPQ from `overlay/` and drop it in the client's `Data/` (higher patch
letter than stock). FrameXML overrides require a **FrameXML-signature-patched `wow.exe`** —
a stock client rejects modified stock FrameXML as "corrupt interface files". The DBC
overrides do not need the patched exe.

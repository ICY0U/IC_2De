# Main Character V3 Runtime Art

This folder records the production contract for the V3 character motion set.
The approved review sources remain unchanged under
`art/review/main-character-v3-motion-study`.

`tools/import-player-v3.ps1` converts those sources into runtime assets with:

- real RGBA transparency;
- a shared 48 x 48 logical cell and bottom-centre root;
- a shared reduced character palette;
- nearest-cell resampling with hard pixel edges;
- the approved per-frame idle, seated, dodge, and shooting timings;
- metadata tags and explicit horizontal mirrors where authored coverage permits.

The generated PNG and JSON files live under `game/assets/runtime` and use the
`player-v3-` prefix. Re-run the importer after any approved source revision.

The V2 walk poses remain the locomotion bridge because this review set did not
include replacement walk cycles. They are re-imported into the same V3 cell,
root, palette, and alpha contract so changing between idle and movement does
not change the character's canvas or sampling rules. Original V2 source and
runtime files are not overwritten.

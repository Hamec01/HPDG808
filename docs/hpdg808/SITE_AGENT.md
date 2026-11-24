# HPDG 808 1.1.0 - website agent handoff

INTERNAL: publication instructions. Do not publish this file. Customer-facing materials: SITE_PAGE.md (page copy),
RELEASE.txt, GUIDE_EN.html and the screenshots.

## Update 1.0.2 -> 1.1.0 (the page is live at https://hamloprod.org/en/vst/hpdg808)

1.1.0 is a feature update: Sound Library (search, filters, favorites), 35 section presets, OSC B, Draw Wave,
LOAD REF + AUTO DRAW (reference resynthesis), the SYNTH preview with PLAY / scrub, 12 new Trap / Drill voicings
(212 presets). Projects and presets from 1.0 open and sound exactly as before.

1. Take `HPDG808_Setup_1.1.0.exe` from this package. Check: size 23379834 bytes, SHA-256
   `78d8d9de811f2a4c006f6865bd697aa9052ded96780b45a3fc53472869424097`.
2. Upload it to bucket `hamloprod-private` at `hpdg808/HPDG808_Setup_1.1.0.exe`. Leave the 1.0.2 file in place
   until the new download is verified; do not touch anything under `hpdg/`.
3. Point the HPDG 808 release record at `release.json` 1.1.0 (version, title, notes, objectKey, sha256, size).
4. On the page (copy in SITE_PAGE.md):
   - version labels 1.0.2 -> 1.1.0 (hero pill, the small line under the buttons, the release block label);
   - 200 -> 212 presets (hero bullet, stats row, release text, card 01);
   - the hero's second bullet now names OSC B, Draw Wave and AUTO DRAW;
   - add the new "WHAT'S NEW / 1.1.0" block with its six cards right after the release block;
   - replace all screenshots with the ones in `images/` (the interface changed: preset bar, section presets) and add
     the two new ones, `hpdg808_library.png` and `hpdg808_autodraw.png`, to the screenshots block.
5. Verify: download from the page, size and SHA-256 match step 1, the installer shows "HPDG 808 1.1.0".
6. HPDG 808 owners: the owner decides whether 1.1.0 goes to existing buyers at no cost and whether they get an email.
   Do not send anything before the owner approves the text.

## Contents of the package

- `HPDG808_Setup_1.1.0.exe` - installer, Windows 10/11 x64: the VST3 to `C:\Program Files\Common Files\VST3\HPDG 808.vst3`,
  the Standalone to `C:\Program Files\HamloProd\HPDG 808`, Start-menu entries, the VC++ Runtime when missing.
  Admin rights; no activation, no license check inside the plugin.
- `HPDG808_Setup_1.1.0.exe.sha256`, `release.json` - version, title, notes, page, objectKey, sha256, size.
- `SITE_PAGE.md` - the product page copy, section by section, in the HPDG page's structure.
- `site/index.html` - a visual mock of that page (open it locally; it loads `../images/`). It shows layout and
  style only; build the real page in the site's own code and components.
- `images/` - 1000 x 682 PNG screenshots of the plugin: `hpdg808_synth.png` (hero), `hpdg808_master.png`,
  `hpdg808_fx.png`, `hpdg808_sample.png`, `hpdg808_mix.png`, `hpdg808_library.png` (Sound Library),
  `hpdg808_autodraw.png` (AUTO DRAW / FULL).
- `RELEASE.txt` - release notes (also shown by the installer). `GUIDE_EN.html` - the user guide (also installed).
- `SHA256SUMS.txt` - checksums of every file in the package.

## Decisions for the owner (ask before publishing; do not invent them)

1. Price, and whether HPDG 808 is sold on its own, free, or given to HPDG owners. Fill `<price>` on the page.
2. Release date (`<release date>` on the page).
3. The page address: `release.json` proposes https://hamloprod.org/en/808; confirm or change it.
4. Whether there is a video. If not, leave out the WATCH THE VIDEO button.

## Publication sequence

1. Verify `SHA256SUMS.txt` and the installer checksum against `release.json`.
2. Upload the installer privately to Contabo, bucket `hamloprod-private`, at `objectKey` (`hpdg808/HPDG808_Setup_1.1.0.exe`).
   Keep it apart from the HPDG release files; do not replace or move anything under `hpdg/`.
3. Create the HPDG 808 product and release record from `release.json`. HPDG 808 has no in-plugin update check:
   do not attach it to HPDG's `/api/hpdg/latest` or HPDG's update record.
4. Downloads: the same signed-URL scheme as HPDG, with HPDG 808's own key or entitlement as the owner decides
   (point 1). Verify: the signed URL works, size and SHA-256 of the downloaded file match `release.json`, an
   invalid key is rejected.
5. Build the page from `SITE_PAGE.md` in the HPDG page's style (dark background, red accent, condensed uppercase
   headings, small mono labels, the stats row and the numbered cards). Use the screenshots as they are; do not crop
   the plugin's controls or retouch the numbers on them.
6. On the HPDG page, a short link block is enough: "HPDG 808 - the bass engine of HPDG as its own instrument ->".
   Do not change HPDG's version, notes or downloads.
7. Check the page at phone width and on desktop; check every link and the download with a test key.
8. Announcements or emails only after the owner approves the text; send a test to the owner first.

## Facts for any text (do not go beyond them)

- 212 presets: Boom Bap 48, Trap 48 (incl. 8 Luxury), Rap 21, Drill 12, DnB 24, Techno 28, Atmospheric 15, Utility 4,
  Trap / Drill voicings 12 (Clean, Punch, Grit, Slide - three each). 35 section presets.
- New in 1.1.0: Sound Library (search by name, genre, category, character; favorites; My presets), section presets
  per tab (SAVE AS, RESET), OSC B on MIX (harmonics 2-8: Level, Shape, Decay, Sustain), Draw Wave (512-point cycle,
  zoom up to 32x), LOAD REF (up to 10 s, visual only) + AUTO DRAW (FULL: the whole reference as editable wave
  frames; CYCLE: one cycle), SYNTH preview with PLAY, scrub, BPM and Snap.
- Projects and presets from 1.0 open and sound exactly as before.
- Tabs: SYNTH, SAMPLE, MIX, FILTER / FX, MASTER. Play modes Retrigger, Legato, Glide.
- Waves: Sine, Triangle, Saw, Square, Reese. Punch up to 48 semitones. Filter LP12, LP24, HP12, BP, Vowel.
  Saturator models Classic, Tape, Tube, Iron, Germanium, Vinyl.
- Export WAV / Drag: the last played note with the master, 48 kHz stereo WAV.
- Sample formats: WAV, AIFF, FLAC, MP3, OGG.
- Windows 10/11 64-bit only; VST3 and Standalone; no AU, no macOS, no AAX.
- The same engine, presets and MASTER are in HPDG's Sub808 lane; from HPDG 0.1.0 Beta 9 the lane's editor has
  everything new in 1.1.0.

Do not add: source-code links, repository or branch names, internal test names or numbers, the names of sample
packs or artists the presets were matched against, or anything about the measurement method beyond "fitted to over
a thousand professional bass one-shots".

# BYOBYOK

**Bring Your Own Bring Your Own Keyboard** — a distraction-free
writing device built from an M5Stack **M5PaperS3** e-ink tablet and whatever
keyboard you already love. Plain `.txt` files on a microSD card, weeks of
battery, and nothing to check but your word count.

Supports composing accented characters, a typewriter/Hemingway mode, daily journaling, writing goals, spell checking, multiple keyboard layouts (Qwerty, Dvorak, Coleman), and more.

<img width="1280" height="960" alt="demo" src="https://github.com/user-attachments/assets/f4a28dc7-9ad6-46cb-ba9b-17a894e4bd88" />

---

## Quick start

**You need:** an M5PaperS3, a FAT32 microSD card, and a keyboard — USB (wired
or 2.4 GHz dongle, via a USB-C OTG adapter) or Bluetooth LE.

**Flash it:**

1. Arduino IDE → install the M5Stack board package (Board Manager URL
   `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`),
   select board **M5PaperS3**, confirm **PSRAM: OPI PSRAM**.
2. Set **Tools → USB Mode = USB-OTG (TinyUSB)** and
   **USB CDC On Boot = Disabled** (needed for USB drive mode).
3. Install libraries: **M5Unified** (≥ 0.2.5), **M5GFX** (≥ 0.2.7),
   **EspUsbHost** (Masayuki Tanaka), **NimBLE-Arduino** (≥ 2.0).
4. Open `BYOBYOK/BYOBYOK.ino` (keep `fonts_latin1.h` beside it), upload.
5. After flashing: unplug, connect the keyboard, insert the card. To re-flash
   later, hold BOOT while tapping RESET (USB host mode replaces the serial
   port — that's expected).

**First session:** the device boots to a file browser. Pick `[ New file ]`,
type, hit ESC → Save. Copy a dictionary to the card as `/wordlist.txt` (any
standard word list (tested with https://github.com/dwyl/english-words) to enable spell check. Optionally drop books on the card
as `.txt` — `tools/epub2txt.py` converts EPUBs.

## Usage

Three rules apply everywhere: **ESC backs out**, **every dialog shows its own
keys**, and **anything that looks like a button can be tapped**. `?` in the
menu shows the controls on-device.

| Screen | Keyboard | Touch |
|---|---|---|
| File menu | `↑`/`↓` + `Enter` · `↑` past the top reaches the button rows · `r` rename · `m` move · `Del` delete · `e` export · `b` Bluetooth · `j` journal · `s` stats · `c` cloud sync · `u` USB drive · `z` sleep · `?` help | tap files/folders · two button rows (a pressed button inverts while it works) |
| Editor | arrows, `Home`/`End`, `PgUp`/`PgDn` · `Shift+arrows` word jump · `Shift+Bksp`/`Del` delete word · `Ctrl+S` save · `Ctrl+W` count · `Ctrl+Z` undo · `Ctrl+F` find · `Ctrl+K` spell · `Ctrl+G` goal · `Ctrl+A`/`E` paragraph · `Ctrl+P` go to page · `Ctrl+Home`/`End` document · `Ctrl+R` redraw · `ESC` menu | tap the page to place the cursor |
| Reader | `←`/`→`/`Space` page · `b` bookmarks/go-to-page · `r` rotate portrait · `e` edit · `ESC` exit | sides = page, top = exit, bottom = reading menu |

Shortcuts follow your **layout** (QWERTY/Dvorak/Colemak cycle in the menu),
and **Cmd counts as Ctrl** for Mac-mode keyboards. `Ctrl+R` repaints any
screen if e-ink ghosting builds up.

**Writing** — the status bar shows the file, clock, page (`p.3/87`) and
battery. Auto-save runs every 20 s and on Enter (5 s throttle); named files
never lose more than seconds of work. `Ctrl+Z` undoes in chunks. Focus mode
(ESC menu) disables cursor movement for forward-only drafting. `Ctrl+G` sets
timed sprints or word goals; `s` in the menu shows daily words, streaks and
lifetime totals. `j` opens today's journal (`/journal/YYYY-MM-DD.txt`),
appending.

**Accents & Esperanto** — hold Option/Alt for a dead key, then the letter:
`Option+u, a` → ä (dead keys: `u` diaeresis, `e` acute, `` ` `` grave, `i`
circumflex, `n` tilde, `b` breve; direct: `Option+a`→å, `o`→ø, `s`→ß, `c`→ç,
`'`→æ). Esperanto: `Option+i` + c/g/h/j/s → ĉĝĥĵŝ, `Option+b, u` → ŭ. The
pending accent shows inverted at the caret. Files save as normal UTF-8.
It even supports Esperanto characters for weirdos like me (saluton, amikoj!)

**Markdown** — the Reader renders `**bold**`, `*italic*` and `# headings`
styled (markers stay visible); the editor keeps plain text.

**Spell check** — `Ctrl+K` lists unknown words with counts. `Enter` jumps to
a word, **`l` learns it** (appended to `/wordlist.txt`, accepted instantly).
Any plain word list works: mixed case, accents, possessives and unsorted
files are normalized at load. Lookup is a binary search, so even a several-MB
multilingual list stays fast; ~4 MB is a comfortable ceiling.

**Reading** — your place is saved per file on every page turn; opening a file
(or `[ Continue ]`) resumes it. The bottom-edge menu pins an explicit
bookmark and jumps to pages. `r` rotates portrait. UTF-8 typography folds to
ASCII on load (curly quotes → `'`, em-dash → `--`); accented and Esperanto
letters render with real glyphs.

**Files in and out**

- **USB drive mode** (`u`): restarts with the SD card as a USB stick. Large
  cards take up to a minute to appear (the computer checks the disk); eject
  on the computer to restart the device and keep future mounts fast.
- **WiFi export** (`e`): the device makes its own hotspot (`BYOBYOK` /
  `byobyok1`) and serves a page to download/upload `.txt` files. Opening the
  page also sets the device clock from your browser. Same-name uploads
  replace (intentionally). ESC or tap stops it and kills the radio.
- **Cloud sync** (`c`): joins your WiFi and HTTP-PUTs the current folder's
  `.txt` files to a WebDAV-style URL, configured by `/sync.cfg` (`ssid=`,
  `pass=`, `url=`, optional `user=`/`pw=`). HTTPS is accepted without
  certificate checks — treat it as private-network grade.
- Or just move the microSD card.

New names that already exist are refused (rename, save-as, new folder) —
nothing is ever silently overwritten. The browser hides OS litter (dot-files,
`System Volume Information`) and the spell dictionary.

**Sleep & power** — the side button is a hardware **reset** on this board, so
sleep via the **Sleep** button, `z` in the menu, ESC → Sleep, or the 5-minute
idle timer. Sleeping saves everything and powers off (~9.6 µA); press the
side button to wake. `[ Continue ]` makes even an accidental reset painless.
The radio is off except during export/sync or while the Bluetooth keyboard
feature is on.

**Bluetooth keyboards** — BLE only (not Bluetooth Classic). Pair from the
`BT kbd` button: 30 s scan, type the shown 6-digit code on the keyboard if
asked. It reconnects automatically when woken (the first wake keypress is
swallowed — normal BLE). The pairing screen also offers **Turn Bluetooth off
(keep pairing)**, **Forget**, and a **Key test** screen that shows exactly
what any keyboard sends. A sleeping BLE keyboard never kicks you out of the
editor; only a USB unplug does (after auto-saving).

---

## Technical notes

**Architecture.** A single sketch (~3,500 lines) around a state machine.
Text lives in a PSRAM **gap buffer** (O(1) edits at the caret); word-wrap and
word count update **incrementally** per keystroke; a ~1.2 MB book costs ~13 µs
of text-engine work per keypress. Undo is a journal of inverse edits with
typing runs coalesced. Latin-1 and twelve Esperanto letters are stored as
single bytes (grid math stays byte-per-cell) and re-encoded to UTF-8 at the
display and file boundaries.

**E-ink strategy.** Quality-with-flash is reserved for content appearing
(screen entry, dialogs, periodic de-ghost). Everything else is flash-free
fast mode: an appended character repaints only its own cells; menu moves
repaint two rows; reader pages turn fast with a clean repaint every
`READER_QUALITY_EVERY` turns. De-ghosting happens after `DEGHOST_EVERY`
partials or during a 10 s typing pause — or manually with `Ctrl+R`.

**Testing.** The text engine, BLE decoder, spell core, markdown state and
composition tables are extracted verbatim from marker blocks in the sketch
and fuzz-tested on a host (`tests/`): 60k+ random edits verified against
reference implementations, undo unwound state-by-state, plus targeted suites.
Regenerate the `.inc` files with the `sed` one-liners in the test headers.

**Files**

- `BYOBYOK/BYOBYOK.ino` — the whole program.
- `BYOBYOK/fonts_latin1.h` — generated DejaVu Mono fonts (3 sizes × 4 styles,
  glyphs 0x20–0xFF + Esperanto); regenerate with `tools/make_fonts.py`.
- `tools/epub2txt.py` — EPUB → clean `.txt` converter (Python 3, stdlib).
- `tests/` — host-side test suites.
- On the card: `/wordlist.txt` (spell dictionary), `/sync.cfg` (cloud sync),
  `/.ptmarks.tsv` (reading places), `/.ptstats.csv` (writing stats),
  `name.txt.bak` (rolling backup taken when a file is opened for editing).

**Tuning** (constants near the top of the sketch): `AUTOSAVE_MS`,
`NL_SAVE_MS`, `IDLE_SLEEP_MS`, `CPU_MHZ`, `DEGHOST_*`, `READER_QUALITY_EVERY`,
`MENU_QUALITY_EVERY`, `UNDO_MAX`, `SPELL_MAX`, `DICT_PATH`, `AP_SSID`/`AP_PASS`,
goal presets, and the layout/compose tables. Feature gates:
`ENABLE_BLE_KEYBOARD`, `ENABLE_USB_DRIVE`, `USE_LATIN1_FONTS`.

**Battery.** 1800 mAh. Typing draws ~70–200 mA (a USB keyboard is powered by
the device — backlit boards can double this; BLE keyboards power themselves
and cost only ~1–3 mA of radio). Standby is months. The battery % is
voltage-derived (no fuel gauge): it reads high while charging (shown `~74%+`)
and sags under load.

**Troubleshooting**

- **USB keyboard not detected** — the OTG port can't power hungry keyboards;
  try a plain wired board, a powered hub, or the keyboard's 2.4 GHz dongle.
  The Key test screen (BT kbd → Key test) shows whether events arrive.
- **BLE won't pair** — must be BLE, not Classic; clear old pairings on the
  keyboard; the pairing window is 30 s.
- **USB drive doesn't appear** — check both Tools settings from Quick start;
  use a data-capable cable.
- **microSD not found** — reformat FAT32, reseat, reset.
- **File too large to open** — a friendly message, not a crash; the practical
  ceiling is a few MB of text.
- **Heavy ghosting** — `Ctrl+R`, or lower the `DEGHOST_*` constants.

**Limitations**
- Battery percentages are estimated based on voltage. There is no actual battery guage in the M5PaperS3. 

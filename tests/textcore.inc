// ==== TEXTCORE-1 BEGIN ==== (extracted verbatim into tests/ - keep in sync)
// Gap buffer: the document is one buffer with a movable "hole" (the gap) at
// the edit point. Typing/deleting at the cursor is O(1) instead of "copy the
// whole document three times", which is what lets novel-sized files stay
// responsive. The buffer is allocated from PSRAM (8 MB on the PaperS3).
#ifndef GB_MALLOC
#define GB_MALLOC(n) (psramFound() ? ps_malloc(n) : malloc(n))
#endif

class GapBuffer {
  char *buf = nullptr;
  int cap = 0, gs = 0, ge = 0;                 // text = [0,gs) + [ge,cap)

  bool grow(int minGap) {                      // re-alloc, keeping the gap at gs
    long need = (long)length() + minGap;
    long ncap = need + (need >> 1) + 4096;     // 1.5x + slack
    char *nb = (char *)GB_MALLOC((size_t)ncap);
    if (!nb) return false;
    int tail = cap - ge;
    if (buf) {
      memcpy(nb, buf, gs);
      memcpy(nb + ncap - tail, buf + ge, tail);
      free(buf);
    }
    buf = nb; ge = (int)ncap - tail; cap = (int)ncap;
    return true;
  }
  void moveGap(int pos) {
    if (pos < gs)      { memmove(buf + ge - (gs - pos), buf + pos, gs - pos); ge -= gs - pos; gs = pos; }
    else if (pos > gs) { memmove(buf + gs, buf + ge, pos - gs); ge += pos - gs; gs = pos; }
  }

public:
  int  length() const { return cap - (ge - gs); }
  char operator[](int i) const { return buf[i < gs ? i : i + (ge - gs)]; }

  bool reserve(int total) {                    // ensure room for `total` chars
    if (total <= cap) return true;
    return grow(total - length());
  }
  bool insert(int pos, const char *s, int n) {
    if (n <= 0) return true;
    if (pos < 0) pos = 0;
    if (pos > length()) pos = length();
    if (ge - gs < n && !grow(n)) return false;
    moveGap(pos);
    memcpy(buf + gs, s, n);
    gs += n;
    return true;
  }
  void erase(int pos, int n) {
    if (pos < 0) { n += pos; pos = 0; }
    if (n > length() - pos) n = length() - pos;
    if (n <= 0) return;
    if (pos + n == gs) { gs = pos; return; }   // backspace fast path
    moveGap(pos);
    ge += n;
  }
  void clear() { free(buf); buf = nullptr; cap = gs = ge = 0; }
  // The two contiguous halves, for streaming the doc out to a file.
  const char *part1() const { return buf; }              int part1Len() const { return gs; }
  const char *part2() const { return buf ? buf + ge : nullptr; } int part2Len() const { return cap - ge; }
  String substring(int a, int b) const {
    int len = length();
    if (a < 0) a = 0;
    if (b > len) b = len;
    String s;
    if (b <= a) return s;
    s.reserve(b - a);
    for (int i = a; i < b; i++) s += (*this)[i];
    return s;
  }
};

GapBuffer doc;                 // the document being edited
int g_docWords = 0;            // live word count (maintained by docReplace)
// ==== TEXTCORE-1 END ====
// ==== TEXTCORE-2 BEGIN ==== (extracted verbatim into tests/ - keep in sync)
void layout() {
  g_lines.clear();
  int n = doc.length();
  // Pre-size the line index in ONE allocation: hard newlines plus a soft-wrap
  // estimate. Without this, growth-by-doubling on a many-line file (e.g. a
  // word list) transiently needs old+new copies at once - an OOM spike.
  {
    int est = 16 + n / (g_cols > 0 ? g_cols : 8);
    for (int k = 0; k < n; k++) if (doc[k] == '\n') est++;
    if ((int)g_lines.capacity() < est) g_lines.reserve(est);
  }
  int lineStart = 0, lastSpace = -1, col = 0, i = 0;
  bool wrappedStart = false;                 // did this line begin from a wrap?
  while (i < n) {
    char c = doc[i];
    if (c == '\n') {
      // A newline right after a wrap shouldn't add a spurious blank line.
      if (!(i == lineStart && wrappedStart)) g_lines.push_back({lineStart, i - lineStart});
      i++; lineStart = i; col = 0; lastSpace = -1; wrappedStart = false;
      continue;
    }
    if (col >= g_cols) {
      if (c == ' ') {                       // absorb a trailing space, then break
        g_lines.push_back({lineStart, i + 1 - lineStart});
        i++; lineStart = i; col = 0; lastSpace = -1; wrappedStart = true;
        continue;
      }
      if (lastSpace >= lineStart) {         // soft break at last space
        int brk = lastSpace + 1;
        g_lines.push_back({lineStart, brk - lineStart});
        lineStart = brk; lastSpace = -1;
        col = i - lineStart;                // keep current i; recount column
        wrappedStart = true;
        continue;
      }
      g_lines.push_back({lineStart, i - lineStart});  // hard break (long word)
      lineStart = i; col = 0; lastSpace = -1; wrappedStart = true;
      continue;
    }
    if (c == ' ') lastSpace = i;
    i++; col++;
  }
  g_lines.push_back({lineStart, n - lineStart});      // final line (may be empty)
}

String lineStr(int idx) {
  const Line &L = g_lines[idx];
  return doc.substring(L.start, L.start + L.len);
}

void caretRowCol(int pos, int &row, int &col) {
  int n = doc.length();
  for (int i = 0; i < (int)g_lines.size(); i++) {
    int s = g_lines[i].start, e = s + g_lines[i].len;
    if (pos < e) { row = i; col = pos - s; return; }
    if (pos == e) {
      bool lastLine   = (i == (int)g_lines.size() - 1);
      bool newlineHere = (e < n) && (doc[e] == '\n');
      if (lastLine || newlineHere) { row = i; col = pos - s; return; }
      // soft-wrap boundary: caret belongs to the start of the next line
    }
  }
  row = (int)g_lines.size() - 1;
  col = g_lines[row].len;
}

int posFromRowCol(int row, int col) {
  if (g_lines.empty()) return 0;
  if (row < 0) row = 0;
  if (row >= (int)g_lines.size()) row = (int)g_lines.size() - 1;
  int c = col;
  if (c < 0) c = 0;
  if (c > g_lines[row].len) c = g_lines[row].len;
  return g_lines[row].start + c;
}

// ---- word count, kept incrementally -------------------------------------
static inline bool wordSpace(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }

// Count "word starts" at indices [a,b): a non-space whose predecessor is a
// space (or the start of the document). Summed over the whole doc this equals
// the word count; after an edit only a small window can change.
int countWordStarts(int a, int b) {
  int n = doc.length();
  if (a < 0) a = 0;
  if (b > n) b = n;
  int w = 0;
  for (int i = a; i < b; i++)
    if (!wordSpace(doc[i]) && (i == 0 || wordSpace(doc[i - 1]))) w++;
  return w;
}
int recountWords() { return countWordStarts(0, doc.length()); }

// ---- word-wise caret movement ----------------------------------------------
// One "word" hop: skip separators, then a whole run of non-separators (going
// left), or the mirror image going right - the classic editor behavior.
int wordLeftPos(int pos) {
  if (pos > (int)doc.length()) pos = doc.length();
  while (pos > 0 && wordSpace(doc[pos - 1])) pos--;
  while (pos > 0 && !wordSpace(doc[pos - 1])) pos--;
  return pos;
}
int wordRightPos(int pos) {
  int n = doc.length();
  if (pos < 0) pos = 0;
  while (pos < n && !wordSpace(doc[pos])) pos++;
  while (pos < n && wordSpace(doc[pos])) pos++;
  return pos;
}

// ---- UTF-8 -> ASCII folding -------------------------------------------------
// The built-in FreeMono fonts are 7-bit, so typographic characters in UTF-8
// files (curly quotes, em-dashes, accents...) would render as blank boxes.
// Files are folded to their ASCII equivalents as they load.

// Incremental UTF-8 decoder: feed one byte at a time (sequences may straddle
// read chunks); returns the completed code point, or -1 while mid-sequence
// (invalid bytes are silently dropped).
struct Utf8Fold {
  uint32_t cp = 0;
  int need = 0;
  long feed(uint8_t b) {
    if (b < 0x80) { need = 0; return (long)b; }
    if ((b & 0xC0) == 0x80) {                  // continuation byte
      if (need > 0) {
        cp = (cp << 6) | (b & 0x3F);
        if (--need == 0) return (long)cp;
      }
      return -1;
    }
    if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; need = 1; return -1; }
    if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; need = 2; return -1; }
    if ((b & 0xF8) == 0xF0) { cp = b & 0x07; need = 3; return -1; }
    need = 0;
    return -1;
  }
};

// When Latin-1 capable fonts are compiled in (USE_LATIN1_FONTS), code points
// A0..FF pass through as single Latin-1 bytes instead of being transliterated
// - the grid math stays byte-per-cell, and the fonts have the glyphs.
bool g_keepLatin1 = false;

// ASCII replacement for a non-ASCII code point ("" = drop, "?" = unknown).
const char *asciiFold(uint32_t cp) {
  if (g_keepLatin1 && cp >= 0xA0 && cp <= 0xFF) {
    static char raw[2] = {0, 0};
    raw[0] = (char)cp;
    return raw;
  }
  // Esperanto circumflex/breve letters: internal slots 0x80.. when the fonts
  // carry the glyphs, standard x-notation otherwise.
  static const uint16_t EO_CP[12] = {0x109,0x108,0x11D,0x11C,0x125,0x124,
                                     0x135,0x134,0x15D,0x15C,0x16D,0x16C};
  static const char *EO_X[12] = {"cx","Cx","gx","Gx","hx","Hx",
                                 "jx","Jx","sx","Sx","ux","Ux"};
  for (int k = 0; k < 12; k++) {
    if (cp != EO_CP[k]) continue;
    if (!g_keepLatin1) return EO_X[k];
    static char slot[2] = {0, 0};
    slot[0] = (char)(0x80 + k);
    return slot;
  }
  switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B:
    case 0x2039: case 0x203A: case 0x02BC: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x201F:
    case 0x00AB: case 0x00BB: return "\"";
    case 0x2010: case 0x2011: case 0x2013: case 0x2212: return "-";
    case 0x2014: case 0x2015: return "--";
    case 0x2026: return "...";
    case 0x00A0: case 0x2002: case 0x2003: case 0x2007:
    case 0x2009: case 0x200A: case 0x202F: return " ";
    case 0x00AD: case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: return "";
    case 0x2022: case 0x00B7: case 0x25CF: return "*";
    case 0x00A9: return "(c)"; case 0x00AE: return "(R)"; case 0x2122: return "(TM)";
    case 0x00BC: return "1/4"; case 0x00BD: return "1/2"; case 0x00BE: return "3/4";
    case 0x00D7: return "x";   case 0x00F7: return "/";
    case 0x00DF: return "ss";  case 0x00DE: case 0x00FE: return "th";
    case 0x00C6: return "AE";  case 0x00E6: return "ae";
    case 0x0152: return "OE";  case 0x0153: return "oe";
  }
  if (cp >= 0x00C0 && cp <= 0x00FF) {          // Latin-1 letters: drop accents
    static const char *tab =
      "AAAAAA CEEEEIIII"                       // 0xC0..0xCF
      "DNOOOOO OUUUUY  "                       // 0xD0..0xDF
      "aaaaaa ceeeeiiii"                       // 0xE0..0xEF
      "dnooooo ouuuuy y";                      // 0xF0..0xFF
    static char one[2] = {0, 0};
    one[0] = tab[cp - 0x00C0];
    return (one[0] == ' ') ? "?" : one;
  }
  return "?";                                  // visible, unlike a tofu box
}

// ---- incremental re-wrap --------------------------------------------------
// Index of the line whose .start == s (starts are strictly increasing).
static int findLineByStart(int s) {
  int lo = 0, hi = (int)g_lines.size() - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if (g_lines[mid].start == s) return mid;
    if (g_lines[mid].start < s) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

// Re-wrap only what an edit could have changed. The edit replaced delLen
// chars at pos with insLen chars (doc is already updated). The wrap state is
// fully reset by every '\n', so we recompute lines from the start of the
// paragraph containing pos up to the first '\n' past the end of the edit;
// every line before that is untouched and every line after it is the old
// line, shifted by the length delta. Falls back to a full layout() if the
// line list looks inconsistent (defensive - should not happen).
void layoutEdit(int pos, int delLen, int insLen) {
  if (g_lines.empty()) { layout(); return; }
  int n = doc.length(), delta = insLen - delLen, editEnd = pos + insLen;

  int p0 = pos;                                  // paragraph start = a line start
  while (p0 > 0 && doc[p0 - 1] != '\n') p0--;
  int L0 = findLineByStart(p0);
  if (L0 < 0) { layout(); return; }

  // Same wrapping loop as layout(), started fresh at p0.
  std::vector<Line> fresh;
  int lineStart = p0, lastSpace = -1, col = 0, i = p0, tailNew = -1;
  bool wrappedStart = false;
  while (i < n) {
    char c = doc[i];
    if (c == '\n') {
      if (!(i == lineStart && wrappedStart)) fresh.push_back({lineStart, i - lineStart});
      i++; lineStart = i; col = 0; lastSpace = -1; wrappedStart = false;
      if (i > editEnd) { tailNew = i; break; }   // '\n' past the edit: old lines resume
      continue;
    }
    if (col >= g_cols) {
      if (c == ' ') {                            // absorb a trailing space, then break
        fresh.push_back({lineStart, i + 1 - lineStart});
        i++; lineStart = i; col = 0; lastSpace = -1; wrappedStart = true;
        continue;
      }
      if (lastSpace >= lineStart) {              // soft break at last space
        int brk = lastSpace + 1;
        fresh.push_back({lineStart, brk - lineStart});
        lineStart = brk; lastSpace = -1;
        col = i - lineStart;
        wrappedStart = true;
        continue;
      }
      fresh.push_back({lineStart, i - lineStart});  // hard break (long word)
      lineStart = i; col = 0; lastSpace = -1; wrappedStart = true;
      continue;
    }
    if (c == ' ') lastSpace = i;
    i++; col++;
  }

  if (tailNew < 0) {                             // re-wrapped to the very end
    fresh.push_back({lineStart, n - lineStart});
    g_lines.resize(L0);
    g_lines.insert(g_lines.end(), fresh.begin(), fresh.end());
    return;
  }
  int T = findLineByStart(tailNew - delta);      // old line where the tail resumes
  if (T < 0) { layout(); return; }
  std::vector<Line> tail(g_lines.begin() + T, g_lines.end());
  for (auto &L : tail) L.start += delta;
  g_lines.resize(L0);
  g_lines.insert(g_lines.end(), fresh.begin(), fresh.end());
  g_lines.insert(g_lines.end(), tail.begin(), tail.end());
}

// ---- macOS-style character composition ---------------------------------------
// Option + dead key, then a base letter: Option+u,a -> ä. Dead keys follow the
// macOS convention (u diaeresis, e acute, ` grave, i circumflex, n tilde) plus
// b (breve) for Esperanto's ŭ; circumflex composes c/g/h/j/s to ĉĝĥĵŝ (the
// internal 0x80.. slots). Returns the internal byte, 0 if the pair doesn't
// compose. Direct Option characters (å ø ß æ ç) need no second key.
struct ComposeEntry { char dk, base; uint8_t out; };
static const ComposeEntry COMPOSE[] = {
  {'u','a',0xE4},{'u','o',0xF6},{'u','u',0xFC},{'u','e',0xEB},{'u','i',0xEF},{'u','y',0xFF},
  {'u','A',0xC4},{'u','O',0xD6},{'u','U',0xDC},{'u','E',0xCB},{'u','I',0xCF},
  {'e','a',0xE1},{'e','e',0xE9},{'e','i',0xED},{'e','o',0xF3},{'e','u',0xFA},{'e','y',0xFD},
  {'e','A',0xC1},{'e','E',0xC9},{'e','I',0xCD},{'e','O',0xD3},{'e','U',0xDA},{'e','Y',0xDD},
  {'`','a',0xE0},{'`','e',0xE8},{'`','i',0xEC},{'`','o',0xF2},{'`','u',0xF9},
  {'`','A',0xC0},{'`','E',0xC8},{'`','I',0xCC},{'`','O',0xD2},{'`','U',0xD9},
  {'i','a',0xE2},{'i','e',0xEA},{'i','i',0xEE},{'i','o',0xF4},{'i','u',0xFB},
  {'i','A',0xC2},{'i','E',0xCA},{'i','I',0xCE},{'i','O',0xD4},{'i','U',0xDB},
  {'i','c',0x80},{'i','C',0x81},{'i','g',0x82},{'i','G',0x83},{'i','h',0x84},
  {'i','H',0x85},{'i','j',0x86},{'i','J',0x87},{'i','s',0x88},{'i','S',0x89},
  {'n','n',0xF1},{'n','N',0xD1},{'n','a',0xE3},{'n','A',0xC3},{'n','o',0xF5},{'n','O',0xD5},
  {'b','u',0x8A},{'b','U',0x8B},
};
uint8_t composeChar(char dk, char base) {
  for (auto &e : COMPOSE) if (e.dk == dk && e.base == base) return e.out;
  return 0;
}
uint8_t optionChar(char key) {
  switch (key) {
    case 'a': return 0xE5; case 'A': return 0xC5;   // å Å
    case 'o': return 0xF8; case 'O': return 0xD8;   // ø Ø
    case 'c': return 0xE7; case 'C': return 0xC7;   // ç Ç
    case 's': return 0xDF;                          // ß
    case '\'': return 0xE6; case '"': return 0xC6;  // æ Æ
  }
  return 0;
}
bool composeDead(char k) {
  return k == 'u' || k == 'e' || k == 'i' || k == 'n' || k == 'b' || k == '`';
}

// ---- markdown emphasis (rendered by the Reader) -----------------------------
// **text** toggles bold, *text* toggles italic (a "* " bullet at the very
// start of a paragraph stays literal), and a paragraph starting with '#' is a
// heading (whole line bold). Emphasis resets at every hard newline. Markers
// stay visible - only the text between them changes style - so the character
// grid and wrap math are untouched.
// Computes the style state at `pos` by scanning from its paragraph start;
// parStart is returned for the renderer's bullet check. (A ** split across a
// soft wrap may momentarily mis-style one line - cosmetic, self-corrects.)
void mdStateAt(int pos, int &parStart, bool &bold, bool &ital, bool &heading) {
  int p0 = pos;
  while (p0 > 0 && doc[p0 - 1] != '\n') p0--;
  parStart = p0;
  bold = ital = false;
  heading = (p0 < (int)doc.length() && doc[p0] == '#');
  int i = p0;
  while (i < pos) {
    if (doc[i] != '*') { i++; continue; }
    if (i + 1 < pos && doc[i + 1] == '*') { bold = !bold; i += 2; continue; }
    bool bullet = (i == p0 && i + 1 < (int)doc.length() &&
                   (doc[i + 1] == ' ' || doc[i + 1] == '\t'));
    if (!bullet) ital = !ital;
    i++;
  }
}

// ---- search ---------------------------------------------------------------
// Case-insensitive find, starting at `from`, wrapping around the document.
// Returns the match index or -1.
int searchDoc(const String &needle, int from) {
  int n = doc.length(), m = (int)needle.length();
  if (m == 0 || m > n) return -1;
  if (from < 0 || from >= n) from = 0;
  auto low = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; };
  for (int k = 0; k < n; k++) {
    int i = (from + k) % n;
    if (i + m > n) continue;
    bool hit = true;
    for (int j = 0; j < m; j++)
      if (low(doc[i + j]) != low(needle[j])) { hit = false; break; }
    if (hit) return i;
  }
  return -1;
}

// ---- undo journal -----------------------------------------------------------
// Every edit goes through docReplace, so undo is a journal of inverses:
// "insLen chars were inserted at pos, replacing del". Runs of single-char
// typing / backspacing coalesce into one entry so Ctrl+Z steps feel chunky.
struct UndoOp { int pos; int insLen; String del; };
std::vector<UndoOp> g_undo;
bool g_undoSuppress = false;         // true while undoLast applies an inverse
const int UNDO_MAX = 200;            // entries kept (typing coalesces, so ~generous)

void undoClear() { g_undo.clear(); }

void recordUndo(int pos, const String &del, int insLen) {
  if (g_undoSuppress) return;
  int delLen = (int)del.length();
  if (!g_undo.empty()) {
    UndoOp &last = g_undo.back();
    // coalesce a run of typing: single insert right after the previous one
    if (delLen == 0 && insLen == 1 && (int)last.del.length() == 0 &&
        last.insLen > 0 && last.insLen < 32 && pos == last.pos + last.insLen) {
      last.insLen++;
      return;
    }
    // coalesce a run of backspacing: single deletes walking left
    if (insLen == 0 && delLen == 1 && last.insLen == 0 &&
        (int)last.del.length() > 0 && (int)last.del.length() < 32 &&
        pos == last.pos - 1) {
      String t; t += del[0]; t += last.del;
      last.del = t;
      last.pos = pos;
      return;
    }
  }
  UndoOp op; op.pos = pos; op.insLen = insLen; op.del = del;
  g_undo.push_back(op);
  if ((int)g_undo.size() > UNDO_MAX) g_undo.erase(g_undo.begin());
}

// Single entry point for all edits: replaces delLen chars at pos with the
// insLen bytes at s, updates the word count by re-examining only the window
// whose word-boundary status could have changed, re-wraps incrementally, and
// records the inverse in the undo journal.
// Returns how many chars were actually inserted (0 if out of memory).
int docReplace(int pos, int delLen, const char *s, int insLen) {
  int n = doc.length();
  if (pos < 0) pos = 0;
  if (pos > n) pos = n;
  if (delLen < 0) delLen = 0;
  if (delLen > n - pos) delLen = n - pos;
  String deleted;
  if (delLen > 0 && !g_undoSuppress) deleted = doc.substring(pos, pos + delLen);
  g_docWords -= countWordStarts(pos, pos + delLen + 1);
  doc.erase(pos, delLen);
  if (insLen > 0 && !doc.insert(pos, s, insLen)) insLen = 0;
  g_docWords += countWordStarts(pos, pos + insLen + 1);
  layoutEdit(pos, delLen, insLen);
  recordUndo(pos, deleted, insLen);
  return insLen;
}

// Revert the newest undo entry. Returns the caret position to move to after
// the revert, or -1 if there was nothing to undo.
int undoLast() {
  if (g_undo.empty()) return -1;
  UndoOp op = g_undo.back();
  g_undo.pop_back();
  g_undoSuppress = true;
  docReplace(op.pos, op.insLen, op.del.c_str(), (int)op.del.length());
  g_undoSuppress = false;
  return op.pos + (int)op.del.length();
}
// ==== TEXTCORE-2 END ====
// ==== SPELLCORE BEGIN ==== (extracted verbatim into tests/ - keep in sync)
// Spell check against a plain word list, one word per line - any standard
// dictionary file works as-is (mixed case, possessives, accents, any sort
// order: the list is normalized at load). Held in memory, binary-searched
// over line offsets.
char *g_dict = nullptr;
int   g_dictLen = 0;
std::vector<int> g_dictOff;              // offset of each line in g_dict

void dictIndex() {
  g_dictOff.clear();
  bool atStart = true;
  for (int i = 0; i < g_dictLen; i++) {
    char c = g_dict[i];
    if (atStart && c != '\n' && c != '\r') { g_dictOff.push_back(i); atStart = false; }
    if (c == '\n') atStart = true;
  }
}

// Normalize the indexed list so binary search works no matter how the file
// was formatted: lowercase everything in place, then sort the line offsets
// into byte order. Standard lists (macOS/Linux words, SCOWL) mix cases and
// collations, and lowercasing changes the byte order anyway - documents are
// compared as lowercase ASCII, so the list must be too. One-time cost at load.
void dictNormalize() {
  for (int i = 0; i < g_dictLen; i++) {
    char c = g_dict[i];
    if (c >= 'A' && c <= 'Z') g_dict[i] = (char)(c + 32);
  }
  std::sort(g_dictOff.begin(), g_dictOff.end(), [](int a, int b) {
    for (int j = 0; ; j++) {
      char ca = (a + j < g_dictLen) ? g_dict[a + j] : '\n';
      char cb = (b + j < g_dictLen) ? g_dict[b + j] : '\n';
      if (ca == '\r' || ca == '\n') ca = 0;
      if (cb == '\r' || cb == '\n') cb = 0;
      if (ca != cb) return (unsigned char)ca < (unsigned char)cb;
      if (ca == 0) return false;
    }
  });
}

// compare a lowercase word of length wl against the dictionary line at off
static int dictCmp(const char *w, int wl, int off) {
  for (int j = 0; ; j++) {
    char d = (off + j < g_dictLen) ? g_dict[off + j] : '\n';
    if (d == '\r' || d == '\n') d = 0;
    char c = (j < wl) ? w[j] : 0;
    if (c != d) return ((unsigned char)c < (unsigned char)d) ? -1 : 1;
    if (c == 0) return 0;
  }
}

// Words learned at runtime ('l' on the spell screen). The sketch also appends
// them to the list file, so they're in the sorted index from the next boot.
std::vector<String> g_dictLearned;
void dictLearn(const char *w) { g_dictLearned.push_back(String(w)); }

bool dictHas(const char *w, int wl) {
  int lo = 0, hi = (int)g_dictOff.size() - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    int c = dictCmp(w, wl, g_dictOff[mid]);
    if (c == 0) return true;
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  for (auto &lw : g_dictLearned)               // small linear overlay
    if ((int)lw.length() == wl && memcmp(lw.c_str(), w, wl) == 0) return true;
  return false;
}

static inline bool wordLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Next word in the doc at/after `from`: fills out[] lowercased (truncated to
// maxLen-1), returns the index just past the word, or -1 at end of document.
// Apostrophes inside a word (don't, dog's) are part of it.
int docNextWord(int from, char *out, int maxLen, int &wl) {
  int n = doc.length(), i = from;
  while (i < n && !wordLetter(doc[i])) i++;
  if (i >= n) return -1;
  wl = 0;
  while (i < n) {
    char c = doc[i];
    bool apos = (c == '\'' && wl > 0 && i + 1 < n && wordLetter(doc[i + 1]));
    if (!wordLetter(c) && !apos) break;
    if (wl < maxLen - 1) {
      if (c >= 'A' && c <= 'Z') c += 32;
      out[wl++] = c;
    }
    i++;
  }
  out[wl] = 0;
  return i;
}

// Scan the whole document; collect up to maxOut unique words that are missing
// from the dictionary, with occurrence counts. Single letters are skipped and
// possessives fall back to the base word. Returns the number collected, or -1
// if no dictionary is loaded.
int spellScan(String *words, int *counts, int maxOut) {
  if (!g_dict || g_dictOff.empty()) return -1;
  char w[32];
  int wl = 0, found = 0, pos = 0;
  while ((pos = docNextWord(pos, w, (int)sizeof(w), wl)) >= 0) {
    if (wl < 2) continue;
    bool ok = dictHas(w, wl);
    if (!ok && wl > 2 && w[wl - 2] == '\'' && w[wl - 1] == 's')
      ok = dictHas(w, wl - 2);                 // dog's -> dog
    if (ok) continue;
    int k = 0;
    for (; k < found; k++) if (words[k] == w) { counts[k]++; break; }
    if (k < found) continue;
    if (found < maxOut) { words[found] = w; counts[found] = 1; found++; }
  }
  return found;
}
// ==== SPELLCORE END ====

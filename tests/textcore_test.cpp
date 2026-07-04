// Host-side equivalence tests for PaperType's text core.
//
// The tested code (GapBuffer, layout, incremental layoutEdit/docReplace,
// word count) is extracted VERBATIM from PaperType.ino between the
// "TEXTCORE" markers - do not edit textcore.inc by hand, regenerate it:
//
//   sed -n '/TEXTCORE-1 BEGIN/,/TEXTCORE-1 END/p; /TEXTCORE-2 BEGIN/,/TEXTCORE-2 END/p; /SPELLCORE BEGIN/,/SPELLCORE END/p' ../BYOBYOK/BYOBYOK.ino > textcore.inc
//
// Build & run:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -o textcore_test textcore_test.cpp
//   ./textcore_test fuzz
//   g++ -std=c++17 -O2 -o textcore_bench textcore_test.cpp
//   ./textcore_bench bench
//
// "fuzz" replays hundreds of thousands of random edits and checks, after
// every single edit, that:
//   1. the gap buffer's content matches a reference std::string
//   2. the incrementally-maintained g_lines match a from-scratch run of the
//      ORIGINAL wrapping algorithm on the reference string
//   3. the incrementally-maintained word count matches the ORIGINAL
//      countWords() algorithm on the reference string

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <random>
#include <string>
#include <vector>

// ---- shims so the Arduino-targeted code compiles on the host --------------
#define GB_MALLOC(n) malloc(n)
using String = std::string;   // GapBuffer::substring only uses reserve / += char

struct Line { int start; int len; };
std::vector<Line> g_lines;
int g_cols = 24;

#include "textcore.inc"       // the code under test, extracted from the sketch

// ---- reference implementations: the ORIGINAL algorithms on std::string ----
static std::vector<Line> layoutRef(const std::string &d) {
  std::vector<Line> lines;
  int n = (int)d.size();
  int lineStart = 0, lastSpace = -1, col = 0, i = 0;
  bool wrappedStart = false;
  while (i < n) {
    char c = d[i];
    if (c == '\n') {
      if (!(i == lineStart && wrappedStart)) lines.push_back({lineStart, i - lineStart});
      i++; lineStart = i; col = 0; lastSpace = -1; wrappedStart = false;
      continue;
    }
    if (col >= g_cols) {
      if (c == ' ') {
        lines.push_back({lineStart, i + 1 - lineStart});
        i++; lineStart = i; col = 0; lastSpace = -1; wrappedStart = true;
        continue;
      }
      if (lastSpace >= lineStart) {
        int brk = lastSpace + 1;
        lines.push_back({lineStart, brk - lineStart});
        lineStart = brk; lastSpace = -1;
        col = i - lineStart;
        wrappedStart = true;
        continue;
      }
      lines.push_back({lineStart, i - lineStart});
      lineStart = i; col = 0; lastSpace = -1; wrappedStart = true;
      continue;
    }
    if (c == ' ') lastSpace = i;
    i++; col++;
  }
  lines.push_back({lineStart, n - lineStart});
  return lines;
}

static int countRef(const std::string &d) {   // original countWords()
  int w = 0; bool in = false;
  for (char c : d) {
    bool sp = (c == ' ' || c == '\n' || c == '\t' || c == '\r');
    if (sp) in = false;
    else if (!in) { w++; in = true; }
  }
  return w;
}

// ---- helpers ---------------------------------------------------------------
static std::string bufContents() {
  std::string s = doc.substring(0, doc.length());
  return s;
}

static void die(const char *what, const std::string &ref, long step, unsigned seed) {
  fprintf(stderr, "\nFAIL: %s  (seed=%u step=%ld cols=%d doclen=%zu)\n",
          what, seed, step, g_cols, ref.size());
  fprintf(stderr, "doc tail: ...%s\n",
          ref.substr(ref.size() > 80 ? ref.size() - 80 : 0).c_str());
  exit(1);
}

static void checkAll(const std::string &ref, long step, unsigned seed) {
  if (doc.length() != (int)ref.size()) die("length mismatch", ref, step, seed);
  if (bufContents() != ref)            die("content mismatch", ref, step, seed);
  if (g_docWords != countRef(ref))     die("word count mismatch", ref, step, seed);
  std::vector<Line> want = layoutRef(ref);
  if (want.size() != g_lines.size())   die("line count mismatch", ref, step, seed);
  for (size_t r = 0; r < want.size(); r++)
    if (want[r].start != g_lines[r].start || want[r].len != g_lines[r].len)
      die("line geometry mismatch", ref, step, seed);
}

// mirror an edit into the reference string
static void refReplace(std::string &ref, int pos, int delLen, const std::string &ins) {
  ref.erase(pos, delLen);
  ref.insert(pos, ins);
}

// ---- fuzz ------------------------------------------------------------------
static char randChar(std::mt19937 &rng) {
  int r = (int)(rng() % 100);
  if (r < 58) return (char)('a' + rng() % 26);
  if (r < 78) return ' ';
  if (r < 86) return '\n';
  if (r < 88) return '\t';
  if (r < 90) return '\r';                 // uploads can contain these
  static const char punct[] = ".,;:'\"!?-()";
  return punct[rng() % (sizeof(punct) - 1)];
}

static void fuzzRun(unsigned seed, int cols, long steps, int maxDoc) {
  std::mt19937 rng(seed);
  g_cols = cols;
  doc.clear(); g_docWords = 0; g_lines.clear();
  layout();                                  // as enterEditor() does
  std::string ref;
  int cursor = 0;

  for (long s = 0; s < steps; s++) {
    int n = doc.length();
    if (cursor > n) cursor = n;
    int op = (int)(rng() % 100);

    if (op < 45) {                                        // type one char
      if (n < maxDoc) {
        std::string ins(1, randChar(rng));
        int got = docReplace(cursor, 0, ins.c_str(), 1);
        refReplace(ref, cursor, 0, ins);
        cursor += got;
      }
    } else if (op < 55) {                                 // type a word
      if (n < maxDoc) {
        std::string w;
        int wl = 1 + (int)(rng() % 12);
        for (int k = 0; k < wl; k++) w += (char)('a' + rng() % 26);
        w += ' ';
        int got = docReplace(cursor, 0, w.c_str(), (int)w.size());
        refReplace(ref, cursor, 0, w);
        cursor += got;
      }
    } else if (op < 60) {                                 // paste a blob
      if (n < maxDoc) {
        std::string p;
        int pl = 30 + (int)(rng() % 270);
        for (int k = 0; k < pl; k++) p += randChar(rng);
        int at = n ? (int)(rng() % (n + 1)) : 0;
        docReplace(at, 0, p.c_str(), (int)p.size());
        refReplace(ref, at, 0, p);
        cursor = at + (int)p.size();
      }
    } else if (op < 75) {                                 // backspace
      if (cursor > 0) {
        docReplace(cursor - 1, 1, nullptr, 0);
        refReplace(ref, cursor - 1, 1, "");
        cursor--;
      }
    } else if (op < 85) {                                 // delete forward
      if (cursor < n) {
        docReplace(cursor, 1, nullptr, 0);
        refReplace(ref, cursor, 1, "");
      }
    } else if (op < 90) {                                 // delete a range
      if (n > 0) {
        int at = (int)(rng() % n);
        int dl = 1 + (int)(rng() % 40);
        if (dl > n - at) dl = n - at;
        docReplace(at, dl, nullptr, 0);
        refReplace(ref, at, dl, "");
        cursor = at;
      }
    } else if (op < 98) {                                 // move cursor
      cursor = n ? (int)(rng() % (n + 1)) : 0;
    } else {                                              // occasional full layout()
      layout();                                           // must equal the ref too
    }
    checkAll(ref, s, seed);
  }
}

static int runFuzz() {
  const int  colsList[]  = {8, 24, 61};
  const long steps       = 4000;
  long total = 0;
  for (int cols : colsList) {
    for (unsigned seed = 1; seed <= 5; seed++) {
      fuzzRun(seed, cols, steps, 6000);
      total += steps;
      printf("  ok: cols=%-3d seed=%u  (%ld edits, every edit verified)\n", cols, seed, steps);
    }
  }
  // targeted nasties: wrap-boundary newlines, long words, space runs
  const char *nasty[] = {
    "", "\n", "\n\n\n", "        ", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    "word word word word word word word word\n\nword",
    "a b c d e f g h i j k l m n o p q r s t u v w x y z",
  };
  for (const char *base : nasty) {
    for (unsigned seed = 100; seed < 103; seed++) {
      g_cols = 8;
      doc.clear(); g_docWords = 0; g_lines.clear(); layout();
      std::string ref;
      docReplace(0, 0, base, (int)strlen(base));
      refReplace(ref, 0, 0, base);
      checkAll(ref, -1, seed);
      std::mt19937 rng(seed);
      for (int s = 0; s < 800; s++) {                     // hammer edits on it
        int n = doc.length();
        int at = n ? (int)(rng() % (n + 1)) : 0;
        if (rng() % 2 && n > 0) {
          int dl = 1 + (int)(rng() % 3);
          if (dl > n - at) dl = n - at;
          docReplace(at, dl, nullptr, 0);
          refReplace(ref, at, dl, "");
        } else {
          std::string ins(1, randChar(rng));
          docReplace(at, 0, ins.c_str(), 1);
          refReplace(ref, at, 0, ins);
        }
        checkAll(ref, s, seed);
      }
    }
  }
  printf("  ok: targeted edge cases\n");
  printf("ALL FUZZ TESTS PASSED (%ld+ verified edits)\n", total);
  return 0;
}

// ---- bench: is a novel actually fast? --------------------------------------
static int runBench() {
  using clk = std::chrono::steady_clock;
  g_cols = 94;                               // roughly the device's column count
  doc.clear(); g_docWords = 0; g_lines.clear();

  std::mt19937 rng(42);
  std::string book;                          // ~1.2 MB of paragraphs
  while (book.size() < 1200 * 1000) {
    int words = 40 + (int)(rng() % 120);
    for (int w = 0; w < words; w++) {
      int wl = 2 + (int)(rng() % 9);
      for (int k = 0; k < wl; k++) book += (char)('a' + rng() % 26);
      book += ' ';
    }
    book += "\n\n";
  }
  doc.insert(0, book.c_str(), (int)book.size());
  g_docWords = recountWords();

  auto t0 = clk::now();
  layout();                                  // the old per-keystroke cost
  auto t1 = clk::now();
  double fullMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

  const int N = 20000;                       // typing at random spots in the book
  t0 = clk::now();
  for (int i = 0; i < N; i++) {
    int at = (int)(rng() % (doc.length() + 1));
    char c = (i % 7 == 0) ? ' ' : (char)('a' + i % 26);
    docReplace(at, 0, &c, 1);
  }
  t1 = clk::now();
  double perKeyUs = std::chrono::duration<double, std::micro>(t1 - t0).count() / N;

  printf("doc: %d chars, %d words, %zu wrapped lines\n",
         doc.length(), g_docWords, g_lines.size());
  printf("full layout() of the whole book: %8.2f ms   <- old cost per keystroke\n", fullMs);
  printf("docReplace() incremental:        %8.2f us   <- new cost per keystroke\n", perKeyUs);
  printf("speedup: ~%.0fx (host CPU; the ESP32 ratio is what matters)\n",
         fullMs * 1000.0 / perKeyUs);
  return 0;
}

// ---- undo tests --------------------------------------------------------------
// Random edits with a snapshot taken whenever a NEW undo unit is created
// (coalesced edits extend the current unit). Unwinding the journal must then
// walk back through the snapshots exactly, with layout + word count intact.
static void undoRun(unsigned seed, int cols) {
  std::mt19937 rng(seed);
  g_cols = cols;
  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear();
  layout();
  std::string ref;
  std::vector<std::string> snaps;              // state before undo unit i

  for (int s = 0; s < 150; s++) {              // stays under UNDO_MAX units
    int n = doc.length();
    size_t before = g_undo.size();
    std::string refBefore = ref;
    int op = (int)(rng() % 100);
    if (op < 55 || n == 0) {                   // type 1 char (coalesces)
      int at = n ? (int)(rng() % (n + 1)) : 0;
      std::string ins(1, randChar(rng));
      docReplace(at, 0, ins.c_str(), 1);
      refReplace(ref, at, 0, ins);
    } else if (op < 75) {                      // backspace-style delete (coalesces)
      int at = (int)(rng() % n);
      docReplace(at, 1, nullptr, 0);
      refReplace(ref, at, 1, "");
    } else if (op < 90) {                      // paste (its own unit)
      std::string p;
      int pl = 5 + (int)(rng() % 60);
      for (int k = 0; k < pl; k++) p += randChar(rng);
      int at = (int)(rng() % (n + 1));
      docReplace(at, 0, p.c_str(), (int)p.size());
      refReplace(ref, at, 0, p);
    } else {                                   // range delete (its own unit)
      int at = (int)(rng() % n);
      int dl = 2 + (int)(rng() % 20);
      if (dl > n - at) dl = n - at;
      if (dl < 1) dl = 1;
      docReplace(at, dl, nullptr, 0);
      refReplace(ref, at, dl, "");
    }
    if (g_undo.size() == before + 1) snaps.push_back(refBefore);
    checkAll(ref, s, seed);
  }
  // unwind everything, checking each restored state (incl. layout + words)
  while (true) {
    int p = undoLast();
    if (p < 0) break;
    if (snaps.empty()) die("journal longer than snapshots", ref, -2, seed);
    ref = snaps.back(); snaps.pop_back();
    if (p < 0 || p > doc.length()) die("undo cursor out of range", ref, -2, seed);
    checkAll(ref, -2, seed);
  }
  if (!snaps.empty()) die("snapshots left after full unwind", ref, -3, seed);
  if (doc.length() != 0) die("unwind did not reach empty doc", ref, -3, seed);
}

static void undoCoalesceAndCapTests() {
  g_cols = 24;
  // typing "abc" coalesces into ONE unit; a single undo removes all three
  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear(); layout();
  docReplace(0, 0, "a", 1); docReplace(1, 0, "b", 1); docReplace(2, 0, "c", 1);
  if (g_undo.size() != 1) die("typing did not coalesce", "abc", -4, 0);
  undoLast();
  if (doc.length() != 0) die("coalesced undo incomplete", "", -4, 0);
  // backspacing a word back-to-front coalesces too
  undoClear();
  docReplace(0, 0, "hello", 5);                // one paste unit
  docReplace(4, 1, nullptr, 0); docReplace(3, 1, nullptr, 0); docReplace(2, 1, nullptr, 0);
  if (g_undo.size() != 2) die("backspaces did not coalesce", "he", -4, 0);
  undoLast();
  if (bufContents() != "hello") die("backspace undo wrong", "hello", -4, 0);
  // cap: the journal must never exceed UNDO_MAX and must stay undoable
  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear(); layout();
  for (int i = 0; i < 300; i++) {
    const char *two = "xy";                    // 2-char inserts never coalesce
    docReplace(doc.length(), 0, two, 2);
  }
  if ((int)g_undo.size() != UNDO_MAX) die("cap not enforced", "", -5, 0);
  int undone = 0;
  while (undoLast() >= 0) undone++;
  if (undone != UNDO_MAX) die("cap unwind count wrong", "", -5, 0);
  if (doc.length() != (300 - UNDO_MAX) * 2) die("cap unwind residue wrong", "", -5, 0);
  printf("  ok: undo coalescing + journal cap\n");
}

// ---- search tests ------------------------------------------------------------
static void searchTests() {
  g_cols = 24;
  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear(); layout();
  const char *text = "The quick brown Fox jumps over the lazy dog.\nfox trot.";
  docReplace(0, 0, text, (int)strlen(text));
  String fox = "fox";
  if (searchDoc(fox, 0) != 16)  die("case-insensitive find failed", text, -6, 0);
  if (searchDoc(fox, 17) != 45) die("find-next failed", text, -6, 0);
  if (searchDoc(fox, 46) != 16) die("wrap-around failed", text, -6, 0);
  String nope = "zebra";
  if (searchDoc(nope, 0) != -1) die("phantom match", text, -6, 0);
  String empty = "";
  if (searchDoc(empty, 0) != -1) die("empty needle matched", text, -6, 0);
  String all = "The quick brown Fox jumps over the lazy dog.\nfox trot.";
  if (searchDoc(all, 10) != 0) die("full-doc needle failed", text, -6, 0);
  printf("  ok: search (case-insensitive, next, wrap, misses)\n");
}

// ---- spell tests -------------------------------------------------------------
static void spellTests() {
  g_cols = 40;
  // tiny sorted dictionary (lowercase, one word per line)
  static const char blob[] = "apple\nbanana\ncat\ndog\nhello\nquick\nthe\nto\nworld\n";
  g_dict = (char *)malloc(sizeof(blob));
  memcpy(g_dict, blob, sizeof(blob));
  g_dictLen = (int)strlen(blob);
  dictIndex();
  if (g_dictOff.size() != 9) die("dict index count wrong", blob, -7, 0);
  String first = "apple", last = "world", missing = "zebra", prefix = "app";
  if (!dictHas(first.c_str(), 5)) die("first dict word not found", blob, -7, 0);
  if (!dictHas(last.c_str(), 5))  die("last dict word not found", blob, -7, 0);
  if (dictHas(missing.c_str(), 5)) die("phantom dict hit", blob, -7, 0);
  if (dictHas(prefix.c_str(), 3))  die("prefix wrongly matched", blob, -7, 0);

  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear(); layout();
  const char *text = "The quick brown Cat sayz hello to the DOG'S wrld... wrld! don't";
  docReplace(0, 0, text, (int)strlen(text));

  String words[16]; int counts[16];
  int n = spellScan(words, counts, 16);
  // expected unknown: brown, sayz, wrld (x2), don't  -- dog's resolves to dog
  if (n != 4) die("unknown word count wrong", text, -7, 0);
  bool okBrown = false, okSayz = false, okWrld = false, okDont = false;
  for (int i = 0; i < n; i++) {
    if (words[i] == "brown" && counts[i] == 1) okBrown = true;
    if (words[i] == "sayz"  && counts[i] == 1) okSayz  = true;
    if (words[i] == "wrld"  && counts[i] == 2) okWrld  = true;
    if (words[i] == "don't" && counts[i] == 1) okDont  = true;
  }
  if (!(okBrown && okSayz && okWrld && okDont)) die("unknown word set wrong", text, -7, 0);

  // no dictionary -> -1
  free(g_dict); g_dict = nullptr; g_dictLen = 0; g_dictOff.clear();
  if (spellScan(words, counts, 16) != -1) die("missing-dict not reported", text, -7, 0);

  // dictNormalize: a messy real-world list (mixed case, CRLF, byte-sorted
  // with capitals first, possessives) must work after normalization
  static const char messy[] = "AARON\r\nAaron's\r\nZebra\r\nBanana\r\napple\r\nzebra's\r\n";
  g_dict = (char *)malloc(sizeof(messy));
  memcpy(g_dict, messy, sizeof(messy));
  g_dictLen = (int)strlen(messy);
  dictIndex();
  dictNormalize();
  String q1 = "aaron", q2 = "aaron's", q3 = "zebra", q4 = "banana", q5 = "apple", q6 = "zebra's", q7 = "aardvark";
  if (!dictHas(q1.c_str(), 5)) die("capitalized entry not found after normalize", messy, -7, 0);
  if (!dictHas(q2.c_str(), 7)) die("possessive entry not found", messy, -7, 0);
  if (!dictHas(q3.c_str(), 5)) die("Zebra->zebra not found", messy, -7, 0);
  if (!dictHas(q4.c_str(), 6) || !dictHas(q5.c_str(), 5) || !dictHas(q6.c_str(), 7))
    die("normalized entries missing", messy, -7, 0);
  if (dictHas(q7.c_str(), 8)) die("phantom hit after normalize", messy, -7, 0);
  free(g_dict); g_dict = nullptr; g_dictLen = 0; g_dictOff.clear();
  printf("  ok: spell check (index, binary search, scan, possessives, normalize)\n");
}

// ---- word-wise movement tests --------------------------------------------
static void wordNavTests() {
  g_cols = 40;
  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear(); layout();
  const char *t = "hello  world\nfoo x";     // len 18
  docReplace(0, 0, t, (int)strlen(t));
  if (wordLeftPos(18) != 17) die("wordLeft from end", t, -8, 0);
  if (wordLeftPos(17) != 13) die("wordLeft x->foo", t, -8, 0);
  if (wordLeftPos(13) != 7)  die("wordLeft foo->world (across \\n)", t, -8, 0);
  if (wordLeftPos(9)  != 7)  die("wordLeft inside word", t, -8, 0);
  if (wordLeftPos(7)  != 0)  die("wordLeft world->hello", t, -8, 0);
  if (wordLeftPos(0)  != 0)  die("wordLeft at start", t, -8, 0);
  if (wordRightPos(0)  != 7)  die("wordRight hello->world", t, -8, 0);
  if (wordRightPos(7)  != 13) die("wordRight world->foo (across \\n)", t, -8, 0);
  if (wordRightPos(13) != 17) die("wordRight foo->x", t, -8, 0);
  if (wordRightPos(17) != 18) die("wordRight x->end", t, -8, 0);
  if (wordRightPos(18) != 18) die("wordRight at end", t, -8, 0);
  printf("  ok: word-wise caret movement\n");
}

// ---- UTF-8 -> ASCII folding tests ------------------------------------------
// Mirrors loadDoc's byte loop, feeding one byte at a time (which also proves
// the decoder survives sequences split across read-chunk boundaries).
static std::string foldBytes(const std::string &in) {
  Utf8Fold u8;
  std::string out;
  for (unsigned char b : in) {
    long cp = u8.feed(b);
    if (cp < 0 || cp == '\r') continue;
    if (cp < 0x80) { out += (char)cp; continue; }
    out += asciiFold((uint32_t)cp);
  }
  return out;
}

static void utf8FoldTests() {
  // the exact lines reported from The Monk
  if (foldBytes("Assuming now a conjuror’s office, I") !=
      "Assuming now a conjuror's office, I") die("curly apostrophe", "", -9, 0);
  if (foldBytes("Thus on your future Fortune prophesy:—") !=
      "Thus on your future Fortune prophesy:--") die("em-dash", "", -9, 0);
  if (foldBytes("Soon as your novelty is o’er,") !=
      "Soon as your novelty is o'er,") die("o'er apostrophe", "", -9, 0);
  // quotes, ellipsis, accents, dashes
  if (foldBytes("“Quotes” … café — naïve – señor") !=
      "\"Quotes\" ... cafe -- naive - senor") die("typography mix", "", -9, 0);
  if (foldBytes("½ × ¾ © Œuvre ß") != "1/2 x 3/4 (c) OEuvre ss") die("symbols", "", -9, 0);
  if (foldBytes("crlf\r\nline") != "crlf\nline") die("CR stripping", "", -9, 0);
  // 4-byte emoji -> a visible '?', not silent garbage
  if (foldBytes("ok \xF0\x9F\x99\x82 done") != "ok ? done") die("emoji placeholder", "", -9, 0);
  // pure ASCII passes through untouched
  if (foldBytes("plain ASCII! 123") != "plain ASCII! 123") die("ascii passthrough", "", -9, 0);
  printf("  ok: UTF-8 -> ASCII folding (incl. The Monk sample)\n");
}

// ---- markdown state + latin-1 fold tests -----------------------------------
static void mdAndLatin1Tests() {
  g_cols = 40;
  doc.clear(); g_docWords = 0; g_lines.clear(); undoClear(); layout();
  const char *t = "plain **bold** and *it*\n# Head\n* bullet *real*\n";
  //               0     6      13    19  23 24     31 33
  docReplace(0, 0, t, (int)strlen(t));
  int ps; bool b, it, hd;
  mdStateAt(0, ps, b, it, hd);
  if (ps != 0 || b || it || hd) die("md initial state", t, -10, 0);
  mdStateAt(9, ps, b, it, hd);                 // inside **bold**
  if (!b || it || hd) die("md bold state", t, -10, 0);
  mdStateAt(15, ps, b, it, hd);                // after closing **
  if (b || it) die("md bold closed", t, -10, 0);
  mdStateAt(21, ps, b, it, hd);                // inside *it*
  if (b || !it) die("md italic state", t, -10, 0);
  mdStateAt(26, ps, b, it, hd);                // inside "# Head"
  if (!hd || ps != 24) die("md heading state", t, -10, 0);
  int bl = (int)strlen("plain **bold** and *it*\n# Head\n");  // paragraph "* bullet *real*"
  mdStateAt(bl + 9, ps, b, it, hd);            // after the bullet star, before *real*
  if (it) die("bullet star wrongly toggled italics", t, -10, 0);
  mdStateAt(bl + 11, ps, b, it, hd);           // inside *real*
  if (!it) die("md italic after bullet", t, -10, 0);

  // latin-1 passthrough: å (C3 A5) folds to 'a' normally, byte 0xE5 when kept
  g_keepLatin1 = false;
  if (strcmp(asciiFold(0xE5), "a") != 0) die("fold a-ring -> a", t, -10, 0);
  g_keepLatin1 = true;
  const char *r = asciiFold(0xE5);
  if ((uint8_t)r[0] != 0xE5 || r[1] != 0) die("latin-1 passthrough", t, -10, 0);
  if (strcmp(asciiFold(0x2019), "'") != 0) die("typography still folds", t, -10, 0);
  // composition tables (macOS convention + Esperanto)
  if (composeChar('u', 'a') != 0xE4 || composeChar('e', 'E') != 0xC9 ||
      composeChar('n', 'n') != 0xF1) die("compose accents", t, -10, 0);
  if (composeChar('i', 'g') != 0x82 || composeChar('i', 'S') != 0x89 ||
      composeChar('b', 'u') != 0x8A) die("compose esperanto", t, -10, 0);
  if (composeChar('x', 'a') != 0 || composeChar('u', 'q') != 0) die("phantom compose", t, -10, 0);
  if (optionChar('a') != 0xE5 || optionChar('s') != 0xDF || optionChar('z') != 0)
    die("option directs", t, -10, 0);
  if (!composeDead('i') || composeDead('a')) die("dead-key set", t, -10, 0);
  // Esperanto folding: gx-notation without fonts, slot 0x82 with them
  g_keepLatin1 = false;
  if (strcmp(asciiFold(0x11D), "gx") != 0 || strcmp(asciiFold(0x16C), "Ux") != 0)
    die("esperanto x-notation", t, -10, 0);
  g_keepLatin1 = true;
  if ((uint8_t)asciiFold(0x11D)[0] != 0x82 || (uint8_t)asciiFold(0x109)[0] != 0x80)
    die("esperanto slots", t, -10, 0);
  g_keepLatin1 = false;
  printf("  ok: markdown state, latin-1 folds, composition, esperanto\n");
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "bench") == 0) return runBench();
  int rc = runFuzz();
  if (rc) return rc;
  for (int cols : {8, 24, 61})
    for (unsigned seed = 11; seed <= 15; seed++) undoRun(seed, cols);
  printf("  ok: undo unwind fuzz (15 runs, every state verified)\n");
  undoCoalesceAndCapTests();
  searchTests();
  spellTests();
  wordNavTests();
  utf8FoldTests();
  mdAndLatin1Tests();
  printf("ALL TEXTCORE TESTS PASSED\n");
  return 0;
}

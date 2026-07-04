#!/usr/bin/env python3
"""Convert an EPUB to clean plain text for PaperType (or anything else).

Usage:
    python3 epub2txt.py book.epub               -> book.txt
    python3 epub2txt.py book.epub -o novel.txt
    python3 epub2txt.py book.epub --keep-unicode

Reads the EPUB's spine so chapters come out in reading order, strips the
XHTML down to paragraphs, and (by default) transliterates typographic
characters to plain ASCII - PaperType's built-in fonts are 7-bit, so smart
quotes, em-dashes and accented letters would otherwise render as blanks.

Python 3.8+, standard library only. Doesn't handle DRM-protected books.
"""

import argparse
import html.parser
import posixpath
import re
import sys
import unicodedata
import urllib.parse
import xml.etree.ElementTree as ET
import zipfile

# typographic -> ASCII replacements applied before the accent-stripping pass
ASCII_MAP = {
    "‘": "'", "’": "'", "‚": "'", "‛": "'",   # single quotes
    "“": '"', "”": '"', "„": '"', "‟": '"',   # double quotes
    "–": "-", "—": "--", "―": "--",                # dashes
    "…": "...",                                              # ellipsis
    " ": " ", " ": " ", " ": " ", " ": " ",   # odd spaces
    " ": " ", " ": " ", " ": " ",
    "­": "", "​": "", "﻿": "",                     # soft hyphen etc.
    "·": "*", "•": "*", "●": "*",                  # bullets
    "«": '"', "»": '"', "‹": "'", "›": "'",   # guillemets
    "‐": "-", "‑": "-", "−": "-",                  # more hyphens
}


class TextExtractor(html.parser.HTMLParser):
    """XHTML -> paragraphs of plain text."""

    BLOCK = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "tr",
        "blockquote", "section", "article", "aside", "header", "footer",
        "figure", "figcaption", "dt", "dd", "pre", "table", "ol", "ul",
    }
    SKIP = {"script", "style", "head", "title", "template", "svg"}
    HEADINGS = {"h1", "h2", "h3", "h4", "h5", "h6"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self.skip += 1
        elif tag == "br":
            self.parts.append("\n")
        elif tag == "hr":
            self.parts.append("\n\n* * *\n\n")
        elif tag in self.BLOCK:
            self.parts.append("\n\n" if tag in self.HEADINGS else "\n")

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)

    def handle_endtag(self, tag):
        if tag in self.SKIP:
            self.skip = max(0, self.skip - 1)
        elif tag in self.BLOCK:
            self.parts.append("\n\n" if tag in self.HEADINGS else "\n")

    def handle_data(self, data):
        if not self.skip:
            self.parts.append(data)

    def text(self):
        t = "".join(self.parts).replace("\r", "")
        t = re.sub(r"[ \t]+", " ", t)
        t = re.sub(r" ?\n ?", "\n", t)
        t = re.sub(r"\n{3,}", "\n\n", t)     # at most one blank line
        return t.strip()


def spine_documents(z):
    """Return (title, [archive paths of the content documents, in order])."""
    container = ET.fromstring(z.read("META-INF/container.xml"))
    ns = {"c": "urn:oasis:names:tc:opendocument:xmlns:container"}
    rootfile = container.find(".//c:rootfile", ns)
    if rootfile is None:
        raise SystemExit("error: not a valid EPUB (no rootfile in container.xml)")
    opf_path = rootfile.get("full-path")
    opf = ET.fromstring(z.read(opf_path))
    base = posixpath.dirname(opf_path)

    o = {"o": "http://www.idpf.org/2007/opf"}
    dc = {"dc": "http://purl.org/dc/elements/1.1/"}
    items = {i.get("id"): i.get("href")
             for i in opf.findall(".//o:manifest/o:item", o)}
    docs = []
    for ref in opf.findall(".//o:spine/o:itemref", o):
        if ref.get("linear", "yes") == "no":
            continue                          # covers, ads, etc.
        href = items.get(ref.get("idref"))
        if not href:
            continue
        href = urllib.parse.unquote(href.split("#")[0])
        path = posixpath.normpath(posixpath.join(base, href)) if base else href
        docs.append(path)
    title_el = opf.find(".//dc:title", dc)
    title = title_el.text.strip() if title_el is not None and title_el.text else None
    return title, docs


def to_ascii(t):
    for k, v in ASCII_MAP.items():
        t = t.replace(k, v)
    # decompose accents (café -> cafe), then drop whatever still isn't ASCII
    t = unicodedata.normalize("NFKD", t)
    return t.encode("ascii", "ignore").decode("ascii")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("epub", help="input .epub file")
    ap.add_argument("-o", "--output", help="output .txt (default: input name)")
    ap.add_argument("--keep-unicode", action="store_true",
                    help="skip the ASCII transliteration pass")
    args = ap.parse_args()

    try:
        z = zipfile.ZipFile(args.epub)
    except (OSError, zipfile.BadZipFile) as e:
        raise SystemExit(f"error: cannot open {args.epub}: {e}")

    title, docs = spine_documents(z)
    if not docs:
        raise SystemExit("error: the EPUB spine lists no content documents")

    chapters = []
    for path in docs:
        try:
            raw = z.read(path)
        except KeyError:
            print(f"warning: spine entry missing from archive: {path}", file=sys.stderr)
            continue
        ex = TextExtractor()
        ex.feed(raw.decode("utf-8", "replace"))
        t = ex.text()
        if t:
            chapters.append(t)

    body = "\n\n\n".join(chapters)
    if title:
        body = title + "\n\n\n" + body
    if not args.keep_unicode:
        body = to_ascii(body)
    body = body.rstrip() + "\n"

    out = args.output or re.sub(r"\.epub$", "", args.epub, flags=re.I) + ".txt"
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    words = len(body.split())
    print(f"{out}: {len(chapters)} chapter(s), {words} words, {len(body)} chars")


if __name__ == "__main__":
    main()

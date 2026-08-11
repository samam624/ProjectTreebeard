#!/usr/bin/env python3
"""Download a datasheet PDF and extract clean text via PyMuPDF.

WebFetch's built-in PDF text extraction fails on many vendor datasheets
(confirmed on TI PDFs: TMUX1574, TMUX1109, TMUX1308/1309 all came back as
"corrupted/compressed binary data" via WebFetch, despite being valid,
normal PDFs). PyMuPDF (fitz) reads the same files cleanly. This script is
the reliable path: download once, dump full text with page markers, then
Grep/Read the .txt output instead of re-fetching the PDF through WebFetch.

Usage:
    python fetch_datasheet.py <url-or-local-path> [output.txt]

If the input is a URL, the PDF is saved next to the output .txt (same
basename) so it doesn't need re-downloading later. If output.txt is
omitted, it's derived from the source filename in the current directory.
"""
import sys
import os
import urllib.request

import fitz  # PyMuPDF


def download(url: str, dest: str) -> None:
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=60) as resp, open(dest, "wb") as f:
        f.write(resp.read())


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    source = sys.argv[1]
    is_url = source.startswith("http://") or source.startswith("https://")

    if is_url:
        basename = os.path.basename(source.split("?")[0]) or "datasheet.pdf"
        pdf_path = basename if basename.endswith(".pdf") else basename + ".pdf"
        if not os.path.exists(pdf_path):
            print(f"Downloading {source} -> {pdf_path}", file=sys.stderr)
            download(source, pdf_path)
    else:
        pdf_path = source

    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(pdf_path)[0] + "_full.txt"

    doc = fitz.open(pdf_path)
    chunks = []
    for i, page in enumerate(doc):
        chunks.append(f"\n===PAGE {i + 1}===\n" + page.get_text())
    text = "".join(chunks)

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)

    print(f"{doc.page_count} pages, {len(text)} chars -> {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()

#!/usr/bin/env python
"""Download a datasheet PDF (or take a local path) and extract it to <name>_extracted.txt.

Rebuilt 2026-09-17 for the MSV4 reference library; same contract the `datasheet-lookup`
skill describes -- PyMuPDF extraction, ===PAGE N=== markers, never WebFetch on a PDF.

    python fetch_datasheet.py <url-or-path> [outdir] [--name NAME]
"""
import sys, os, urllib.request, pathlib

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    name = None
    for i, a in enumerate(sys.argv):
        if a == "--name":
            name = sys.argv[i + 1]
    src = args[0]
    outdir = pathlib.Path(args[1] if len(args) > 1 else ".")
    outdir.mkdir(parents=True, exist_ok=True)

    if src.startswith("http"):
        name = name or os.path.basename(src.split("?")[0]) or "datasheet.pdf"
        if not name.endswith(".pdf"):
            name += ".pdf"
        pdf = outdir / name
        req = urllib.request.Request(src, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=90) as r:
            pdf.write_bytes(r.read())
    else:
        pdf = pathlib.Path(src)
        if name:
            dest = outdir / (name if name.endswith(".pdf") else name + ".pdf")
            if dest.resolve() != pdf.resolve():
                dest.write_bytes(pdf.read_bytes())
            pdf = dest

    import fitz
    doc = fitz.open(pdf)
    txt = pdf.with_name(pdf.stem + "_extracted.txt")
    with open(txt, "w", encoding="utf-8") as f:
        for n, page in enumerate(doc, 1):
            f.write(f"\n===PAGE {n}===\n")
            f.write(page.get_text())
    print(f"OK {pdf}  ({pdf.stat().st_size} bytes, {doc.page_count} pages)")
    print(f"   -> {txt}")

main()

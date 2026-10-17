#!/usr/bin/env python3
"""
docs/lab2-report.md -> docs/lab2-report.pdf (Markdown -> HTML -> Chrome headless print-to-pdf).
Потрібні: python-markdown, Google Chrome.  Запуск: python3 tools/report_pdf.py
"""
import pathlib
import subprocess
import sys

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "lab2-report.md"
HTML = ROOT / "docs" / "lab2-report.html"
PDF = ROOT / "docs" / "lab2-report.pdf"
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

CSS = """
@page { size: A4; margin: 18mm 16mm 18mm 20mm; }
body { font-family: 'Times New Roman', Times, serif; font-size: 12pt; line-height: 1.35; color: #000; }
h2 { font-size: 15pt; margin-top: 18pt; page-break-after: avoid; }
h3 { font-size: 13pt; margin-top: 12pt; page-break-after: avoid; }
table { border-collapse: collapse; width: 100%; font-size: 9.5pt; margin: 6pt 0; page-break-inside: auto; }
th, td { border: 1px solid #777; padding: 3px 5px; vertical-align: top; }
th { background: #eee; }
code { font-family: Menlo, Consolas, monospace; font-size: 9pt; }
pre { font-size: 8.5pt; background: #f6f6f6; padding: 6px; white-space: pre-wrap; word-break: break-all; }
img { max-width: 100%; page-break-inside: avoid; }
.title-page { height: 245mm; display: flex; flex-direction: column; justify-content: space-between; font-size: 14pt; }
.center { text-align: center; } .right { text-align: right; margin-left: 50%; text-align: left; }
.page-break { page-break-after: always; }
"""


def main():
    body = markdown.markdown(SRC.read_text(encoding="utf-8"), extensions=["tables", "fenced_code", "md_in_html"])
    HTML.write_text(f"<!doctype html><html lang='uk'><head><meta charset='utf-8'><title>ЛР2 — Сас Є.О., ІО-31</title>"
                    f"<style>{CSS}</style></head><body>{body}</body></html>", encoding="utf-8")
    res = subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                          f"--print-to-pdf={PDF}", HTML.as_uri()], capture_output=True, text=True, timeout=180)
    HTML.unlink()
    if not PDF.exists():
        print(res.stderr[-2000:])
        sys.exit(1)
    print(f"written {PDF.relative_to(ROOT)} ({PDF.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()

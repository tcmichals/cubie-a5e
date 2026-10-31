#!/usr/bin/env python3
"""
Generate a Blogger-ready HTML post snippet for Part 5.

Features:
- Uses public GitHub raw CDN URLs for the 3 SVG architecture diagrams so Blogger loads them smoothly in both Compose and HTML view.
- Embeds scoped CSS for tables, code blocks, syntax highlighting, and responsive SVG diagrams.
- Clean fragment output without <!DOCTYPE>, <html>, <head>, or <body> tags so it drops directly into Blogger's HTML view (<>).
"""

import re
import subprocess
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
MD_PATH = BASE_DIR / "part5_a733_e902_remoteproc_and_heterogeneous_comparison.md"
OUT_PATH = BASE_DIR / "part5_for_blogger.html"

GITHUB_RAW_BASE = "https://raw.githubusercontent.com/tcmichals/cubie-a5e/main/docs/articles/media"

BLOGGER_STYLE = """<style>
/* Scoped styling for technical article in Blogger */
.blogger-article {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  line-height: 1.65;
  color: #24292f;
  margin: 0 auto;
}
.blogger-article h1, .blogger-article h2, .blogger-article h3 {
  color: #1f2328;
  font-weight: 600;
  margin-top: 1.8rem;
  margin-bottom: 0.8rem;
}
.blogger-article h2 {
  border-bottom: 1px solid #d0d7de;
  padding-bottom: 0.3em;
}
.blogger-article pre {
  background-color: #1f242c;
  color: #f0f6fc;
  border-radius: 6px;
  padding: 1rem;
  overflow-x: auto;
  font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
  font-size: 0.88rem;
  line-height: 1.45;
}
.blogger-article code {
  font-family: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
  background-color: rgba(175, 184, 193, 0.2);
  padding: 0.2em 0.4em;
  border-radius: 4px;
  font-size: 85%;
}
.blogger-article pre code {
  background-color: transparent;
  padding: 0;
  color: inherit;
}
.blogger-article table {
  border-collapse: collapse;
  width: 100%;
  margin: 1.5rem 0;
  font-size: 0.9rem;
}
.blogger-article th, .blogger-article td {
  border: 1px solid #d0d7de;
  padding: 8px 12px;
  text-align: left;
}
.blogger-article th {
  background-color: #f6f8fa;
  font-weight: 600;
}
.blogger-article tr:nth-child(even) {
  background-color: #fcfcfc;
}
.blogger-article figure {
  margin: 2rem 0;
  text-align: center;
}
.blogger-article figure img {
  max-width: 100%;
  height: auto;
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  display: block;
  margin: 0 auto;
}
.blogger-article figcaption {
  margin-top: 0.6rem;
  font-size: 0.85rem;
  color: #57609a;
  font-style: italic;
}
.blogger-article blockquote {
  border-left: 4px solid #2da44e;
  margin: 1.5rem 0;
  padding: 0.5rem 1rem;
  color: #57606a;
  background-color: #f6f8fa;
  border-radius: 0 6px 6px 0;
}

/* Syntax highlighting token colors */
.blogger-article .co { color: #8b949e; font-style: italic; } /* Comment */
.blogger-article .kw { color: #ff7b72; font-weight: bold; } /* Keyword */
.blogger-article .dt { color: #79c0ff; }                    /* DataType */
.blogger-article .dv { color: #a5d6ff; }                    /* DecVal */
.blogger-article .bn { color: #a5d6ff; }                    /* BaseN */
.blogger-article .fl { color: #a5d6ff; }                    /* Float */
.blogger-article .ch { color: #a5d6ff; }                    /* Char */
.blogger-article .st { color: #a5d6ff; }                    /* String */
.blogger-article .fu { color: #d2a8ff; font-weight: bold; } /* Function */
.blogger-article .cf { color: #ff7b72; font-weight: bold; } /* ControlFlow */
.blogger-article .op { color: #79c0ff; }                    /* Operator */
.blogger-article .pp { color: #ffa657; font-weight: bold; } /* Preprocessor */
.blogger-article .va { color: #ffa657; }                    /* Variable */
</style>
"""

def main():
    raw_md = MD_PATH.read_text(encoding="utf-8")
    
    # Rewrite local media/*.svg paths to public GitHub raw CDN
    md_with_cdn = re.sub(r'media/([a-zA-Z0-9_\-]+\.svg)', f'{GITHUB_RAW_BASE}/\\1', raw_md)
    
    temp_md = BASE_DIR / ".temp_blogger_in.md"
    temp_md.write_text(md_with_cdn, encoding="utf-8")
    
    try:
        # Generate pandoc fragment (no -s so no <html>/<head>/<body> wrapper)
        result = subprocess.run(
            ["pandoc", str(temp_md), "--highlight-style=espresso"],
            capture_output=True,
            text=True,
            check=True
        )
        body_html = result.stdout
        
        # Wrap in article container with embedded styling
        final_post = f"{BLOGGER_STYLE}\n<div class=\"blogger-article\">\n{body_html}\n</div>\n"
        OUT_PATH.write_text(final_post, encoding="utf-8")
        print(f"[OK] Successfully built Blogger post snippet: {OUT_PATH} ({OUT_PATH.stat().st_size} bytes)")
    finally:
        if temp_md.exists():
            temp_md.unlink()

if __name__ == "__main__":
    main()

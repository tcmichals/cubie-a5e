#!/usr/bin/env python3
"""
Convert Markdown technical articles to standalone HTML with embedded SVGs.

Supports two embedding modes:
  1. Base64 Data URI (default via pandoc --embed-resources):
     <img src="data:image/svg+xml;base64,...">
  2. Raw Inline SVG (recommended for CMS/blog platforms that strip data URIs):
     <figure class="svg-diagram"><svg ...>...</svg><figcaption>...</figcaption></figure>
"""

import sys
import re
import os
import subprocess
import argparse
from pathlib import Path

DEFAULT_CSS = """
:root {
  --font-body: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  --font-mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace;
  --bg-color: #0d1117;
  --text-color: #c9d1d9;
  --heading-color: #f0f6fc;
  --link-color: #58a6ff;
  --code-bg: #161b22;
  --border-color: #30363d;
  --accent-color: #238636;
}

body {
  font-family: var(--font-body);
  line-height: 1.65;
  color: var(--text-color);
  background-color: var(--bg-color);
  max-width: 960px;
  margin: 0 auto;
  padding: 2rem 1.5rem;
}

h1, h2, h3, h4, h5, h6 {
  color: var(--heading-color);
  margin-top: 1.8rem;
  margin-bottom: 0.8rem;
  font-weight: 600;
  border-bottom: 1px solid var(--border-color);
  padding-bottom: 0.3em;
}

h1 { font-size: 2.1rem; border-bottom: 2px solid var(--border-color); }
h2 { font-size: 1.6rem; }
h3 { font-size: 1.3rem; border-bottom: none; }

a { color: var(--link-color); text-decoration: none; }
a:hover { text-decoration: underline; }

pre {
  background-color: var(--code-bg);
  border: 1px solid var(--border-color);
  border-radius: 6px;
  padding: 1rem;
  overflow-x: auto;
  font-family: var(--font-mono);
  font-size: 0.9rem;
}

code {
  font-family: var(--font-mono);
  background-color: rgba(110, 118, 129, 0.4);
  padding: 0.2em 0.4em;
  border-radius: 4px;
  font-size: 85%;
}

pre code {
  background-color: transparent;
  padding: 0;
}

blockquote {
  border-left: 4px solid var(--accent-color);
  margin: 1.5rem 0;
  padding: 0.5rem 1rem;
  color: #8b949e;
  background-color: rgba(35, 134, 54, 0.1);
  border-radius: 0 6px 6px 0;
}

table {
  border-collapse: collapse;
  width: 100%;
  margin: 1.5rem 0;
}

th, td {
  border: 1px solid var(--border-color);
  padding: 8px 12px;
  text-align: left;
}

th {
  background-color: var(--code-bg);
  color: var(--heading-color);
}

tr:nth-child(even) {
  background-color: rgba(22, 27, 34, 0.5);
}

img {
  max-width: 100%;
  height: auto;
  border-radius: 6px;
  display: block;
  margin: 1.5rem auto;
}

figure.diagram-container {
  margin: 2rem 0;
  text-align: center;
}

figure.diagram-container svg {
  max-width: 100%;
  height: auto;
  border-radius: 8px;
  border: 1px solid var(--border-color);
}

figure.diagram-container figcaption {
  margin-top: 0.6rem;
  font-size: 0.85rem;
  color: #8b949e;
  font-style: italic;
}
"""

def inline_raw_svgs(html_content: str, base_dir: Path) -> str:
    """
    Replace <img ... src="path/to/file.svg" ...> with raw <svg>...</svg> blocks.
    """
    pattern = re.compile(
        r'<img\s+([^>]*?)src=["\']([^"\']+\.svg)["\']([^>]*?)/?>',
        re.IGNORECASE
    )

    def replace_match(match):
        attrs_before = match.group(1)
        src_path = match.group(2)
        attrs_after = match.group(3)
        
        # Extract alt/title for caption
        alt_match = re.search(r'alt=["\']([^"\']*)["\']', attrs_before + " " + attrs_after)
        caption_text = alt_match.group(1) if alt_match else ""

        full_svg_path = (base_dir / src_path).resolve()
        if not full_svg_path.exists():
            print(f"[WARN] SVG file not found: {full_svg_path}")
            return match.group(0)

        svg_text = full_svg_path.read_text(encoding="utf-8")
        # Strip XML declaration if present
        svg_text = re.sub(r'<\?xml[^>]*\?>', '', svg_text).strip()

        caption_html = f"<figcaption>{caption_text}</figcaption>" if caption_text else ""
        return f'<figure class="diagram-container">\n{svg_text}\n{caption_html}\n</figure>'

    return pattern.sub(replace_match, html_content)

def main():
    parser = argparse.ArgumentParser(description="Convert Markdown article to self-contained HTML with embedded SVGs.")
    parser.add_argument("markdown_file", help="Path to input markdown file")
    parser.add_argument("-o", "--output", help="Path to output html file (default: same name with .html)")
    parser.add_argument("--mode", choices=["inline-xml", "data-uri"], default="inline-xml",
                        help="SVG embedding mode: 'inline-xml' (raw <svg> tags) or 'data-uri' (base64 img src)")
    parser.add_argument("--css", help="Optional path to custom CSS file")

    args = parser.parse_args()
    md_path = Path(args.markdown_file).resolve()
    if not md_path.exists():
        print(f"Error: {md_path} does not exist", file=sys.stderr)
        sys.exit(1)

    out_path = Path(args.output).resolve() if args.output else md_path.with_suffix(".html")
    base_dir = md_path.parent

    # Prepare CSS
    css_content = DEFAULT_CSS
    if args.css:
        custom_css_path = Path(args.css).resolve()
        if custom_css_path.exists():
            css_content = custom_css_path.read_text(encoding="utf-8")

    # Step 1: Run Pandoc to generate basic standalone HTML
    css_temp = base_dir / ".pandoc_temp.css"
    css_temp.write_text(css_content, encoding="utf-8")

    try:
        if args.mode == "data-uri":
            cmd = [
                "pandoc",
                str(md_path),
                "-s",
                "--embed-resources",
                f"--css={css_temp.name}",
                "--highlight-style=espresso",
                "-o",
                str(out_path)
            ]
            subprocess.run(cmd, cwd=base_dir, check=True)
            print(f"[OK] Generated standalone HTML (Data URI mode): {out_path}")
        else: # inline-xml
            cmd = [
                "pandoc",
                str(md_path),
                "-s",
                f"--css={css_temp.name}",
                "--highlight-style=espresso",
                "-o",
                str(out_path)
            ]
            subprocess.run(cmd, cwd=base_dir, check=True)
            
            # Read back generated HTML and inline SVGs directly
            html_content = out_path.read_text(encoding="utf-8")
            inlined_html = inline_raw_svgs(html_content, base_dir)
            out_path.write_text(inlined_html, encoding="utf-8")
            print(f"[OK] Generated standalone HTML (Inline XML mode): {out_path}")

    finally:
        if css_temp.exists():
            css_temp.unlink()

if __name__ == "__main__":
    main()

#!/bin/sh
# Publish wiki/ to the GitHub wiki (github.com/tcmichals/cubie-a5e/wiki).
# The wiki repo exists only after the first page was saved on github.com.
set -e
top=$(cd "$(dirname "$0")/.." && pwd)
wiki="$top/../cubie-a5e.wiki"
python3 "$top/tools/make_sweep_wiki.py"
[ -d "$wiki/.git" ] || git clone -q git@github.com:tcmichals/cubie-a5e.wiki.git "$wiki"
git -C "$wiki" pull -q --rebase
cp "$top"/wiki/*.md "$wiki/"
git -C "$wiki" add -A
if git -C "$wiki" diff --cached --quiet; then
	echo "[WIKI] no change"
	exit 0
fi
git -C "$wiki" -c user.name='Tim Michals' -c user.email=tcmichals@gmail.com \
	commit -q -m "Update from cubie-a5e $(git -C "$top" rev-parse --short HEAD)"
git -C "$wiki" push -q origin HEAD
echo "[WIKI] pushed: https://github.com/tcmichals/cubie-a5e/wiki"

#!/bin/sh
# Writes every source file of the port from the literate chapters in lit/ (notangle).
# cpif only replaces a file whose content changed, so make does not rebuild what is the same.
cd "$(dirname "$0")/.." || exit 1
for f in $(cat lit/FILES); do
  mkdir -p "$(dirname "$f")"
  notangle -R"$f" lit/*.nw | cpif "$f" || exit 1
done

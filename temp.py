import sys
import os

#!/usr/bin/env python3
"""
Read file and folder paths from stdin (one per line). End input with Ctrl-Z (Windows) or Ctrl-D (Unix).
For each file or all files inside each folder (recursively), write path and contents into prompt.txt.
"""

def collect_paths(lines):
    seen = set()
    results = []
    for raw in lines:
        p = raw.strip()
        if not p:
            continue
        p = os.path.abspath(os.path.expanduser(p))
        if os.path.isfile(p):
            if p not in seen:
                seen.add(p); results.append(p)
        elif os.path.isdir(p):
            for root, _, files in os.walk(p):
                for fn in files:
                    fp = os.path.join(root, fn)
                    if fp not in seen:
                        seen.add(fp); results.append(fp)
        else:
            sys.stderr.write(f"warning: path not found: {p}\n")
    return results

def read_file_text(path):
    try:
        with open(path, "rb") as f:
            data = f.read()
        data = data.decode("utf-8", errors="replace")
        data = data.split("\n")
        data = "\n".join(list(filter(lambda x: x!=" " and x!="", data)))
        return data
    except Exception as e:
        return f"<error reading file: {e}>"

def main():
    lines = sys.stdin.readlines()
    files = collect_paths(lines)
    out_path = "prompt.txt"
    with open(out_path, "w", encoding="utf-8") as out:
        for fp in files:
            out.write(f"--- {fp} ---\n")
            out.write(read_file_text(fp))
            out.write("\n\n")
    print(out_path)

if __name__ == "__main__":
    main()
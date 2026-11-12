"""Scene-understanding trial: print the answers written by describe.py as text.

  python show.py <results dir>/<model name> [photo stem ...]
"""
import glob
import json
import os
import sys


def main():
    folder = sys.argv[1]
    wanted = set(sys.argv[2:])
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        stem = os.path.splitext(os.path.basename(path))[0]
        if wanted and stem not in wanted:
            continue
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        if "photo" not in d:
            continue  # a report written by segment.py
        items = d["items"]
        tm = items.get("timings") or {}
        print("== %s  [%s]  %.1f s, %s prompt tokens, %s reply tokens, %.0f tokens/s" % (
            stem, d["model"], items["seconds"], tm.get("prompt_n"), tm.get("predicted_n"),
            tm.get("predicted_per_second") or 0))
        p = items["parsed"]
        if not p:
            print("   NOT VALID JSON (%s): %s" % (items["finish_reason"], (items["raw"] or "")[:200]))
            continue
        print("   scene: %s" % p["scene"])
        print("   light from: %s   main subject: %s" % (p["light_from"], p["main_subject"]))
        for it in p["items"]:
            print("   %-3s %-26s parent=%-3s %-20s imp=%d %-5s %s" % (
                it["id"], it["name"][:26], it["parent"] or "-", it["treatment"], it["importance"],
                it["edges"], it["how_many"]))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Writes Content/UI/Hud.aui, the in-game HUD layout (an Aether UI layout: a widget tree as JSON).
Widgets the Luau Contract script drives by name: ContamText, CleanBar, TimerText, Prompt, Message.
usage: build_hud.py <project dir>
"""
import json, os, sys, uuid

def text(name, value, size, color, **extra):
    d = {"type": "Text", "name": name, "text": value, "size": size, "color": color}
    d.update(extra)
    return d

def main(project):
    gold, white, dim = [1.0, 0.82, 0.25, 1.0], [1, 1, 1, 1], [0.85, 0.9, 1.0, 0.85]
    panel = {"type": "Border", "name": "ContractPanel", "background": {"kind": "Color", "tint": [0.02, 0.04, 0.09, 0.72]},
             "padding": [22, 14, 22, 16], "slot": {"anchors": [0, 0, 0, 0], "position": [28, 28], "size": [440, 150]},
             "children": [{"type": "VerticalBox", "children": [
                 text("Title", "WILLOW HOUSE", 26, dim),
                 {"type": "Spacer", "size": [8, 6]},
                 text("ContamText", "CONTAMINATION  0 / 0", 34, gold),
                 {"type": "Spacer", "size": [8, 8]},
                 {"type": "ProgressBar", "name": "CleanBar", "percent": 0.0, "background": {"kind": "Color", "tint": [0.13, 0.15, 0.2, 1.0]},
                  "bar": {"kind": "Color", "tint": [0.3, 1.0, 0.55, 1.0]}, "slot": {"size": [396, 14]}},
                 {"type": "Spacer", "size": [8, 8]},
                 text("TimerText", "TIME  00:00", 26, white)]}]}
    root = {"type": "Canvas", "name": "Hud", "children": [
        panel,
        text("Prompt", "", 30, white, justify="Center", slot={"anchors": [0.5, 0.82, 0.5, 0.82], "alignment": [0.5, 0.5], "auto_size": True}),
        text("Message", "", 64, [0.3, 1.0, 0.55, 1.0], justify="Center", slot={"anchors": [0.5, 0.3, 0.5, 0.3], "alignment": [0.5, 0.5], "auto_size": True}),
        text("Hint", "WASD  MOVE     MOUSE  LOOK     HOLD LEFT MOUSE  CLEAN", 24, dim, justify="Center",
             slot={"anchors": [0.5, 1, 0.5, 1], "alignment": [0.5, 1], "position": [0, -22], "auto_size": True})]}
    out = {"$type": "Layout", "version": 1, "root": root}
    path = os.path.join(project, "Content", "UI", "Hud.aui")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    ns = uuid.UUID("6d1f0a52-5b0e-4a50-9e0b-3a7f9d0c5a11")
    with open(path + ".ameta", "w", newline="\n") as f:
        json.dump({"$type": "AssetMeta", "$v": 1, "guid": str(uuid.uuid5(ns, "ui:Hud")), "importer": "UILayout", "importer_version": 1,
                   "labels": [], "settings": {}, "source_hash": ""}, f, indent=2)
        f.write("\n")
    print("wrote", path)

if __name__ == "__main__":
    main(sys.argv[1])

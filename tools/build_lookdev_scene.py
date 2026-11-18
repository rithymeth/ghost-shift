#!/usr/bin/env python3
"""Builds the Ghost Shift lookdev scene (a moonlit Willow House room) from the First Person template scene.

Keeps the template's SkyLight, player/camera and ground; replaces the pickups with the lookdev blockout
models (tools/build_lookdev.py) plus lights. Deterministic: GUIDs derive from stable names.

usage: build_lookdev_scene.py <project dir>
"""
import json, math, os, sys, uuid

NS = uuid.UUID("6d1f0a52-5b0e-4a50-9e0b-3a7f9d0c5a11")


def gid(name):
    return str(uuid.uuid5(NS, name))


def quat(axis, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return {"x": (s, 0, 0, math.cos(h)), "y": (0, s, 0, math.cos(h)), "z": (0, 0, s, math.cos(h))}[axis]


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def tf(pos, rot=(0, 0, 0, 1), scale=(1, 1, 1)):
    return {"$v": 2, "position": list(pos), "rotation": [round(v, 6) for v in rot], "scale": list(scale)}


def main(project):
    models = os.path.join(project, "Content", "Models")
    scene_path = os.path.join(project, "Content", "Scenes", "Main.ascene")
    scene = json.load(open(scene_path))
    keep = []
    for e in scene["entities"]:
        c = e["components"]
        tags = c.get("Tags", {}).get("names", [])
        if "SkyLight" in c:
            c["SkyLight"] = {"$v": 1, "color": [0.35, 0.45, 0.85], "intensity": 0.35}
            keep.append(e)
        elif "Player" in tags:
            c["Transform"]["position"] = [0.0, 1.7, 4.5]
            keep.append(e)
        elif "Ground" in tags:
            c["Transform"]["position"] = [0.0, -0.3, 0.0]  # sits under the lookdev floor, no z-fighting
            keep.append(e)
    ents = keep

    model_guid = {}
    for f in sorted(os.listdir(models)):
        if f.startswith("gs_") and f.endswith(".gltf"):
            g = gid("model:" + f)
            model_guid[f[:-5]] = g
            with open(os.path.join(models, f + ".ameta"), "w", newline="\n") as m:
                json.dump({"$type": "AssetMeta", "$v": 1, "guid": g, "importer": "Model", "importer_version": 1,
                           "labels": [], "settings": {}, "source_hash": ""}, m, indent=2)
                m.write("\n")

    def add(name, comps):
        ents.append({"guid": gid("entity:" + name), "components": comps})

    def model(name, mdl, pos, rot=(0, 0, 0, 1), tags=("Lookdev",)):
        add(name, {"Transform": tf(pos, rot), "Tags": {"$v": 1, "names": list(tags)},
                   "ModelRenderer": {"$v": 1, "asset_path": "Models/%s.gltf" % mdl, "model": model_guid[mdl]}})

    def point(name, pos, color, intensity, rng):
        add(name, {"Transform": tf(pos), "Tags": {"$v": 1, "names": ["Lookdev", "Light"]},
                   "PointLight": {"$v": 1, "color": list(color), "intensity": intensity, "range": rng, "cast_shadows": False}})

    # Floor: 3 x 3 tiles of 4 m.
    for ix in (-4, 0, 4):
        for iz in (-4, 0, 4):
            model("floor_%d_%d" % (ix, iz), "gs_floor", (ix, 0, iz))
    # Walls: back (-Z) with a window in the middle, side walls (+-X), front (+Z) behind the player.
    q90 = quat("y", 90)
    for ix in (-4, 4):
        model("wall_back_%d" % ix, "gs_wall", (ix, 0, -6))
    model("wall_back_window", "gs_wall_window", (0, 0, -6))
    for sx in (-6, 6):
        for iz in (-4, 0, 4):
            model("wall_side_%d_%d" % (sx, iz), "gs_wall", (sx, 0, iz), q90)
    for ix in (-4, 0, 4):
        model("wall_front_%d" % ix, "gs_wall", (ix, 0, 6))
    # Set dressing.
    model("table", "gs_table", (0, 0, -1.5))
    model("vacuum", "gs_vacuum", (0.2, 0.86, -1.5), quat("y", 25), ("Lookdev", "Tool"))
    model("lamp_left", "gs_lamp", (-4.5, 0, -4.5))
    model("lamp_right", "gs_lamp", (4.5, 0, -4.5))
    model("ghost_prankster", "gs_ghost_prankster", (-1.8, 0.4, -3.8), quat("y", 20), ("Lookdev", "Ghost"))
    model("ectoplasm_a", "gs_ectoplasm", (1.8, 0, -3.0), tags=("Lookdev", "Contamination"))
    model("stain_a", "gs_stain", (-2.4, 0, -0.5), tags=("Lookdev", "Contamination"))
    model("stain_b", "gs_stain", (2.6, 0, 0.8), tags=("Lookdev", "Contamination"))
    # Lights. Moon: a cool directional light shining in through the window (-Z side, pointing +Z and down).
    moon_rot = qmul(quat("y", 180), quat("x", -40))
    add("moon", {"Transform": tf((0, 5, -8), moon_rot), "Tags": {"$v": 1, "names": ["Lookdev", "Light"]},
                 "DirectionalLight": {"$v": 1, "color": [0.45, 0.6, 1.0], "intensity": 1.2, "cast_shadows": True}})
    point("light_lamp_left", (-4.5, 1.7, -4.5), (1.0, 0.62, 0.25), 18.0, 9.0)
    point("light_lamp_right", (4.5, 1.7, -4.5), (1.0, 0.62, 0.25), 18.0, 9.0)
    point("light_ghost", (-1.8, 1.7, -3.8), (0.2, 0.85, 1.0), 14.0, 7.0)
    point("light_vacuum", (0.2, 1.0, -1.5), (0.2, 0.9, 1.0), 4.0, 3.0)
    point("light_stain_a", (-2.4, 0.5, -0.5), (0.7, 0.2, 1.0), 6.0, 4.0)
    point("light_stain_b", (2.6, 0.5, 0.8), (0.7, 0.2, 1.0), 6.0, 4.0)
    point("light_ecto", (1.8, 0.6, -3.0), (0.2, 1.0, 0.45), 6.0, 4.0)

    scene["entities"] = ents
    with open(scene_path, "w", newline="\n") as f:
        json.dump(scene, f, indent=2, sort_keys=True)
        f.write("\n")
    print("scene: %d entities" % len(ents))


if __name__ == "__main__":
    main(sys.argv[1])

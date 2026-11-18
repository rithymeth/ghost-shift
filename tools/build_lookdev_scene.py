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
            c["SkyLight"] = {"$v": 1, "color": [0.35, 0.45, 0.85], "intensity": 0.5}
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

    def point(name, pos, color, intensity, rng, glow=None):
        add(name, {"Transform": tf(pos), "Tags": {"$v": 1, "names": ["Lookdev", "Light"] + (["Glow:" + glow] if glow else [])},
                   "PointLight": {"$v": 1, "color": list(color), "intensity": intensity, "range": rng, "cast_shadows": False}})

    # Floor: 3 x 3 tiles of 4 m.
    for ix in (-4, 0, 4):
        for iz in (-4, 0, 4):
            model("floor_%d_%d" % (ix, iz), "gs_floor", (ix, 0, iz))
    # Ceiling: the floor slab again, underside at the wall height.
    for ix in (-4, 0, 4):
        for iz in (-4, 0, 4):
            model("ceiling_%d_%d" % (ix, iz), "gs_floor", (ix, 3.2, iz))
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
    model("ectoplasm_a", "gs_ectoplasm", (1.8, 0, -3.0), tags=("Lookdev", "Contamination", "Kind:Ecto", "Glow:ecto_a"))
    model("stain_a", "gs_stain", (-2.4, 0, -0.5), tags=("Lookdev", "Contamination", "Kind:Stain", "Glow:stain_a"))
    model("stain_b", "gs_stain", (2.6, 0, 0.8), tags=("Lookdev", "Contamination", "Kind:Stain", "Glow:stain_b"))
    # More contamination (the starter house needs a contract worth playing) and the van that empties the vacuum's tank.
    for name, kind, pos in [("ecto_b", "gs_ectoplasm", (-4.0, 0, -2.0)), ("stain_c", "gs_stain", (4.0, 0, -1.8)),
                            ("ecto_c", "gs_ectoplasm", (-3.2, 0, 3.0)), ("stain_d", "gs_stain", (3.2, 0, 3.4)),
                            ("stain_e", "gs_stain", (0.0, 0, -4.5)), ("ecto_d", "gs_ectoplasm", (-5.0, 0, 0.8))]:
        model(name, kind, pos, tags=("Lookdev", "Contamination", "Kind:Ecto" if kind == "gs_ectoplasm" else "Kind:Stain", "Glow:" + name))
    for name, pos in [("dust_a", (-1.5, 0, 2.0)), ("dust_b", (1.0, 0, 3.8)), ("dust_c", (4.8, 0, 1.6))]:
        model(name, "gs_dust", pos, tags=("Lookdev", "Contamination", "Kind:Dust"))
    model("van", "gs_van", (-3.5, 0, 4.7), tags=("Lookdev", "Van"))
    # Lights. Moon: a cool directional light shining in through the window (-Z side, pointing +Z and down).
    moon_rot = qmul(quat("y", 180), quat("x", -40))
    add("moon", {"Transform": tf((0, 5, -8), moon_rot), "Tags": {"$v": 1, "names": ["Lookdev", "Light"]},
                 "DirectionalLight": {"$v": 1, "color": [0.45, 0.6, 1.0], "intensity": 4.0, "cast_shadows": True}})
    point("light_lamp_left", (-4.5, 1.7, -4.5), (1.0, 0.62, 0.25), 18.0, 9.0)
    point("light_lamp_right", (4.5, 1.7, -4.5), (1.0, 0.62, 0.25), 18.0, 9.0)
    point("light_ghost", (-1.8, 1.7, -3.8), (0.2, 0.85, 1.0), 14.0, 7.0)
    point("light_vacuum", (0.2, 1.0, -1.5), (0.2, 0.9, 1.0), 4.0, 3.0)
    point("light_stain_a", (-2.4, 0.5, -0.5), (0.7, 0.2, 1.0), 6.0, 4.0, "stain_a")
    point("light_stain_b", (2.6, 0.5, 0.8), (0.7, 0.2, 1.0), 6.0, 4.0, "stain_b")
    point("light_ecto", (1.8, 0.6, -3.0), (0.2, 1.0, 0.45), 6.0, 4.0, "ecto_a")
    for name, color, pos in [("ecto_b", (0.2, 1.0, 0.45), (-4.0, 0.6, -2.0)), ("stain_c", (0.7, 0.2, 1.0), (4.0, 0.5, -1.8)),
                             ("ecto_c", (0.2, 1.0, 0.45), (-3.2, 0.6, 3.0)), ("stain_d", (0.7, 0.2, 1.0), (3.2, 0.5, 3.4)),
                             ("stain_e", (0.7, 0.2, 1.0), (0.0, 0.5, -4.5)), ("ecto_d", (0.2, 1.0, 0.45), (-5.0, 0.6, 0.8))]:
        point("light_" + name, pos, color, 6.0, 4.0, name)

    # Gameplay: the ghost patrols, and a director entity runs the contract.
    for e in ents:
        if e["guid"] == gid("entity:ghost_prankster"):
            e["components"]["ScriptComponent"] = {"$v": 1, "script": gid("script:GhostPatrol"), "properties": []}

    def write(path, autoclean):
        out = dict(scene)
        director = {"guid": gid("entity:director"), "components": {
            "Transform": tf((0, 0, 0)), "Tags": {"$v": 1, "names": ["Director"]},
            "WidgetComponent": {"$v": 1, "layout": "UI/Hud.aui", "z": 0, "visible": True},
            "ScriptComponent": {"$v": 1, "script": gid("script:Contract"),
                                "properties": [{"name": "autoclean", "value": "true"}] if autoclean else []}}}
        out["entities"] = ents + [director]
        with open(path, "w", newline="\n") as f:
            json.dump(out, f, indent=2, sort_keys=True)
            f.write("\n")

    write(scene_path, False)
    write(os.path.join(os.path.dirname(scene_path), "M1_Test.ascene"), True)

    # Fixed-camera lookdev shots: the player controller and the ghost's patrol are removed so frames are comparable.
    def shot(name, eye, target, fov=60.0):
        out = json.loads(json.dumps(scene))
        dx, dy, dz = target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]
        yaw = math.atan2(-dx, -dz)                       # the camera looks down -Z at yaw 0
        pitch = math.atan2(dy, math.hypot(dx, dz))
        qy, qx = quat("y", math.degrees(yaw)), quat("x", math.degrees(pitch))
        rot = qmul(qy, qx)
        ents2 = []
        for e in ents:
            e = json.loads(json.dumps(e))
            c = e["components"]
            if "Player" in c.get("Tags", {}).get("names", []):
                c["Transform"] = tf(eye, rot)
                c.pop("ScriptComponent", None)
                c["Camera"]["fov_degrees"] = fov
            if e["guid"] == gid("entity:ghost_prankster"):
                c.pop("ScriptComponent", None)
            ents2.append(e)
        out["entities"] = ents2
        with open(os.path.join(os.path.dirname(scene_path), name + ".ascene"), "w", newline="\n") as f:
            json.dump(out, f, indent=2, sort_keys=True)
            f.write("\n")

    # Playable test: the real first-person controller, starting near the ectoplasm and facing it (so a held Clean button works).
    def play(name, eye):
        out = json.loads(json.dumps(scene))
        out["entities"] = [json.loads(json.dumps(e)) for e in ents]
        out["entities"].append({"guid": gid("entity:director"), "components": {
            "Transform": tf((0, 0, 0)), "Tags": {"$v": 1, "names": ["Director"]},
            "WidgetComponent": {"$v": 1, "layout": "UI/Hud.aui", "z": 0, "visible": True},
            "ScriptComponent": {"$v": 1, "script": gid("script:Contract"), "properties": []}}})
        for e in out["entities"]:
            if "Player" in e["components"].get("Tags", {}).get("names", []):
                e["components"]["Transform"]["position"] = list(eye)
        with open(os.path.join(os.path.dirname(scene_path), name + ".ascene"), "w", newline="\n") as f:
            json.dump(out, f, indent=2, sort_keys=True)
            f.write("\n")

    play("Play_Clean", (1.8, 1.7, -0.3))
    shot("Lookdev_A", (1.5, 1.7, 5.5), (-0.5, 1.0, -4.0))   # wide: the window wall, lamps, stains
    shot("Lookdev_C", (0.0, 1.6, 0.5), (-3.5, 1.0, 4.7), 60.0)  # the van, from inside the room
    shot("Lookdev_D", (0.0, 2.6, 5.0), (0.0, 0.0, 2.0), 70.0)  # looking down at the dust near the front
    shot("Lookdev_B", (1.8, 1.5, 0.6), (0.0, 0.9, -1.8), 55.0)  # close: the table with the vacuum, the ghost behind
    print("scene: %d entities (+ director)" % len(ents))


if __name__ == "__main__":
    main(sys.argv[1])

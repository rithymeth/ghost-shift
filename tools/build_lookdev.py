#!/usr/bin/env python3
"""Generates the Ghost Shift lookdev blockout models as self-contained glTF 2.0 files.

These are PROCEDURAL BLOCKOUTS used to judge lighting, emissive glow and scale in the Aether
renderer. They are not final art. Final models replace them (see docs / PLAN).

usage: build_lookdev.py <Content/models dir>
"""
import base64, json, math, struct, sys, os


def box(sx, sy, sz, ox=0.0, oy=0.0, oz=0.0):
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    P, N, T, I = [], [], [], []
    for n, u, v in faces:
        base = len(P)
        for cu, cv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [(n[k] + u[k] * cu + v[k] * cv) * 0.5 for k in range(3)]
            P.append((p[0] * sx + ox, p[1] * sy + oy, p[2] * sz + oz))
            N.append(n)
            T.append(((cu + 1) * 0.5, 1 - (cv + 1) * 0.5))
        I += [base, base + 1, base + 2, base, base + 2, base + 3]
    return P, N, T, I


def sphere(r, ox=0.0, oy=0.0, oz=0.0, sy=1.0, rings=16, segs=24):
    P, N, T, I = [], [], [], []
    for i in range(rings + 1):
        a = math.pi * i / rings
        for j in range(segs + 1):
            b = 2 * math.pi * j / segs
            n = (math.sin(a) * math.cos(b), math.cos(a), math.sin(a) * math.sin(b))
            P.append((n[0] * r + ox, n[1] * r * sy + oy, n[2] * r + oz))
            N.append(n)
            T.append((j / segs, i / rings))
    for i in range(rings):
        for j in range(segs):
            a = i * (segs + 1) + j
            b = a + segs + 1
            I += [a, b, a + 1, a + 1, b, b + 1]
    return P, N, T, I


def cylinder(r, h, ox=0.0, oy=0.0, oz=0.0, axis="y", segs=24):
    P, N, T, I = [], [], [], []

    def tr(x, y, z):
        if axis == "x":
            return (y + ox, x + oy, z + oz)  # axis along X
        if axis == "z":
            return (x + ox, z + oy, y + oz)  # axis along Z
        return (x + ox, y + oy, z + oz)

    def trn(x, y, z):
        if axis == "x":
            return (y, x, z)
        if axis == "z":
            return (x, z, y)
        return (x, y, z)

    for j in range(segs + 1):
        b = 2 * math.pi * j / segs
        c, s = math.cos(b), math.sin(b)
        for y, v in ((-h / 2, 1.0), (h / 2, 0.0)):
            P.append(tr(c * r, y, s * r))
            N.append(trn(c, 0, s))
            T.append((j / segs, v))
    for j in range(segs):
        a = j * 2
        I += [a, a + 1, a + 2, a + 1, a + 3, a + 2]
    for y, ny in ((h / 2, 1), (-h / 2, -1)):
        c0 = len(P)
        P.append(tr(0, y, 0)); N.append(trn(0, ny, 0)); T.append((0.5, 0.5))
        for j in range(segs + 1):
            b = 2 * math.pi * j / segs
            P.append(tr(math.cos(b) * r, y, math.sin(b) * r)); N.append(trn(0, ny, 0))
            T.append((0.5 + 0.5 * math.cos(b), 0.5 + 0.5 * math.sin(b)))
        for j in range(segs):
            if ny > 0:
                I += [c0, c0 + j + 2, c0 + j + 1]
            else:
                I += [c0, c0 + j + 1, c0 + j + 2]
    return P, N, T, I


def mat(name, rgb, metal=0.0, rough=0.7, emissive=None, strength=1.0):
    m = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": [rgb[0], rgb[1], rgb[2], 1.0],
                                                "metallicFactor": metal, "roughnessFactor": rough}}
    if emissive:
        m["emissiveFactor"] = list(emissive)
        if strength != 1.0:
            m["extensions"] = {"KHR_materials_emissive_strength": {"emissiveStrength": strength}}
    return m


def write_gltf(path, name, parts):
    """parts: list of (geometry, material dict)."""
    blob = bytearray()
    views, accessors, prims, materials = [], [], [], []
    used_ext = set()

    def add(data, target):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    for (P, N, T, I), m in parts:
        mi = len(materials)
        materials.append(m)
        if "extensions" in m:
            used_ext.update(m["extensions"].keys())
        pos = b"".join(struct.pack("<3f", *p) for p in P)
        nor = b"".join(struct.pack("<3f", *n) for n in N)
        uvs = b"".join(struct.pack("<2f", *t) for t in T)
        idx = b"".join(struct.pack("<I", i) for i in I)
        mn = [min(p[k] for p in P) for k in range(3)]
        mx = [max(p[k] for p in P) for k in range(3)]
        a0 = len(accessors)
        accessors.append({"bufferView": add(pos, 34962), "componentType": 5126, "count": len(P), "type": "VEC3", "min": mn, "max": mx})
        accessors.append({"bufferView": add(nor, 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
        accessors.append({"bufferView": add(uvs, 34962), "componentType": 5126, "count": len(T), "type": "VEC2"})
        accessors.append({"bufferView": add(idx, 34963), "componentType": 5125, "count": len(I), "type": "SCALAR"})
        prims.append({"attributes": {"POSITION": a0, "NORMAL": a0 + 1, "TEXCOORD_0": a0 + 2}, "indices": a0 + 3, "material": mi, "mode": 4})
    doc = {"asset": {"version": "2.0", "generator": "ghost-shift lookdev blockout"},
           "buffers": [{"uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(blob)).decode(), "byteLength": len(blob)}],
           "bufferViews": views, "accessors": accessors, "materials": materials,
           "meshes": [{"name": name, "primitives": prims}], "nodes": [{"name": name, "mesh": 0}],
           "scenes": [{"nodes": [0]}], "scene": 0}
    if used_ext:
        doc["extensionsUsed"] = sorted(used_ext)
    with open(path, "w", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


def main(out):
    os.makedirs(out, exist_ok=True)
    W = lambda n, parts: write_gltf(os.path.join(out, n + ".gltf"), n, parts)
    wood = mat("DarkWood", (0.17, 0.09, 0.05), 0.0, 0.55)
    plaster = mat("Plaster", (0.28, 0.30, 0.42), 0.0, 0.85)
    trim = mat("Trim", (0.10, 0.07, 0.12), 0.0, 0.5)
    W("gs_floor", [(box(4, 0.2, 4, 0, -0.1, 0), wood)])
    W("gs_wall", [(box(4, 3, 0.2, 0, 1.5, 0), plaster), (box(4, 0.15, 0.25, 0, 0.075, 0), trim)])
    # A real opening (no glass): moonlight can enter, and the sky shows through. Frame trim around the hole.
    W("gs_wall_window", [(box(4, 0.9, 0.2, 0, 0.45, 0), plaster), (box(4, 0.9, 0.2, 0, 2.55, 0), plaster),
                         (box(1.0, 1.2, 0.2, -1.5, 1.5, 0), plaster), (box(1.0, 1.2, 0.2, 1.5, 1.5, 0), plaster),
                         (box(2.1, 0.08, 0.3, 0, 0.94, 0.02), trim), (box(2.1, 0.08, 0.3, 0, 2.06, 0.02), trim),
                         (box(0.08, 1.2, 0.3, -1.0, 1.5, 0.02), trim), (box(0.08, 1.2, 0.3, 1.0, 1.5, 0.02), trim),
                         (box(0.04, 1.12, 0.06, 0, 1.5, 0), trim), (box(2.0, 0.04, 0.06, 0, 1.5, 0), trim)])
    W("gs_table", [(box(1.4, 0.08, 0.8, 0, 0.76, 0), wood)] + [(box(0.08, 0.76, 0.08, sx * 0.62, 0.38, sz * 0.32), wood) for sx in (-1, 1) for sz in (-1, 1)])
    W("gs_lamp", [(cylinder(0.04, 1.6, 0, 0.8, 0), trim), (sphere(0.18, 0, 1.7, 0), mat("LampGlow", (1, 0.8, 0.5), 0.0, 0.3, (1.0, 0.62, 0.25), 8.0))])
    # The cleaning van: where the vacuum's tank is emptied (blockout: a white box, a cyan stripe and a dark roof rack).
    van_body = mat("VanBody", (0.85, 0.87, 0.9), 0.1, 0.45)
    van_stripe = mat("VanStripe", (0.05, 0.4, 0.5), 0.0, 0.3, (0.1, 0.85, 1.0), 2.5)
    W("gs_van", [(box(1.8, 1.5, 2.4, 0, 0.95, 0), van_body), (box(1.82, 0.12, 2.42, 0, 1.0, 0), van_stripe),
                 (box(1.6, 0.1, 2.0, 0, 1.75, 0), trim), (box(0.3, 0.3, 0.3, -0.7, 0.2, 0.9), trim), (box(0.3, 0.3, 0.3, 0.7, 0.2, 0.9), trim),
                 (box(0.3, 0.3, 0.3, -0.7, 0.2, -0.9), trim), (box(0.3, 0.3, 0.3, 0.7, 0.2, -0.9), trim)])
    W("gs_ectoplasm", [(cylinder(0.55, 0.03, 0, 0.015, 0), mat("Ecto", (0.1, 0.5, 0.2), 0.0, 0.15, (0.2, 1.0, 0.45), 2.5))])
    W("gs_stain", [(cylinder(0.45, 0.02, 0, 0.011, 0), mat("CursedStain", (0.25, 0.05, 0.4), 0.0, 0.3, (0.7, 0.2, 1.0), 2.0))])
    # Prankster: glowing cyan ghost with a hat (see concept sheet).
    ghost = mat("GhostBody", (0.05, 0.35, 0.45), 0.0, 0.25, (0.1, 0.75, 1.0), 1.6)
    hat = mat("GhostHat", (0.12, 0.06, 0.25), 0.0, 0.5)
    eye = mat("GhostEye", (1, 1, 1), 0.0, 0.1, (1.0, 1.0, 1.0), 6.0)
    W("gs_ghost_prankster", [(sphere(0.45, 0, 1.3, 0, 1.2), ghost), (sphere(0.3, 0, 0.75, 0, 1.6), ghost),
                             (cylinder(0.5, 0.05, 0, 1.68, 0), hat), (cylinder(0.28, 0.3, 0, 1.85, 0), hat),
                             (sphere(0.07, -0.16, 1.35, 0.38), eye), (sphere(0.07, 0.16, 1.35, 0.38), eye)])
    # Spectral vacuum: body, tank, glowing lens (see concept sheet).
    yellow = mat("VacYellow", (1.0, 0.75, 0.05), 0.1, 0.4)
    steel = mat("VacSteel", (0.2, 0.2, 0.24), 0.9, 0.3)
    lens = mat("VacLens", (0.1, 0.4, 0.5), 0.0, 0.1, (0.2, 0.9, 1.0), 8.0)
    W("gs_vacuum", [(cylinder(0.09, 0.8, 0, 0, 0, "z"), steel), (cylinder(0.14, 0.18, 0, 0, -0.45, "z"), steel),
                    (cylinder(0.105, 0.04, 0, 0, -0.5, "z"), lens), (box(0.14, 0.2, 0.3, 0, -0.12, 0.1), yellow),
                    (cylinder(0.13, 0.5, 0, 0.1, 0.45, "z"), yellow)])
    print("wrote", len(os.listdir(out)), "models to", out)


if __name__ == "__main__":
    main(sys.argv[1])

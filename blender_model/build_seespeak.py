# ============================================================================
# SeeSpeak - สคริปต์สร้างโมเดล 3D ใน Blender อัตโนมัติ (Blender 4.2+ / 5.x)
# ----------------------------------------------------------------------------
# วิธีใช้ (ไม่ต้องใช้ MCP):
#   1) เปิดผ่าน Command line:
#      "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" -b -P build_seespeak.py -- <โฟลเดอร์ผลลัพธ์> [norender]
#   2) หรือเปิด Blender > แท็บ Scripting > Open ไฟล์นี้ > กด Run Script
# หน่วย: 1 Blender Unit = 1 มิลลิเมตร
# แกน: X = ความยาว (กล้องอยู่ด้าน -X), Y = ความกว้าง (ด้านหลังมีลำโพง/USB-C อยู่ +Y), Z = ความสูง (ปุ่มอยู่ด้านบน +Z)
# ============================================================================

import bpy
import bmesh
import math
import os
import sys
from mathutils import Vector, Matrix, Euler

# ----------------------------------------------------------------------------
# อาร์กิวเมนต์
# ----------------------------------------------------------------------------
argv = sys.argv
args = argv[argv.index("--") + 1:] if "--" in argv else []
try:
    _here = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _here = bpy.path.abspath("//") or os.getcwd()
OUT_DIR = args[0] if args else _here
DO_RENDER = not (len(args) > 1 and args[1] == "norender")
RENDER_DIR = os.path.join(OUT_DIR, "renders")
os.makedirs(RENDER_DIR, exist_ok=True)

# ----------------------------------------------------------------------------
# พารามิเตอร์ขนาด (มม.) - ปรับได้
# ----------------------------------------------------------------------------
L = 72.0          # ความยาวรวมตัวเครื่อง (ไม่รวมเลนส์ที่ยื่น)
W = 26.0          # ความกว้าง (หน้า-หลัง)
H = 24.0          # ความสูง
R = 5.0           # รัศมีมุมโค้งหน้าตัด
CAP_LEN = 7.0     # ความยาวฝาปิดหัว-ท้าย
GAP = 0.2         # ร่องรอยต่อระหว่างฝากับตัวกลาง
CAP_GROW = 0.25   # ฝาหัว-ท้ายใหญ่กว่าตัวกลางเล็กน้อย

LMID = L - 2 * CAP_LEN - 2 * GAP
X_FRONT = -L / 2
TOP = H / 2
PANEL_DEPTH = 0.3
Z_PANEL = TOP - PANEL_DEPTH

# ----------------------------------------------------------------------------
# ล้างฉาก
# ----------------------------------------------------------------------------
for ob in list(bpy.data.objects):
    bpy.data.objects.remove(ob, do_unlink=True)
for coll in list(bpy.data.collections):
    bpy.data.collections.remove(coll)
for blk in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
    for d in list(blk):
        blk.remove(d)

scene = bpy.context.scene
COLL = bpy.data.collections.new("SeeSpeak_Model")
scene.collection.children.link(COLL)
CUT = bpy.data.collections.new("_cutters")
scene.collection.children.link(CUT)
SETUP = bpy.data.collections.new("Scene_Setup")
scene.collection.children.link(SETUP)


# ----------------------------------------------------------------------------
# ฟังก์ชันช่วยสร้างรูปทรง
# ----------------------------------------------------------------------------
def new_obj(name, mesh, coll=None):
    ob = bpy.data.objects.new(name, mesh)
    (coll or COLL).objects.link(ob)
    return ob


def rr_profile(a, b, r, segs):
    r = min(r, a / 2, b / 2)
    pts = []
    corners = [(a / 2 - r, b / 2 - r, 0), (-a / 2 + r, b / 2 - r, 90),
               (-a / 2 + r, -b / 2 + r, 180), (a / 2 - r, -b / 2 + r, 270)]
    for cx, cy, st in corners:
        for i in range(segs + 1):
            t = math.radians(st + 90 * i / segs)
            p = (cx + r * math.cos(t), cy + r * math.sin(t))
            if not pts or abs(p[0] - pts[-1][0]) > 1e-6 or abs(p[1] - pts[-1][1]) > 1e-6:
                pts.append(p)
    if abs(pts[0][0] - pts[-1][0]) < 1e-6 and abs(pts[0][1] - pts[-1][1]) < 1e-6:
        pts.pop()
    return pts


def map_axis(u, v, t, axis):
    if axis == 'X':
        return (t, u, v)
    if axis == 'Y':
        return (u, t, v)
    return (u, v, t)


def prism(name, a, b, r, depth, axis='Z', loc=(0, 0, 0), segs=12, coll=None):
    """แท่งสี่เหลี่ยมมุมมน: หน้าตัด a x b (รัศมี r) ยื่นไปตามแกน axis ยาว depth"""
    prof = rr_profile(a, b, r, segs)
    bm = bmesh.new()
    lo = [bm.verts.new(map_axis(u, v, -depth / 2, axis)) for u, v in prof]
    hi = [bm.verts.new(map_axis(u, v, depth / 2, axis)) for u, v in prof]
    n = len(prof)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = new_obj(name, me, coll)
    ob.location = loc
    return ob


def _axis_rot(bm, axis):
    if axis == 'X':
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, 'Y'))
    elif axis == 'Y':
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(90), 3, 'X'))


def cyl(name, r, depth, axis='Z', loc=(0, 0, 0), segs=64, coll=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segs,
                          radius1=r, radius2=r, depth=depth)
    _axis_rot(bm, axis)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = new_obj(name, me, coll)
    ob.location = loc
    return ob


def box(name, sx, sy, sz, loc=(0, 0, 0), coll=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(sx, sy, sz), verts=bm.verts)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = new_obj(name, me, coll)
    ob.location = loc
    return ob


def sphere(name, r, loc=(0, 0, 0), scale=(1, 1, 1), coll=None):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=64, v_segments=32, radius=r)
    bmesh.ops.scale(bm, vec=scale, verts=bm.verts)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = new_obj(name, me, coll)
    ob.location = loc
    return ob


def torus(name, R_major, r_minor, axis='Z', loc=(0, 0, 0), zscale=1.0, coll=None):
    bm = bmesh.new()
    seg_u, seg_v = 64, 16
    rings = []
    for i in range(seg_u):
        a = 2 * math.pi * i / seg_u
        ring = []
        for j in range(seg_v):
            b = 2 * math.pi * j / seg_v
            x = (R_major + r_minor * math.cos(b)) * math.cos(a)
            y = (R_major + r_minor * math.cos(b)) * math.sin(a)
            z = r_minor * math.sin(b) * zscale
            ring.append(bm.verts.new((x, y, z)))
        rings.append(ring)
    for i in range(seg_u):
        for j in range(seg_v):
            i2, j2 = (i + 1) % seg_u, (j + 1) % seg_v
            bm.faces.new((rings[i][j], rings[i2][j], rings[i2][j2], rings[i][j2]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    _axis_rot(bm, axis)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = new_obj(name, me, coll)
    ob.location = loc
    return ob


def bevel(ob, width, segs=3, angle=30):
    m = ob.modifiers.new("Bevel", 'BEVEL')
    m.width = width
    m.segments = segs
    m.limit_method = 'ANGLE'
    m.angle_limit = math.radians(angle)
    return m


def cut(target, cutter=None, collection=None, transfer=False):
    m = target.modifiers.new("Bool", 'BOOLEAN')
    m.operation = 'DIFFERENCE'
    if collection is not None:
        m.operand_type = 'COLLECTION'
        m.collection = collection
    else:
        m.object = cutter
    try:
        m.solver = 'EXACT'
    except Exception:
        pass
    if transfer:
        try:
            m.material_mode = 'TRANSFER'
        except Exception:
            pass
    return m


def apply_mods(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)


def smooth(ob, angle=32):
    me = ob.data
    try:
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(angle))
    except Exception:
        for p in me.polygons:
            p.use_smooth = True


def set_mat(ob, mat):
    ob.data.materials.clear()
    ob.data.materials.append(mat)


# ----------------------------------------------------------------------------
# วัสดุ
# ----------------------------------------------------------------------------
def make_mat(name, color, rough=0.5, metal=0.0, coat=0.0, extra=None, bump=0.0, bump_scale=25.0):
    m = bpy.data.materials.new(name)
    try:
        m.use_nodes = True
    except Exception:
        pass
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    out.location = (300, 0)
    nt.links.new(bsdf.outputs[0], out.inputs['Surface'])

    def s(k, v):
        if k in bsdf.inputs:
            bsdf.inputs[k].default_value = v

    s("Base Color", (*color, 1.0))
    s("Roughness", rough)
    s("Metallic", metal)
    s("Coat Weight", coat)
    for k, v in (extra or {}).items():
        s(k, v)
    if bump > 0:
        noise = nt.nodes.new('ShaderNodeTexNoise')
        noise.inputs['Scale'].default_value = bump_scale
        noise.inputs['Detail'].default_value = 6.0
        bmp = nt.nodes.new('ShaderNodeBump')
        bmp.inputs['Strength'].default_value = bump
        bmp.inputs['Distance'].default_value = 0.02
        nt.links.new(noise.outputs['Fac'], bmp.inputs['Height'])
        nt.links.new(bmp.outputs['Normal'], bsdf.inputs['Normal'])
        noise.location = (-500, -200)
        bmp.location = (-250, -200)
    m.diffuse_color = (*color, 1.0)
    return m


M_BODY = make_mat("Body_MatteBlack", (0.009, 0.009, 0.010), rough=0.5, bump=0.12)
M_CAP = make_mat("Cap_SatinBlack", (0.007, 0.007, 0.008), rough=0.35, bump=0.05)
M_BEZEL = make_mat("Bezel_GlossBlack", (0.006, 0.006, 0.007), rough=0.22)
M_BUTTON = make_mat("Button_Rubber", (0.012, 0.012, 0.013), rough=0.6, bump=0.2, bump_scale=60)
M_DARK = make_mat("Hole_Dark", (0.003, 0.003, 0.003), rough=0.8)
M_SLOT = make_mat("Slot_Gloss", (0.006, 0.006, 0.007), rough=0.22)
M_GUN = make_mat("Metal_Gunmetal", (0.12, 0.12, 0.13), rough=0.28, metal=1.0)
M_CHROME = make_mat("Metal_Chrome", (0.75, 0.75, 0.77), rough=0.12, metal=1.0)
M_ICON = make_mat("Icon_Print", (0.45, 0.45, 0.47), rough=0.5)
M_TONGUE = make_mat("USB_Tongue", (0.03, 0.03, 0.035), rough=0.4)
M_GLASS = make_mat("Lens_Glass", (0.004, 0.004, 0.03), rough=0.03, coat=1.0,
                   extra={"Coat Roughness": 0.02, "Thin Film Thickness": 560.0,
                          "Thin Film IOR": 1.38, "Specular IOR Level": 0.7})
M_FLOOR = make_mat("Floor", (0.8, 0.8, 0.82), rough=0.7)

# ============================================================================
# 1) ตัวกลาง (Main Body)
# ============================================================================
body = prism("Body_Main", W, H, R, LMID, axis='X', loc=(0, 0, 0), segs=14)
set_mat(body, M_BODY)
bevel(body, 0.8, segs=3)

# --- ด้านบน: แผงยุบตื้น ---
c_panel_top = prism("cut_panel_top", LMID - 6, 14.5, 2.5, 2.0, 'Z', (0, 0, TOP + 1 - PANEL_DEPTH), coll=CUT)
cut(body, c_panel_top)
# --- ด้านล่าง: แผงยุบตื้น (ตำแหน่งแผ่นแม่เหล็ก) ---
c_panel_bot = prism("cut_panel_bot", LMID - 9, 14.5, 2.0, 2.0, 'Z', (0, 0, -TOP - 1 + PANEL_DEPTH), coll=CUT)
cut(body, c_panel_bot)

# --- ปุ่มกด: เบ้ารอบปุ่ม ---
BTN_X = -16.5
c_btn = cyl("cut_button_ring", 5.6, 2.0, 'Z', (BTN_X, 0, Z_PANEL + 1 - 0.5), coll=CUT)
cut(body, c_btn)

# --- สวิตช์เลื่อน: ร่อง ---
SW_X = 3.5
c_slot = prism("cut_slot", 15.0, 6.6, 3.3, 4.0, 'Z', (SW_X, 0, Z_PANEL + 2 - 1.4), coll=CUT)
set_mat(c_slot, M_SLOT)
cut(body, c_slot, transfer=True)

# --- จุดไฟสถานะด้านบน ---
c_dot = cyl("cut_top_dot", 0.5, 2.0, 'Z', (-LMID / 2 + 1.8, -4.5, TOP + 1 - 0.6), segs=24, coll=CUT)
set_mat(c_dot, M_DARK)
cut(body, c_dot, transfer=True)

# --- ด้านหลัง (+Y): ตะแกรงลำโพง ---
GRILLE = bpy.data.collections.new("_cut_grille")
scene.collection.children.link(GRILLE)
GX, SP = 15.0, 2.3
for row, (z, count) in enumerate([(2.3, 5), (0.0, 6), (-2.3, 5)]):
    for i in range(count):
        x = GX + (i - (count - 1) / 2) * SP
        h = cyl(f"cut_grille_{row}_{i}", 0.62, 2.4, 'Y', (x, W / 2, z), segs=24, coll=GRILLE)
        set_mat(h, M_DARK)
cut(body, collection=GRILLE, transfer=True)

# --- ด้านหลัง: ช่อง USB-C ---
USB_X = -4.0
c_usb = prism("cut_usbc", 9.4, 3.6, 1.8, 14.0, 'Y', (USB_X, W / 2, 0), segs=12, coll=CUT)
set_mat(c_usb, M_DARK)
cut(body, c_usb, transfer=True)

# --- ด้านหลัง: รูไมโครโฟน ---
c_mic = cyl("cut_mic", 0.55, 2.4, 'Y', (-14.5, W / 2, 0), segs=24, coll=CUT)
set_mat(c_mic, M_DARK)
cut(body, c_mic, transfer=True)

apply_mods(body)
smooth(body)

# ============================================================================
# 2) ฝาหัว (มีกล้อง) และฝาท้าย
# ============================================================================
cap_f = prism("Cap_Front", W + CAP_GROW, H + CAP_GROW, R + CAP_GROW / 2, CAP_LEN, 'X',
              (X_FRONT + CAP_LEN / 2, 0, 0), segs=14)
set_mat(cap_f, M_CAP)
bevel(cap_f, 0.7, segs=4)
# รูไฟแฟลช/ไฟสถานะข้างเลนส์ (ฝั่ง -Y)
c_led = cyl("cut_front_led", 0.7, 2.0, 'X', (X_FRONT, -11.0, 0), segs=24, coll=CUT)
set_mat(c_led, M_DARK)
cut(cap_f, c_led, transfer=True)
apply_mods(cap_f)
smooth(cap_f)

cap_r = prism("Cap_Rear", W + CAP_GROW, H + CAP_GROW, R + CAP_GROW / 2, CAP_LEN, 'X',
              (L / 2 - CAP_LEN / 2, 0, 0), segs=14)
set_mat(cap_r, M_CAP)
bevel(cap_r, 0.7, segs=4)
apply_mods(cap_r)
smooth(cap_r)

# ============================================================================
# 3) ชุดกล้องด้านหน้า
# ============================================================================
BEZ_D = 3.0
BEZ_PROTRUDE = 1.6
bez_x = X_FRONT - BEZ_PROTRUDE + BEZ_D / 2
bezel = prism("Camera_Bezel", 17.0, 16.0, 4.5, BEZ_D, 'X', (bez_x, 0, 0), segs=12)
set_mat(bezel, M_BEZEL)
bevel(bezel, 0.6, segs=4)
apply_mods(bezel)
smooth(bezel)

x_bez_face = X_FRONT - BEZ_PROTRUDE
ring = cyl("Lens_Ring", 6.3, 1.6, 'X', (x_bez_face - 0.4, 0, 0))
set_mat(ring, M_GUN)
bevel(ring, 0.3, segs=3)
apply_mods(ring)
smooth(ring)

x_ring_face = x_bez_face - 1.2
disk = cyl("Lens_InnerBlack", 5.3, 0.3, 'X', (x_ring_face - 0.1, 0, 0))
set_mat(disk, M_DARK)
smooth(disk)

inner_ring = torus("Lens_ChromeRing", 4.85, 0.18, 'X', (x_ring_face - 0.25, 0, 0))
set_mat(inner_ring, M_CHROME)
smooth(inner_ring, 80)

glass = sphere("Lens_Glass", 4.6, (x_ring_face - 0.2, 0, 0), scale=(0.32, 1, 1))
set_mat(glass, M_GLASS)
smooth(glass, 80)

# ============================================================================
# 4) ปุ่มกด + สวิตช์เลื่อน + ไอคอนด้านบน
# ============================================================================
button = cyl("Button", 4.7, 2.2, 'Z', (BTN_X, 0, Z_PANEL - 0.5 + 1.1))
set_mat(button, M_BUTTON)
bevel(button, 0.6, segs=4)
apply_mods(button)
smooth(button)

knob = prism("Switch_Knob", 6.0, 5.0, 1.2, 2.4, 'Z', (SW_X - 3.6, 0, Z_PANEL - 1.4 + 1.2), segs=8)
set_mat(knob, M_BUTTON)
bevel(knob, 0.4, segs=3)
for k, dx in enumerate((-0.9, 0.9)):
    g = box(f"cut_knob_groove_{k}", 0.35, 6.0, 1.0, (SW_X - 3.6 + dx, 0, Z_PANEL + 1.0), coll=CUT)
    cut(knob, g)
apply_mods(knob)
smooth(knob)

ICON_Z = Z_PANEL


def lock_icon(name, x, y, opened=False):
    # ไอคอนแม่กุญแจพิมพ์บนผิว (ด้านบนของไอคอนชี้ไปทาง -X เหมือนในรูป)
    b = box(name + "_body", 1.1, 1.3, 0.1, (x + 0.55, y, ICON_Z))
    set_mat(b, M_ICON)
    sx = x - (0.25 if opened else 0.0)
    sy = y + (0.35 if opened else 0.0)
    t = torus(name + "_shackle", 0.42, 0.1, 'Z', (sx, sy, ICON_Z), zscale=0.5)
    set_mat(t, M_ICON)
    smooth(t, 80)


lock_icon("Icon_Lock", SW_X + 10.0, -2.2, opened=False)
lock_icon("Icon_Unlock", SW_X + 10.0, 2.2, opened=True)

# ไอคอนแตะนิ้ว (สัญลักษณ์อย่างง่าย: วงแหวน + จุด)
tap_ring = torus("Icon_Tap_Ring", 0.75, 0.09, 'Z', (BTN_X + 8.2, 0, ICON_Z), zscale=0.5)
set_mat(tap_ring, M_ICON)
tap_dot = cyl("Icon_Tap_Dot", 0.3, 0.1, 'Z', (BTN_X + 8.2, 0, ICON_Z), segs=24)
set_mat(tap_dot, M_ICON)

# ============================================================================
# 5) พอร์ต USB-C (ขอบโลหะ + ลิ้นด้านใน)
# ============================================================================
shell = prism("USBC_Shell", 9.4, 3.6, 1.8, 6.0, 'Y', (USB_X, W / 2 - 3.1, 0), segs=12)
set_mat(shell, M_CHROME)
c_shell = prism("cut_usbc_inner", 8.6, 2.8, 1.4, 8.0, 'Y', (USB_X, W / 2 - 3.1, 0), segs=12, coll=CUT)
cut(shell, c_shell)
apply_mods(shell)
smooth(shell)

back_plate = box("USBC_Back", 9.0, 0.5, 3.2, (USB_X, W / 2 - 5.8, 0))
set_mat(back_plate, M_DARK)
tongue = box("USBC_Tongue", 6.4, 4.4, 0.7, (USB_X, W / 2 - 3.4, 0))
set_mat(tongue, M_TONGUE)

# ============================================================================
# ลบตัวตัด (cutters) และจัดกลุ่ม
# ============================================================================
for c in (CUT, GRILLE):
    for ob in list(c.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.data.collections.remove(c)

root = bpy.data.objects.new("SeeSpeak_Device", None)
root.empty_display_type = 'PLAIN_AXES'
root.empty_display_size = 20
COLL.objects.link(root)
for ob in COLL.objects:
    if ob is not root:
        ob.parent = root

# ============================================================================
# ฉาก: พื้น ไฟ กล้อง
# ============================================================================
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.001
scene.unit_settings.length_unit = 'MILLIMETERS'

floor_z = -(H + CAP_GROW) / 2
floor = prism("Floor", 4000, 4000, 1, 1.0, 'Z', (0, 0, floor_z - 0.5), segs=1, coll=SETUP)
set_mat(floor, M_FLOOR)

world = bpy.data.worlds.new("Studio") if not scene.world else scene.world
scene.world = world
try:
    world.use_nodes = True
except Exception:
    pass
bg = world.node_tree.nodes.get("Background")
if bg is None:
    bg = world.node_tree.nodes.new("ShaderNodeBackground")
    wout = world.node_tree.nodes.get("World Output") or world.node_tree.nodes.new("ShaderNodeOutputWorld")
    world.node_tree.links.new(bg.outputs[0], wout.inputs[0])
bg.inputs[0].default_value = (0.55, 0.55, 0.57, 1)
bg.inputs[1].default_value = 0.35


def look_rot(loc, target):
    d = Vector(target) - Vector(loc)
    return d.to_track_quat('-Z', 'Y').to_euler()


def area_light(name, loc, size, energy, target=(0, 0, 0)):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.size = size
    ld.energy = energy
    ob = bpy.data.objects.new(name, ld)
    SETUP.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = look_rot(loc, target)
    return ob


area_light("Key", (-180, -220, 300), 250, 1.3e6)
area_light("Fill", (220, -250, 120), 300, 0.45e6)
area_light("Rim", (120, 260, 260), 200, 1.4e6)
area_light("Top", (0, 0, 400), 300, 0.6e6)


def camera(name, loc, target=None, rot=None, ortho=None, lens=85):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.clip_start = 1
    cd.clip_end = 20000
    if ortho:
        cd.type = 'ORTHO'
        cd.ortho_scale = ortho
    ob = bpy.data.objects.new(name, cd)
    SETUP.objects.link(ob)
    ob.location = loc
    if rot is not None:
        ob.rotation_euler = Euler([math.radians(a) for a in rot])
    else:
        ob.rotation_euler = look_rot(loc, target)
    return ob


cam_hero = camera("Cam_Hero", (-150, -195, 125), target=(1, 0, -2), lens=85)
cam_hero2 = camera("Cam_Hero_Rear", (165, 190, 115), target=(-1, 0, -2), lens=85)
cam_front = camera("Cam_Front", (-300, 0, 0), rot=(90, 0, -90), ortho=44)
cam_top = camera("Cam_Top", (0, 0, 300), rot=(0, 0, 0), ortho=90)
cam_back = camera("Cam_Back", (0, 300, 0), rot=(90, 0, 180), ortho=90)
cam_bottom = camera("Cam_Bottom", (0, 0, -300), rot=(180, 0, 0), ortho=90)
scene.camera = cam_hero

# ----------------------------------------------------------------------------
# ตั้งค่าเรนเดอร์
# ----------------------------------------------------------------------------
scene.render.engine = 'CYCLES'
try:
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
except Exception:
    pass
scene.render.resolution_x = 1400
scene.render.resolution_y = 900
scene.render.resolution_percentage = 100
try:
    scene.view_settings.view_transform = 'AgX'
except Exception:
    pass

blend_path = os.path.join(OUT_DIR, "seespeak_model.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print("[SeeSpeak] saved:", blend_path)

if DO_RENDER:
    views = [("01_hero", cam_hero, False), ("02_hero_rear", cam_hero2, False),
             ("03_front", cam_front, True), ("04_top", cam_top, False),
             ("05_back", cam_back, True), ("06_bottom", cam_bottom, True)]
    for name, cam, hide_floor in views:
        scene.camera = cam
        floor.hide_render = hide_floor
        scene.render.filepath = os.path.join(RENDER_DIR, name + ".png")
        bpy.ops.render.render(write_still=True)
        print("[SeeSpeak] rendered:", name)
    floor.hide_render = False
    scene.camera = cam_hero
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print("[SeeSpeak] done")


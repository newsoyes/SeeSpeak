# ============================================================================
# SeeSpeak - โมเดลสำหรับพิมพ์ 3D ใช้งานจริง (Blender 4.2+ / 5.x)
# ----------------------------------------------------------------------------
# รัน:  blender.exe -b --factory-startup -P build_seespeak_print.py -- <โฟลเดอร์ผลลัพธ์> [norender]
# หน่วย: 1 Blender Unit = 1 มม.
# แกน (ตำแหน่งประกอบ):  X = ความยาว (กล้องอยู่ฝั่ง -X)
#                       Y = ความกว้าง (+Y = ฝาข้างที่หันเข้าหาขาแว่น/ขมับ มีรูลำโพง + แม่เหล็ก)
#                       Z = ความสูง (+Z = ด้านบน มีปุ่มกด/สวิตช์เลื่อน, -Z = ด้านล่าง มี USB-C)
# ชิ้นงานที่พิมพ์ (แยกไฟล์ STL):
#   1) ตัวเครื่องหลัก (Body)     2) ฝาข้าง (Side Lid)     3) ปุ่มกด (Button)
#   4) ปุ่มสไลด์ (Slide Knob)   5) แผ่นยึดขาแว่น (Glasses Mount Plate)
# ============================================================================

import bpy
import bmesh
import math
import os
import sys
import struct
from mathutils import Vector, Matrix, Euler

argv = sys.argv
args = argv[argv.index("--") + 1:] if "--" in argv else []
try:
    _here = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _here = os.getcwd()
OUT_DIR = args[0] if args else _here
DO_RENDER = not (len(args) > 1 and args[1] == "norender")
RENDER_DIR = os.path.join(OUT_DIR, "renders_print")
STL_DIR = os.path.join(OUT_DIR, "stl_print")
os.makedirs(RENDER_DIR, exist_ok=True)
os.makedirs(STL_DIR, exist_ok=True)

# ============================================================================
# พารามิเตอร์ (มม.)
# ============================================================================
L, W, H = 64.0, 24.0, 27.0       # ขนาดภายนอกตัวเครื่อง (ไม่รวมกรอบกล้องที่ยื่น 1.6 มม.)
R = 4.5                          # รัศมีมุมโค้งภายนอก
WALL = 1.8                       # ความหนาผนัง
RC = 2.0                         # รัศมีมุมโพรงด้านใน
END_BEVEL = 0.7                  # ลบมุมขอบหัว-ท้าย
XO, YO, ZO = L / 2, W / 2, H / 2
XI, YI, ZI = XO - WALL, YO - WALL, ZO - WALL   # ผิวในของผนัง
YS = 8.0                         # ระนาบแยกตัวเครื่อง / ฝาข้าง
PANEL = 0.3                      # แผงยุบตื้นด้านบน/ล่าง
ZP = ZO - PANEL
CLR = 0.2                        # ระยะเผื่อ (clearance) ทั่วไป

# กล้อง OV5640 (โมดูลตัวเหลี่ยม 8.5 x 8.5 มม.)
CAM_HOLE = 8.9                   # ช่องสี่เหลี่ยมทะลุ (8.5 + เผื่อ 0.4)
CAM_TILT = 15.0                  # มุมก้มกล้อง (องศา)
CAM_Z = 1.0
BEZEL = 14.0
BEZEL_PROTRUDE = 1.6
X_BEZEL = -XO - BEZEL_PROTRUDE
SLEEVE_T = 1.2

# ไฟแฟลช LED
LED_D, LED_Y = 2.5, -8.8

# ปุ่มกด + Tactile switch 6x6 (สูงรวมก้าน 5.0 มม.)
BTN_X = -10.0
BTN_HOLE_R = 4.3
BTN_CAP_R = 4.1
BTN_FLANGE_R = 4.9
BTN_PROTRUDE = 1.5
TACT = 6.0
TACT_H = 5.0
TACT_BODY_H = 3.5
FLANGE_BOT = ZI - 2.0            # ปีกปุ่มหนา 2 มม. ใต้ผนัง
TACT_TOP = FLANGE_BOT - 0.1
TACT_BOT = TACT_TOP - TACT_H

# สวิตช์เลื่อน SS-12D00 (ตัว 8.6 x 3.6 x 3.6, ก้าน 1.5 มม. สูง 4 มม., ระยะเลื่อน 2 มม.)
SW_X = 4.0
SW_L, SW_W, SW_H = 8.6, 3.6, 3.6
LEVER, LEVER_H, TRAVEL = 1.5, 4.0, 2.0
PAD_T = 1.6
SLOT_DEPTH = 0.8
SW_TOP = ZI - PAD_T
SW_BOT = SW_TOP - SW_H
KNOB_L, KNOB_W = 7.0, 5.2
RECESS_L, RECESS_W = KNOB_L + TRAVEL + 0.4, KNOB_W + 0.8
RECESS_FLOOR = ZP - SLOT_DEPTH

# XIAO ESP32S3 Sense (ตั้งแนวตั้ง USB-C ชี้ลงด้านล่าง)
XIAO_X0, XIAO_X1 = -25.0, -15.0
XIAO_Y = -0.5
XIAO_W, XIAO_H = 17.5, 21.0
USB_X = -17.4                    # กึ่งกลางพอร์ต USB-C (ปรับให้ตรงบอร์ดจริงได้)
USB_W, USB_T = 9.4, 3.8          # ช่องทะลุ (ปลั๊ก Type-C 8.4 x 2.6)
USB_REC_W, USB_REC_T, USB_REC_D = 12.4, 6.8, 0.6   # ช่องหลบหัวสายชาร์จ

# ลำโพง 20 มม. / มอเตอร์สั่น 10x3 / แม่เหล็ก 5x2 (ติดบนฝาข้าง)
SPK_X, SPK_D, SPK_T = 16.0, 20.0, 4.5
MOT_X, MOT_D, MOT_T = -10.0, 10.0, 3.0
MAG_XS = (-19.0, -3.0)
MAG_D, MAG_T = 12.4, 2.2

# สกรูยึดฝา M2 (เกลียวปล่อย) 4 ตัว
BOSS_R = 2.4
BOSS_X, BOSS_Z = 28.0, 9.5
SCREW_PILOT_R = 0.8
SCREW_CLR_R = 1.1
SCREW_HEAD_R = 2.1

# ขอบลิ้นฝาข้าง (lip)
LIP_T, LIP_H = 0.8, 1.5

# แผ่นยึดขาแว่น
MP_L, MP_W, MP_T = 38.0, 16.0, 3.4
MP_X = (MAG_XS[0] + MAG_XS[1]) / 2

# ============================================================================
# ล้างฉาก
# ============================================================================
for ob in list(bpy.data.objects):
    bpy.data.objects.remove(ob, do_unlink=True)
for coll in list(bpy.data.collections):
    bpy.data.collections.remove(coll)
for blk in (bpy.data.meshes, bpy.data.materials, bpy.data.lights, bpy.data.cameras):
    for d in list(blk):
        blk.remove(d)

scene = bpy.context.scene


def new_coll(name, parent=None):
    c = bpy.data.collections.new(name)
    (parent or scene.collection).children.link(c)
    return c


C_WORK = new_coll("_work")
C_ASM = new_coll("01_Assembly (ประกอบแล้ว + ปุ่มวางข้างๆ)")
C_PRINT = new_coll("02_Print_Layout (วางตามทิศพิมพ์)")
C_FIT = new_coll("03_Fit_Check_Dummies (อุปกรณ์จำลอง)")
C_SETUP = new_coll("Scene_Setup")


# ============================================================================
# ฟังก์ชันสร้างรูปทรง (ฝังตำแหน่งลง mesh เสมอ, object อยู่ที่ origin)
# ============================================================================
def mesh_obj(name, bm, coll=None, M=None):
    if M is not None:
        bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    (coll or C_WORK).objects.link(ob)
    return ob


def rr_profile(a, b, r, segs):
    r = max(0.0, min(r, a / 2, b / 2))
    if r < 1e-4:
        return [(a / 2, b / 2), (-a / 2, b / 2), (-a / 2, -b / 2), (a / 2, -b / 2)]
    pts = []
    for cx, cy, st in [(a / 2 - r, b / 2 - r, 0), (-a / 2 + r, b / 2 - r, 90),
                       (-a / 2 + r, -b / 2 + r, 180), (a / 2 - r, -b / 2 + r, 270)]:
        for i in range(segs + 1):
            t = math.radians(st + 90 * i / segs)
            p = (cx + r * math.cos(t), cy + r * math.sin(t))
            if not pts or abs(p[0] - pts[-1][0]) > 1e-6 or abs(p[1] - pts[-1][1]) > 1e-6:
                pts.append(p)
    if abs(pts[0][0] - pts[-1][0]) < 1e-6 and abs(pts[0][1] - pts[-1][1]) < 1e-6:
        pts.pop()
    return pts


def _map(u, v, t, axis):
    return {'X': (t, u, v), 'Y': (u, t, v), 'Z': (u, v, t)}[axis]


def prism(name, a, b, r, t0, t1, axis='Z', c=(0, 0), segs=12, coll=None, M=None):
    """แท่งสี่เหลี่ยมมุมมน หน้าตัด a x b (รัศมี r) ตามแกน axis จาก t0 ถึง t1, จุดศูนย์กลางหน้าตัด c=(u,v)"""
    prof = rr_profile(a, b, r, segs)
    bm = bmesh.new()
    lo = [bm.verts.new(_map(u + c[0], v + c[1], t0, axis)) for u, v in prof]
    hi = [bm.verts.new(_map(u + c[0], v + c[1], t1, axis)) for u, v in prof]
    n = len(prof)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_obj(name, bm, coll, M)


def cyl(name, r, t0, t1, axis='Z', c=(0, 0), segs=64, coll=None, M=None):
    bm = bmesh.new()
    vs0, vs1 = [], []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        u, v = c[0] + r * math.cos(a), c[1] + r * math.sin(a)
        vs0.append(bm.verts.new(_map(u, v, t0, axis)))
        vs1.append(bm.verts.new(_map(u, v, t1, axis)))
    for i in range(segs):
        j = (i + 1) % segs
        bm.faces.new((vs0[i], vs0[j], vs1[j], vs1[i]))
    bm.faces.new(list(reversed(vs0)))
    bm.faces.new(vs1)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_obj(name, bm, coll, M)


def box(name, x0, x1, y0, y1, z0, z1, coll=None, M=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(x1 - x0, y1 - y0, z1 - z0), verts=bm.verts)
    bmesh.ops.translate(bm, vec=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), verts=bm.verts)
    return mesh_obj(name, bm, coll, M)


def wedge(name, x0, x1, pts_yz, coll=None):
    """แท่งสามเหลี่ยม: หน้าตัดใน YZ (3 จุด) ยื่นตามแกน X"""
    bm = bmesh.new()
    a = [bm.verts.new((x0, y, z)) for y, z in pts_yz]
    b = [bm.verts.new((x1, y, z)) for y, z in pts_yz]
    bm.faces.new(a)
    bm.faces.new(list(reversed(b)))
    for i in range(3):
        j = (i + 1) % 3
        bm.faces.new((a[i], b[i], b[j], a[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return mesh_obj(name, bm, coll)


def apply_mods(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)


def bevel(ob, width, segs=3, angle=30):
    m = ob.modifiers.new("Bevel", 'BEVEL')
    m.width = width
    m.segments = segs
    m.limit_method = 'ANGLE'
    m.angle_limit = math.radians(angle)
    apply_mods(ob)
    return ob


def remove(ob):
    me = ob.data
    bpy.data.objects.remove(ob, do_unlink=True)
    if me is not None and me.users == 0:
        bpy.data.meshes.remove(me)


def boolean(target, other, op='DIFFERENCE', keep=False):
    m = target.modifiers.new("Bool", 'BOOLEAN')
    m.operation = op
    m.object = other
    try:
        m.solver = 'EXACT'
    except Exception:
        pass
    apply_mods(target)
    if not keep:
        remove(other)
    return target


def sub(target, *cutters):
    for c in cutters:
        boolean(target, c, 'DIFFERENCE')
    return target


def add(target, *parts):
    for p in parts:
        boolean(target, p, 'UNION')
    return target


def inter(target, other, keep=False):
    return boolean(target, other, 'INTERSECT', keep)


def join(name, objs):
    bm = bmesh.new()
    for o in objs:
        tmp = bmesh.new()
        tmp.from_mesh(o.data)
        tmp.transform(o.matrix_world)
        me_tmp = bpy.data.meshes.new("tmp")
        tmp.to_mesh(me_tmp)
        tmp.free()
        bm.from_mesh(me_tmp)
        bpy.data.meshes.remove(me_tmp)
        remove(o)
    return mesh_obj(name, bm)


def duplicate(ob, name, coll):
    n = bpy.data.objects.new(name, ob.data.copy())
    n.matrix_world = ob.matrix_world.copy()
    coll.objects.link(n)
    return n


def bake_transform(ob):
    ob.data.transform(ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)


def tube(name, r_out, r_in, t0, t1, axis, c):
    o = cyl(name, r_out, t0, t1, axis, c)
    i = cyl(name + "_in", r_in, t0 - 1, t1 + 1, axis, c)
    return sub(o, i)


def rr_ring(name, a, b, r, w, t0, t1, axis, c=(0, 0)):
    o = prism(name, a, b, r, t0, t1, axis, c)
    i = prism(name + "_in", a - 2 * w, b - 2 * w, max(r - w, 0.2), t0 - 1, t1 + 1, axis, c)
    return sub(o, i)


# เมทริกซ์มุมก้มกล้อง (หมุนรอบแกน Y ที่หน้ากรอบกล้อง)
M_CAM = Matrix.Translation((X_BEZEL, 0, CAM_Z)) @ Matrix.Rotation(math.radians(-CAM_TILT), 4, 'Y')

print("[SeeSpeak] building body ...")
# ============================================================================
# 1) เปลือกนอก + กรอบกล้อง + คว้านโพรง
# ============================================================================
body = prism("Body", W, H, R, -XO, XO, 'X', segs=16)
bevel(body, END_BEVEL, segs=4)

bez = prism("bezel", BEZEL, BEZEL, 3.5, X_BEZEL, -XO + 0.6, 'X', segs=10)
bevel(bez, 0.5, segs=3)
add(body, bez)

cavity = prism("cavity", 2 * YI, 2 * ZI, RC, -XI, XI, 'X', segs=8)
sub(body, cavity)

# ============================================================================
# 2) โครงสร้างภายใน (ส่วนของตัวเครื่อง)
# ============================================================================
# 2.1 ปลอกยึดกล้อง (square sleeve) + ฐานรองใต้ปลอกเพื่อพิมพ์ได้โดยไม่ต้องใช้ support
clip_box = box("clip", -XO + 0.1, 0, -50, 50, -50, 50)
sleeve = prism("cam_sleeve", CAM_HOLE + 2 * SLEEVE_T, CAM_HOLE + 2 * SLEEVE_T, 1.0, 2.5, 7.0, 'X', M=M_CAM)
inter(sleeve, clip_box, keep=True)
ped = box("cam_pedestal", 2.5, 7.0, -YI - 0.2, -(CAM_HOLE / 2 + SLEEVE_T) + 0.1,
          -(CAM_HOLE / 2 + SLEEVE_T), (CAM_HOLE / 2 + SLEEVE_T), M=M_CAM)
inter(ped, clip_box)
add(body, sleeve, ped)

# 2.2 ฐานยึด Tactile switch (เปิดด้าน +Y สำหรับสอดสวิตช์)
bx = BTN_X
tact_parts = [
    box("tact_fin_a", bx - TACT / 2 - 0.2 - 1.2, bx - TACT / 2 - 0.2, -YI - 0.2, TACT / 2 + 0.2, TACT_BOT - 1.2, FLANGE_BOT - 0.3),
    box("tact_fin_b", bx + TACT / 2 + 0.2, bx + TACT / 2 + 0.2 + 1.2, -YI - 0.2, TACT / 2 + 0.2, TACT_BOT - 1.2, FLANGE_BOT - 0.3),
    box("tact_plate", bx - TACT / 2 - 1.4, bx + TACT / 2 + 1.4, -YI - 0.2, 2.6, TACT_BOT - 1.2, TACT_BOT),
    box("tact_stop", bx - TACT / 2 - 0.3, bx + TACT / 2 + 0.3, -TACT / 2 - 0.2 - 1.2, -TACT / 2 - 0.2, TACT_BOT, TACT_BOT + TACT_BODY_H),
]
add(body, *tact_parts)
sub(body, box("tact_pin_slot", bx - TACT / 2 - 0.2, bx + TACT / 2 + 0.2, -3.9, -2.6, TACT_BOT - 1.5, TACT_BOT + 0.4))
# เว้นร่องหลบขอบบอร์ด XIAO ที่ขึ้นมาเฉียดปุ่มกด
sub(body, box("xiao_btn_notch", XIAO_X0 - 0.5, XIAO_X1 + 0.5, XIAO_Y - XIAO_W / 2 - 0.5, XIAO_Y + XIAO_W / 2 + 0.5, TACT_BOT - 2.0, ZI + 0.5))

# 2.3 แผ่นเสริมใต้ร่องสวิตช์เลื่อน + ฐานยึด SS-12D00
sx = SW_X
sw_parts = [
    box("sw_pad", sx - 6.0, sx + 6.0, -YI - 0.2, 4.5, SW_TOP, ZI + 0.2),
    box("sw_fin_a", sx - SW_L / 2 - 0.2 - 1.2, sx - SW_L / 2 - 0.2, -YI - 0.2, SW_W / 2 + 0.2, SW_BOT - 1.2, SW_TOP + 0.1),
    box("sw_fin_b", sx + SW_L / 2 + 0.2, sx + SW_L / 2 + 0.2 + 1.2, -YI - 0.2, SW_W / 2 + 0.2, SW_BOT - 1.2, SW_TOP + 0.1),
    box("sw_plate", sx - SW_L / 2 - 1.4, sx + SW_L / 2 + 1.4, -YI - 0.2, SW_W / 2 + 0.2, SW_BOT - 1.2, SW_BOT),
    box("sw_stop", sx - SW_L / 2 - 0.3, sx + SW_L / 2 + 0.3, -SW_W / 2 - 0.2 - 1.2, -SW_W / 2 - 0.2, SW_BOT, SW_TOP + 0.1),
]
add(body, *sw_parts)
sub(body, box("sw_pin_slot", sx - 3.8, sx + 3.8, -0.7, 0.7, SW_BOT - 1.5, SW_BOT + 0.3))

# 2.4 เสาสกรู (ต่อเนื่องจากตัวเครื่องถึงฝาข้าง แล้วค่อยแยกทีหลัง) + ครีบรับ 45° ใต้เสา
for sxn in (-1, 1):
    for szn in (-1, 1):
        bxp, bzp = sxn * BOSS_X, szn * BOSS_Z
        post = cyl("boss", BOSS_R, 2.0, YI + 0.4, 'Y', (bxp, bzp), segs=32)
        zw = szn * (ZI + 0.3)
        zi_ = szn * (BOSS_Z - BOSS_R)
        d = abs(zw - zi_)
        g = wedge("gusset", bxp - BOSS_R, bxp + BOSS_R, [(2.05, zw), (2.05, zi_), (2.05 - d, zw)])
        add(body, post, g)

# ============================================================================
# 3) งานตัดภายนอก
# ============================================================================
# 3.1 ร่องรอยต่อตกแต่ง (เหมือนฝาหัว-ท้ายในรูป)
for gx in (-XO + 7.0, XO - 7.0):
    ring = rr_ring("groove", W + 4, H + 4, R + 2, 2.5, gx - 0.3, gx + 0.3, 'X')
    sub(body, ring)

# 3.2 ด้านบน: แผงยุบ, รูปุ่มกด, ร่องสวิตช์เลื่อน, รูทะลุก้านสวิตช์, จุดไฟสถานะ
sub(body,
    prism("panel_top", 44.0, 14.0, 2.5, ZP, ZO + 2, 'Z'),
    cyl("btn_hole", BTN_HOLE_R, ZI - 1, ZO + 2, 'Z', (BTN_X, 0)),
    prism("sw_recess", RECESS_L, RECESS_W, 1.5, RECESS_FLOOR, ZO + 2, 'Z', (SW_X, 0)),
    prism("sw_through", LEVER + TRAVEL + 0.7, LEVER + 0.7, 0.3, SW_TOP - 0.5, ZO + 2, 'Z', (SW_X, 0), segs=4),
    cyl("top_dot", 0.6, ZO - 0.5, ZO + 1, 'Z', (-XO + 8.5, -5.0), segs=24))

# 3.3 ด้านล่าง: แผงยุบ + ช่อง USB-C (มีช่องหลบหัวสาย ทำให้ผนังตรงพอร์ตเหลือ 0.9 มม.)
sub(body,
    prism("panel_bot", 44.0, 14.0, 2.5, -ZO - 2, -ZP, 'Z'),
    prism("usb_recess", USB_REC_T, USB_REC_W, 3.0, -ZO - 2, -ZP + USB_REC_D, 'Z', (USB_X, XIAO_Y)),
    prism("usb_hole", USB_T, USB_W, 1.6, -ZO - 2, -ZI + 0.7, 'Z', (USB_X, XIAO_Y)))

# 3.4 ด้านหน้า: ช่องกล้องสี่เหลี่ยมทะลุ (เอียง 15°) + รูไฟแฟลช
sub(body,
    prism("cam_hole", CAM_HOLE, CAM_HOLE, 0.6, -6.0, 12.0, 'X', M=M_CAM),
    cyl("led_hole", LED_D / 2, -XO - 2, -XI + 0.5, 'X', (LED_Y, 0), segs=32))

# 3.5 ฝาข้าง (+Y): เสริมเนื้อรอบแม่เหล็ก, วงแหวนลำโพง, วงแหวนมอเตอร์สั่น
# แผ่นรองรับแม่เหล็ก 12 มม. ด้านในฝาข้าง (หนา 1.3 มม. ให้ก้นเบ้าแม่เหล็กตันสนิท แข็งแรง)
mag_pad = prism("mag_boss_pad", abs(MAG_XS[1] - MAG_XS[0]) + MAG_D + 2.0, MAG_D + 2.0, 3.0, YI - 1.3, YI + 0.3, 'Y', (MP_X, 0), segs=16)
bevel(mag_pad, 0.6, segs=3)
add(body, mag_pad)
add(body, tube("spk_ring", SPK_D / 2 + 0.2 + 0.7, SPK_D / 2 + 0.2, YI - 2.0, YI + 0.3, 'Y', (SPK_X, 0)))

# 3.6 รูลำโพง 16 รู
holes = []
for row, (z, n) in enumerate([(2.4, 5), (0.0, 6), (-2.4, 5)]):
    for i in range(n):
        holes.append(cyl("g", 0.7, YI - 2.5, YO + 1, 'Y', (SPK_X + (i - (n - 1) / 2) * 2.4, z), segs=20))
sub(body, join("grille", holes))

# 3.7 เบ้าแม่เหล็ก (ด้านนอกฝาข้าง)
for mx in MAG_XS:
    sub(body, cyl("mag_pocket", MAG_D / 2, YO - MAG_T, YO + 1, 'Y', (mx, 0), segs=48))

# 3.8 รูสกรู: รูนำในเสาตัวเครื่อง / รูทะลุ + เบ้าหัวสกรูบนฝาข้าง
for sxn in (-1, 1):
    for szn in (-1, 1):
        c = (sxn * BOSS_X, szn * BOSS_Z)
        sub(body,
            cyl("pilot", SCREW_PILOT_R, 1.0, YS + 0.5, 'Y', c, segs=24),
            cyl("clr", SCREW_CLR_R, YS, YO + 1, 'Y', c, segs=24),
            cyl("head", SCREW_HEAD_R, YO - 1.2, YO + 1, 'Y', c, segs=32))

# ============================================================================
# 4) แยกเป็นตัวเครื่อง (A) และฝาข้าง (B) + ขอบลิ้นล็อกฝา
# ============================================================================
print("[SeeSpeak] splitting ...")
part_A = duplicate(body, "1_Body_Main", C_WORK)
inter(part_A, box("halfA", -60, 60, -60, YS - 0.05, -60, 60))
part_B = duplicate(body, "2_Side_Lid", C_WORK)
inter(part_B, box("halfB", -60, 60, YS, 60, -60, 60))
remove(body)

lip = rr_ring("lip", 2 * XI - 2 * 0.25, 2 * ZI - 2 * 0.25, 1.0, LIP_T, YS - LIP_H, YS - 0.05, 'Y')
band = rr_ring("lip_band", 2 * XI + 1.2, 2 * ZI + 1.2, 1.0, 0.6 + 0.15 + LIP_T, YS - 0.01, YS + 1.2, 'Y')
add(lip, band)
for sxn in (-1, 1):
    for szn in (-1, 1):
        sub(lip, cyl("notch", BOSS_R + 0.3, YS - 3, YS + 3, 'Y', (sxn * BOSS_X, szn * BOSS_Z), segs=32))
sub(lip, box("xiao_notch", XIAO_X0 - 0.6, XIAO_X1 + 0.6, YS - 3, YS + 3, -ZO - 1, -ZI + 1.8))
add(part_B, lip)

# ============================================================================
# 5) ปุ่มกด / ปุ่มสไลด์ / แผ่นยึดขาแว่น (ตำแหน่งประกอบ)
# ============================================================================
print("[SeeSpeak] small parts ...")
button = cyl("3_Button", BTN_FLANGE_R, FLANGE_BOT, ZI - 0.0, 'Z', (BTN_X, 0), segs=64)
cap = cyl("btn_cap", BTN_CAP_R, ZI - 0.5, ZP + BTN_PROTRUDE, 'Z', (BTN_X, 0), segs=64)
bevel(cap, 0.5, segs=4)
add(button, cap)
bevel(button, 0.25, segs=2)

knob_x = SW_X - TRAVEL / 2
knob_bot = RECESS_FLOOR + 0.2
lever_top = SW_TOP + LEVER_H
knob = prism("4_Slide_Knob", KNOB_L, KNOB_W, 1.2, knob_bot, ZP + 1.6, 'Z', (knob_x, 0), segs=8)
bevel(knob, 0.4, segs=3)
sub(knob, box("socket", knob_x - (LEVER + 0.3) / 2, knob_x + (LEVER + 0.3) / 2, -(LEVER + 0.3) / 2, (LEVER + 0.3) / 2,
              knob_bot - 1, lever_top + 0.15))
for dx in (-1.2, 1.2):
    sub(knob, box("rib", knob_x + dx - 0.2, knob_x + dx + 0.2, -3, 3, ZP + 1.2, ZP + 3))

plate = prism("5_Glasses_Mount_Plate", MP_L, MP_W, 1.8, 0, MP_T, 'Z', segs=10)
bevel(plate, 0.4, segs=2)
for dx in (MAG_XS[0] - MP_X, MAG_XS[1] - MP_X):
    sub(plate, cyl("mp_mag", MAG_D / 2, MP_T - MAG_T, MP_T + 1, 'Z', (dx, 0), segs=48))

# ============================================================================
# 6) อุปกรณ์จำลองสำหรับตรวจการชน (fit check)
# ============================================================================
e = 0.05
dummies = {
    "OV5640 camera": prism("dm_camera", 8.5, 8.5, 0.3, 0.2, 5.7, 'X', M=M_CAM, coll=C_FIT),
    "XIAO ESP32S3 Sense": box("dm_xiao", XIAO_X0, XIAO_X1, XIAO_Y - XIAO_W / 2, XIAO_Y + XIAO_W / 2,
                              -ZI + 0.6, -ZI + XIAO_H + 0.5, coll=C_FIT),
    "Tactile switch": box("dm_tact", BTN_X - 3, BTN_X + 3, -3, 3, TACT_BOT + e, TACT_BOT + TACT_BODY_H, coll=C_FIT),
    "Tactile actuator": cyl("dm_tact_act", 1.75, TACT_BOT + TACT_BODY_H, TACT_TOP, 'Z', (BTN_X, 0), segs=24, coll=C_FIT),
    "SS-12D00 body": box("dm_sw", SW_X - SW_L / 2, SW_X + SW_L / 2, -SW_W / 2, SW_W / 2, SW_BOT + e, SW_TOP - e, coll=C_FIT),
    "SS-12D00 lever (OFF)": box("dm_lever_a", knob_x - LEVER / 2, knob_x + LEVER / 2, -LEVER / 2, LEVER / 2, SW_TOP, lever_top, coll=C_FIT),
    "SS-12D00 lever (ON)": box("dm_lever_b", knob_x + TRAVEL - LEVER / 2, knob_x + TRAVEL + LEVER / 2, -LEVER / 2, LEVER / 2, SW_TOP, lever_top, coll=C_FIT),
    "Speaker 20mm": cyl("dm_speaker", SPK_D / 2, YI - SPK_T, YI - e, 'Y', (SPK_X, 0), coll=C_FIT),
    "Vibration motor": cyl("dm_motor", MOT_D / 2, YI - 1.3 - MOT_T, YI - 1.3 - e, 'Y', (MOT_X, 0), coll=C_FIT),
    "Battery 401030": box("dm_battery", -5, 25, -YI + 1.0, -YI + 11.0, -ZI + 0.5, -ZI + 4.5, coll=C_FIT),
    "MAX98357A": box("dm_max", -5, 12.5, -YI + 1.0, -YI + 4.0, -6.5, 4.0, coll=C_FIT),
    "Magnet 1": cyl("dm_mag1", (MAG_D - 0.2) / 2, YO - MAG_T, YO - e, 'Y', (MAG_XS[0], 0), segs=48, coll=C_FIT),
    "Magnet 2": cyl("dm_mag2", (MAG_D - 0.2) / 2, YO - MAG_T, YO - e, 'Y', (MAG_XS[1], 0), segs=48, coll=C_FIT),
}

# ============================================================================
# 7) การตรวจสอบ
# ============================================================================
print("[SeeSpeak] checking ...")
report = []


def bm_of(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.transform(ob.matrix_world)
    return bm


def volume(ob):
    bm = bm_of(ob)
    v = bm.calc_volume(signed=True)
    bm.free()
    return v


def manifold_info(ob):
    bm = bm_of(ob)
    nm = sum(1 for ed in bm.edges if not ed.is_manifold)
    bnd = sum(1 for ed in bm.edges if ed.is_boundary)
    shells = 0
    seen = set()
    for f in bm.faces:
        if f.index in seen:
            continue
        shells += 1
        stack = [f]
        seen.add(f.index)
        while stack:
            cur = stack.pop()
            for ed in cur.edges:
                for nf in ed.link_faces:
                    if nf.index not in seen:
                        seen.add(nf.index)
                        stack.append(nf)
    bm.free()
    return nm, bnd, shells


def inter_volume(a, b):
    tmp = duplicate(a, "tmp_check", C_WORK)
    m = tmp.modifiers.new("i", 'BOOLEAN')
    m.operation = 'INTERSECT'
    m.object = b
    try:
        m.solver = 'EXACT'
    except Exception:
        pass
    apply_mods(tmp)
    v = abs(volume(tmp)) if len(tmp.data.polygons) else 0.0
    remove(tmp)
    return v


def place_copy(ob, name, M):
    n = duplicate(ob, name, C_WORK)
    n.data.transform(M)
    return n


# ปุ่ม/ปุ่มสไลด์ในตำแหน่งกด/เลื่อนสุด สำหรับตรวจ
btn_chk = place_copy(button, "chk_btn", Matrix.Translation((0, 0, -0.05)))
knob_on = place_copy(knob, "chk_knob_on", Matrix.Translation((TRAVEL, 0, 0)))
plate_asm = place_copy(plate, "chk_plate", Matrix.Translation((MP_X, YO + MP_T, 0)) @ Matrix.Rotation(math.radians(90), 4, 'X'))

printed = {"1_Body_Main": part_A, "2_Side_Lid": part_B, "3_Button": button,
           "4_Slide_Knob": knob, "5_Glasses_Mount_Plate": plate}

all_ok = True
report.append("=== 1) ความสมบูรณ์ของ mesh (watertight / manifold) ===")
for nm, ob in printed.items():
    n_nm, n_bnd, shells = manifold_info(ob)
    v = volume(ob)
    ok = (n_nm == 0 and n_bnd == 0 and v > 0)
    all_ok &= ok
    mass = v / 1000 * 1.24
    report.append(f"{'OK ' if ok else 'ERR'} {nm}: non-manifold={n_nm}, boundary={n_bnd}, shells={shells}, "
                  f"volume={v / 1000:.2f} cm3, PLA≈{mass:.1f} g")

report.append("")
report.append("=== 2) ตรวจการชนกันของชิ้นพิมพ์ (ตำแหน่งประกอบ) ===")
pairs = [("ตัวเครื่อง × ฝาข้าง", part_A, part_B),
         ("ตัวเครื่อง × ปุ่มกด", part_A, btn_chk),
         ("ตัวเครื่อง × ปุ่มสไลด์ (OFF)", part_A, knob),
         ("ตัวเครื่อง × ปุ่มสไลด์ (ON)", part_A, knob_on),
         ("ฝาข้าง × แผ่นยึดขาแว่น", part_B, plate_asm)]
for label, a, b in pairs:
    v = inter_volume(a, b)
    ok = v < 0.02
    all_ok &= ok
    report.append(f"{'OK ' if ok else 'ERR'} {label}: ชนกัน {v:.3f} mm3")

report.append("")
report.append("=== 3) ตรวจว่าอุปกรณ์จริงใส่ได้ ไม่ชนตัวเครื่อง/ฝา/ปุ่ม ===")
for label, d in dummies.items():
    worst = 0.0
    hit = []
    for pn, p in (("ตัวเครื่อง", part_A), ("ฝาข้าง", part_B), ("ปุ่มกด", btn_chk), ("ปุ่มสไลด์", knob)):
        if label.startswith("SS-12D00 lever") and pn == "ปุ่มสไลด์":
            continue  # ก้านสวิตช์ต้องเสียบอยู่ในรูของปุ่มสไลด์
        v = inter_volume(d, p)
        if v > 0.02:
            hit.append(f"{pn} {v:.2f}mm3")
        worst = max(worst, v)
    if label == "SS-12D00 lever (ON)":
        v = inter_volume(d, knob_on)
        if v > 0.02:
            hit.append(f"ปุ่มสไลด์(ON) {v:.2f}mm3")
    ok = not hit
    all_ok &= ok
    report.append(f"{'OK ' if ok else 'ERR'} {label}" + ("" if ok else " ชน: " + ", ".join(hit)))

report.append("")
report.append("=== 4) ตรวจอุปกรณ์ชนกันเอง ===")
names = [k for k in dummies if not k.startswith("SS-12D00 lever")]
clash = []
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        a, b = dummies[names[i]], dummies[names[j]]
        if {names[i], names[j]} == {"Tactile switch", "Tactile actuator"}:
            continue
        v = inter_volume(a, b)
        if v > 0.02:
            clash.append(f"{names[i]} × {names[j]} = {v:.2f}mm3")
all_ok &= not clash
report.append("OK  ไม่มีอุปกรณ์ชนกัน" if not clash else "ERR " + "; ".join(clash))

for o in (btn_chk, knob_on, plate_asm):
    remove(o)

# ============================================================================
# 8) วางชิ้นงานตามทิศพิมพ์ + ตรวจ overhang + ส่งออก STL
# ============================================================================
print("[SeeSpeak] print layout + STL ...")
print_rot = {
    "1_Body_Main": Matrix.Rotation(math.radians(90), 4, 'X'),     # ด้านนอก -Y วางบนฐาน, ช่องเปิดหันขึ้น
    "2_Side_Lid": Matrix.Rotation(math.radians(-90), 4, 'X'),     # ด้านนอก +Y วางบนฐาน
    "3_Button": Matrix.Identity(4),                               # ปีกปุ่มวางบนฐาน
    "4_Slide_Knob": Matrix.Rotation(math.radians(180), 4, 'X'),   # หน้าปุ่มคว่ำลงฐาน รูก้านหันขึ้น
    "5_Glasses_Mount_Plate": Matrix.Identity(4),                  # ด้านเรียบวางบนฐาน
}
print_objs = {}
cursor_x = 0.0
for nm, ob in printed.items():
    p = duplicate(ob, "PRINT_" + nm, C_PRINT)
    p.data.transform(print_rot[nm])
    vs = [v.co for v in p.data.vertices]
    mn = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    mx = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    p.data.transform(Matrix.Translation((-mn.x + cursor_x, -mn.y - 120.0, -mn.z)))
    cursor_x += (mx.x - mn.x) + 8.0
    print_objs[nm] = p

report.append("")
report.append("=== 5) Overhang (> 45° ในทิศพิมพ์, ไม่รวมผิวติดฐาน 0.6 มม. แรก) ===")
for nm, p in print_objs.items():
    me = p.data
    area = 0.0
    for poly in me.polygons:
        if poly.normal.z < -0.7072 and poly.center.z > 0.6:
            area += poly.area
    report.append(f"{nm}: พื้นที่ overhang ≈ {area:.1f} mm2")


def write_stl(ob, path):
    me = ob.data
    me.calc_loop_triangles()
    with open(path, "wb") as f:
        f.write(b"SeeSpeak print part".ljust(80, b" "))
        f.write(struct.pack("<I", len(me.loop_triangles)))
        for tri in me.loop_triangles:
            n = tri.normal
            f.write(struct.pack("<3f", n.x, n.y, n.z))
            for vi in tri.vertices:
                c = me.vertices[vi].co
                f.write(struct.pack("<3f", c.x, c.y, c.z))
            f.write(b"\x00\x00")


for nm, p in print_objs.items():
    # STL แต่ละชิ้น: ย้ายให้มุมล่างอยู่ที่จุด 0
    tmp = duplicate(p, "tmp_stl", C_WORK)
    vs = [v.co for v in tmp.data.vertices]
    tmp.data.transform(Matrix.Translation((-min(v.x for v in vs), -min(v.y for v in vs), 0)))
    write_stl(tmp, os.path.join(STL_DIR, nm + ".stl"))
    remove(tmp)
report.append("")
report.append("STL: " + STL_DIR)

# ============================================================================
# 9) จัดฉาก: ชิ้นงานประกอบ + ปุ่มวางแยกข้างๆ
# ============================================================================
for ob in (part_A, part_B):
    C_WORK.objects.unlink(ob)
    C_ASM.objects.link(ob)
# ปุ่มกด/ปุ่มสไลด์/แผ่นยึด วางแยกไว้ด้านข้างตัวเครื่อง (ยังไม่ประกอบ)
button.data.transform(Matrix.Translation((-BTN_X + 50, -20, -FLANGE_BOT - ZO)))
knob.data.transform(Matrix.Translation((-knob_x + 50, -5, -knob_bot - ZO)))
plate.data.transform(Matrix.Translation((50 + 25, 12, -ZO)))
for ob in (button, knob, plate):
    C_WORK.objects.unlink(ob)
    C_ASM.objects.link(ob)


def make_mat(name, color, rough=0.5, metal=0.0, sss=0.0):
    m = bpy.data.materials.new(name)
    try:
        m.use_nodes = True
    except Exception:
        pass
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    nt.links.new(bsdf.outputs[0], out.inputs['Surface'])
    for k, v in (("Base Color", (*color, 1)), ("Roughness", rough), ("Metallic", metal),
                 ("Subsurface Weight", sss)):
        if k in bsdf.inputs:
            bsdf.inputs[k].default_value = v
    m.diffuse_color = (*color, 1)
    return m


M_WHITE = make_mat("PLA_White", (0.82, 0.82, 0.8), rough=0.55, sss=0.05)
M_FLOOR = make_mat("Floor", (0.35, 0.36, 0.38), rough=0.8)
DUMMY_COL = {"dm_camera": (0.6, 0.6, 0.62), "dm_xiao": (0.05, 0.25, 0.08), "dm_tact": (0.1, 0.1, 0.1),
             "dm_tact_act": (0.6, 0.3, 0.05), "dm_sw": (0.15, 0.15, 0.6), "dm_lever_a": (0.9, 0.9, 0.9),
             "dm_lever_b": (0.9, 0.9, 0.9), "dm_speaker": (0.7, 0.1, 0.1), "dm_motor": (0.7, 0.6, 0.1),
             "dm_battery": (0.1, 0.3, 0.7), "dm_max": (0.4, 0.05, 0.5), "dm_mag1": (0.5, 0.5, 0.5),
             "dm_mag2": (0.5, 0.5, 0.5)}


def smooth(ob, angle=30):
    me = ob.data
    try:
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(angle))
    except Exception:
        pass


for c in (C_ASM, C_PRINT):
    for ob in c.objects:
        ob.data.materials.clear()
        ob.data.materials.append(M_WHITE)
        smooth(ob)
for ob in C_FIT.objects:
    ob.data.materials.clear()
    ob.data.materials.append(make_mat("M_" + ob.name, DUMMY_COL.get(ob.name, (0.5, 0.5, 0.5)), rough=0.4))
    smooth(ob)

# ลบวัตถุชั่วคราว
for ob in list(C_WORK.objects):
    remove(ob)
bpy.data.collections.remove(C_WORK)

# --- ฉาก ---
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.001
scene.unit_settings.length_unit = 'MILLIMETERS'
floor = prism("Floor", 3000, 3000, 1, -ZO - 1.0, -ZO, 'Z', segs=1, coll=C_SETUP)
floor.data.materials.append(M_FLOOR)

world = scene.world or bpy.data.worlds.new("Studio")
scene.world = world
try:
    world.use_nodes = True
except Exception:
    pass
bg = world.node_tree.nodes.get("Background")
if bg:
    bg.inputs[0].default_value = (0.42, 0.43, 0.45, 1)
    bg.inputs[1].default_value = 0.5


def look_rot(loc, target):
    return (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()


def area_light(name, loc, size, energy, target=(0, 0, 0)):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.size = size
    ld.energy = energy
    ob = bpy.data.objects.new(name, ld)
    C_SETUP.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = look_rot(loc, target)


area_light("Key", (-180, -220, 300), 250, 0.9e6)
area_light("Fill", (220, -250, 120), 300, 0.35e6)
area_light("Rim", (120, 260, 260), 200, 0.8e6)
area_light("Top", (0, 0, 400), 300, 0.4e6)
area_light("Low", (0, 0, -300), 300, 0.25e6)


def camera(name, loc, target, lens=85, ortho=None):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.clip_start = 1
    cd.clip_end = 20000
    if ortho:
        cd.type = 'ORTHO'
        cd.ortho_scale = ortho
    ob = bpy.data.objects.new(name, cd)
    C_SETUP.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = look_rot(loc, target)
    return ob


cams = {
    "01_hero": camera("Cam_Hero", (-150, -230, 150), (12, 0, -3), lens=70),
    "02_lid_side": camera("Cam_Lid", (130, 230, 120), (5, 0, -3), lens=70),
    "03_bottom_usb": camera("Cam_Bottom", (-90, -150, -170), (0, 0, 0), lens=75),
    "04_front_camera": camera("Cam_Front", (-260, -60, 40), (0, 0, 0), lens=110),
    "05_inside_fit": camera("Cam_Inside", (40, 230, 110), (0, 0, 0), lens=75),
    "06_print_layout": camera("Cam_Print", (110, -290, 260), (110, -105, 0), lens=55),
}
scene.camera = cams["01_hero"]

scene.render.engine = 'CYCLES'
try:
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 40
    scene.cycles.use_denoising = True
except Exception:
    pass
scene.render.resolution_x = 1400
scene.render.resolution_y = 900
try:
    scene.view_settings.view_transform = 'AgX'
except Exception:
    pass

C_FIT.hide_render = True
C_FIT.hide_viewport = True
C_PRINT.hide_render = True

# --- บันทึกรายงาน ---
report.insert(0, f"SeeSpeak print check  |  ผลรวม: {'ผ่านทั้งหมด' if all_ok else 'มีจุดต้องแก้'}")
report.insert(1, f"ขนาดตัวเครื่อง {L} x {W} x {H} มม. (+กรอบกล้อง {BEZEL_PROTRUDE}), ผนัง {WALL} มม.")
report.insert(2, "")
txt = "\n".join(report)
print(txt)
with open(os.path.join(OUT_DIR, "print_check_report.txt"), "w", encoding="utf-8") as f:
    f.write(txt)

blend_path = os.path.join(OUT_DIR, "seespeak_print.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend_path)

if DO_RENDER:
    lid = bpy.data.objects["2_Side_Lid"]
    for name, cam in cams.items():
        scene.camera = cam
        inside = name == "05_inside_fit"
        prt = name == "06_print_layout"
        lid.hide_render = inside
        C_FIT.hide_render = not inside
        C_PRINT.hide_render = not prt
        C_ASM.hide_render = prt
        floor.hide_render = name == "03_bottom_usb"
        scene.render.filepath = os.path.join(RENDER_DIR, name + ".png")
        bpy.ops.render.render(write_still=True)
        print("[SeeSpeak] rendered", name)
    lid.hide_render = False
    C_FIT.hide_render = True
    C_PRINT.hide_render = True
    C_ASM.hide_render = False
    floor.hide_render = False
    scene.camera = cams["01_hero"]
    bpy.ops.wm.save_as_mainfile(filepath=blend_path)
print("[SeeSpeak] done")

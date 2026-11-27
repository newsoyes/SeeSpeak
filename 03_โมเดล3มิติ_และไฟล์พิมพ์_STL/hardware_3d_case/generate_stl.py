import os
import numpy as np
import manifold3d as m3d
import trimesh

# ============================================================================
# สคริปต์สร้างไฟล์ 3D (.STL) สำหรับเคส SeeSpeak (Watertight 100% พร้อมพิมพ์ 3 มิติ)
# ============================================================================

OUT_DIR = r"D:\แข่งสิ่งประดิษ\hardware_3d_case"
os.makedirs(OUT_DIR, exist_ok=True)

# พารามิเตอร์ขนาด (มิลลิเมตร) ตรงกับไฟล์ seespeak_case.scad
case_length = 56.0
case_height = 24.0
case_width  = 16.5
wall_thick  = 1.6
corner_r    = 2.2

cam_tilt_angle = 15.0
cam_lens_hole  = 8.8
led_hole_d     = 4.2

spk_diameter   = 20.6
spk_pos_x      = 42.0

vib_diameter   = 10.5
vib_depth      = 3.2

btn_hole_size  = 6.5
btn_pos_x      = 16.0
sw_hole_len    = 9.0
sw_hole_wid    = 4.2

usbc_wid       = 9.6
usbc_hei       = 4.2

mag_diameter   = 5.3
mag_depth      = 2.1
mag_spacing    = 20.0
mount_length   = 36.0
mount_height   = 11.0
mount_thick    = 3.2


def rounded_box(l, w, h, r, circular_segments=48):
    c1 = m3d.Manifold.cylinder(h, r, r, circular_segments).translate([r, r, 0])
    c2 = m3d.Manifold.cylinder(h, r, r, circular_segments).translate([l - r, r, 0])
    c3 = m3d.Manifold.cylinder(h, r, r, circular_segments).translate([r, w - r, 0])
    c4 = m3d.Manifold.cylinder(h, r, r, circular_segments).translate([l - r, w - r, 0])
    return m3d.Manifold.batch_hull([c1, c2, c3, c4])


def build_main_body():
    # 1. ก้อนกล่องหลักภายนอก
    body = rounded_box(case_length, case_width, case_height, corner_r)

    # 2. ปาดหน้าด้านหน้าเอียงก้ม 15 องศา
    front_cut = (
        m3d.Manifold.cube([15.0, case_width + 2.0, case_height * 2.0])
        .translate([-15.0, 0.0, -case_height * 1.5])
        .rotate([0.0, cam_tilt_angle, 0.0])
        .translate([0.0, -1.0, case_height])
    )
    body = body - front_cut

    # 3. คว้านโพรงด้านในสำหรับวางอุปกรณ์
    inner_cavity = m3d.Manifold.cube([
        case_length - (wall_thick * 2.0) - 2.5,
        case_width - (wall_thick * 2.0),
        case_height
    ]).translate([wall_thick + 2.5, wall_thick, wall_thick])
    body = body - inner_cavity

    # 4. เจาะช่องเลนส์กล้อง OV5640 ด้านหน้า (ทำมุมก้ม 15 องศา)
    cam_hole = (
        m3d.Manifold.cylinder(20.0, cam_lens_hole / 2.0, cam_lens_hole / 2.0, 64)
        .translate([0.0, 0.0, -10.0])
        .rotate([0.0, 90.0, 0.0])
        .rotate([0.0, cam_tilt_angle, 0.0])
        .translate([2.2, case_width / 2.0, case_height * 0.62])
    )
    body = body - cam_hole

    # 5. เจาะช่องเม็ดไฟแฟลช SMD LED ข้างใต้กล้อง
    led_hole = (
        m3d.Manifold.cylinder(20.0, led_hole_d / 2.0, led_hole_d / 2.0, 48)
        .translate([0.0, 0.0, -10.0])
        .rotate([0.0, 90.0, 0.0])
        .rotate([0.0, cam_tilt_angle, 0.0])
        .translate([3.5, case_width / 2.0, case_height * 0.28])
    )
    body = body - led_hole

    # 6. เจาะช่องปุ่มกดสั่งงาน 6x6 มม. ด้านข้าง
    btn_hole = m3d.Manifold.cube([btn_hole_size, wall_thick + 3.0, btn_hole_size]).translate([
        btn_pos_x - (btn_hole_size / 2.0),
        -1.0,
        (case_height - btn_hole_size) / 2.0
    ])
    body = body - btn_hole

    # 7. เจาะช่องเสียบสายชาร์จ USB Type-C ด้านท้าย
    usbc_hole = m3d.Manifold.cube([wall_thick + 2.0, usbc_wid, usbc_hei]).translate([
        case_length - wall_thick - 1.0,
        (case_width - usbc_wid) / 2.0,
        3.5
    ])
    body = body - usbc_hole

    # 8. เจาะช่องสวิตช์เลื่อนเปิด-ปิด SS-12D00 ด้านท้ายส่วนบน
    sw_hole = m3d.Manifold.cube([wall_thick + 2.0, sw_hole_wid, sw_hole_len - 2.0]).translate([
        case_length - wall_thick - 1.0,
        (case_width - sw_hole_wid) / 2.0,
        case_height - 7.5
    ])
    body = body - sw_hole

    # 9. เจาะรูตะแกรงเสียงลำโพง 20 มม. (ด้านล่างและด้านข้างใกล้หู)
    for i in range(-2, 3):
        slot_b = m3d.Manifold.cube([1.6, case_width - 6.0, wall_thick + 2.0]).translate([
            spk_pos_x + (i * 2.8) - 0.8,
            3.0,
            -1.0
        ])
        body = body - slot_b

    for j in range(-2, 3):
        slot_s = m3d.Manifold.cube([1.6, wall_thick + 2.0, 10.0]).translate([
            spk_pos_x + (j * 2.8) - 0.8,
            -1.0,
            (case_height / 2.0) - 5.0
        ])
        body = body - slot_s

    # 10. เบ้าวงกลมด้านในสำหรับวางลำโพง 20 มม.
    spk_recess = (
        m3d.Manifold.cylinder(2.0, spk_diameter / 2.0, spk_diameter / 2.0, 64)
        .rotate([90.0, 0.0, 0.0])
        .translate([spk_pos_x, wall_thick + 0.8, case_height / 2.0])
    )
    body = body - spk_recess

    # 11. ผนังกั้นภายใน (Interior Partition) แยกโซนบอร์ด XIAO กับโซนลำโพง 20 มม.
    p1 = m3d.Manifold.cube([1.4, 3.5, case_height - 5.0]).translate([28.5, wall_thick, wall_thick])
    p2 = m3d.Manifold.cube([1.4, 3.5, case_height - 5.0]).translate([
        28.5,
        case_width - wall_thick - 3.5,
        wall_thick
    ])
    body = body + p1 + p2
    return body


def build_lid_cover():
    lid_l = case_length - 3.0
    lid_w = case_width
    lid_h = 3.2

    lid = rounded_box(lid_l, lid_w, lid_h, corner_r)
    lip = m3d.Manifold.cube([
        lid_l - (wall_thick * 2.0) - 0.5,
        lid_w - (wall_thick * 2.0) - 0.5,
        1.6
    ]).translate([wall_thick + 0.25, wall_thick + 0.25, lid_h])
    lid = lid + lip

    # คว้านด้านในขอบบ่า
    inner_cut = m3d.Manifold.cube([
        lid_l - (wall_thick * 2.0) - 3.2,
        lid_w - (wall_thick * 2.0) - 3.2,
        2.5
    ]).translate([wall_thick + 1.6, wall_thick + 1.6, lid_h - 0.5])
    lid = lid - inner_cut

    # เบ้ากลมด้านในสำหรับวางมอเตอร์สั่นเหรียญ 10x3 มม.
    vib_hole = m3d.Manifold.cylinder(vib_depth, vib_diameter / 2.0, vib_diameter / 2.0, 48).translate([
        lid_l * 0.65,
        lid_w / 2.0,
        lid_h - 1.2
    ])
    lid = lid - vib_hole

    # เบ้าฝังแม่เหล็กนีโอไดเมียม 2 เม็ด ด้านนอกฝา
    mag1 = m3d.Manifold.cylinder(mag_depth + 0.1, mag_diameter / 2.0, mag_diameter / 2.0, 48).translate([
        (lid_l / 2.0) - (mag_spacing / 2.0),
        lid_w / 2.0,
        -0.1
    ])
    mag2 = m3d.Manifold.cylinder(mag_depth + 0.1, mag_diameter / 2.0, mag_diameter / 2.0, 48).translate([
        (lid_l / 2.0) + (mag_spacing / 2.0),
        lid_w / 2.0,
        -0.1
    ])
    lid = lid - mag1 - mag2

    # ร่องบ่าบังคับทิศทางตรงกลาง
    align_groove = m3d.Manifold.cube([8.0, 4.4, 1.3]).translate([
        (lid_l / 2.0) - 4.0,
        (lid_w / 2.0) - 2.2,
        -0.1
    ])
    lid = lid - align_groove
    return lid


def build_glasses_mount_plate():
    plate = rounded_box(mount_length, mount_height, mount_thick, 1.8)
    align_key = m3d.Manifold.cube([7.6, 4.0, 1.1]).translate([
        (mount_length / 2.0) - 3.8,
        (mount_height / 2.0) - 2.0,
        mount_thick
    ])
    plate = plate + align_key

    # เบ้าฝังแม่เหล็ก 2 เม็ด
    mag1 = m3d.Manifold.cylinder(mag_depth + 0.2, mag_diameter / 2.0, mag_diameter / 2.0, 48).translate([
        (mount_length / 2.0) - (mag_spacing / 2.0),
        mount_height / 2.0,
        mount_thick - mag_depth
    ])
    mag2 = m3d.Manifold.cylinder(mag_depth + 0.2, mag_diameter / 2.0, mag_diameter / 2.0, 48).translate([
        (mount_length / 2.0) + (mag_spacing / 2.0),
        mount_height / 2.0,
        mount_thick - mag_depth
    ])
    plate = plate - mag1 - mag2
    return plate


def build_button_cap():
    base = m3d.Manifold.cylinder(1.2, 8.5 / 2.0, 8.5 / 2.0, 48)
    top = m3d.Manifold.cylinder(2.5, 5.8 / 2.0, 5.8 / 2.0, 48).translate([0.0, 0.0, 1.2])
    stem_hole = m3d.Manifold.cube([3.4, 3.4, 2.2]).translate([-1.7, -1.7, -0.1])
    return (base + top) - stem_hole


def save_manifold_to_stl(m_obj, filename):
    mesh = m_obj.to_mesh()
    tm = trimesh.Trimesh(vertices=mesh.vert_properties[:, :3], faces=mesh.tri_verts)
    filepath = os.path.join(OUT_DIR, filename)
    tm.export(filepath)
    print(f"Saved: {filepath} (Vertices: {len(tm.vertices)}, Faces: {len(tm.faces)}, Watertight: {tm.is_watertight})")
    return tm


if __name__ == "__main__":
    m_body = build_main_body()
    m_lid = build_lid_cover()
    m_mount = build_glasses_mount_plate()
    m_cap = build_button_cap()

    save_manifold_to_stl(m_body, "1_seespeak_main_body.stl")
    save_manifold_to_stl(m_lid, "2_seespeak_lid_cover.stl")
    save_manifold_to_stl(m_mount, "3_seespeak_glasses_mount_plate.stl")
    save_manifold_to_stl(m_cap, "4_seespeak_button_cap.stl")

    # ไฟล์รวมทุกชิ้นในถาดพิมพ์เดียว
    m_all = (
        m_body
        + m_lid.translate([0.0, case_width + 6.0, 0.0])
        + m_mount.translate([0.0, (case_width * 2.0) + 12.0, 0.0])
        + m_cap.translate([mount_length + 10.0, (case_width * 2.0) + 17.0, 0.0])
    )
    save_manifold_to_stl(m_all, "0_seespeak_all_parts_combined.stl")

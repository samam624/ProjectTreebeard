"""Fusion 360 script: MSV3 probe-brain enclosure and solar-panel mount.

Run from Utilities > Scripts and Add-Ins. It creates Base, Lid, two pivot arms,
and a 30 x 54 mm solar-panel frame, then exports a native .f3d plus STEP copies.
Dimensions are in millimetres; Fusion API point coordinates use internal cm.
"""

import adsk.core
import adsk.fusion
import os
import traceback


OUTER_CONTOUR = [
    (-2.80, 82.00),
    (20.80, 82.00),
    (25.00, 86.20),
    (25.00, 142.60),
    (20.60, 149.00),
    (-2.60, 149.00),
    (-7.00, 142.60),
    (-7.00, 86.20),
]

INNER_CONTOUR = [
    (-1.65, 84.00),
    (19.65, 84.00),
    (22.65, 86.35),
    (22.65, 142.25),
    (18.65, 146.65),
    (-0.65, 146.65),
    (-4.65, 142.25),
    (-4.65, 86.35),
]

PCB_THICKNESS = 1.60  # Correct JLCPCB thickness; STEP incorrectly used 1.812 mm.
BASE_BOTTOM_Z = -6.35
BASE_CAVITY_FLOOR_Z = -4.25
BASE_TOP_Z = -0.80
LID_BOTTOM_Z = -0.75
LID_INNER_ROOF_Z = 5.70
LID_TOP_Z = 7.55
PCB_CLAMP_INTERFERENCE = 0.12
SOLAR_PIVOT_Y = 118.50
SOLAR_PIVOT_Z = -2.85
SOLAR_ARM_CENTER_DISTANCE = 36.00
SOLAR_PANEL_PIVOT_Y = 149.68
SOLAR_PANEL_PIVOT_Z = 15.15
SOLAR_ARM_THICKNESS = 1.60
SOLAR_CLEARANCE_DIAMETER = 2.40
SOLAR_INSERT_POCKET_DIAMETER = 2.90
SOLAR_INSERT_POCKET_DEPTH = 3.20
CASE_WIRE_ENTRY_DIAMETER = 1.60
CASE_WIRE_ENTRY_CENTERS_Z = (2.20, 4.60)
SOLAR_PANEL_POCKET_WIDTH = 30.40
SOLAR_PANEL_POCKET_HEIGHT = 54.40
SOLAR_PANEL_FRAME_WIDTH = 36.00
SOLAR_PANEL_FRAME_HEIGHT = 60.00
SOLAR_PANEL_BACK_Z = 13.65
SOLAR_PANEL_THICKNESS = 3.00
SOLAR_PANEL_RECESS_DEPTH = 1.60
SOLAR_PANEL_WIRE_HOLE_DIAMETER = 2.20
PROBE_SLOT_XY_CLEARANCE = 0.20
PROBE_SLOT_Z_CLEARANCE = 0.15


def _cm(mm_value):
    return mm_value / 10.0


def _point(x_mm, y_mm, z_mm=0.0):
    return adsk.core.Point3D.create(_cm(x_mm), _cm(y_mm), _cm(z_mm))


def _component(root_component, name):
    transform = adsk.core.Matrix3D.create()
    occurrence = root_component.occurrences.addNewComponent(transform)
    occurrence.component.name = name
    return occurrence.component


def _offset_plane(component, z_mm, cache):
    key = round(z_mm, 6)
    if key in cache:
        return cache[key]
    if abs(z_mm) < 1e-9:
        cache[key] = component.xYConstructionPlane
        return cache[key]
    plane_input = component.constructionPlanes.createInput()
    plane_input.setByOffset(
        component.xYConstructionPlane,
        adsk.core.ValueInput.createByReal(_cm(z_mm)),
    )
    plane = component.constructionPlanes.add(plane_input)
    plane.name = 'Z_{:.2f}_mm'.format(z_mm)
    plane.isLightBulbOn = False
    cache[key] = plane
    return plane


def _offset_yz_plane(component, x_mm, cache):
    key = round(x_mm, 6)
    if key in cache:
        return cache[key]
    if abs(x_mm) < 1e-9:
        cache[key] = component.yZConstructionPlane
        return cache[key]
    plane_input = component.constructionPlanes.createInput()
    plane_input.setByOffset(
        component.yZConstructionPlane,
        adsk.core.ValueInput.createByReal(_cm(x_mm)),
    )
    plane = component.constructionPlanes.add(plane_input)
    plane.name = 'X_{:.2f}_mm'.format(x_mm)
    plane.isLightBulbOn = False
    cache[key] = plane
    return plane


def _offset_xz_plane(component, y_mm, cache):
    key = round(y_mm, 6)
    if key in cache:
        return cache[key]
    if abs(y_mm) < 1e-9:
        cache[key] = component.xZConstructionPlane
        return cache[key]
    plane_input = component.constructionPlanes.createInput()
    plane_input.setByOffset(
        component.xZConstructionPlane,
        adsk.core.ValueInput.createByReal(_cm(y_mm)),
    )
    plane = component.constructionPlanes.add(plane_input)
    plane.name = 'Y_{:.2f}_mm'.format(y_mm)
    plane.isLightBulbOn = False
    cache[key] = plane
    return plane


def _prism(component, name, points, z_min, z_max, plane_cache):
    plane = _offset_plane(component, z_min, plane_cache)
    sketch = component.sketches.add(plane)
    sketch.name = name + '_Sketch'
    sketch.isComputeDeferred = True
    lines = sketch.sketchCurves.sketchLines
    for index, start in enumerate(points):
        end = points[(index + 1) % len(points)]
        lines.addByTwoPoints(_point(start[0], start[1]), _point(end[0], end[1]))
    sketch.isComputeDeferred = False
    profile = sketch.profiles.item(0)
    extrude = component.features.extrudeFeatures.addSimple(
        profile,
        adsk.core.ValueInput.createByReal(_cm(z_max - z_min)),
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    extrude.name = name
    body = extrude.bodies.item(0)
    body.name = name
    sketch.isVisible = False
    return body


def _prism_yz(component, name, points_yz, x_min, x_max, plane_cache):
    plane = _offset_yz_plane(component, x_min, plane_cache)
    sketch = component.sketches.add(plane)
    sketch.name = name + '_Sketch'
    sketch.isComputeDeferred = True
    lines = sketch.sketchCurves.sketchLines
    for index, start in enumerate(points_yz):
        end = points_yz[(index + 1) % len(points_yz)]
        lines.addByTwoPoints(_point(start[0], start[1]), _point(end[0], end[1]))
    sketch.isComputeDeferred = False
    extrude = component.features.extrudeFeatures.addSimple(
        sketch.profiles.item(0),
        adsk.core.ValueInput.createByReal(_cm(x_max - x_min)),
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    extrude.name = name
    body = extrude.bodies.item(0)
    body.name = name
    sketch.isVisible = False
    return body


def _box(component, name, x_min, y_min, z_min, x_max, y_max, z_max, plane_cache):
    points = [
        (x_min, y_min),
        (x_max, y_min),
        (x_max, y_max),
        (x_min, y_max),
    ]
    return _prism(component, name, points, z_min, z_max, plane_cache)


def _cylinder_z(component, name, z_min, length, center_x, center_y, radius, plane_cache):
    plane = _offset_plane(component, z_min, plane_cache)
    sketch = component.sketches.add(plane)
    sketch.name = name + '_Sketch'
    sketch.sketchCurves.sketchCircles.addByCenterRadius(
        _point(center_x, center_y),
        _cm(radius),
    )
    extrude = component.features.extrudeFeatures.addSimple(
        sketch.profiles.item(0),
        adsk.core.ValueInput.createByReal(_cm(length)),
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    extrude.name = name
    body = extrude.bodies.item(0)
    body.name = name
    sketch.isVisible = False
    return body


def _cylinder_x(component, name, x_min, length, center_y, center_z, radius, plane_cache):
    plane = _offset_yz_plane(component, x_min, plane_cache)
    sketch = component.sketches.add(plane)
    sketch.name = name + '_Sketch'
    # Local coordinates on the YZ plane are Y and Z respectively.
    sketch.sketchCurves.sketchCircles.addByCenterRadius(
        _point(center_y, center_z),
        _cm(radius),
    )
    extrude = component.features.extrudeFeatures.addSimple(
        sketch.profiles.item(0),
        adsk.core.ValueInput.createByReal(_cm(length)),
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    extrude.name = name
    body = extrude.bodies.item(0)
    body.name = name
    sketch.isVisible = False
    return body


def _cylinder_y_from_max(component, name, y_max, length, center_x, center_z, radius, plane_cache):
    # Fusion's XZ plane normal points toward -Y, so a positive extrusion from
    # y_max produces the requested cylinder toward y_max - length.
    plane = _offset_xz_plane(component, y_max, plane_cache)
    sketch = component.sketches.add(plane)
    sketch.name = name + '_Sketch'
    sketch.sketchCurves.sketchCircles.addByCenterRadius(
        _point(center_x, center_z),
        _cm(radius),
    )
    extrude = component.features.extrudeFeatures.addSimple(
        sketch.profiles.item(0),
        adsk.core.ValueInput.createByReal(_cm(length)),
        adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
    )
    extrude.name = name
    body = extrude.bodies.item(0)
    body.name = name
    sketch.isVisible = False
    return body


def _combine(component, target, tool, operation, name):
    tools = adsk.core.ObjectCollection.create()
    tools.add(tool)
    combine_input = component.features.combineFeatures.createInput(target, tools)
    combine_input.operation = operation
    combine_input.isKeepToolBodies = False
    combine_input.isNewComponent = False
    feature = component.features.combineFeatures.add(combine_input)
    feature.name = name
    return feature.bodies.item(0)


def _join(component, target, tool, name):
    return _combine(
        component,
        target,
        tool,
        adsk.fusion.FeatureOperations.JoinFeatureOperation,
        name,
    )


def _cut(component, target, tool, name):
    return _combine(
        component,
        target,
        tool,
        adsk.fusion.FeatureOperations.CutFeatureOperation,
        name,
    )


def _build_base(root_component):
    component = _component(root_component, 'MSV3 Brain Enclosure Base')
    planes = {}
    yz_planes = {}
    base = _prism(
        component,
        'Base Outer',
        OUTER_CONTOUR,
        BASE_BOTTOM_Z,
        BASE_TOP_Z,
        planes,
    )
    cavity = _prism(
        component,
        'Base Cavity Tool',
        INNER_CONTOUR,
        BASE_CAVITY_FLOOR_Z,
        BASE_TOP_Z + 1.0,
        planes,
    )
    base = _cut(component, base, cavity, 'Hollow Base')

    probe_slot = _prism(
        component,
        'Sealable Probe Exit Tool',
        (
            (-PROBE_SLOT_XY_CLEARANCE, 81.50),
            (18.00 + PROBE_SLOT_XY_CLEARANCE, 81.50),
            (19.25 + PROBE_SLOT_XY_CLEARANCE, 84.25),
            (-1.25 - PROBE_SLOT_XY_CLEARANCE, 84.25),
        ),
        -PCB_THICKNESS - PROBE_SLOT_Z_CLEARANCE,
        PROBE_SLOT_Z_CLEARANCE,
        planes,
    )
    base = _cut(component, base, probe_slot, 'Sealable Probe Exit')

    # Continuous shelves overlap 0.15 mm of the PCB edge and run only through
    # component-free solder-mask margins.
    shelf_top = -PCB_THICKNESS
    shelf_bottom = shelf_top - 0.75
    shelf_specs = ((-4.95, -3.85), (21.85, 22.95))
    for index, (x_min, x_max) in enumerate(shelf_specs, start=1):
        shelf = _box(
            component,
            'PCB Edge Shelf {}'.format(index),
            x_min,
            95.00,
            shelf_bottom,
            x_max,
            139.00,
            shelf_top,
            planes,
        )
        base = _join(component, base, shelf, 'Join PCB Edge Shelf {}'.format(index))

    guide_specs = ((-4.95, -4.05), (22.05, 22.95))
    for index, (x_min, x_max) in enumerate(guide_specs, start=1):
        guide = _box(
            component,
            'PCB Side Guide {}'.format(index),
            x_min,
            95.00,
            -1.75,
            x_max,
            139.00,
            -0.20,
            planes,
        )
        base = _join(component, base, guide, 'Join PCB Side Guide {}'.format(index))

    index = 0
    for center_y in (102.0, 130.0):
        for x_min, x_max in ((25.00, 25.45), (-7.45, -7.00)):
            index += 1
            bump = _box(
                component,
                'Snap Bump {}'.format(index),
                x_min,
                center_y - 2.0,
                -1.45,
                x_max,
                center_y + 2.0,
                -0.30,
                planes,
            )
            base = _join(component, base, bump, 'Join Snap Bump {}'.format(index))

    # M2 heat-set-insert bosses occupy the former USB-C side-access position.
    # The solar load is carried on the base rather than the removable lid.
    boss_index = 0
    for x_min in (-12.00, 25.00):
        boss_index += 1
        boss = _cylinder_x(
            component,
            'Solar Pivot Boss {}'.format(boss_index),
            x_min,
            5.00,
            SOLAR_PIVOT_Y,
            SOLAR_PIVOT_Z,
            3.50,
            yz_planes,
        )
        base = _join(component, base, boss, 'Join Solar Boss {}'.format(boss_index))

    insert_specs = (
        (-12.20, SOLAR_INSERT_POCKET_DEPTH + 0.20),
        (30.00 - SOLAR_INSERT_POCKET_DEPTH, SOLAR_INSERT_POCKET_DEPTH + 0.20),
    )
    for index, (x_min, length) in enumerate(insert_specs, start=1):
        insert_tool = _cylinder_x(
            component,
            'Solar Insert Pocket Tool {}'.format(index),
            x_min,
            length,
            SOLAR_PIVOT_Y,
            SOLAR_PIVOT_Z,
            SOLAR_INSERT_POCKET_DIAMETER / 2.0,
            yz_planes,
        )
        base = _cut(component, base, insert_tool, 'Solar Insert Pocket {}'.format(index))

    base.name = 'MSV3 Brain Enclosure Base'
    return component, base


def _build_lid(root_component):
    component = _component(root_component, 'MSV3 Brain Enclosure Lid')
    planes = {}
    yz_planes = {}
    lid = _prism(
        component,
        'Lid Outer',
        OUTER_CONTOUR,
        LID_BOTTOM_Z,
        LID_TOP_Z,
        planes,
    )
    cavity = _prism(
        component,
        'Lid Cavity Tool',
        INNER_CONTOUR,
        -1.75,
        LID_INNER_ROOF_Z,
        planes,
    )
    lid = _cut(component, lid, cavity, 'Hollow Lid')

    probe_slot = _prism(
        component,
        'Sealable Probe Exit Tool',
        (
            (-PROBE_SLOT_XY_CLEARANCE, 81.50),
            (18.00 + PROBE_SLOT_XY_CLEARANCE, 81.50),
            (19.25 + PROBE_SLOT_XY_CLEARANCE, 84.25),
            (-1.25 - PROBE_SLOT_XY_CLEARANCE, 84.25),
        ),
        -PCB_THICKNESS - PROBE_SLOT_Z_CLEARANCE,
        PROBE_SLOT_Z_CLEARANCE,
        planes,
    )
    lid = _cut(component, lid, probe_slot, 'Sealable Probe Exit')

    rail_specs = ((-4.95, -3.85), (21.85, 22.95))
    for index, (x_min, x_max) in enumerate(rail_specs, start=1):
        rail = _box(
            component,
            'PCB Compression Rail {}'.format(index),
            x_min,
            95.00,
            -PCB_CLAMP_INTERFERENCE,
            x_max,
            139.00,
            0.95,
            planes,
        )
        lid = _join(component, lid, rail, 'Join PCB Compression Rail {}'.format(index))

    arm_index = 0
    slot_tools = []
    for center_y in (102.0, 130.0):
        arm_specs = (
            (25.25, 26.45, 24.80, 26.45),
            (-8.45, -7.25, -8.45, -6.80),
        )
        for arm_x_min, arm_x_max, bridge_x_min, bridge_x_max in arm_specs:
            arm_index += 1
            arm = _box(
                component,
                'Snap Arm {}'.format(arm_index),
                arm_x_min,
                center_y - 3.50,
                -2.80,
                arm_x_max,
                center_y + 3.50,
                1.55,
                planes,
            )
            lid = _join(component, lid, arm, 'Join Snap Arm {}'.format(arm_index))
            bridge = _box(
                component,
                'Snap Arm Bridge {}'.format(arm_index),
                bridge_x_min,
                center_y - 3.50,
                0.70,
                bridge_x_max,
                center_y + 3.50,
                1.55,
                planes,
            )
            lid = _join(component, lid, bridge, 'Join Snap Bridge {}'.format(arm_index))
            slot_tools.append(_box(
                component,
                'Snap Window Tool {}'.format(arm_index),
                arm_x_min - 0.20,
                center_y - 2.20,
                -1.60,
                arm_x_max + 0.20,
                center_y + 2.20,
                0.02,
                planes,
            ))

    for index, slot_tool in enumerate(slot_tools, start=1):
        lid = _cut(component, lid, slot_tool, 'Snap Window {}'.format(index))

    for index, center_z in enumerate(CASE_WIRE_ENTRY_CENTERS_Z, start=1):
        wire_entry = _cylinder_x(
            component,
            'Side Wire Entry Tool {}'.format(index),
            -7.20,
            3.00,
            SOLAR_PIVOT_Y,
            center_z,
            CASE_WIRE_ENTRY_DIAMETER / 2.0,
            yz_planes,
        )
        lid = _cut(component, lid, wire_entry, 'Side Wire Entry {}'.format(index))

    lid.name = 'MSV3 Brain Enclosure Lid'
    return component, lid


def _build_solar_arm(root_component, side):
    title = 'Left' if side == 'left' else 'Right'
    component = _component(root_component, 'MSV3 Solar Arm ' + title)
    planes = {}
    yz_planes = {}
    x_min = -13.85 if side == 'left' else 30.25
    x_max = x_min + SOLAR_ARM_THICKNESS
    radius = 3.00

    arm = _cylinder_x(
        component,
        title + ' Arm Enclosure End',
        x_min,
        SOLAR_ARM_THICKNESS,
        SOLAR_PIVOT_Y,
        SOLAR_PIVOT_Z,
        radius,
        yz_planes,
    )
    far_end = _cylinder_x(
        component,
        title + ' Arm Panel End',
        x_min,
        SOLAR_ARM_THICKNESS,
        SOLAR_PANEL_PIVOT_Y,
        SOLAR_PANEL_PIVOT_Z,
        radius,
        yz_planes,
    )
    arm = _join(component, arm, far_end, 'Join Arm Panel End')
    delta_y = SOLAR_PANEL_PIVOT_Y - SOLAR_PIVOT_Y
    delta_z = SOLAR_PANEL_PIVOT_Z - SOLAR_PIVOT_Z
    center_distance = (delta_y * delta_y + delta_z * delta_z) ** 0.5
    unit_y = delta_y / center_distance
    unit_z = delta_z / center_distance
    perp_y = -unit_z
    perp_z = unit_y
    bridge_points = (
        (SOLAR_PIVOT_Y + perp_y * radius, SOLAR_PIVOT_Z + perp_z * radius),
        (SOLAR_PANEL_PIVOT_Y + perp_y * radius, SOLAR_PANEL_PIVOT_Z + perp_z * radius),
        (SOLAR_PANEL_PIVOT_Y - perp_y * radius, SOLAR_PANEL_PIVOT_Z - perp_z * radius),
        (SOLAR_PIVOT_Y - perp_y * radius, SOLAR_PIVOT_Z - perp_z * radius),
    )
    bridge = _prism_yz(
        component,
        title + ' Arm Bridge',
        bridge_points,
        x_min,
        x_max,
        yz_planes,
    )
    arm = _join(component, arm, bridge, 'Join Arm Bridge')

    pivot_centers = (
        (SOLAR_PIVOT_Y, SOLAR_PIVOT_Z),
        (SOLAR_PANEL_PIVOT_Y, SOLAR_PANEL_PIVOT_Z),
    )
    for index, (center_y, center_z) in enumerate(pivot_centers, start=1):
        tool = _cylinder_x(
            component,
            title + ' Arm Pivot Tool {}'.format(index),
            x_min - 0.20,
            SOLAR_ARM_THICKNESS + 0.40,
            center_y,
            center_z,
            SOLAR_CLEARANCE_DIAMETER / 2.0,
            yz_planes,
        )
        arm = _cut(component, arm, tool, 'Arm Pivot Hole {}'.format(index))

    if side == 'left':
        channel_half_width = 1.30
        channel_start_y = SOLAR_PIVOT_Y + unit_y * 1.60
        channel_start_z = SOLAR_PIVOT_Z + unit_z * 1.60
        channel_end_y = SOLAR_PANEL_PIVOT_Y - unit_y * 1.60
        channel_end_z = SOLAR_PANEL_PIVOT_Z - unit_z * 1.60
        channel_points = (
            (channel_start_y + perp_y * channel_half_width,
             channel_start_z + perp_z * channel_half_width),
            (channel_end_y + perp_y * channel_half_width,
             channel_end_z + perp_z * channel_half_width),
            (channel_end_y - perp_y * channel_half_width,
             channel_end_z - perp_z * channel_half_width),
            (channel_start_y - perp_y * channel_half_width,
             channel_start_z - perp_z * channel_half_width),
        )
        channel = _prism_yz(
            component,
            'Solar Lead Channel Tool',
            channel_points,
            x_max - 0.55,
            x_max + 0.10,
            yz_planes,
        )
        arm = _cut(component, arm, channel, 'Solar Lead Channel')

    arm.name = 'MSV3 Solar Arm ' + title
    return component, arm


def _build_solar_panel_frame(root_component):
    component = _component(root_component, 'MSV3 Solar Panel Frame')
    planes = {}
    yz_planes = {}
    center_x = 9.00
    center_y = SOLAR_PANEL_PIVOT_Y
    frame_min_x = center_x - SOLAR_PANEL_FRAME_WIDTH / 2.0
    frame_max_x = center_x + SOLAR_PANEL_FRAME_WIDTH / 2.0
    frame_min_y = center_y - SOLAR_PANEL_FRAME_HEIGHT / 2.0
    frame_max_y = frame_min_y + SOLAR_PANEL_FRAME_HEIGHT
    chamfer = 2.00
    contour = (
        (frame_min_x + chamfer, frame_min_y),
        (frame_max_x - chamfer, frame_min_y),
        (frame_max_x, frame_min_y + chamfer),
        (frame_max_x, frame_max_y - chamfer),
        (frame_max_x - chamfer, frame_max_y),
        (frame_min_x + chamfer, frame_max_y),
        (frame_min_x, frame_max_y - chamfer),
        (frame_min_x, frame_min_y + chamfer),
    )
    frame_front_z = SOLAR_PANEL_BACK_Z + SOLAR_PANEL_THICKNESS
    frame = _prism(
        component,
        'Solar Frame Outer',
        contour,
        SOLAR_PANEL_BACK_Z,
        frame_front_z,
        planes,
    )

    # Front-loading recess with a solid rear wall.
    pocket_min_x = center_x - SOLAR_PANEL_POCKET_WIDTH / 2.0
    pocket_max_x = center_x + SOLAR_PANEL_POCKET_WIDTH / 2.0
    pocket_min_y = center_y - SOLAR_PANEL_POCKET_HEIGHT / 2.0
    pocket_max_y = center_y + SOLAR_PANEL_POCKET_HEIGHT / 2.0
    pocket = _box(
        component,
        'Solar Panel Front Recess Tool',
        pocket_min_x,
        pocket_min_y,
        frame_front_z - SOLAR_PANEL_RECESS_DEPTH,
        pocket_max_x,
        pocket_max_y,
        frame_front_z + 0.25,
        planes,
    )
    frame = _cut(component, frame, pocket, 'Solar Panel Front Recess')

    # The two wire holes are inside the recess and are the only rear openings.
    wire_centers_y = (center_y - 22.50, center_y + 22.50)
    for index, wire_y in enumerate(wire_centers_y, start=1):
        wire_tool = _cylinder_z(
            component,
            'Panel Wire Hole Tool {}'.format(index),
            SOLAR_PANEL_BACK_Z - 0.25,
            SOLAR_PANEL_THICKNESS + 0.50,
            center_x,
            wire_y,
            SOLAR_PANEL_WIRE_HOLE_DIAMETER / 2.0,
            planes,
        )
        frame = _cut(component, frame, wire_tool, 'Panel Wire Hole {}'.format(index))

    for index, x_min in enumerate((-12.00, 26.50), start=1):
        lug = _cylinder_x(
            component,
            'Panel Pivot Lug {}'.format(index),
            x_min,
            3.50,
            SOLAR_PANEL_PIVOT_Y,
            SOLAR_PANEL_PIVOT_Z,
            2.60,
            yz_planes,
        )
        frame = _join(component, frame, lug, 'Join Panel Pivot Lug {}'.format(index))

    insert_specs = (
        (-12.20, SOLAR_INSERT_POCKET_DEPTH + 0.20),
        (30.00 - SOLAR_INSERT_POCKET_DEPTH, SOLAR_INSERT_POCKET_DEPTH + 0.20),
    )
    for index, (x_min, length) in enumerate(insert_specs, start=1):
        insert_tool = _cylinder_x(
            component,
            'Panel Insert Pocket Tool {}'.format(index),
            x_min,
            length,
            SOLAR_PANEL_PIVOT_Y,
            SOLAR_PANEL_PIVOT_Z,
            SOLAR_INSERT_POCKET_DIAMETER / 2.0,
            yz_planes,
        )
        frame = _cut(component, frame, insert_tool, 'Panel Insert Pocket {}'.format(index))

    frame.name = 'MSV3 Solar Panel Frame'
    return component, frame


def _add_reference_parameters(design):
    parameters = design.userParameters
    definitions = (
        ('pcb_thickness', PCB_THICKNESS, 'Correct JLCPCB board thickness'),
        ('pcb_side_clearance', 0.65, 'Nominal XY clearance around PCB'),
        ('base_floor_thickness', BASE_CAVITY_FLOOR_Z - BASE_BOTTOM_Z, 'Base floor'),
        ('base_split_z', BASE_TOP_Z, 'Base side of PCB-midplane split'),
        ('lid_split_z', LID_BOTTOM_Z, 'Lid side of PCB-midplane split'),
        ('lid_roof_thickness', LID_TOP_Z - LID_INNER_ROOF_Z, 'Lid roof'),
        ('snap_deflection', 0.20, 'Nominal skirt deflection over snap bump'),
        ('pcb_clamp_interference', PCB_CLAMP_INTERFERENCE, 'Continuous edge-rail PCB pinch'),
        ('solar_panel_nominal_width', 30.00, 'Nominal solar panel width'),
        ('solar_panel_nominal_height', 54.00, 'Nominal solar panel height'),
        ('solar_panel_pocket_width', SOLAR_PANEL_POCKET_WIDTH, 'Front recess width'),
        ('solar_panel_pocket_height', SOLAR_PANEL_POCKET_HEIGHT, 'Front recess height'),
        ('solar_panel_recess_depth', SOLAR_PANEL_RECESS_DEPTH, 'Front recess depth'),
        ('case_wire_entry_diameter', CASE_WIRE_ENTRY_DIAMETER, 'Twin glue-sealable side entries'),
        ('probe_slot_xy_clearance', PROBE_SLOT_XY_CLEARANCE, 'Probe exit glue clearance'),
        ('probe_slot_z_clearance', PROBE_SLOT_Z_CLEARANCE, 'Probe exit glue clearance'),
        ('solar_arm_thickness', SOLAR_ARM_THICKNESS, 'Lightweight arm thickness'),
        ('solar_arm_center_distance', SOLAR_ARM_CENTER_DISTANCE, 'Minimum roof-clearing pivot spacing'),
        ('solar_panel_tray_thickness', SOLAR_PANEL_THICKNESS, 'Thin tray body'),
        ('solar_pivot_clearance_diameter', SOLAR_CLEARANCE_DIAMETER, 'M2 arm holes'),
        ('solar_insert_pocket_diameter', SOLAR_INSERT_POCKET_DIAMETER, 'M2 heat-set insert pocket'),
        ('solar_insert_pocket_depth', SOLAR_INSERT_POCKET_DEPTH, 'M2 heat-set insert depth'),
    )
    for name, value_mm, comment in definitions:
        parameters.add(
            name,
            adsk.core.ValueInput.createByString('{} mm'.format(value_mm)),
            'mm',
            comment,
        )


def run(_context):
    app = adsk.core.Application.get()
    ui = app.userInterface
    try:
        document = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        root.name = 'MSV3 Probe Brain Enclosure and Solar Mount'

        _add_reference_parameters(design)
        base_component, _base_body = _build_base(root)
        lid_component, _lid_body = _build_lid(root)
        left_arm_component, _left_arm_body = _build_solar_arm(root, 'left')
        right_arm_component, _right_arm_body = _build_solar_arm(root, 'right')
        panel_component, _panel_body = _build_solar_panel_frame(root)

        script_dir = os.path.dirname(os.path.realpath(__file__))
        output_dir = os.path.abspath(os.path.join(script_dir, '..', '..', 'output', 'fusion_exports'))
        os.makedirs(output_dir, exist_ok=True)
        manager = design.exportManager

        archive_path = os.path.join(output_dir, 'MSV3_brain_enclosure_native.f3d')
        manager.execute(manager.createFusionArchiveExportOptions(archive_path, root))
        manager.execute(manager.createSTEPExportOptions(
            os.path.join(output_dir, 'MSV3_brain_enclosure_fusion_assembly.step'),
            root,
        ))
        manager.execute(manager.createSTEPExportOptions(
            os.path.join(output_dir, 'MSV3_brain_enclosure_fusion_base.step'),
            base_component,
        ))
        manager.execute(manager.createSTEPExportOptions(
            os.path.join(output_dir, 'MSV3_brain_enclosure_fusion_lid.step'),
            lid_component,
        ))
        manager.execute(manager.createSTEPExportOptions(
            os.path.join(output_dir, 'MSV3_solar_arm_left_fusion.step'),
            left_arm_component,
        ))
        manager.execute(manager.createSTEPExportOptions(
            os.path.join(output_dir, 'MSV3_solar_arm_right_fusion.step'),
            right_arm_component,
        ))
        manager.execute(manager.createSTEPExportOptions(
            os.path.join(output_dir, 'MSV3_solar_panel_frame_fusion.step'),
            panel_component,
        ))

        app.activeViewport.fit()
        ui.messageBox(
            'MSV3 enclosure created. Native F3D and STEP files were exported to:\n{}'.format(output_dir),
            'MSV3 Brain Enclosure',
        )
    except Exception:
        ui.messageBox('Failed:\n{}'.format(traceback.format_exc()))


def stop(_context):
    pass

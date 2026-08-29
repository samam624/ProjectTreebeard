import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const outputDir = path.join(projectDir, 'output');
const wasmPath = path.join(
  projectDir,
  'tools',
  'node',
  'node_modules',
  'replicad-opencascadejs',
  'dist',
  'replicad_single.wasm',
);

const parameters = {
  // The Altium STEP used 1.812 mm. The real JLCPCB board is 1.60 mm.
  pcbTopZ: 0.0,
  pcbThickness: 1.60,
  pcbSideClearance: 0.65,
  baseBottomZ: -6.35,
  baseCavityFloorZ: -4.25,
  baseTopZ: -0.80,
  lidBottomZ: -0.75,
  lidInnerRoofZ: 5.70,
  lidTopZ: 7.55,
  snapClearance: 0.25,
  snapBumpDepth: 0.45,
  snapArmThickness: 1.20,
  pcbClampInterference: 0.12,
  solarPivotY: 118.50,
  solarPivotZ: -2.85,
  solarArmLength: 36.00,
  solarArmThickness: 1.60,
  solarM2ClearanceDiameter: 2.40,
  solarInsertPocketDiameter: 2.90,
  solarInsertPocketDepth: 3.20,
  caseWireEntryDiameter: 1.60,
  caseWireEntryCentersZ: [2.20, 4.60],
  solarPanelPocketWidth: 30.40,
  solarPanelPocketHeight: 54.40,
  solarPanelNominalWidth: 30.00,
  solarPanelNominalHeight: 54.00,
  solarPanelFrameWidth: 36.00,
  solarPanelFrameHeight: 60.00,
  solarPanelPivotY: 149.68,
  solarPanelPivotZ: 15.15,
  solarPanelBackZ: 13.65,
  solarPanelThickness: 3.00,
  solarPanelRecessDepth: 1.60,
  solarPanelWireHoleDiameter: 2.20,
  probeSlotXYClearance: 0.20,
  probeSlotZClearance: 0.15,
};

// XY coordinates stay aligned to the source PCB STEP file.
// The inner contour clears the brain and continues past the housing at the
// bottom to leave the 18 mm soil probe exposed.
const outerContour = [
  [-2.80, 82.00],
  [20.80, 82.00],
  [25.00, 86.20],
  [25.00, 142.60],
  [20.60, 149.00],
  [-2.60, 149.00],
  [-7.00, 142.60],
  [-7.00, 86.20],
];

const innerContour = [
  [-1.65, 84.00],
  [19.65, 84.00],
  [22.65, 86.35],
  [22.65, 142.25],
  [18.65, 146.65],
  [-0.65, 146.65],
  [-4.65, 142.25],
  [-4.65, 86.35],
];

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

function prism(points, zMin, zMax) {
  const sketcher = new replicad.Sketcher('XY').movePointerTo(points[0]);
  for (const point of points.slice(1)) sketcher.lineTo(point);
  return sketcher.close().extrude(zMax - zMin).translateZ(zMin);
}

function box(xMin, yMin, zMin, xMax, yMax, zMax) {
  return replicad.makeBox([xMin, yMin, zMin], [xMax, yMax, zMax]);
}

function cylinderX(xMin, length, centerY, centerZ, radius) {
  return replicad.makeCylinder(radius, length, [xMin, centerY, centerZ], [1, 0, 0]);
}

function joinAll(shape, additions) {
  let result = shape;
  for (const addition of additions) result = result.fuse(addition);
  return result;
}

function cutAll(shape, tools) {
  let result = shape;
  for (const tool of tools) result = result.cut(tool);
  return result;
}

function buildBase() {
  let base = prism(outerContour, parameters.baseBottomZ, parameters.baseTopZ);
  base = base.cut(prism(innerContour, parameters.baseCavityFloorZ, parameters.baseTopZ + 1));

  // The bottom wall is closed except for a close-fitting, tapered slot around
  // the exposed soil-probe PCB. The 0.15-0.20 mm gap is intended for epoxy.
  const probeSlotContour = [
    [-parameters.probeSlotXYClearance, 81.50],
    [18.00 + parameters.probeSlotXYClearance, 81.50],
    [19.25 + parameters.probeSlotXYClearance, 84.25],
    [-1.25 - parameters.probeSlotXYClearance, 84.25],
  ];
  base = base.cut(prism(
    probeSlotContour,
    -parameters.pcbThickness - parameters.probeSlotZClearance,
    parameters.probeSlotZClearance,
  ));

  // Continuous edge shelves support the PCB along clear solder-mask margins.
  // Their 0.15 mm board overlap is narrow enough to avoid all components.
  const shelfTopZ = -parameters.pcbThickness;
  const shelfBottomZ = shelfTopZ - 0.75;
  const shelves = [
    box(-4.95, 95.0, shelfBottomZ, -3.85, 139.0, shelfTopZ),
    box(21.85, 95.0, shelfBottomZ, 22.95, 139.0, shelfTopZ),
  ];
  base = joinAll(base, shelves);

  // Lower side guides leave 0.05 mm nominal lateral clearance and prevent the
  // board from drifting sideways. They stop below the lid compression rails.
  base = joinAll(base, [
    box(-4.95, 95.0, -1.75, -4.05, 139.0, -0.20),
    box(22.05, 95.0, -1.75, 22.95, 139.0, -0.20),
  ]);

  // Rectangular snap bumps. The mating lid arms flex about 0.20 mm as they
  // pass over these bumps; PETG/ABS/ASA are preferred for repeated use.
  const snapCentersY = [102.0, 130.0];
  const bumps = [];
  for (const centerY of snapCentersY) {
    bumps.push(box(25.00, centerY - 2.0, -1.45, 25.45, centerY + 2.0, -0.30));
    bumps.push(box(-7.45, centerY - 2.0, -1.45, -7.00, centerY + 2.0, -0.30));
  }
  base = joinAll(base, bumps);

  // M2 heat-set-insert bosses occupy the former USB-C side-access position.
  // They carry the solar-panel load on the base rather than the snap lid.
  const bossRadius = 3.50;
  base = base.fuse(cylinderX(-12.00, 5.00, parameters.solarPivotY, parameters.solarPivotZ, bossRadius));
  base = base.fuse(cylinderX(25.00, 5.00, parameters.solarPivotY, parameters.solarPivotZ, bossRadius));
  const insertRadius = parameters.solarInsertPocketDiameter / 2;
  base = base.cut(cylinderX(-12.20, parameters.solarInsertPocketDepth + 0.20,
    parameters.solarPivotY, parameters.solarPivotZ, insertRadius));
  base = base.cut(cylinderX(30.00 - parameters.solarInsertPocketDepth, parameters.solarInsertPocketDepth + 0.20,
    parameters.solarPivotY, parameters.solarPivotZ, insertRadius));

  return base;
}

function buildLid() {
  let lid = prism(outerContour, parameters.lidBottomZ, parameters.lidTopZ);
  lid = lid.cut(prism(innerContour, -1.75, parameters.lidInnerRoofZ));

  // The matching upper half of the probe exit allows the PCB to be laid into
  // the base and pinched/sealed when the lid is installed.
  const probeSlotContour = [
    [-parameters.probeSlotXYClearance, 81.50],
    [18.00 + parameters.probeSlotXYClearance, 81.50],
    [19.25 + parameters.probeSlotXYClearance, 84.25],
    [-1.25 - parameters.probeSlotXYClearance, 84.25],
  ];
  lid = lid.cut(prism(
    probeSlotContour,
    -parameters.pcbThickness - parameters.probeSlotZClearance,
    parameters.probeSlotZClearance,
  ));

  // Two continuous rails replace the four block-like internal pads. Their
  // lower faces produce 0.12 mm compression against the 1.60 mm PCB while
  // remaining outside every component envelope.
  lid = joinAll(lid, [
    box(-4.95, 95.0, -parameters.pcbClampInterference, -3.85, 139.0, 0.95),
    box(21.85, 95.0, -parameters.pcbClampInterference, 22.95, 139.0, 0.95),
  ]);

  const snapCentersY = [102.0, 130.0];
  const arms = [];
  const slotTools = [];
  for (const centerY of snapCentersY) {
    // Right and left external cantilever arms, tied into the lid above the seam.
    arms.push(box(25.25, centerY - 3.50, -2.80, 26.45, centerY + 3.50, 1.55));
    arms.push(box(24.80, centerY - 3.50, 0.70, 26.45, centerY + 3.50, 1.55));
    arms.push(box(-8.45, centerY - 3.50, -2.80, -7.25, centerY + 3.50, 1.55));
    arms.push(box(-8.45, centerY - 3.50, 0.70, -6.80, centerY + 3.50, 1.55));

    // Windows leave 1.3 mm rails at each side of every arm.
    slotTools.push(box(25.05, centerY - 2.20, -1.60, 26.65, centerY + 2.20, 0.02));
    slotTools.push(box(-8.65, centerY - 2.20, -1.60, -7.05, centerY + 2.20, 0.02));
  }
  lid = joinAll(lid, arms);
  lid = cutAll(lid, slotTools);

  // Two tiny, glue-sealable solar-lead entries pass through the left side wall
  // beside the arm channel, safely away from the top antenna region.
  const wireRadius = parameters.caseWireEntryDiameter / 2;
  for (const centerZ of parameters.caseWireEntryCentersZ) {
    lid = lid.cut(cylinderX(-7.20, 3.00, parameters.solarPivotY, centerZ, wireRadius));
  }
  return lid;
}

function buildSolarArm(side) {
  const isLeft = side === 'left';
  const xMin = isLeft ? -13.85 : 30.25;
  const xMax = xMin + parameters.solarArmThickness;
  const radius = 3.00;
  const nearY = parameters.solarPivotY;
  const nearZ = parameters.solarPivotZ;
  const farY = parameters.solarPanelPivotY;
  const farZ = parameters.solarPanelPivotZ;
  const deltaY = farY - nearY;
  const deltaZ = farZ - nearZ;
  const centerDistance = Math.hypot(deltaY, deltaZ);
  const bridgeAngle = Math.atan2(deltaZ, deltaY) * 180 / Math.PI;
  let arm = cylinderX(xMin, parameters.solarArmThickness, nearY, nearZ, radius);
  arm = arm.fuse(cylinderX(xMin, parameters.solarArmThickness,
    farY, farZ, radius));
  const bridge = box(
    xMin,
    nearY,
    nearZ - radius,
    xMax,
    nearY + centerDistance,
    nearZ + radius,
  ).rotate(bridgeAngle, [0, nearY, nearZ], [1, 0, 0]);
  arm = arm.fuse(bridge);
  const holeRadius = parameters.solarM2ClearanceDiameter / 2;
  arm = arm.cut(cylinderX(xMin - 0.20, parameters.solarArmThickness + 0.40,
    parameters.solarPivotY, parameters.solarPivotZ, holeRadius));
  arm = arm.cut(cylinderX(xMin - 0.20, parameters.solarArmThickness + 0.40,
    farY, parameters.solarPanelPivotZ, holeRadius));

  // A shallow open groove on the left arm carries both solar leads down the
  // enclosure side. It stops clear of both M2 pivot holes.
  if (isLeft) {
    const channel = box(
      xMax - 0.55,
      nearY + 1.60,
      nearZ - 1.30,
      xMax + 0.10,
      nearY + centerDistance - 1.60,
      nearZ + 1.30,
    ).rotate(bridgeAngle, [0, nearY, nearZ], [1, 0, 0]);
    arm = arm.cut(channel);
  }
  return arm;
}

function buildSolarPanelFrame() {
  const frameMinX = 9.0 - parameters.solarPanelFrameWidth / 2;
  const frameMaxX = 9.0 + parameters.solarPanelFrameWidth / 2;
  const frameMinY = parameters.solarPanelPivotY - parameters.solarPanelFrameHeight / 2;
  const frameMaxY = frameMinY + parameters.solarPanelFrameHeight;
  const chamfer = 2.0;
  const frameContour = [
    [frameMinX + chamfer, frameMinY],
    [frameMaxX - chamfer, frameMinY],
    [frameMaxX, frameMinY + chamfer],
    [frameMaxX, frameMaxY - chamfer],
    [frameMaxX - chamfer, frameMaxY],
    [frameMinX + chamfer, frameMaxY],
    [frameMinX, frameMaxY - chamfer],
    [frameMinX, frameMinY + chamfer],
  ];
  const frameFrontZ = parameters.solarPanelBackZ + parameters.solarPanelThickness;
  let frame = prism(frameContour, parameters.solarPanelBackZ, frameFrontZ);

  // Front-loading recess: 30.4 x 54.4 mm for a nominal 30 x 54 mm panel.
  // The 1.4 mm rear wall keeps the holder thin, light, and glue-sealable.
  const pocketCx = 9.0;
  const pocketCy = parameters.solarPanelPivotY;
  const pocketMinX = pocketCx - parameters.solarPanelPocketWidth / 2;
  const pocketMaxX = pocketCx + parameters.solarPanelPocketWidth / 2;
  const pocketMinY = pocketCy - parameters.solarPanelPocketHeight / 2;
  const pocketMaxY = pocketCy + parameters.solarPanelPocketHeight / 2;
  const recessBottomZ = frameFrontZ - parameters.solarPanelRecessDepth;
  frame = frame.cut(box(pocketMinX, pocketMinY, recessBottomZ,
    pocketMaxX, pocketMaxY, frameFrontZ + 0.25));

  // The only rear-wall openings are two individual wire passages. Both sit
  // inside the solar-panel recess near its upper and lower ends.
  const wireRadius = parameters.solarPanelWireHoleDiameter / 2;
  const lowerWireY = pocketCy - 22.50;
  const upperWireY = pocketCy + 22.50;
  frame = frame.cut(replicad.makeCylinder(wireRadius, parameters.solarPanelThickness + 0.50,
    [9.0, lowerWireY, parameters.solarPanelBackZ - 0.25], [0, 0, 1]));
  frame = frame.cut(replicad.makeCylinder(wireRadius, parameters.solarPanelThickness + 0.50,
    [9.0, upperWireY, parameters.solarPanelBackZ - 0.25], [0, 0, 1]));

  // Lightweight local lugs provide enough thickness for the M2 inserts while
  // the rest of the panel tray remains only 3 mm thick.
  const lugRadius = 2.60;
  frame = frame.fuse(cylinderX(-12.00, 3.50,
    parameters.solarPanelPivotY, parameters.solarPanelPivotZ, lugRadius));
  frame = frame.fuse(cylinderX(26.50, 3.50,
    parameters.solarPanelPivotY, parameters.solarPanelPivotZ, lugRadius));

  // Blind M2 heat-set-insert pockets are centered vertically on the carrier.
  const insertRadius = parameters.solarInsertPocketDiameter / 2;
  frame = frame.cut(cylinderX(-12.20, parameters.solarInsertPocketDepth + 0.20,
    parameters.solarPanelPivotY, parameters.solarPanelPivotZ, insertRadius));
  frame = frame.cut(cylinderX(30.00 - parameters.solarInsertPocketDepth,
    parameters.solarInsertPocketDepth + 0.20,
    parameters.solarPanelPivotY, parameters.solarPanelPivotZ, insertRadius));
  return frame;
}

async function writeBlob(filePath, blob) {
  const bytes = Buffer.from(await blob.arrayBuffer());
  fs.writeFileSync(filePath, bytes);
}

fs.mkdirSync(outputDir, { recursive: true });

const base = buildBase();
const lid = buildLid();
const solarArmLeft = buildSolarArm('left');
const solarArmRight = buildSolarArm('right');
const solarPanelFrame = buildSolarPanelFrame();

await writeBlob(path.join(outputDir, 'MSV3_brain_enclosure_base.step'), base.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_brain_enclosure_lid.step'), lid.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_solar_arm_left.step'), solarArmLeft.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_solar_arm_right.step'), solarArmRight.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_solar_panel_frame.step'), solarPanelFrame.blobSTEP());
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_assembly.step'),
  replicad.exportSTEP([
    { shape: base, name: 'MSV3 Brain Enclosure Base', color: '#3d647a' },
    { shape: lid, name: 'MSV3 Brain Enclosure Lid', color: '#8fb3c7' },
    { shape: solarArmLeft, name: 'MSV3 Solar Arm Left', color: '#d5a84b' },
    { shape: solarArmRight, name: 'MSV3 Solar Arm Right', color: '#d5a84b' },
    { shape: solarPanelFrame, name: 'MSV3 Solar Panel Frame', color: '#4a545c' },
  ]),
);

const explodedLid = lid.clone().translateX(40);
const explodedLeftArm = solarArmLeft.clone().translateX(-12);
const explodedRightArm = solarArmRight.clone().translateX(12);
const explodedPanelFrame = solarPanelFrame.clone().translateY(25);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_exploded.step'),
  replicad.exportSTEP([
    { shape: base, name: 'MSV3 Brain Enclosure Base', color: '#3d647a' },
    { shape: explodedLid, name: 'MSV3 Brain Enclosure Lid', color: '#8fb3c7' },
    { shape: explodedLeftArm, name: 'MSV3 Solar Arm Left', color: '#d5a84b' },
    { shape: explodedRightArm, name: 'MSV3 Solar Arm Right', color: '#d5a84b' },
    { shape: explodedPanelFrame, name: 'MSV3 Solar Panel Frame', color: '#4a545c' },
  ]),
);

const baseForPrint = base.clone().translateZ(-parameters.baseBottomZ);
const lidForPrint = lid.clone().mirror('XY', [0, 0, parameters.lidTopZ]).translateZ(-parameters.lidTopZ);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_base_print.stl'),
  baseForPrint.blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_lid_print.stl'),
  lidForPrint.blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);

function armForPrint(arm) {
  const rotated = arm.clone().rotate(90, [0, 0, 0], [0, 1, 0]);
  return rotated.translateZ(-rotated.boundingBox.bounds[0][2]);
}

// Print the closed rear wall on the bed with the panel recess facing upward.
// This avoids bridging the full 30 x 54 mm recess.
const solarPanelFrameForPrint = solarPanelFrame.clone()
  .translateZ(-solarPanelFrame.boundingBox.bounds[0][2]);
await writeBlob(path.join(outputDir, 'MSV3_solar_arm_left_print.stl'),
  armForPrint(solarArmLeft).blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }));
await writeBlob(path.join(outputDir, 'MSV3_solar_arm_right_print.stl'),
  armForPrint(solarArmRight).blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }));
await writeBlob(path.join(outputDir, 'MSV3_solar_panel_frame_print.stl'),
  solarPanelFrameForPrint.blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }));

const report = {
  version: 'prototype-7-roof-clearance-arms',
  units: 'mm',
  parameters,
  sourcePcbBrain: {
    width: 26.0,
    yMin: 87.0,
    yMax: 146.0,
    actualThickness: 1.60,
    stepThicknessIgnored: 1.81224174,
    modeledComponentEnvelope: { zMin: -3.40, zMax: 4.80 },
  },
  enclosure: {
    exteriorBoundsWithoutSolarArms: { xMin: -12.0, xMax: 30.0, yMin: 82.0, yMax: 149.0, zMin: -6.35, zMax: 7.55 },
    nominalBodyWidth: 32.0,
    maximumSnapWidth: 34.90,
    height: 13.90,
    splitPlane: {
      baseTopZ: parameters.baseTopZ,
      lidBottomZ: parameters.lidBottomZ,
      position: 'through PCB thickness so both halves form the probe exit',
    },
    usbOpening: 'removed; enclosure wall is closed',
    pcbClampInterference: parameters.pcbClampInterference,
    probeExitSeal: {
      xyClearance: parameters.probeSlotXYClearance,
      zClearance: parameters.probeSlotZClearance,
      splitBetweenBaseAndLid: true,
      sealingMethod: 'fill close-fitting perimeter with epoxy or outdoor polyurethane adhesive',
    },
    caseWireEntries: {
      count: 2,
      diameter: parameters.caseWireEntryDiameter,
      centersZ: parameters.caseWireEntryCentersZ,
      location: 'left side wall beside channeled arm; glue-sealable',
    },
  },
  solarAssembly: {
    panelNominalSize: [parameters.solarPanelNominalWidth, parameters.solarPanelNominalHeight],
    panelFrontRecessSize: [parameters.solarPanelPocketWidth, parameters.solarPanelPocketHeight],
    frameOuterSize: [parameters.solarPanelFrameWidth, parameters.solarPanelFrameHeight],
    armPivotCenterDistance: parameters.solarArmLength,
    panelRecessDepth: parameters.solarPanelRecessDepth,
    trayThickness: parameters.solarPanelThickness,
    closedRearWallThickness: parameters.solarPanelThickness - parameters.solarPanelRecessDepth,
    foldedState: 'panel tray parallel above the enclosure; arms at full 36.00 mm center distance',
    roofSweepClearance: {
      limitingCornerDistanceFromPivot: Math.hypot(149.00 - parameters.solarPivotY,
        parameters.lidTopZ - parameters.solarPivotZ),
      armEndRadius: 3.00,
      minimumRadialClearance: parameters.solarArmLength - 3.00
        - Math.hypot(149.00 - parameters.solarPivotY,
          parameters.lidTopZ - parameters.solarPivotZ),
    },
    pivotFastener: 'M2 screw into 2.90 mm diameter x 3.20 mm deep heat-set-insert pocket; tighten for friction adjustment',
    armClearanceHoleDiameter: parameters.solarM2ClearanceDiameter,
    insertPocket: {
      diameter: parameters.solarInsertPocketDiameter,
      depth: parameters.solarInsertPocketDepth,
    },
    wireHoles: { count: 2, diameter: parameters.solarPanelWireHoleDiameter,
      centersY: [parameters.solarPanelPivotY - 22.50, parameters.solarPanelPivotY + 22.50],
      position: 'inside panel recess; glue-sealable' },
    wireRouting: 'open 2.60 x 0.55 mm channel on left arm to twin side entries',
  },
  printGuidance: {
    preferredMaterials: ['PETG', 'ASA', 'ABS'],
    prototypeLayerHeight: 0.20,
    perimeters: 4,
    baseOrientation: 'floor down (already oriented in STL)',
    lidOrientation: 'exterior roof down (already oriented in STL)',
    solarArmOrientation: 'flat (already oriented in STL)',
    solarPanelFrameOrientation: 'closed rear wall down, front recess up (already oriented in STL)',
    supports: 'none expected',
  },
};
fs.writeFileSync(path.join(outputDir, 'design_report.json'), `${JSON.stringify(report, null, 2)}\n`);

console.log(JSON.stringify({
  outputDir,
  baseBounds: base.boundingBox.bounds,
  lidBounds: lid.boundingBox.bounds,
  solarArmLeftBounds: solarArmLeft.boundingBox.bounds,
  solarArmRightBounds: solarArmRight.boundingBox.bounds,
  solarPanelFrameBounds: solarPanelFrame.boundingBox.bounds,
  files: fs.readdirSync(outputDir).sort(),
}, null, 2));

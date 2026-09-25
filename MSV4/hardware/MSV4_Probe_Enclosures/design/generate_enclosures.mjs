import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import opencascade from '../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const outputDir = path.join(projectDir, 'output');
const sourcePcbPath = path.resolve(projectDir, '..', 'MSV4.step');
const wasmPath = path.resolve(
  projectDir,
  '..',
  '..',
  '..',
  'MSV3',
  'hardware',
  'MSV3_Probe_PCB_Enclosure',
  'tools',
  'node',
  'node_modules',
  'replicad-opencascadejs',
  'dist',
  'replicad_single.wasm',
);

const p = {
  pcbTopZ: 0.0,
  pcbActualThickness: 1.60,
  pcbStepThickness: 0.41148,
  pcbSideClearance: 0.35,
  boardHeadXMin: -13.0,
  boardHeadXMax: 13.0,
  boardHeadYMin: 98.0,
  boardHeadYMax: 145.0,

  caseXMin: -18.30,
  caseXMax: 18.30,
  caseYMin: 94.80,
  caseYMax: 148.50,
  cavityXMin: -13.35,
  cavityXMax: 13.35,
  cavityYMin: 101.70,
  cavityYMax: 145.30,
  baseBottomZ: -4.50,
  baseFloorTopZ: -3.30,
  baseTopZ: 0.25,
  lidBottomZ: 0.30,
  lidInnerRoofZ: 4.00,
  armLidTopZ: 5.00,
  integratedLidTopZ: 6.40,
  pcbClampInterference: 0.10,
  frontTransitionEndY: 98.80,
  probeOuterBottomZ: -2.30,
  probeOuterTopZ: 0.70,

  probeSlotXYClearance: 0.25,
  probeSlotZClearance: 0.20,
  gasketGrooveDepth: 0.55,
  gasketGrooveWidth: 0.80,
  gasketNominalThickness: 0.80,

  screwClearanceDiameter: 2.40,
  countersinkTopDiameter: 4.20,
  countersinkDepth: 0.90,
  insertPocketDiameter: 2.90,
  insertPocketDepth: 3.20,
  screwTipReliefDiameter: 2.40,
  screwTipReliefBottomZ: -3.45,
  screwCenters: [
    { x: -15.30, y: 101.50 },
    { x: 15.30, y: 101.50 },
    { x: -13.80, y: 143.50 },
    { x: 13.80, y: 143.50 },
  ],

  solarPanelNominalLength: 36.00,
  solarPanelNominalWidth: 30.00,
  solarPanelPocketLength: 36.40,
  solarPanelPocketWidth: 30.40,
  solarPanelRecessDepth: 1.60,
  integratedPanelCenterY: 122.00,
  integratedPanelBackWall: 0.80,
  panelWireHoleDiameter: 1.60,

  // D1 is the upper of the two visible bottom-side packages in MSV4.step. The
  // modeled LED lens is centered at Y=138.0. The teardrop point faces upward,
  // toward the antenna/top edge, matching the earlier enclosure language.
  ledCenterX: -10.00,
  ledCenterY: 138.00000113987,
  ledWindowRadius: 1.55,
  ledWindowTipY: 140.82,

  // The E73 PCB antenna occupies the board-end keepout.  The solar panel ends
  // 0.75 mm before it; keep these as separately validated design controls.
  antennaKeepoutYMin: 140.75,
  antennaKeepoutYMax: 145.00,

  // Two-piece screwless enclosure. The back/base carries the PCB-shaped
  // seating groove; the solar lid is bonded to it at the plain seam.
  minimalBaseBottomZ: -4.25,
  minimalBaseFloorTopZ: -3.25,
  minimalBaseTopZ: 0.25,
  minimalLidBottomZ: 0.30,
  minimalLidTopZ: 6.40,
  minimalGrooveXYClearance: 0.25,
  minimalGrooveBottomZ: -1.75,
  minimalGrooveTopZ: 0.15,
  minimalOuterX: 16.20,
  minimalOuterYMin: 96.00,
  minimalOuterYMax: 146.20,

  armPivotY: 121.50,
  armPivotZ: -2.10,
  armPanelPivotY: 149.20,
  armPanelPivotZ: 14.00,
  armThickness: 1.60,
  armEndRadius: 2.80,
  armClearanceDiameter: 2.40,
  armInsertPocketDiameter: 2.90,
  armInsertPocketDepth: 3.20,
  pivotBossRadius: 3.00,
  panelFrameWidth: 32.80,
  panelFrameLength: 38.80,
  panelFrameBackZ: 12.70,
  panelFrameThickness: 2.60,
  panelFrameCornerRadius: 1.40,
  caseWireEntryDiameter: 1.60,
  caseWireEntryCentersZ: [1.35, 3.20],

  batteryNominal: [10.0, 20.0, 2.0],
  batteryReference: [10.4, 20.4, 2.2],
  batteryCenterX: -7.00,
  batteryCenterY: 113.00,
  batteryBottomZ: 1.70,
};

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

function box(xMin, yMin, zMin, xMax, yMax, zMax) {
  return replicad.makeBox([xMin, yMin, zMin], [xMax, yMax, zMax]);
}

function cylinderZ(zMin, length, centerX, centerY, radius) {
  return replicad.makeCylinder(radius, length, [centerX, centerY, zMin], [0, 0, 1]);
}

function cylinderX(xMin, length, centerY, centerZ, radius) {
  return replicad.makeCylinder(radius, length, [xMin, centerY, centerZ], [1, 0, 0]);
}

function prism(points, zMin, zMax) {
  const sketcher = new replicad.Sketcher('XY').movePointerTo(points[0]);
  for (const point of points.slice(1)) sketcher.lineTo(point);
  return sketcher.close().extrude(zMax - zMin).translateZ(zMin);
}

function teardropPrism(centerX, centerY, radius, tipY, zMin, zMax) {
  const direction = Math.sign(tipY - centerY) || 1;
  const sketcher = new replicad.Sketcher('XY')
    .movePointerTo([centerX, tipY])
    .bezierCurveTo(
      [centerX - radius, centerY],
      [[centerX - 0.15, centerY + direction * radius * 1.15], [centerX - radius, centerY + direction * radius * 0.70]],
    )
    .bezierCurveTo(
      [centerX, centerY - direction * radius],
      [[centerX - radius, centerY - direction * radius * 0.65], [centerX - radius * 0.55, centerY - direction * radius]],
    )
    .bezierCurveTo(
      [centerX + radius, centerY],
      [[centerX + radius * 0.55, centerY - direction * radius], [centerX + radius, centerY - direction * radius * 0.65]],
    )
    .bezierCurveTo(
      [centerX, tipY],
      [[centerX + radius, centerY + direction * radius * 0.70], [centerX + 0.15, centerY + direction * radius * 1.15]],
    );
  return sketcher.close().extrude(zMax - zMin).translateZ(zMin);
}

function prismYZ(points, xMin, xMax) {
  const sketcher = new replicad.Sketcher('YZ', xMin).movePointerTo(points[0]);
  for (const point of points.slice(1)) sketcher.lineTo(point);
  return sketcher.close().extrude(xMax - xMin);
}

function roundedRect(xMin, yMin, xMax, yMax, radius, zMin, zMax) {
  let result = box(xMin + radius, yMin, zMin, xMax - radius, yMax, zMax)
    .fuse(box(xMin, yMin + radius, zMin, xMax, yMax - radius, zMax));
  for (const x of [xMin + radius, xMax - radius]) {
    for (const y of [yMin + radius, yMax - radius]) {
      result = result.fuse(cylinderZ(zMin, zMax - zMin, x, y, radius));
    }
  }
  return result;
}

const outerContour = [
  [-10.20, 94.80],
  [10.20, 94.80],
  [10.20, 96.00],
  [14.00, 97.50],
  [18.30, 98.80],
  [18.30, 104.50],
  [16.60, 108.00],
  [16.60, 146.20],
  [11.20, 148.50],
  [-11.20, 148.50],
  [-16.60, 146.20],
  [-16.60, 108.00],
  [-18.30, 104.50],
  [-18.30, 98.80],
  [-14.00, 97.50],
  [-10.20, 96.00],
];

const cavityContour = [
  [-13.35, 101.70],
  [13.35, 101.70],
  [13.35, 141.25],
  [9.35, 145.30],
  [-9.35, 145.30],
  [-13.35, 141.25],
];

const gasketOuterContour = [
  [-9.75, 97.30],
  [9.75, 97.30],
  [13.75, 101.50],
  [13.75, 141.45],
  [9.75, 145.70],
  [-9.75, 145.70],
  [-13.75, 141.45],
  [-13.75, 101.50],
];

const gasketInnerContour = [
  [-8.95, 98.10],
  [8.95, 98.10],
  [12.95, 101.90],
  [12.95, 141.05],
  [8.95, 144.90],
  [-8.95, 144.90],
  [-12.95, 141.05],
  [-12.95, 101.90],
];

const minimalOuterContour = [
  [-10.20, p.minimalOuterYMin],
  [10.20, p.minimalOuterYMin],
  [14.20, 100.00],
  [p.minimalOuterX, 104.00],
  [p.minimalOuterX, 140.70],
  [10.20, p.minimalOuterYMax],
  [-10.20, p.minimalOuterYMax],
  [-p.minimalOuterX, 140.70],
  [-p.minimalOuterX, 104.00],
  [-14.20, 100.00],
];

const minimalBoardGrooveContour = [
  [-9.00 - p.minimalGrooveXYClearance, 95.75],
  [9.00 + p.minimalGrooveXYClearance, 95.75],
  [13.00 + p.minimalGrooveXYClearance, 101.75],
  [13.00 + p.minimalGrooveXYClearance, 141.25],
  [9.00 + p.minimalGrooveXYClearance, 145.25],
  [-9.00 - p.minimalGrooveXYClearance, 145.25],
  [-13.00 - p.minimalGrooveXYClearance, 141.25],
  [-13.00 - p.minimalGrooveXYClearance, 101.75],
];

function outerCase(zMin, zMax) {
  return prism(outerContour, zMin, zMax);
}

function cavity(zMin, zMax) {
  return prism(cavityContour, zMin, zMax);
}

function taperBaseAtProbe(shape) {
  const tool = prismYZ([
    [p.caseYMin - 0.20, p.baseBottomZ - 1.00],
    [p.frontTransitionEndY, p.baseBottomZ - 1.00],
    [p.frontTransitionEndY, p.baseBottomZ],
    [p.caseYMin, p.probeOuterBottomZ],
    [p.caseYMin - 0.20, p.probeOuterBottomZ],
  ], p.caseXMin - 1.00, p.caseXMax + 1.00);
  return shape.cut(tool);
}

function taperLidAtProbe(shape, topZ) {
  const tool = prismYZ([
    [p.caseYMin - 0.20, p.probeOuterTopZ],
    [p.caseYMin, p.probeOuterTopZ],
    [p.frontTransitionEndY, topZ],
    [p.frontTransitionEndY, topZ + 1.00],
    [p.caseYMin - 0.20, topZ + 1.00],
  ], p.caseXMin - 1.00, p.caseXMax + 1.00);
  return shape.cut(tool);
}

function probeSlot(zMin, zMax) {
  // This follows the exported MSV4 outline through its blade-to-head shoulder.
  // Only the electronics head above the shoulder is enclosed.
  return prism([
    [-9.25, 94.20],
    [9.25, 94.20],
    [9.25, 98.00],
    [13.25, 102.25],
    [-13.25, 102.25],
    [-9.25, 98.00],
  ], zMin, zMax);
}

function gasketRing(zMin, zMax, clearance = 0.0) {
  const expand = (points, amount) => points.map(([x, y]) => [
    x + Math.sign(x) * amount,
    y + (y < 100 ? -amount : y > 143 ? amount : 0),
  ]);
  const outer = prism(expand(gasketOuterContour, clearance), zMin, zMax);
  const inner = prism(expand(gasketInnerContour, -clearance), zMin - 0.10, zMax + 0.10);
  return outer.cut(inner).cut(probeSlot(zMin - 0.20, zMax + 0.20));
}

function countersink(location, lidTopZ) {
  const bottomZ = lidTopZ - p.countersinkDepth;
  const bottom = replicad.sketchCircle(
    p.screwClearanceDiameter / 2,
    { plane: 'XY', origin: [location.x, location.y, bottomZ] },
  );
  const top = replicad.sketchCircle(
    p.countersinkTopDiameter / 2,
    { plane: 'XY', origin: [location.x, location.y, lidTopZ + 0.02] },
  );
  return bottom.loftWith(top, { ruled: true });
}

function addCaseFasteners(baseShape, lidShape, lidTopZ) {
  let base = baseShape;
  let lid = lidShape;
  for (const location of p.screwCenters) {
    const leadIn = cylinderZ(
      p.baseTopZ - 0.35,
      0.45,
      location.x,
      location.y,
      p.insertPocketDiameter / 2 + 0.20,
    );
    base = base.cut(leadIn);
    base = base.cut(cylinderZ(
      p.baseTopZ - p.insertPocketDepth,
      p.insertPocketDepth + 0.20,
      location.x,
      location.y,
      p.insertPocketDiameter / 2,
    ));
    base = base.cut(cylinderZ(
      p.screwTipReliefBottomZ,
      p.baseTopZ - p.insertPocketDepth - p.screwTipReliefBottomZ + 0.10,
      location.x,
      location.y,
      p.screwTipReliefDiameter / 2,
    ));

    lid = lid.cut(cylinderZ(
      p.lidBottomZ - 0.20,
      lidTopZ - p.lidBottomZ + 0.40,
      location.x,
      location.y,
      p.screwClearanceDiameter / 2,
    ));
    lid = lid.cut(countersink(location, lidTopZ));
    lid = lid.cut(cylinderZ(
      lidTopZ - 0.01,
      0.20,
      location.x,
      location.y,
      p.countersinkTopDiameter / 2,
    ));
  }
  return { base, lid };
}

function buildCommonBase(withArmPivots) {
  let base = outerCase(p.baseBottomZ, p.baseTopZ)
    .cut(cavity(p.baseFloorTopZ, p.baseTopZ + 0.50))
    .cut(probeSlot(
      -p.pcbActualThickness - p.probeSlotZClearance,
      p.pcbTopZ + p.probeSlotZClearance,
    ));
  base = taperBaseAtProbe(base);

  base = base.cut(teardropPrism(
    p.ledCenterX,
    p.ledCenterY,
    p.ledWindowRadius,
    p.ledWindowTipY,
    p.baseBottomZ - 0.20,
    p.baseTopZ + 0.20,
  ));

  // Continuous narrow edge shelves and guides use the component-free outer
  // PCB margins. The support overlap is only 0.15 mm per side.
  const shelfTop = -p.pcbActualThickness;
  base = base
    .fuse(box(-13.50, 104.00, shelfTop - 0.75, -12.85, 140.00, shelfTop))
    .fuse(box(12.85, 104.00, shelfTop - 0.75, 13.50, 140.00, shelfTop))
    .fuse(box(-13.50, 104.00, -1.80, -13.10, 140.00, -0.25))
    .fuse(box(13.10, 104.00, -1.80, 13.50, 140.00, -0.25));

  if (withArmPivots) {
    base = base
      .fuse(cylinderX(-20.10, 3.50, p.armPivotY, p.armPivotZ, p.pivotBossRadius))
      .fuse(cylinderX(16.60, 3.50, p.armPivotY, p.armPivotZ, p.pivotBossRadius))
      .cut(cylinderX(
        -20.30,
        p.armInsertPocketDepth + 0.20,
        p.armPivotY,
        p.armPivotZ,
        p.armInsertPocketDiameter / 2,
      ))
      .cut(cylinderX(
        20.30 - p.armInsertPocketDepth - 0.20,
        p.armInsertPocketDepth + 0.20,
        p.armPivotY,
        p.armPivotZ,
        p.armInsertPocketDiameter / 2,
      ));
  }
  return base;
}

function buildMinimalSolarBase() {
  // The board-shaped top recess makes the PCB drop into one unambiguous
  // position. A deeper local cavity clears every corrected bottom component.
  let base = prism(minimalOuterContour, p.minimalBaseBottomZ, p.minimalBaseTopZ)
    .cut(prism(
      minimalBoardGrooveContour,
      p.minimalGrooveBottomZ,
      p.minimalBaseTopZ + 0.20,
    ))
    .cut(cavity(p.minimalBaseFloorTopZ, p.minimalGrooveBottomZ + 0.05));

  base = base.cut(teardropPrism(
    p.ledCenterX,
    p.ledCenterY,
    p.ledWindowRadius,
    p.ledWindowTipY,
    p.minimalBaseBottomZ - 0.20,
    p.minimalBaseTopZ + 0.20,
  ));
  return base;
}

function buildMinimalSolarLid() {
  let lid = prism(minimalOuterContour, p.minimalLidBottomZ, p.minimalLidTopZ)
    .cut(cavity(p.minimalLidBottomZ - 0.20, p.lidInnerRoofZ));

  const pocketMinX = -p.solarPanelPocketWidth / 2;
  const pocketMaxX = p.solarPanelPocketWidth / 2;
  const pocketMinY = p.integratedPanelCenterY - p.solarPanelPocketLength / 2;
  const pocketMaxY = p.integratedPanelCenterY + p.solarPanelPocketLength / 2;
  const pocketBottomZ = p.minimalLidTopZ - p.solarPanelRecessDepth;
  lid = lid.cut(box(
    pocketMinX,
    pocketMinY,
    pocketBottomZ,
    pocketMaxX,
    pocketMaxY,
    p.minimalLidTopZ + 0.25,
  ));
  for (const x of [-2.00, 2.00]) {
    lid = lid.cut(cylinderZ(
      p.lidInnerRoofZ - 0.20,
      p.minimalLidTopZ - p.lidInnerRoofZ + 0.40,
      x,
      pocketMinY + 2.20,
      p.panelWireHoleDiameter / 2,
    ));
  }
  return lid;
}

function buildLidShell(kind) {
  const isIntegrated = kind === 'integrated';
  const topZ = isIntegrated ? p.integratedLidTopZ : p.armLidTopZ;

  // One continuous top skin replaces the old stepped roof and screw scallops.
  // The PCB-following outline carries straight into the exposed probe blade.
  let lid = outerCase(p.lidBottomZ, topZ)
    .cut(cavity(p.lidBottomZ - 0.20, p.lidInnerRoofZ));

  lid = lid.cut(probeSlot(
    -p.pcbActualThickness - p.probeSlotZClearance,
    p.pcbTopZ + p.probeSlotZClearance,
  ));
  lid = taperLidAtProbe(lid, topZ);

  // Compression rails provide 0.10 mm nominal pinch on the PCB edge. They
  // remain inside the gasket and rise to the roof, so the sealing groove does
  // not isolate them as loose solids.
  lid = lid
    .fuse(box(
      -12.95,
      105.00,
      -p.pcbClampInterference,
      -12.75,
      139.00,
      p.lidInnerRoofZ + 0.05,
    ))
    .fuse(box(
      12.75,
      105.00,
      -p.pcbClampInterference,
      12.95,
      139.00,
      p.lidInnerRoofZ + 0.05,
    ));

  // A shallow cord/formed-in-place gasket groove follows the cavity. It is
  // deliberately open only at the probe throat, which is epoxy/RTV sealed.
  lid = lid.cut(gasketRing(
    p.lidBottomZ - 0.05,
    p.lidBottomZ + p.gasketGrooveDepth,
  ));

  if (isIntegrated) {
    const pocketMinX = -p.solarPanelPocketWidth / 2;
    const pocketMaxX = p.solarPanelPocketWidth / 2;
    const pocketMinY = p.integratedPanelCenterY - p.solarPanelPocketLength / 2;
    const pocketMaxY = p.integratedPanelCenterY + p.solarPanelPocketLength / 2;
    const pocketBottomZ = p.integratedLidTopZ - p.solarPanelRecessDepth;
    lid = lid.cut(box(
      pocketMinX,
      pocketMinY,
      pocketBottomZ,
      pocketMaxX,
      pocketMaxY,
      p.integratedLidTopZ + 0.25,
    ));

    // Covered by the bonded panel; pot both lead passages after soldering.
    for (const x of [-2.00, 2.00]) {
      lid = lid.cut(cylinderZ(
        p.lidInnerRoofZ - 0.20,
        p.integratedLidTopZ - p.lidInnerRoofZ + 0.40,
        x,
        pocketMinY + 2.20,
        p.panelWireHoleDiameter / 2,
      ));
    }
  } else {
    for (const centerZ of p.caseWireEntryCentersZ) {
      lid = lid.cut(cylinderX(
        -16.80,
        3.80,
        p.armPivotY,
        centerZ,
        p.caseWireEntryDiameter / 2,
      ));
    }
  }
  return lid;
}

function buildArm(side) {
  const isLeft = side === 'left';
  const xMin = isLeft ? -22.00 : 20.40;
  const xMax = xMin + p.armThickness;
  const nearY = p.armPivotY;
  const nearZ = p.armPivotZ;
  const farY = p.armPanelPivotY;
  const farZ = p.armPanelPivotZ;
  const deltaY = farY - nearY;
  const deltaZ = farZ - nearZ;
  const centerDistance = Math.hypot(deltaY, deltaZ);
  const angle = Math.atan2(deltaZ, deltaY) * 180 / Math.PI;

  let arm = cylinderX(xMin, p.armThickness, nearY, nearZ, p.armEndRadius)
    .fuse(cylinderX(xMin, p.armThickness, farY, farZ, p.armEndRadius));
  arm = arm.fuse(box(
    xMin,
    nearY,
    nearZ - p.armEndRadius,
    xMax,
    nearY + centerDistance,
    nearZ + p.armEndRadius,
  ).rotate(angle, [0, nearY, nearZ], [1, 0, 0]));

  for (const [centerY, centerZ] of [[nearY, nearZ], [farY, farZ]]) {
    arm = arm.cut(cylinderX(
      xMin - 0.20,
      p.armThickness + 0.40,
      centerY,
      centerZ,
      p.armClearanceDiameter / 2,
    ));
  }

  if (isLeft) {
    arm = arm.cut(box(
      xMax - 0.55,
      nearY + 1.60,
      nearZ - 1.30,
      xMax + 0.10,
      nearY + centerDistance - 1.60,
      nearZ + 1.30,
    ).rotate(angle, [0, nearY, nearZ], [1, 0, 0]));
  }
  return arm;
}

function buildPanelFrame() {
  const centerX = 0.0;
  const centerY = p.armPanelPivotY;
  const xMin = centerX - p.panelFrameWidth / 2;
  const xMax = centerX + p.panelFrameWidth / 2;
  const yMin = centerY - p.panelFrameLength / 2;
  const yMax = centerY + p.panelFrameLength / 2;
  const frontZ = p.panelFrameBackZ + p.panelFrameThickness;

  let frame = roundedRect(
    xMin,
    yMin,
    xMax,
    yMax,
    p.panelFrameCornerRadius,
    p.panelFrameBackZ,
    frontZ,
  );
  frame = frame.cut(box(
    -p.solarPanelPocketWidth / 2,
    centerY - p.solarPanelPocketLength / 2,
    frontZ - p.solarPanelRecessDepth,
    p.solarPanelPocketWidth / 2,
    centerY + p.solarPanelPocketLength / 2,
    frontZ + 0.25,
  ));

  for (const wireY of [centerY - 14.00, centerY + 14.00]) {
    frame = frame.cut(cylinderZ(
      p.panelFrameBackZ - 0.20,
      p.panelFrameThickness + 0.40,
      0.0,
      wireY,
      p.panelWireHoleDiameter / 2,
    ));
  }

  // The frame lugs meet the outside faces of the two arms. Inserts install
  // from the outside so the panel can still be removed after bonding.
  frame = frame
    .fuse(cylinderX(-20.40, 4.00, centerY, p.armPanelPivotZ, 2.70))
    .fuse(cylinderX(16.40, 4.00, centerY, p.armPanelPivotZ, 2.70))
    .cut(cylinderX(
      -20.60,
      p.armInsertPocketDepth + 0.20,
      centerY,
      p.armPanelPivotZ,
      p.armInsertPocketDiameter / 2,
    ))
    .cut(cylinderX(
      20.60 - p.armInsertPocketDepth - 0.20,
      p.armInsertPocketDepth + 0.20,
      centerY,
      p.armPanelPivotZ,
      p.armInsertPocketDiameter / 2,
    ));
  return frame;
}

function buildGasket() {
  return gasketRing(0.0, p.gasketNominalThickness);
}

function boardOutline(zMin, zMax) {
  return prism([
    [-1.018, 0.0],
    [1.021, 0.0],
    [9.0, 15.48],
    [9.0, 98.0],
    [13.0, 102.0],
    [13.0, 141.0],
    [9.0, 145.0],
    [-9.0, 145.0],
    [-13.0, 141.0],
    [-13.0, 102.0],
    [-9.0, 98.0],
    [-9.0, 15.482],
  ], zMin, zMax);
}

async function correctedPcbReference() {
  if (!fs.existsSync(sourcePcbPath)) throw new Error(`Missing source PCB STEP: ${sourcePcbPath}`);
  const bytes = fs.readFileSync(sourcePcbPath);
  const imported = await replicad.importSTEP(new Blob([bytes], { type: 'application/STEP' }));
  const sourceSolids = Array.from(
    replicad.iterTopo(imported.wrapped, 'solid'),
    (shape) => replicad.cast(shape),
  );
  if (sourceSolids.length !== 233) {
    throw new Error(`Expected 233 solids in MSV4.step; found ${sourceSolids.length}.`);
  }

  const sourceBoard = sourceSolids[0].clone();
  const reconstructedRawBoard = boardOutline(-p.pcbStepThickness, 0.0);
  const sourceMinusReconstruction = replicad.measureVolume(
    sourceBoard.clone().cut(reconstructedRawBoard.clone()),
  );
  const reconstructionMinusSource = replicad.measureVolume(
    reconstructedRawBoard.clone().cut(sourceBoard.clone()),
  );
  const footprintMismatchVolume = sourceMinusReconstruction + reconstructionMinusSource;
  if (footprintMismatchVolume > 0.0001) {
    throw new Error(
      `Reconstructed PCB footprint differs from MSV4.step by ${footprintMismatchVolume} mm^3.`,
    );
  }

  const thicknessCorrection = p.pcbActualThickness - p.pcbStepThickness;
  const components = sourceSolids.slice(1).map((solid) => {
    const clone = solid.clone();
    // All six bottom-side component solids terminate on the raw STEP board's
    // lower surface. Move them with that surface when correcting to 1.6 mm.
    if (clone.boundingBox.bounds[1][2] <= -p.pcbStepThickness + 0.002) {
      return clone.translateZ(-thicknessCorrection);
    }
    return clone;
  });
  const physicalBoard = boardOutline(-p.pcbActualThickness, 0.0);
  return {
    sourceSolidCount: sourceSolids.length,
    sourceSha256: crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase(),
    footprintMismatchVolume,
    physicalBoard,
    components,
    compound: replicad.makeCompound([physicalBoard, ...components]),
  };
}

function batteryReference() {
  const [width, length, thickness] = p.batteryReference;
  return roundedRect(
    p.batteryCenterX - width / 2,
    p.batteryCenterY - length / 2,
    p.batteryCenterX + width / 2,
    p.batteryCenterY + length / 2,
    1.20,
    p.batteryBottomZ,
    p.batteryBottomZ + thickness,
  );
}

function integratedPanelReference() {
  const bottom = p.integratedLidTopZ - p.solarPanelRecessDepth + 0.10;
  return box(
    -p.solarPanelNominalWidth / 2,
    p.integratedPanelCenterY - p.solarPanelNominalLength / 2,
    bottom,
    p.solarPanelNominalWidth / 2,
    p.integratedPanelCenterY + p.solarPanelNominalLength / 2,
    bottom + 1.40,
  );
}

function antennaKeepoutReference() {
  return box(
    -9.00,
    p.antennaKeepoutYMin,
    0.0,
    9.00,
    p.antennaKeepoutYMax,
    p.minimalLidTopZ,
  );
}

function armPanelReference() {
  const bottom = p.panelFrameBackZ + p.panelFrameThickness - p.solarPanelRecessDepth + 0.10;
  return box(
    -p.solarPanelNominalWidth / 2,
    p.armPanelPivotY - p.solarPanelNominalLength / 2,
    bottom,
    p.solarPanelNominalWidth / 2,
    p.armPanelPivotY + p.solarPanelNominalLength / 2,
    bottom + 1.40,
  );
}

function measure(shape) {
  return {
    min: shape.boundingBox.bounds[0],
    max: shape.boundingBox.bounds[1],
    volume: replicad.measureVolume(shape),
    solidCount: Array.from(replicad.iterTopo(shape.wrapped, 'solid')).length,
  };
}

async function writeBlob(filename, blob) {
  fs.writeFileSync(filename, Buffer.from(await blob.arrayBuffer()));
}

function stlOptions() {
  return { tolerance: 0.04, angularTolerance: 0.18, binary: true };
}

function placeOnBed(shape) {
  const clone = shape.clone();
  return clone.translateZ(-clone.boundingBox.bounds[0][2]);
}

function flipRoofDown(shape, topZ) {
  const clone = shape.clone().mirror('XY', [0, 0, topZ]);
  return clone.translateZ(-clone.boundingBox.bounds[0][2]);
}

function rearEdgeDown(shape, rearY) {
  const clone = shape.clone().rotate(90, [0, rearY, 0], [1, 0, 0]);
  return clone.translateZ(-clone.boundingBox.bounds[0][2]);
}

function armOnBed(shape) {
  const clone = shape.clone().rotate(90, [0, 0, 0], [0, 1, 0]);
  return clone.translateZ(-clone.boundingBox.bounds[0][2]);
}

fs.mkdirSync(outputDir, { recursive: true });

let integratedBase = buildCommonBase(false);
let integratedLid = buildLidShell('integrated');
({ base: integratedBase, lid: integratedLid } = addCaseFasteners(
  integratedBase,
  integratedLid,
  p.integratedLidTopZ,
));

let armBase = buildCommonBase(true);
let armLid = buildLidShell('arm');
({ base: armBase, lid: armLid } = addCaseFasteners(armBase, armLid, p.armLidTopZ));
const leftArm = buildArm('left');
const rightArm = buildArm('right');
const panelFrame = buildPanelFrame();
const gasket = buildGasket();
const minimalBase = buildMinimalSolarBase();
const minimalLid = buildMinimalSolarLid();
const pcb = await correctedPcbReference();
const battery = batteryReference();
const integratedPanel = integratedPanelReference();
const detachedPanel = armPanelReference();
const antennaKeepout = antennaKeepoutReference();

const colors = {
  base: '#355f72',
  lid: '#8eb8ca',
  gasket: '#303030',
  arm: '#d8ae4b',
  frame: '#4e5961',
  pcb: '#267737',
  battery: '#b8c0c8',
  panel: '#253f63',
  keepout: '#e25555',
};

const individualSteps = [
  ['MSV4_integrated_solar_base.step', integratedBase],
  ['MSV4_integrated_solar_lid.step', integratedLid],
  ['MSV4_arm_solar_base.step', armBase],
  ['MSV4_arm_solar_lid.step', armLid],
  ['MSV4_solar_arm_left.step', leftArm],
  ['MSV4_solar_arm_right.step', rightArm],
  ['MSV4_solar_panel_frame.step', panelFrame],
  ['MSV4_case_gasket.step', gasket],
  ['MSV4_minimum_profile_integrated_solar_base.step', minimalBase],
  ['MSV4_minimum_profile_integrated_solar_lid.step', minimalLid],
];
for (const [filename, shape] of individualSteps) {
  await writeBlob(path.join(outputDir, filename), shape.blobSTEP());
}

await writeBlob(
  path.join(outputDir, 'MSV4_integrated_solar_enclosure_assembly.step'),
  replicad.exportSTEP([
    { shape: integratedBase, name: 'MSV4 Integrated Solar Base', color: colors.base },
    { shape: integratedLid, name: 'MSV4 Integrated Solar Lid', color: colors.lid },
    { shape: gasket.clone().translateZ(p.baseTopZ), name: 'MSV4 Case Gasket', color: colors.gasket },
    { shape: integratedPanel, name: '36 x 30 mm Solar Panel Reference - Do Not Print', color: colors.panel },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV4_integrated_solar_enclosure_fit_check.step'),
  replicad.exportSTEP([
    { shape: integratedBase, name: 'MSV4 Integrated Solar Base', color: colors.base },
    { shape: integratedLid, name: 'MSV4 Integrated Solar Lid', color: colors.lid },
    { shape: gasket.clone().translateZ(p.baseTopZ), name: 'MSV4 Case Gasket', color: colors.gasket },
    { shape: pcb.compound, name: 'MSV4 PCB - Corrected 1.6 mm Reference', color: colors.pcb },
    { shape: battery, name: '10 x 20 x 2 mm Top-Side Battery Placement Reference', color: colors.battery },
    { shape: integratedPanel, name: '36 x 30 mm Solar Panel Reference', color: colors.panel },
  ]),
);

await writeBlob(
  path.join(outputDir, 'MSV4_arm_solar_enclosure_assembly.step'),
  replicad.exportSTEP([
    { shape: armBase, name: 'MSV4 Arm Solar Base', color: colors.base },
    { shape: armLid, name: 'MSV4 Arm Solar Lid', color: colors.lid },
    { shape: gasket.clone().translateZ(p.baseTopZ), name: 'MSV4 Case Gasket', color: colors.gasket },
    { shape: leftArm, name: 'MSV4 Solar Arm Left', color: colors.arm },
    { shape: rightArm, name: 'MSV4 Solar Arm Right', color: colors.arm },
    { shape: panelFrame, name: 'MSV4 36 x 30 Solar Panel Frame', color: colors.frame },
    { shape: detachedPanel, name: '36 x 30 mm Solar Panel Reference - Do Not Print', color: colors.panel },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV4_arm_solar_enclosure_fit_check.step'),
  replicad.exportSTEP([
    { shape: armBase, name: 'MSV4 Arm Solar Base', color: colors.base },
    { shape: armLid, name: 'MSV4 Arm Solar Lid', color: colors.lid },
    { shape: gasket.clone().translateZ(p.baseTopZ), name: 'MSV4 Case Gasket', color: colors.gasket },
    { shape: leftArm, name: 'MSV4 Solar Arm Left', color: colors.arm },
    { shape: rightArm, name: 'MSV4 Solar Arm Right', color: colors.arm },
    { shape: panelFrame, name: 'MSV4 36 x 30 Solar Panel Frame', color: colors.frame },
    { shape: pcb.compound, name: 'MSV4 PCB - Corrected 1.6 mm Reference', color: colors.pcb },
    { shape: battery, name: '10 x 20 x 2 mm Top-Side Battery Placement Reference', color: colors.battery },
    { shape: detachedPanel, name: '36 x 30 mm Solar Panel Reference', color: colors.panel },
  ]),
);

await writeBlob(
  path.join(outputDir, 'MSV4_minimum_profile_integrated_solar_assembly.step'),
  replicad.exportSTEP([
    { shape: minimalBase, name: 'MSV4 Minimum Profile Adhesive Base', color: colors.base },
    { shape: minimalLid, name: 'MSV4 Minimum Profile Adhesive Solar Lid', color: colors.lid },
    { shape: integratedPanel, name: '36 x 30 mm Solar Panel Reference - Do Not Print', color: colors.panel },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV4_minimum_profile_integrated_solar_fit_check.step'),
  replicad.exportSTEP([
    { shape: minimalBase, name: 'MSV4 Minimum Profile Adhesive Base', color: colors.base },
    { shape: minimalLid, name: 'MSV4 Minimum Profile Adhesive Solar Lid', color: colors.lid },
    { shape: pcb.compound, name: 'MSV4 PCB - Corrected 1.6 mm Reference', color: colors.pcb },
    { shape: battery, name: '10 x 20 x 2 mm Top-Side Battery Placement Reference', color: colors.battery },
    { shape: integratedPanel, name: '36 x 30 mm Solar Panel Reference', color: colors.panel },
    { shape: antennaKeepout, name: 'E73 Antenna Keepout Reference - No Solar Panel', color: colors.keepout },
  ]),
);

const printBodies = [
  ['MSV4_integrated_solar_base_print.stl', placeOnBed(integratedBase)],
  ['MSV4_integrated_solar_lid_rear_edge_print.stl', rearEdgeDown(integratedLid, p.caseYMax)],
  ['MSV4_arm_solar_base_print.stl', placeOnBed(armBase)],
  ['MSV4_arm_solar_lid_print.stl', flipRoofDown(armLid, p.armLidTopZ)],
  ['MSV4_solar_arm_left_print.stl', armOnBed(leftArm)],
  ['MSV4_solar_arm_right_print.stl', armOnBed(rightArm)],
  ['MSV4_solar_panel_frame_print.stl', placeOnBed(panelFrame)],
  ['MSV4_case_gasket_TPU_print.stl', placeOnBed(gasket)],
  ['MSV4_minimum_profile_integrated_solar_base_print.stl', placeOnBed(minimalBase)],
  ['MSV4_minimum_profile_integrated_solar_lid_rear_edge_print.stl', rearEdgeDown(minimalLid, p.minimalOuterYMax)],
];
for (const [filename, shape] of printBodies) {
  await writeBlob(path.join(outputDir, filename), shape.blobSTL(stlOptions()));
}

const armCenterDistance = Math.hypot(
  p.armPanelPivotY - p.armPivotY,
  p.armPanelPivotZ - p.armPivotZ,
);
const roofCornerDistance = Math.hypot(
  p.caseYMax - p.armPivotY,
  p.armLidTopZ - p.armPivotZ,
);
const report = {
  version: 'MSV4-enclosures-v5.0-led-window-and-closed-minimum-case',
  revisionNote: 'Upward-pointing clear-epoxy LED windows over the upper D1 package on all three bases, plus a screwless two-piece minimum-profile enclosure with a thin closed back, PCB-shaped assembly groove, bonded seam, and explicit antenna keepout.',
  units: 'mm',
  source: path.relative(projectDir, sourcePcbPath),
  parameters: p,
  boardCorrection: {
    sourceSolidCount: pcb.sourceSolidCount,
    sourceSha256: pcb.sourceSha256,
    footprintMismatchVolume: pcb.footprintMismatchVolume,
    exportedBoardThickness: p.pcbStepThickness,
    productionBoardThickness: p.pcbActualThickness,
    bottomComponentShift: -(p.pcbActualThickness - p.pcbStepThickness),
    reason: 'MSV4 fab instruction specifies 1.6 mm FR4; Altium STEP contains the default thin stackup.',
  },
  closure: {
    fastener: 'four M2 90-degree countersunk flat-head screws: 10 mm integrated lid, 8 mm arm lid',
    insert: 'four M2 heat-set inserts in 2.90 mm x 3.20 mm blind pockets',
    screwStations: p.screwCenters,
    layout: 'unbroken circular lands inside a locally widened front shoulder; flush countersinks and no external ears',
    gasket: '0.80 mm wide x 0.55 mm deep PCB-following groove for silicone cord or formed-in-place RTV; optional 0.80 mm TPU gasket supplied',
    probeExit: 'split close-fit throat with 0.25 mm XY / 0.20 mm Z adhesive clearance',
  },
  commonCase: {
    exteriorMaximum: [p.caseXMax - p.caseXMin, p.caseYMax - p.caseYMin],
    centralBodyWidth: 33.20,
    neckWidth: 20.40,
    noseThickness: p.probeOuterTopZ - p.probeOuterBottomZ,
    transition: 'planform follows the PCB shoulder and both exterior Z faces slope to a 3.0 mm shell around the 1.6 mm blade',
    boardSideClearance: p.pcbSideClearance,
    internalRoofZ: p.lidInnerRoofZ,
    baseFloorZ: p.baseFloorTopZ,
    pcbClampInterference: p.pcbClampInterference,
    batteryClearanceReference: p.batteryReference,
    batteryPlacementReference: {
      center: [p.batteryCenterX, p.batteryCenterY],
      bottomZ: p.batteryBottomZ,
      note: 'Illustrative top-side 10 x 20 x 2 mm cell position; the user-selected 4.0 mm board-to-roof envelope is the controlling requirement.',
    },
    ledWindow: {
      shape: 'teardrop pointing upward toward the antenna/top edge',
      center: [p.ledCenterX, p.ledCenterY],
      bulbRadius: p.ledWindowRadius,
      tipY: p.ledWindowTipY,
      sealing: 'fill flush with optically clear epoxy after assembly',
    },
  },
  integratedSolar: {
    panelNominal: [p.solarPanelNominalLength, p.solarPanelNominalWidth],
    panelPocket: [p.solarPanelPocketLength, p.solarPanelPocketWidth, p.solarPanelRecessDepth],
    panelBackWall: p.integratedPanelBackWall,
    closedCaseThickness: p.integratedLidTopZ - p.baseBottomZ,
    overallBounds: {
      base: measure(integratedBase),
      lid: measure(integratedLid),
      gasket: measure(gasket),
    },
    printOrientation: 'lid rear edge down; add a 6-8 mm brim',
  },
  armSolar: {
    panelNominal: [p.solarPanelNominalLength, p.solarPanelNominalWidth],
    panelPocket: [p.solarPanelPocketLength, p.solarPanelPocketWidth, p.solarPanelRecessDepth],
    panelFrame: [p.panelFrameLength, p.panelFrameWidth, p.panelFrameThickness],
    pivotCenterDistance: armCenterDistance,
    closedCaseThickness: p.armLidTopZ - p.baseBottomZ,
    minimumRoofSweepClearance: armCenterDistance - p.armEndRadius - roofCornerDistance,
    pivotHardware: 'four additional M2 x 5-6 mm screws and four 2.90 x 3.20 mm heat-set inserts',
    overallBounds: {
      base: measure(armBase),
      lid: measure(armLid),
      leftArm: measure(leftArm),
      rightArm: measure(rightArm),
      panelFrame: measure(panelFrame),
    },
  },
  minimumProfileSolar: {
    construction: 'two-piece closed adhesive enclosure; thin back/base plus solar lid, with no gasket, screws, inserts, or articulated parts',
    retention: 'PCB head and shoulder seat into a 0.25 mm XY / 0.15 mm Z-clearance groove in the base; bond the plain base/lid seam with epoxy or outdoor polyurethane adhesive',
    panelNominal: [p.solarPanelNominalLength, p.solarPanelNominalWidth],
    panelPocket: [p.solarPanelPocketLength, p.solarPanelPocketWidth, p.solarPanelRecessDepth],
    antennaKeepout: {
      x: [-9.00, 9.00],
      y: [p.antennaKeepoutYMin, p.antennaKeepoutYMax],
      panelRearEdgeY: p.integratedPanelCenterY + p.solarPanelNominalLength / 2,
      separation: p.antennaKeepoutYMin - (p.integratedPanelCenterY + p.solarPanelNominalLength / 2),
    },
    closedCaseThickness: p.minimalLidTopZ - p.minimalBaseBottomZ,
    overallBounds: {
      base: measure(minimalBase),
      lid: measure(minimalLid),
    },
    printOrientation: 'base floor-down; lid rear edge down with a 6-8 mm brim',
  },
  printGuidance: {
    material: ['ASA preferred outdoors', 'PETG for first fit print'],
    layerHeight: 0.20,
    perimeters: 5,
    topBottomLayers: 6,
    infill: '25-35%',
    seam: 'place away from probe throat and gasket land',
    dimensionalNote: 'print the fit coupons or one complete set before a batch; heat-set insert brands vary',
  },
};
fs.writeFileSync(path.join(outputDir, 'MSV4_enclosure_design_report.json'), `${JSON.stringify(report, null, 2)}\n`);

console.log(JSON.stringify({
  outputDir,
  generatedFiles: fs.readdirSync(outputDir).sort(),
  integratedBase: measure(integratedBase),
  integratedLid: measure(integratedLid),
  armBase: measure(armBase),
  armLid: measure(armLid),
  minimalBase: measure(minimalBase),
  minimalLid: measure(minimalLid),
  armCenterDistance,
  minimumRoofSweepClearance: armCenterDistance - p.armEndRadius - roofCornerDistance,
}, null, 2));

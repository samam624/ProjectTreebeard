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

const files = {
  integratedBase: 'MSV4_integrated_solar_base.step',
  integratedLid: 'MSV4_integrated_solar_lid.step',
  integratedAssembly: 'MSV4_integrated_solar_enclosure_assembly.step',
  integratedFit: 'MSV4_integrated_solar_enclosure_fit_check.step',
  armBase: 'MSV4_arm_solar_base.step',
  armLid: 'MSV4_arm_solar_lid.step',
  armLeft: 'MSV4_solar_arm_left.step',
  armRight: 'MSV4_solar_arm_right.step',
  panelFrame: 'MSV4_solar_panel_frame.step',
  gasket: 'MSV4_case_gasket.step',
  armAssembly: 'MSV4_arm_solar_enclosure_assembly.step',
  armFit: 'MSV4_arm_solar_enclosure_fit_check.step',
  minimalBase: 'MSV4_minimum_profile_integrated_solar_base.step',
  minimalLid: 'MSV4_minimum_profile_integrated_solar_lid.step',
  minimalAssembly: 'MSV4_minimum_profile_integrated_solar_assembly.step',
  minimalFit: 'MSV4_minimum_profile_integrated_solar_fit_check.step',
};

const stlFiles = {
  integratedBase: 'MSV4_integrated_solar_base_print.stl',
  integratedLid: 'MSV4_integrated_solar_lid_rear_edge_print.stl',
  armBase: 'MSV4_arm_solar_base_print.stl',
  armLid: 'MSV4_arm_solar_lid_print.stl',
  armLeft: 'MSV4_solar_arm_left_print.stl',
  armRight: 'MSV4_solar_arm_right_print.stl',
  panelFrame: 'MSV4_solar_panel_frame_print.stl',
  gasket: 'MSV4_case_gasket_TPU_print.stl',
  minimalBase: 'MSV4_minimum_profile_integrated_solar_base_print.stl',
  minimalLid: 'MSV4_minimum_profile_integrated_solar_lid_rear_edge_print.stl',
};

const screwCenters = [
  [-15.30, 101.50],
  [15.30, 101.50],
  [-13.80, 143.50],
  [13.80, 143.50],
];

const errors = [];
const checks = {};

function near(actual, expected, tolerance = 0.003) {
  return Math.abs(actual - expected) <= tolerance;
}

function vectorNear(actual, expected, tolerance = 0.003) {
  return actual.every((value, index) => near(value, expected[index], tolerance));
}

function box(xMin, yMin, zMin, xMax, yMax, zMax) {
  return replicad.makeBox([xMin, yMin, zMin], [xMax, yMax, zMax]);
}

function cylinderZ(zMin, length, centerX, centerY, radius) {
  return replicad.makeCylinder(radius, length, [centerX, centerY, zMin], [0, 0, 1]);
}

function cylinderX(xMin, length, centerY, centerZ, radius) {
  return replicad.makeCylinder(radius, length, [xMin, centerY, centerZ], [1, 0, 0]);
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

function probeThroatClearance(zMin, zMax) {
  return prism([
    [-9.20, 94.30],
    [9.20, 94.30],
    [9.20, 98.00],
    [13.20, 102.20],
    [-13.20, 102.20],
    [-9.20, 98.00],
  ], zMin, zMax);
}

function solidCount(shape) {
  return Array.from(replicad.iterTopo(shape.wrapped, 'solid')).length;
}

function intersectionVolume(first, second) {
  return replicad.measureVolume(first.intersect(second));
}

async function importStep(filename) {
  const bytes = fs.readFileSync(filename);
  return replicad.importSTEP(new Blob([bytes], { type: 'application/STEP' }));
}

function parseBinaryStl(filename) {
  const bytes = fs.readFileSync(filename);
  if (bytes.length < 84) throw new Error(`${filename} is not a binary STL.`);
  const triangleCount = bytes.readUInt32LE(80);
  const expectedLength = 84 + triangleCount * 50;
  if (bytes.length !== expectedLength) throw new Error(`${filename} has an invalid binary STL length.`);

  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  const edgeUses = new Map();
  let degenerateTriangles = 0;
  for (let triangle = 0; triangle < triangleCount; triangle += 1) {
    const start = 84 + triangle * 50 + 12;
    const vertices = [];
    for (let vertex = 0; vertex < 3; vertex += 1) {
      const coordinates = [];
      for (let axis = 0; axis < 3; axis += 1) {
        const value = bytes.readFloatLE(start + vertex * 12 + axis * 4);
        coordinates.push(value);
        min[axis] = Math.min(min[axis], value);
        max[axis] = Math.max(max[axis], value);
      }
      vertices.push(coordinates);
    }
    const edgeA = vertices[1].map((value, axis) => value - vertices[0][axis]);
    const edgeB = vertices[2].map((value, axis) => value - vertices[0][axis]);
    const cross = [
      edgeA[1] * edgeB[2] - edgeA[2] * edgeB[1],
      edgeA[2] * edgeB[0] - edgeA[0] * edgeB[2],
      edgeA[0] * edgeB[1] - edgeA[1] * edgeB[0],
    ];
    if (Math.hypot(...cross) < 1e-8) degenerateTriangles += 1;
    const keys = vertices.map((vertex) => vertex.join(','));
    for (const [a, b] of [[0, 1], [1, 2], [2, 0]]) {
      const key = [keys[a], keys[b]].sort().join('|');
      edgeUses.set(key, (edgeUses.get(key) ?? 0) + 1);
    }
  }
  const nonManifoldEdges = Array.from(edgeUses.values()).filter((uses) => uses !== 2).length;
  return {
    triangleCount,
    byteLength: bytes.length,
    min,
    max,
    degenerateTriangles,
    nonManifoldEdges,
    watertight: degenerateTriangles === 0 && nonManifoldEdges === 0,
  };
}

for (const filename of [...Object.values(files), ...Object.values(stlFiles)]) {
  const absolute = path.join(outputDir, filename);
  if (!fs.existsSync(absolute)) errors.push(`Missing output: ${absolute}`);
}
if (errors.length) {
  console.error(JSON.stringify({ passed: false, errors }, null, 2));
  process.exit(1);
}

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

const imported = {};
for (const [name, filename] of Object.entries(files)) {
  imported[name] = await importStep(path.join(outputDir, filename));
}

checks.solidCounts = Object.fromEntries(
  Object.entries(imported).map(([name, shape]) => [name, solidCount(shape)]),
);
const expectedCounts = {
  integratedBase: 1,
  integratedLid: 1,
  integratedAssembly: 4,
  integratedFit: 238,
  armBase: 1,
  armLid: 1,
  armLeft: 1,
  armRight: 1,
  panelFrame: 1,
  gasket: 1,
  armAssembly: 7,
  armFit: 241,
  minimalBase: 1,
  minimalLid: 1,
  minimalAssembly: 3,
  minimalFit: 238,
};
for (const [name, expected] of Object.entries(expectedCounts)) {
  if (checks.solidCounts[name] !== expected) {
    errors.push(`${name}: expected ${expected} solids, got ${checks.solidCounts[name]}.`);
  }
}

checks.bounds = Object.fromEntries(
  Object.entries(imported).map(([name, shape]) => [name, shape.boundingBox.bounds]),
);
const expectedBounds = {
  integratedBase: [[-18.3, 94.8, -4.5], [18.3, 148.5, 0.25]],
  integratedLid: [[-18.3, 94.8, -0.10], [18.3, 148.5, 6.40]],
  armBase: [[-20.1, 94.8, -5.1], [20.1, 148.5, 0.90]],
  armLid: [[-18.3, 94.8, -0.10], [18.3, 148.5, 5.00]],
  minimalBase: [[-16.2, 96.0, -4.25], [16.2, 146.2, 0.25]],
  minimalLid: [[-16.2, 96.0, 0.30], [16.2, 146.2, 6.40]],
};
for (const [name, [expectedMin, expectedMax]] of Object.entries(expectedBounds)) {
  const [actualMin, actualMax] = checks.bounds[name];
  if (!vectorNear(actualMin, expectedMin) || !vectorNear(actualMax, expectedMax)) {
    errors.push(`${name}: unexpected bounds.`);
  }
}

checks.baseLidOverlap = {
  integrated: intersectionVolume(imported.integratedBase, imported.integratedLid),
  arm: intersectionVolume(imported.armBase, imported.armLid),
  minimal: intersectionVolume(imported.minimalBase, imported.minimalLid),
};
for (const [name, volume] of Object.entries(checks.baseLidOverlap)) {
  if (volume > 0.002) errors.push(`${name}: base and lid overlap by ${volume} mm^3.`);
}

const ledCenter = [-10.00, 138.00000113987];
const ledWindowTool = teardropPrism(
  ledCenter[0],
  ledCenter[1],
  1.55,
  140.82,
  -4.70,
  5.90,
);
checks.ledWindows = {
  sourceBodyExpectedBounds: [[-10.625058678585, 137.298743182419, -2.946238195673], [-9.374942761412, 138.701259097319, -2.056064607721]],
  integratedObstructionVolume: intersectionVolume(imported.integratedBase, ledWindowTool),
  armObstructionVolume: intersectionVolume(imported.armBase, ledWindowTool),
  minimalObstructionVolume: intersectionVolume(imported.minimalBase, ledWindowTool),
  shape: 'teardrop, point upward toward antenna/top edge',
  clearEpoxyFill: true,
};
for (const name of ['integratedObstructionVolume', 'armObstructionVolume', 'minimalObstructionVolume']) {
  if (checks.ledWindows[name] > 0.002) errors.push(`${name}: clear-epoxy LED window is obstructed.`);
}

checks.fasteners = {};
for (const variant of ['integrated', 'arm']) {
  const base = imported[`${variant}Base`];
  const lid = imported[`${variant}Lid`];
  const lidTopZ = variant === 'integrated' ? 6.40 : 5.00;
  checks.fasteners[variant] = [];
  for (const [x, y] of screwCenters) {
    const result = {
      x,
      y,
      insertVoidVolume: intersectionVolume(base, cylinderZ(-2.90, 3.05, x, y, 1.40)),
      tipReliefVoidVolume: intersectionVolume(base, cylinderZ(-3.40, 0.40, x, y, 1.15)),
      baseWallMaterial: intersectionVolume(base, cylinderZ(-4.45, 4.60, x, y, 2.35)),
      lidHoleVoidVolume: intersectionVolume(lid, cylinderZ(0.35, lidTopZ - 1.25, x, y, 1.15)),
      countersinkOpeningVoidVolume: intersectionVolume(lid, cylinderZ(lidTopZ - 0.01, 0.10, x, y, 2.05)),
      topLandMaterial: intersectionVolume(lid, cylinderZ(lidTopZ - 1.00, 0.85, x, y, 2.35)),
      countersinkPerimeterMaterial: intersectionVolume(
        lid,
        cylinderZ(lidTopZ - 0.10, 0.08, x, y, 2.60).cut(
          cylinderZ(lidTopZ - 0.12, 0.12, x, y, 2.15),
        ),
      ),
    };
    checks.fasteners[variant].push(result);
    if (result.insertVoidVolume > 0.002) errors.push(`${variant}: obstructed insert pocket at ${x},${y}.`);
    if (result.tipReliefVoidVolume > 0.002) errors.push(`${variant}: obstructed tip relief at ${x},${y}.`);
    if (result.baseWallMaterial < 30) errors.push(`${variant}: insufficient base material at ${x},${y}.`);
    if (result.lidHoleVoidVolume > 0.002) errors.push(`${variant}: obstructed lid clearance at ${x},${y}.`);
    if (result.countersinkOpeningVoidVolume > 0.002) errors.push(`${variant}: obstructed countersink at ${x},${y}.`);
    if (result.topLandMaterial < 5) errors.push(`${variant}: insufficient flush top land at ${x},${y}.`);
    if (result.countersinkPerimeterMaterial < 0.50) {
      errors.push(`${variant}: countersink perimeter breaks through the case at ${x},${y}.`);
    }
  }
}

const rawPcb = await importStep(sourcePcbPath);
const sourcePcbBytes = fs.readFileSync(sourcePcbPath);
const rawPcbSolids = Array.from(
  replicad.iterTopo(rawPcb.wrapped, 'solid'),
  (shape) => replicad.cast(shape),
);
const sourceBoard = rawPcbSolids[0].clone();
const reconstructedRawBoard = boardOutline(-0.41148, 0.0);
const sourceMinusReconstruction = replicad.measureVolume(
  sourceBoard.clone().cut(reconstructedRawBoard.clone()),
);
const reconstructionMinusSource = replicad.measureVolume(
  reconstructedRawBoard.clone().cut(sourceBoard.clone()),
);
const footprintMismatchVolume = sourceMinusReconstruction + reconstructionMinusSource;
const bottomShift = -(1.60 - 0.41148);
const components = rawPcbSolids.slice(1).map((solid) => {
  const clone = solid.clone();
  if (clone.boundingBox.bounds[1][2] <= -0.41148 + 0.002) return clone.translateZ(bottomShift);
  return clone;
});
const componentEnvelope = components.reduce((envelope, shape) => {
  const [min, max] = shape.boundingBox.bounds;
  for (let axis = 0; axis < 3; axis += 1) {
    envelope.min[axis] = Math.min(envelope.min[axis], min[axis]);
    envelope.max[axis] = Math.max(envelope.max[axis], max[axis]);
  }
  return envelope;
}, { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] });
const componentBounds = components.map((shape) => shape.boundingBox.bounds);
const componentCompound = replicad.makeCompound(components);
const batteryBounds = {
  xMin: -12.20,
  xMax: -1.80,
  yMin: 102.80,
  yMax: 123.20,
  zMin: 1.70,
  zMax: 3.90,
};
const batteryReference = roundedRect(
  batteryBounds.xMin,
  batteryBounds.yMin,
  batteryBounds.xMax,
  batteryBounds.yMax,
  1.20,
  batteryBounds.zMin,
  batteryBounds.zMax,
);
checks.pcb = {
  rawSolidCount: rawPcbSolids.length,
  sourceSha256: crypto.createHash('sha256').update(sourcePcbBytes).digest('hex').toUpperCase(),
  footprintMismatchVolume,
  correctedBottomShift: bottomShift,
  componentEnvelope,
};
if (checks.pcb.rawSolidCount !== 233) errors.push('MSV4.step source solid count changed from 233.');
if (checks.pcb.footprintMismatchVolume > 0.0001) {
  errors.push(`Corrected PCB footprint differs from MSV4.step by ${checks.pcb.footprintMismatchVolume} mm^3.`);
}
const ledBodyBounds = componentBounds[214];
checks.ledWindows.sourceBodyActualBounds = ledBodyBounds;
if (!vectorNear(ledBodyBounds[0], checks.ledWindows.sourceBodyExpectedBounds[0], 0.003)
  || !vectorNear(ledBodyBounds[1], checks.ledWindows.sourceBodyExpectedBounds[1], 0.003)) {
  errors.push('The bottom-side D1 LED body moved; re-register the teardrop window before release.');
}
checks.batteryToComponentCollisionVolume = intersectionVolume(
  batteryReference,
  componentCompound,
);
if (checks.batteryToComponentCollisionVolume > 0.01) {
  errors.push(
    `Nominal battery intersects corrected PCB components by ${checks.batteryToComponentCollisionVolume} mm^3.`,
  );
}
checks.referencePartCollisionVolume = {
  batteryToIntegratedBase: intersectionVolume(batteryReference, imported.integratedBase),
  batteryToIntegratedLid: intersectionVolume(batteryReference, imported.integratedLid),
  batteryToArmBase: intersectionVolume(batteryReference, imported.armBase),
  batteryToArmLid: intersectionVolume(batteryReference, imported.armLid),
  integratedPanelToLid: intersectionVolume(
    box(-15.00, 104.00, 4.90, 15.00, 140.00, 6.30),
    imported.integratedLid,
  ),
  armPanelToFrame: intersectionVolume(
    box(-15.00, 131.20, 13.80, 15.00, 167.20, 15.20),
    imported.panelFrame,
  ),
  minimalBatteryToBase: intersectionVolume(batteryReference, imported.minimalBase),
  minimalBatteryToLid: intersectionVolume(batteryReference, imported.minimalLid),
  minimalPanelToLid: intersectionVolume(
    box(-15.00, 104.00, 4.90, 15.00, 140.00, 6.30),
    imported.minimalLid,
  ),
};
for (const [name, volume] of Object.entries(checks.referencePartCollisionVolume)) {
  if (volume > 0.01) errors.push(`${name}: reference part collision is ${volume} mm^3.`);
}

const throatReference = probeThroatClearance(-1.55, 0.15);
checks.probeThroatCollisionVolume = {
  integratedBase: intersectionVolume(imported.integratedBase, throatReference),
  integratedLid: intersectionVolume(imported.integratedLid, throatReference),
  armBase: intersectionVolume(imported.armBase, throatReference),
  armLid: intersectionVolume(imported.armLid, throatReference),
};
for (const [name, volume] of Object.entries(checks.probeThroatCollisionVolume)) {
  if (volume > 0.002) errors.push(`${name}: corrected PCB blade throat is obstructed by ${volume} mm^3.`);
}

checks.probeProfileTaper = {
  integratedLidRemovedAboveNose: intersectionVolume(
    imported.integratedLid,
    box(-9.50, 94.81, 1.10, 9.50, 94.99, 5.90),
  ),
  armLidRemovedAboveNose: intersectionVolume(
    imported.armLid,
    box(-9.50, 94.81, 1.10, 9.50, 94.99, 4.50),
  ),
  integratedBaseRemovedBelowNose: intersectionVolume(
    imported.integratedBase,
    box(-9.50, 94.81, -4.40, 9.50, 94.99, -2.70),
  ),
  armBaseRemovedBelowNose: intersectionVolume(
    imported.armBase,
    box(-9.50, 94.81, -4.40, 9.50, 94.99, -2.70),
  ),
  integratedLidSkinAtNose: intersectionVolume(
    imported.integratedLid,
    box(-9.00, 94.81, 0.22, 9.00, 94.99, 1.05),
  ),
  integratedBaseSkinAtNose: intersectionVolume(
    imported.integratedBase,
    box(-9.00, 94.81, -2.65, 9.00, 94.99, -1.82),
  ),
};
for (const name of [
  'integratedLidRemovedAboveNose',
  'armLidRemovedAboveNose',
  'integratedBaseRemovedBelowNose',
  'armBaseRemovedBelowNose',
]) {
  if (checks.probeProfileTaper[name] > 0.002) {
    errors.push(`${name}: probe profile taper was not cut cleanly.`);
  }
}
if (checks.probeProfileTaper.integratedLidSkinAtNose < 1.0) {
  errors.push('Integrated lid has insufficient sloped sealing skin above the PCB nose.');
}
if (checks.probeProfileTaper.integratedBaseSkinAtNose < 1.0) {
  errors.push('Integrated base has insufficient sloped sealing skin below the PCB nose.');
}

checks.componentCollisionVolume = {
  integratedBase: intersectionVolume(imported.integratedBase, componentCompound),
  integratedLid: intersectionVolume(imported.integratedLid, componentCompound),
  armBase: intersectionVolume(imported.armBase, componentCompound),
  armLid: intersectionVolume(imported.armLid, componentCompound),
  minimalBase: intersectionVolume(imported.minimalBase, componentCompound),
  minimalLid: intersectionVolume(imported.minimalLid, componentCompound),
};
for (const [name, volume] of Object.entries(checks.componentCollisionVolume)) {
  if (volume > 0.01) errors.push(`${name}: PCB component collision volume is ${volume} mm^3.`);
}

const localBatterySupportZ = componentBounds
  .filter(([min, max]) => {
    return max[2] > 0
      && min[0] < batteryBounds.xMax
      && max[0] > batteryBounds.xMin
      && min[1] < batteryBounds.yMax
      && max[1] > batteryBounds.yMin;
  })
  .reduce((highest, [, max]) => Math.max(highest, max[2]), 0);
checks.clearances = {
  sidePerBoardEdge: 13.35 - 13.00,
  floorBelowCorrectedLowestComponent: checks.pcb.componentEnvelope.min[2] - (-3.30),
  roofAboveHighestComponent: 4.00 - checks.pcb.componentEnvelope.max[2],
  batteryAboveLocalComponents: batteryBounds.zMin - localBatterySupportZ,
  batteryTopToRoof: 4.00 - batteryBounds.zMax,
  seamGap: 0.30 - 0.25,
  armRoofSweep: Math.hypot(149.20 - 121.50, 14.00 - (-2.10))
    - 2.80 - Math.hypot(148.50 - 121.50, 5.00 - (-2.10)),
};
if (checks.clearances.sidePerBoardEdge < 0.30) errors.push('PCB side clearance is below 0.30 mm.');
if (checks.clearances.floorBelowCorrectedLowestComponent < 0.30) errors.push('Corrected bottom component floor clearance is below 0.30 mm.');
if (checks.clearances.roofAboveHighestComponent < 0.80) errors.push('Top component roof clearance is below 0.80 mm.');
if (checks.clearances.batteryAboveLocalComponents < 0.20) errors.push('Top-side battery reference is too close to local PCB components.');
if (checks.clearances.batteryTopToRoof < 0.08) errors.push('Top-side battery reference is too close to the 4 mm roof envelope.');
if (checks.clearances.armRoofSweep < 1.0) errors.push('Solar arm roof sweep clearance is below 1.0 mm.');

const physicalBoard = boardOutline(-1.60, 0.0);
const antennaKeepout = box(-9.00, 140.75, 0.0, 9.00, 145.00, 6.40);
const integratedPanelReference = box(-15.00, 104.00, 4.90, 15.00, 140.00, 6.30);
checks.minimumProfile = {
  pcbGrooveCollisionVolume: intersectionVolume(imported.minimalBase, physicalBoard),
  lidPcbCollisionVolume: intersectionVolume(imported.minimalLid, physicalBoard),
  antennaToSolarPanelCollisionVolume: intersectionVolume(antennaKeepout, integratedPanelReference),
  antennaToSolarPanelSeparation: 140.75 - 140.00,
  grooveXYClearance: 0.25,
  grooveTopClearance: 0.15,
  floorBelowCorrectedLowestComponent: checks.pcb.componentEnvelope.min[2] - (-3.25),
  closedCaseThickness: 6.40 - (-4.25),
  hardwareCount: 0,
  lowerShellPresent: true,
  closedCase: true,
};
if (checks.minimumProfile.pcbGrooveCollisionVolume > 0.002) {
  errors.push(`Minimum-profile PCB groove collides with the production board by ${checks.minimumProfile.pcbGrooveCollisionVolume} mm^3.`);
}
if (checks.minimumProfile.lidPcbCollisionVolume > 0.002) {
  errors.push(`Minimum-profile lid collides with the production board by ${checks.minimumProfile.lidPcbCollisionVolume} mm^3.`);
}
if (checks.minimumProfile.floorBelowCorrectedLowestComponent < 0.30) {
  errors.push('Minimum-profile closed back has less than 0.30 mm clearance below the corrected lowest component.');
}
if (checks.minimumProfile.antennaToSolarPanelCollisionVolume > 0.002) {
  errors.push('Minimum-profile solar panel overlaps the E73 antenna keepout.');
}
if (checks.minimumProfile.antennaToSolarPanelSeparation < 0.20) {
  errors.push('Minimum-profile solar panel has less than 0.20 mm plan-view separation from the antenna keepout.');
}

checks.openings = {
  integratedPanelPocket: intersectionVolume(
    imported.integratedLid,
    box(-15.15, 103.85, 4.85, 15.15, 140.15, 6.35),
  ),
  minimalPanelPocket: intersectionVolume(
    imported.minimalLid,
    box(-15.15, 103.85, 4.85, 15.15, 140.15, 6.35),
  ),
  panelFramePocket: intersectionVolume(
    imported.panelFrame,
    box(-15.15, 131.05, 13.75, 15.15, 167.35, 15.25),
  ),
  integratedPanelWireLeft: intersectionVolume(
    imported.integratedLid,
    cylinderZ(3.85, 2.70, -2.00, 122.00, 0.75),
  ),
  integratedPanelWireRight: intersectionVolume(
    imported.integratedLid,
    cylinderZ(3.85, 2.70, 2.00, 122.00, 0.75),
  ),
  minimalPanelWireLeft: intersectionVolume(
    imported.minimalLid,
    cylinderZ(3.85, 2.70, -2.00, 122.00, 0.75),
  ),
  minimalPanelWireRight: intersectionVolume(
    imported.minimalLid,
    cylinderZ(3.85, 2.70, 2.00, 122.00, 0.75),
  ),
  panelFrameWireLeft: intersectionVolume(
    imported.panelFrame,
    cylinderZ(12.55, 2.90, -2.00, 149.20, 0.75),
  ),
  panelFrameWireRight: intersectionVolume(
    imported.panelFrame,
    cylinderZ(12.55, 2.90, 2.00, 149.20, 0.75),
  ),
  armCaseWireLow: intersectionVolume(
    imported.armLid,
    cylinderX(-16.70, 3.40, 121.50, 1.35, 0.75),
  ),
  armCaseWireHigh: intersectionVolume(
    imported.armLid,
    cylinderX(-16.70, 3.40, 121.50, 3.20, 0.75),
  ),
  leftPivotInsert: intersectionVolume(
    imported.armBase,
    cylinderX(-20.20, 3.05, 121.50, -2.10, 1.40),
  ),
  rightPivotInsert: intersectionVolume(
    imported.armBase,
    cylinderX(17.15, 3.05, 121.50, -2.10, 1.40),
  ),
};
for (const [name, volume] of Object.entries(checks.openings)) {
  if (volume > 0.002) errors.push(`${name}: modeled opening is obstructed (${volume} mm^3).`);
}

checks.stl = {};
for (const [name, filename] of Object.entries(stlFiles)) {
  const report = parseBinaryStl(path.join(outputDir, filename));
  checks.stl[name] = report;
  if (!near(report.min[2], 0, 0.002)) errors.push(`${name}: STL is not on the print bed.`);
  if (report.triangleCount < 12) errors.push(`${name}: STL has no useful mesh.`);
  if (!report.watertight) {
    errors.push(`${name}: STL is not watertight (${report.degenerateTriangles} degenerate, ${report.nonManifoldEdges} non-manifold edges).`);
  }
}

const report = { passed: errors.length === 0, errors, checks };
fs.writeFileSync(
  path.join(outputDir, 'MSV4_enclosure_validation_report.json'),
  `${JSON.stringify(report, null, 2)}\n`,
);
console.log(JSON.stringify(report, null, 2));
if (errors.length) process.exit(1);

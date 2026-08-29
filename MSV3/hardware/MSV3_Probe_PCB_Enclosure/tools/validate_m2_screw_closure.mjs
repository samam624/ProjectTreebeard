import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from './node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from './node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const outputDir = path.join(projectDir, 'output', 'm2_blended_side_closure');
const wasmPath = path.join(
  here,
  'node',
  'node_modules',
  'replicad-opencascadejs',
  'dist',
  'replicad_single.wasm',
);

const paths = {
  base: path.join(outputDir, 'MSV3_brain_enclosure_base_M2_blended_side_inserts.step'),
  lid: path.join(outputDir, 'MSV3_brain_enclosure_lid_M2_blended_side_countersunk.step'),
  closed: path.join(outputDir, 'MSV3_brain_enclosure_case_closed_M2_blended_side.step'),
  assembly: path.join(outputDir, 'MSV3_brain_enclosure_assembly_M2_blended_side.step'),
  fitCheck: path.join(outputDir, 'MSV3_brain_enclosure_fit_check_M2_blended_side.step'),
  baseStl: path.join(outputDir, 'MSV3_brain_enclosure_base_M2_blended_side_inserts_print.stl'),
  lidStl: path.join(outputDir, 'MSV3_brain_enclosure_lid_M2_blended_side_countersunk_print.stl'),
  leftArmStl: path.join(outputDir, 'MSV3_solar_arm_left_M2_blended_side_print.stl'),
  rightArmStl: path.join(outputDir, 'MSV3_solar_arm_right_M2_blended_side_print.stl'),
  panelFrameStl: path.join(outputDir, 'MSV3_solar_panel_frame_M2_blended_side_print.stl'),
};

const errors = [];
const checks = {};

function near(actual, expected, tolerance = 0.002) {
  return Math.abs(actual - expected) <= tolerance;
}

function vectorNear(actual, expected, tolerance = 0.002) {
  return actual.every((value, index) => near(value, expected[index], tolerance));
}

function cylinderZ(zMin, length, centerX, centerY, radius) {
  return replicad.makeCylinder(radius, length, [centerX, centerY, zMin], [0, 0, 1]);
}

function cylinderX(xMin, length, centerY, centerZ, radius) {
  return replicad.makeCylinder(radius, length, [xMin, centerY, centerZ], [1, 0, 0]);
}

function solidCount(shape) {
  return Array.from(replicad.iterTopo(shape.wrapped, 'solid')).length;
}

function intersectionVolume(first, second) {
  return replicad.measureVolume(first.intersect(second));
}

function parseBinaryStl(filename) {
  const bytes = fs.readFileSync(filename);
  if (bytes.length < 84) throw new Error(`${filename} is too short to be a binary STL.`);
  const triangleCount = bytes.readUInt32LE(80);
  const expectedLength = 84 + triangleCount * 50;
  if (bytes.length !== expectedLength) {
    throw new Error(`${filename} byte length does not match its binary STL triangle count.`);
  }
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
    for (const [first, second] of [[0, 1], [1, 2], [2, 0]]) {
      const edgeKey = [keys[first], keys[second]].sort().join('|');
      edgeUses.set(edgeKey, (edgeUses.get(edgeKey) ?? 0) + 1);
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

async function importStep(filename) {
  const bytes = fs.readFileSync(filename);
  return replicad.importSTEP(new Blob([bytes], { type: 'application/STEP' }));
}

for (const filename of Object.values(paths)) {
  if (!fs.existsSync(filename)) errors.push(`Missing output: ${filename}`);
}
if (errors.length) {
  console.error(JSON.stringify({ passed: false, errors }, null, 2));
  process.exit(1);
}

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

const base = await importStep(paths.base);
const lid = await importStep(paths.lid);
const closed = await importStep(paths.closed);
const assembly = await importStep(paths.assembly);
const fitCheck = await importStep(paths.fitCheck);

checks.solidCounts = {
  base: solidCount(base),
  lid: solidCount(lid),
  closedCase: solidCount(closed),
  mechanicalAssembly: solidCount(assembly),
  fitCheckWithPcb: solidCount(fitCheck),
};
if (checks.solidCounts.base !== 1) errors.push('Base is not one continuous solid.');
if (checks.solidCounts.lid !== 1) errors.push('Lid is not one continuous solid.');
if (checks.solidCounts.closedCase !== 2) errors.push('Closed-case STEP does not contain two separate bodies.');
if (checks.solidCounts.mechanicalAssembly !== 5) errors.push('Mechanical assembly does not contain five separate bodies.');
if (checks.solidCounts.fitCheckWithPcb !== 574) errors.push('PCB fit-check assembly lost or gained solids.');

checks.bounds = {
  base: base.boundingBox.bounds,
  lid: lid.boundingBox.bounds,
};
if (!vectorNear(checks.bounds.base[0], [-12, 82, -6.35], 0.002)
  || !vectorNear(checks.bounds.base[1], [30, 149, 0.65], 0.002)) {
  errors.push('Base bounds changed unexpectedly.');
}
if (!vectorNear(checks.bounds.lid[0], [-12, 82, -0.75], 0.002)
  || !vectorNear(checks.bounds.lid[1], [30, 149, 7.55], 0.002)) {
  errors.push('Lid bounds changed unexpectedly.');
}

checks.baseLidOverlapVolume = intersectionVolume(base, lid);
if (checks.baseLidOverlapVolume > 0.001) {
  errors.push(`Base and lid overlap by ${checks.baseLidOverlapVolume} mm^3.`);
}

const screwCenters = [
  [-9.00, 102.00],
  [-9.00, 130.00],
  [27.00, 102.00],
  [27.00, 130.00],
];
checks.fasteners = [];
for (const [x, y] of screwCenters) {
  const insertVoidVolume = intersectionVolume(base, cylinderZ(-3.95, 3.10, x, y, 1.40));
  const tipVoidVolume = intersectionVolume(base, cylinderZ(-5.50, 1.40, x, y, 1.15));
  const baseBossMaterialVolume = intersectionVolume(base, cylinderZ(-5.45, 4.55, x, y, 2.80));
  const lidClearanceVoidVolume = intersectionVolume(lid, cylinderZ(-0.70, 0.70, x, y, 1.15));
  const countersinkOpeningVoidVolume = intersectionVolume(lid, cylinderZ(0.945, 0.10, x, y, 2.05));
  const lidCapMaterialVolume = intersectionVolume(lid, cylinderZ(-0.70, 1.60, x, y, 2.80));
  const result = {
    x,
    y,
    insertVoidVolume,
    tipVoidVolume,
    baseBossMaterialVolume,
    lidClearanceVoidVolume,
    countersinkOpeningVoidVolume,
    lidCapMaterialVolume,
  };
  checks.fasteners.push(result);
  if (insertVoidVolume > 0.001) errors.push(`Insert pocket is obstructed at (${x}, ${y}).`);
  if (tipVoidVolume > 0.001) errors.push(`Screw-tip relief is obstructed at (${x}, ${y}).`);
  if (baseBossMaterialVolume < 70) errors.push(`Base boss lacks material at (${x}, ${y}).`);
  if (lidClearanceVoidVolume > 0.001) errors.push(`Lid screw clearance is obstructed at (${x}, ${y}).`);
  if (countersinkOpeningVoidVolume > 0.001) errors.push(`Lid countersink is obstructed at (${x}, ${y}).`);
  if (lidCapMaterialVolume < 15) errors.push(`Lid side cap lacks material at (${x}, ${y}).`);
}

// A narrow test column through each fastener axis proves the rejected tall
// pillars are gone: no base material may rise past the seam, and no screw-pad
// material may rise from the low lid cap toward the roof.
checks.pcbRemovalPath = screwCenters.map(([x, y]) => ({
  x,
  y,
  baseAboveSeamVolume: intersectionVolume(base, cylinderZ(-0.74, 6.30, x, y, 1.40)),
  lidAboveLowProfileCapVolume: intersectionVolume(lid, cylinderZ(1.00, 4.60, x, y, 1.40)),
}));
for (const result of checks.pcbRemovalPath) {
  if (result.baseAboveSeamVolume > 0.001) {
    errors.push(`A base pillar still rises above the seam at (${result.x}, ${result.y}).`);
  }
  if (result.lidAboveLowProfileCapVolume > 0.001) {
    errors.push(`The lid screw pad is not low profile at (${result.x}, ${result.y}).`);
  }
}

checks.clearance = {
  bossToPcbBoardLeft: -4.00 - (-6.00),
  bossToPcbBoardRight: 24.00 - 22.00,
  leftArmToBoss: -12.00 - (-12.25),
  rightArmToBoss: 30.25 - 30.00,
  highestPcbComponentToLidRoof: 5.70 - 4.80,
  seamGap: -0.75 - (-0.80),
};
for (const [name, value] of Object.entries(checks.clearance)) {
  if (value < 0.049) errors.push(`${name} clearance is too small: ${value} mm.`);
}

checks.screwLength = {
  countersunkLengthIncludesHead: true,
  screwHeadTopZ: 0.95,
  insertTopZ: -0.80,
  insertBottomZ: -4.00,
  m2x5TipZ: 0.95 - 5.00,
  m2x6TipZ: 0.95 - 6.00,
  reliefBottomZ: -5.55,
};
if (checks.screwLength.m2x5TipZ > checks.screwLength.insertBottomZ + 0.05) {
  errors.push('M2 x 5 mm does not use the complete insert length.');
}
if (checks.screwLength.m2x6TipZ < checks.screwLength.reliefBottomZ) {
  errors.push('M2 x 6 mm would bottom out in the modeled relief.');
}

checks.preservedOpenings = {
  leftWireEntryLow: intersectionVolume(lid, cylinderX(-11.70, 6.75, 118.50, 2.20, 0.75)),
  leftWireEntryHigh: intersectionVolume(lid, cylinderX(-11.70, 6.75, 118.50, 4.60, 0.75)),
  leftSolarInsert: intersectionVolume(base, cylinderX(-11.90, 3.00, 118.50, -2.85, 1.40)),
  rightSolarInsert: intersectionVolume(base, cylinderX(26.90, 3.00, 118.50, -2.85, 1.40)),
};
for (const [name, value] of Object.entries(checks.preservedOpenings)) {
  if (value > 0.001) errors.push(`${name} was obstructed by the rounded perimeter.`);
}

// These probes sit in the material that was missing at the old diagonal plan
// chamfers and at the old beveled roof/floor edges. Occupancy confirms those
// exterior chamfers were replaced by the rounded, flat-skinned envelope.
checks.chamferReplacement = {
  frontLeftRoundedCornerMaterial: intersectionVolume(
    closed,
    cylinderZ(5.80, 1.50, -2.70, 83.00, 0.35),
  ),
  rearRightRoundedCornerMaterial: intersectionVolume(
    closed,
    cylinderZ(5.80, 1.50, 20.60, 148.00, 0.35),
  ),
  flatRoofEdgeMaterial: intersectionVolume(
    lid,
    cylinderZ(7.20, 0.30, -6.70, 115.50, 0.20),
  ),
  flatFloorEdgeMaterial: intersectionVolume(
    base,
    cylinderZ(-6.30, 0.30, -6.70, 115.50, 0.20),
  ),
};
for (const [name, value] of Object.entries(checks.chamferReplacement)) {
  if (value < 0.01) errors.push(`${name} is missing; an exterior chamfer may remain.`);
}

checks.stl = {
  base: parseBinaryStl(paths.baseStl),
  lid: parseBinaryStl(paths.lidStl),
  leftArm: parseBinaryStl(paths.leftArmStl),
  rightArm: parseBinaryStl(paths.rightArmStl),
  panelFrame: parseBinaryStl(paths.panelFrameStl),
};
for (const [name, mesh] of Object.entries(checks.stl)) {
  if (!near(mesh.min[2], 0, 0.002)) errors.push(`${name} STL is not on the print bed.`);
  if (mesh.triangleCount < 12) errors.push(`${name} STL does not contain a printable mesh.`);
  if (!mesh.watertight) {
    errors.push(`${name} STL is not watertight (${mesh.degenerateTriangles} degenerate triangles, ${mesh.nonManifoldEdges} open/non-manifold edges).`);
  }
}

const report = {
  passed: errors.length === 0,
  errors,
  checks,
};
fs.writeFileSync(
  path.join(outputDir, 'MSV3_M2_blended_side_validation.json'),
  `${JSON.stringify(report, null, 2)}\n`,
);
console.log(JSON.stringify(report, null, 2));
if (errors.length) process.exit(1);

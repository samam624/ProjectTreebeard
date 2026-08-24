import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const inputPath = path.join(projectDir, 'MSV3_brain_enclosure_assembly.step');
const outputDir = path.join(projectDir, 'output', 'm2_blended_side_closure');
const wasmPath = path.join(
  projectDir,
  'tools',
  'node',
  'node_modules',
  'replicad-opencascadejs',
  'dist',
  'replicad_single.wasm',
);

const dimensions = {
  fastener: 'M2 x 5-6 mm 90-degree countersunk flat-head screw',
  insertPocketDiameter: 2.90,
  insertPocketDepth: 3.20,
  screwClearanceDiameter: 2.40,
  screwTipReliefDiameter: 2.40,
  countersinkTopDiameter: 4.20,
  countersinkBottomDiameter: 2.40,
  countersinkDepth: 0.90,
  blendedBossNominalDiameter: 6.00,
  blendedBossTransitionLength: 5.50,
  baseBottomZ: -6.35,
  baseFloorTopZ: -4.25,
  baseTopZ: -0.80,
  lidBottomZ: -0.75,
  lidBossTopZ: 0.95,
  lidInnerRoofZ: 5.70,
  lidTopZ: 7.55,
  screwTipReliefBottomZ: -5.55,
  caseLeftX: -7.00,
  caseRightX: 25.00,
  caseFrontY: 82.00,
  caseBackY: 149.00,
  caseCornerRadius: 4.20,
  caseCenterX: 9.00,
  caseCenterY: 115.50,
  screwCenters: [
    { id: 'left-front', side: 'left', x: -9.00, y: 102.00 },
    { id: 'left-rear', side: 'left', x: -9.00, y: 130.00 },
    { id: 'right-front', side: 'right', x: 27.00, y: 102.00 },
    { id: 'right-rear', side: 'right', x: 27.00, y: 130.00 },
  ],
};

function boundsNear(actual, expected, tolerance = 0.02) {
  return actual.every((value, index) => Math.abs(value - expected[index]) <= tolerance);
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

function prism(points, zMin, zMax) {
  const sketcher = new replicad.Sketcher('XY').movePointerTo(points[0]);
  for (const point of points.slice(1)) sketcher.lineTo(point);
  return sketcher.close().extrude(zMax - zMin).translateZ(zMin);
}

function roundedCase(zMin, zMax) {
  return replicad.sketchRoundedRectangle(
    dimensions.caseRightX - dimensions.caseLeftX,
    dimensions.caseBackY - dimensions.caseFrontY,
    dimensions.caseCornerRadius,
    {
      plane: 'XY',
      origin: [dimensions.caseCenterX, dimensions.caseCenterY, zMin],
    },
  ).extrude(zMax - zMin);
}

// These are not round cylinders stuck onto the case. Each boss keeps a full
// 3 mm radius around the screw, then flows into the straight enclosure wall
// with tangent arc/Bezier transitions over 11 mm. The result is a shallow,
// integrated side scallop that retains the user's preferred screw locations.
function blendedSideBoss(location, zMin, zMax) {
  const y = location.y;
  const transition = dimensions.blendedBossTransitionLength;
  const radius = dimensions.blendedBossNominalDiameter / 2;
  const wallX = location.side === 'left' ? dimensions.caseLeftX : dimensions.caseRightX;
  const insideX = location.side === 'left' ? wallX + 1 : wallX - 1;
  const circleBottom = [location.x, y - radius];
  const circleTop = [location.x, y + radius];
  const outermost = [location.x + (location.side === 'left' ? -radius : radius), y];

  let sketcher = new replicad.Sketcher('XY', zMin)
    .movePointerTo([insideX, y - transition])
    .lineTo([wallX, y - transition]);

  if (location.side === 'left') {
    sketcher = sketcher
      .bezierCurveTo(circleBottom, [[wallX, y - 4.0], [location.x + 1.0, y - radius]])
      .threePointsArcTo(circleTop, outermost)
      .bezierCurveTo([wallX, y + transition], [[location.x + 1.0, y + radius], [wallX, y + 4.0]]);
  } else {
    sketcher = sketcher
      .bezierCurveTo(circleBottom, [[wallX, y - 4.0], [location.x - 1.0, y - radius]])
      .threePointsArcTo(circleTop, outermost)
      .bezierCurveTo([wallX, y + transition], [[location.x - 1.0, y + radius], [wallX, y + 4.0]]);
  }

  return sketcher
    .lineTo([insideX, y + transition])
    .close()
    .extrude(zMax - zMin);
}

function countersink(location) {
  const bottomZ = dimensions.lidBossTopZ - dimensions.countersinkDepth;
  const bottom = replicad.sketchCircle(
    dimensions.countersinkBottomDiameter / 2,
    { plane: 'XY', origin: [location.x, location.y, bottomZ] },
  );
  const top = replicad.sketchCircle(
    dimensions.countersinkTopDiameter / 2,
    { plane: 'XY', origin: [location.x, location.y, dimensions.lidBossTopZ] },
  );
  return bottom.loftWith(top, { ruled: true });
}

function measure(shape) {
  return {
    type: shape.constructor.name,
    min: shape.boundingBox.bounds[0],
    max: shape.boundingBox.bounds[1],
    volume: replicad.measureVolume(shape),
  };
}

async function writeBlob(filePath, blob) {
  fs.writeFileSync(filePath, Buffer.from(await blob.arrayBuffer()));
}

if (!fs.existsSync(inputPath)) {
  throw new Error(`Missing revised source assembly: ${inputPath}`);
}

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

const sourceBytes = fs.readFileSync(inputPath);
const sourceAssembly = await replicad.importSTEP(
  new Blob([sourceBytes], { type: 'application/STEP' }),
);
const sourceSolids = Array.from(
  replicad.iterTopo(sourceAssembly.wrapped, 'solid'),
  (shape) => replicad.cast(shape),
);

if (sourceSolids.length !== 574) {
  throw new Error(`Expected 574 source solids (5 mechanical + 569 PCB); got ${sourceSolids.length}.`);
}

const [sourceBase, sourceLid, solarArmLeft, solarArmRight, solarPanelFrame] = sourceSolids;
const expectedBounds = [
  { name: 'base', shape: sourceBase, min: [-12, 82, -6.35], max: [30, 149, 0.65] },
  { name: 'lid', shape: sourceLid, min: [-8.45, 82, -2.80], max: [26.45, 149, 7.55] },
  { name: 'left arm', shape: solarArmLeft, min: [-13.85, 115.50, -5.85], max: [-12.25, 152.68, 18.15] },
  { name: 'right arm', shape: solarArmRight, min: [30.25, 115.50, -5.85], max: [31.85, 152.68, 18.15] },
  { name: 'panel frame', shape: solarPanelFrame, min: [-12, 119.68, 13.15], max: [30, 179.68, 17.75] },
];

for (const expected of expectedBounds) {
  const [actualMin, actualMax] = expected.shape.boundingBox.bounds;
  if (!boundsNear(actualMin, expected.min) || !boundsNear(actualMax, expected.max)) {
    throw new Error(`${expected.name} is not in the expected source-solid position.`);
  }
}

let base = sourceBase.clone();
let lid = sourceLid.clone();

const originalOuterContour = [
  [-2.80, 82.00],
  [20.80, 82.00],
  [25.00, 86.20],
  [25.00, 142.60],
  [20.60, 149.00],
  [-2.60, 149.00],
  [-7.00, 142.60],
  [-7.00, 86.20],
];

// Remove the old four snap arms and mating bumps before installing the screw
// closure. The cuts stop at the original wall so no internal PCB support or
// pressure feature is touched.
for (const centerY of [102.00, 130.00]) {
  lid = lid.cut(box(-8.70, centerY - 3.70, -3.00, -7.00, centerY + 3.70, 1.80));
  lid = lid.cut(box(25.00, centerY - 3.70, -3.00, 26.70, centerY + 3.70, 1.80));
  base = base.cut(box(-7.60, centerY - 2.20, -1.60, -7.00, centerY + 2.20, -0.20));
  base = base.cut(box(25.00, centerY - 2.20, -1.60, 25.60, centerY + 2.20, -0.20));
}

// Replace the source's diagonal/chamfered exterior corners with a single
// rounded-rectangle envelope. The full roof and floor skins also remove the
// old Z-direction edge chamfers. Material is added only in already-solid roof
// and floor zones or outside the original outer contour; the cavity and the
// user's revised internal PCB pressure geometry remain unchanged.
const originalOuterPrism = prism(
  originalOuterContour,
  dimensions.baseBottomZ - 0.20,
  dimensions.lidTopZ + 0.20,
);
base = base
  .fuse(roundedCase(dimensions.baseBottomZ, dimensions.baseFloorTopZ))
  .fuse(
    roundedCase(dimensions.baseFloorTopZ, dimensions.baseTopZ)
      .cut(originalOuterPrism),
  );
lid = lid
  .fuse(
    roundedCase(dimensions.lidBottomZ, dimensions.lidTopZ)
      .cut(originalOuterPrism),
  )
  .fuse(roundedCase(dimensions.lidInnerRoofZ, dimensions.lidTopZ));

// Restore the existing solar-pivot insert entries and solar-wire passages
// after rounding the perimeter.
base = base.cut(cylinderX(-12.20, 3.40, 118.50, -2.85, 1.45));
base = base.cut(cylinderX(26.80, 3.40, 118.50, -2.85, 1.45));
for (const centerZ of [2.20, 4.60]) {
  lid = lid.cut(cylinderX(-12.00, 7.90, 118.50, centerZ, 0.80));
}

const insertRadius = dimensions.insertPocketDiameter / 2;
const screwRadius = dimensions.screwClearanceDiameter / 2;

for (const location of dimensions.screwCenters) {
  // The base inserts end at the seam. Nothing rises into the open PCB cavity,
  // so the lid lifts away and the board can be removed straight upward.
  base = base.fuse(blendedSideBoss(location, dimensions.baseBottomZ, dimensions.baseTopZ));
  base = base.cut(cylinderZ(
    dimensions.baseTopZ - dimensions.insertPocketDepth,
    dimensions.insertPocketDepth + 0.20,
    location.x,
    location.y,
    insertRadius,
  ));
  base = base.cut(cylinderZ(
    dimensions.screwTipReliefBottomZ,
    dimensions.baseTopZ - dimensions.insertPocketDepth
      - dimensions.screwTipReliefBottomZ + 0.10,
    location.x,
    location.y,
    screwRadius,
  ));

  // The lid contributes only a 1.70 mm low-profile cap at the seam. A 90-degree
  // countersink leaves the M2 flat-head screw flush with this small side pad.
  lid = lid.fuse(blendedSideBoss(location, dimensions.lidBottomZ, dimensions.lidBossTopZ));
  lid = lid.cut(cylinderZ(
    dimensions.lidBottomZ - 0.20,
    dimensions.lidBossTopZ - dimensions.lidBottomZ + 0.40,
    location.x,
    location.y,
    screwRadius,
  ));
  lid = lid.cut(countersink(location));
  lid = lid.cut(cylinderZ(
    dimensions.lidBossTopZ - 0.01,
    0.20,
    location.x,
    location.y,
    dimensions.countersinkTopDiameter / 2,
  ));
}

const pcbReference = replicad.makeCompound(sourceSolids.slice(5).map((solid) => solid.clone()));

fs.mkdirSync(outputDir, { recursive: true });

await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_base_M2_blended_side_inserts.step'),
  base.blobSTEP(),
);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_lid_M2_blended_side_countersunk.step'),
  lid.blobSTEP(),
);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_case_closed_M2_blended_side.step'),
  replicad.exportSTEP([
    { shape: base, name: 'MSV3 Brain Enclosure Base - M2 Blended Side Inserts', color: '#3d647a' },
    { shape: lid, name: 'MSV3 Brain Enclosure Lid - M2 Blended Side Caps', color: '#8fb3c7' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_assembly_M2_blended_side.step'),
  replicad.exportSTEP([
    { shape: base, name: 'MSV3 Brain Enclosure Base - M2 Blended Side Inserts', color: '#3d647a' },
    { shape: lid, name: 'MSV3 Brain Enclosure Lid - M2 Blended Side Caps', color: '#8fb3c7' },
    { shape: solarArmLeft, name: 'MSV3 Solar Arm Left', color: '#d5a84b' },
    { shape: solarArmRight, name: 'MSV3 Solar Arm Right', color: '#d5a84b' },
    { shape: solarPanelFrame, name: 'MSV3 Solar Panel Frame', color: '#4a545c' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_fit_check_M2_blended_side.step'),
  replicad.exportSTEP([
    { shape: base, name: 'MSV3 Brain Enclosure Base - M2 Blended Side Inserts', color: '#3d647a' },
    { shape: lid, name: 'MSV3 Brain Enclosure Lid - M2 Blended Side Caps', color: '#8fb3c7' },
    { shape: solarArmLeft, name: 'MSV3 Solar Arm Left', color: '#d5a84b' },
    { shape: solarArmRight, name: 'MSV3 Solar Arm Right', color: '#d5a84b' },
    { shape: solarPanelFrame, name: 'MSV3 Solar Panel Frame', color: '#4a545c' },
    { shape: pcbReference, name: 'MSV3 Probe PCB Reference', color: '#2f7d32' },
  ]),
);

const baseForPrint = base.clone().translateZ(-base.boundingBox.bounds[0][2]);
const lidForPrint = lid.clone()
  .mirror('XY', [0, 0, dimensions.lidTopZ])
  .translateZ(-dimensions.lidTopZ);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_base_M2_blended_side_inserts_print.stl'),
  baseForPrint.blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);
await writeBlob(
  path.join(outputDir, 'MSV3_brain_enclosure_lid_M2_blended_side_countersunk_print.stl'),
  lidForPrint.blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);

// Export every remaining mechanical body in a slicer-ready orientation so a
// lab laptop only needs Bambu Studio or OrcaSlicer—not Fusion 360. The arms
// print flat on their broad 1.6 mm faces; the panel frame prints rear-wall down
// with its solar-panel recess facing upward.
function armForPrint(arm) {
  const rotated = arm.clone().rotate(90, [0, 0, 0], [0, 1, 0]);
  return rotated.translateZ(-rotated.boundingBox.bounds[0][2]);
}

const solarPanelFrameForPrint = solarPanelFrame.clone()
  .translateZ(-solarPanelFrame.boundingBox.bounds[0][2]);
await writeBlob(
  path.join(outputDir, 'MSV3_solar_arm_left_M2_blended_side_print.stl'),
  armForPrint(solarArmLeft).blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);
await writeBlob(
  path.join(outputDir, 'MSV3_solar_arm_right_M2_blended_side_print.stl'),
  armForPrint(solarArmRight).blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);
await writeBlob(
  path.join(outputDir, 'MSV3_solar_panel_frame_M2_blended_side_print.stl'),
  solarPanelFrameForPrint.blobSTL({ tolerance: 0.05, angularTolerance: 0.20, binary: true }),
);

const baseLidOverlapVolume = replicad.measureVolume(base.intersect(lid));
const pcbBoardBounds = sourceSolids[5].boundingBox.bounds;
const componentXBounds = sourceSolids.slice(6).reduce((bounds, solid) => {
  const [min, max] = solid.boundingBox.bounds;
  return [Math.min(bounds[0], min[0]), Math.max(bounds[1], max[0])];
}, [Infinity, -Infinity]);
const bossOuterLeftX = dimensions.screwCenters[0].x - dimensions.blendedBossNominalDiameter / 2;
const bossOuterRightX = dimensions.screwCenters[2].x + dimensions.blendedBossNominalDiameter / 2;
const leftArmGap = bossOuterLeftX - solarArmLeft.boundingBox.bounds[1][0];
const rightArmGap = solarArmRight.boundingBox.bounds[0][0] - bossOuterRightX;

const report = {
  version: 'user-revision-m2-blended-side-closure-3',
  units: 'mm',
  source: path.relative(projectDir, inputPath),
  sourceMechanicalBodiesPreserved: [
    'revised brain enclosure base',
    'revised brain enclosure lid',
    'left solar arm',
    'right solar arm',
    'revised solar panel frame',
  ],
  closure: dimensions,
  behavior: {
    referenceStyle: 'four low-profile flush side screws in smoothly blended enclosure scallops',
    preferredSideLocationsRestored: true,
    directVerticalClamping: true,
    oldExternalSnapArmsRemoved: true,
    oldBaseSnapBumpsRemoved: true,
    isolatedRoundEars: false,
    tallInternalPillars: false,
    pcbRemovalPathFullyOpen: true,
    exteriorDiagonalChamfersReplacedByRoundedCorners: true,
    exteriorRoofAndFloorEdgeChamfersRemoved: true,
    pcbAndInternalPressureGeometryOtherwiseUnchanged: true,
    heatSetInsertInstallationDirection: 'downward into the four base bosses from the open seam',
    screwInstallationDirection: 'downward through the four flush lid-side countersinks',
    screwLengthNote: 'Countersunk screw length includes the head; M2 x 5 uses the complete insert and M2 x 6 enters the modeled tip relief.',
  },
  checks: {
    sourceSolidCount: sourceSolids.length,
    outputMechanicalSolidCount: 5,
    base: measure(base),
    lid: measure(lid),
    baseLidOverlapVolume,
    leftArmToBossClearance: leftArmGap,
    rightArmToBossClearance: rightArmGap,
    pcbBoardXBounds: [pcbBoardBounds[0][0], pcbBoardBounds[1][0]],
    componentXBounds,
    bossToPcbBoardSideClearance: 2.00,
    lidRoofToHighestPcbComponentClearance: 0.90,
  },
  printGuidance: {
    baseOrientation: 'floor down; already oriented in STL',
    lidOrientation: 'exterior roof down; already oriented in STL',
    supports: 'none expected',
    countersinkNote: 'Enable elephant-foot compensation or lightly deburr the four bed-facing countersinks.',
    suggestedPerimeters: 4,
    suggestedLayerHeight: 0.20,
  },
};
fs.writeFileSync(
  path.join(outputDir, 'MSV3_M2_blended_side_report.json'),
  `${JSON.stringify(report, null, 2)}\n`,
);

console.log(JSON.stringify({
  inputPath,
  outputDir,
  sourceSolidCount: sourceSolids.length,
  base: measure(base),
  lid: measure(lid),
  baseLidOverlapVolume,
  armToBossClearance: { left: leftArmGap, right: rightArmGap },
  files: fs.readdirSync(outputDir).sort(),
}, null, 2));

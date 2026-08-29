import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const outputDir = path.join(projectDir, 'output');
const wasmPath = path.resolve(
  here,
  '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.wasm',
);
const sourceEnclosurePath = path.join(projectDir, 'display_enclosure.step');
const sourcePcbPath = path.join(projectDir, 'MSV3_desktop_display.step');
const sourceAntennaPath = 'C:/Users/Public/Documents/Altium/MSV2_ESP32_DisplayCenter/antenna_step.stp';

const parameters = {
  // Keep the supplied PCB STEP at its native origin. The legacy bezel and all
  // generated enclosure parts are moved into that coordinate frame at export.
  pcbTranslation: [0.00, 0.00, 0.00],
  enclosureToPcbTranslation: [-6.39, -13.903, 3.00],
  bezelOpening: { xMin: -47.00, xMax: 41.00, yMin: 2.013, yMax: 60.013 },
  displayActiveArea: { width: 83.52, height: 55.68 },
  mainOuter: { xMin: -64.00, xMax: 58.00, yMin: -4.00, yMax: 78.00 },
  shellFrontZ: -6.15,
  shellBackZ: -21.00,
  shellInnerBackZ: -18.40,
  wallThickness: 2.20,
  frontFaceBackZ: 2.08,
  visibleFrontZ: 3.10,
  // Three unobstructed display-PCB mounting holes plus the controller PCB's
  // upper-left mounting hole. The display's upper-left hole is blocked by the
  // controller PCB behind it and is deliberately not used for a through screw.
  closureHoleCenters: [
    [-56.88, 3.5645],
    [45.16, 3.5645], [45.16, 58.4630],
    [-54.11, 72.40],
  ],
  closurePostEndZ: [-4.00, -4.00, -4.00, -8.08],
  closureInsertFaceZ: [-2.15, -2.15, -2.15, -7.54],
  enclosureScrewClearanceDiameter: 2.40,
  enclosureScrewHeadDiameter: 4.60,
  enclosureScrewHeadDepth: 2.00,
  enclosureInsertDiameter: 2.90,
  enclosureInsertDepth: 3.20,
  enclosureInsertBossRadius: 3.20,
  usbOpening: { xMin: -36.30, xMax: -25.90, zMin: -11.80, zMax: -6.55 },
  usbCableBay: { xMin: -39.00, xMax: -23.00, yMin: -12.20, yMax: 5.60 },
  bottomFeet: { yMin: -12.00, yMax: -4.00, leftXMin: -62.00, leftXMax: -43.00, rightXMin: 37.00, rightXMax: 56.00 },
  antennaMount: {
    x: 50.50, y: 70.00,
    holeDiameter: 10.00,
  },
  antennaFold: {
    hingeX: 40.40, hingeZ: -2.10, splitX: 45.40,
    localFoldDegrees: 90.00,
    globalRotationY: 90.00,
    translation: [52.90, 70.00, -0.60],
  },
  kickstandPivot: { y: 41.00, z: -23.80 },
  kickstandFar: { y: -7.50, z: -24.80 },
  kickstandArmThickness: 4.00,
  kickstandPivotHoleDiameter: 2.40,
  kickstandInsertDiameter: 2.90,
  kickstandInsertDepth: 3.20,
};

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

function box(xMin, yMin, zMin, xMax, yMax, zMax) {
  return replicad.makeBox([xMin, yMin, zMin], [xMax, yMax, zMax]);
}

function cylinderX(xMin, length, centerY, centerZ, radius) {
  return replicad.makeCylinder(radius, length, [xMin, centerY, centerZ], [1, 0, 0]);
}

function cylinderZ(zMin, length, centerX, centerY, radius) {
  return replicad.makeCylinder(radius, length, [centerX, centerY, zMin], [0, 0, 1]);
}

function joinAll(base, additions) {
  return additions.reduce((shape, addition) => shape.fuse(addition), base);
}

function cutAll(base, cutters) {
  return cutters.reduce((shape, cutter) => shape.cut(cutter), base);
}

async function importStep(filename) {
  return replicad.importSTEP(new Blob([fs.readFileSync(filename)]));
}

async function writeBlob(filename, blob) {
  fs.writeFileSync(filename, Buffer.from(await blob.arrayBuffer()));
}

function solidChildren(shape) {
  return shape._listTopo('solid').map((wrapped) => replicad.cast(wrapped));
}

function buildFrontCover() {
  const p = parameters;
  const innerXMin = p.mainOuter.xMin + p.wallThickness;
  const innerXMax = p.mainOuter.xMax - p.wallThickness;
  const innerYMin = p.mainOuter.yMin + p.wallThickness;
  const innerYMax = p.mainOuter.yMax - p.wallThickness;

  // Build the cover from new primitives instead of modifying the old STEP.
  // The full-width face slab produces one clean planar front surface with only
  // the intended LCD opening. Four continuous perimeter walls close every side;
  // in particular, there is no controller clearance slot in the left wall.
  let cover = box(
    p.mainOuter.xMin, p.mainOuter.yMin, p.frontFaceBackZ,
    p.mainOuter.xMax, p.mainOuter.yMax, p.visibleFrontZ,
  ).cut(box(
    p.bezelOpening.xMin, p.bezelOpening.yMin, p.frontFaceBackZ - 0.20,
    p.bezelOpening.xMax, p.bezelOpening.yMax, p.visibleFrontZ + 0.20,
  ));

  cover = joinAll(cover, [
    box(p.mainOuter.xMin, p.mainOuter.yMin, p.shellFrontZ,
      innerXMin, p.mainOuter.yMax, p.frontFaceBackZ),
    box(innerXMax, p.mainOuter.yMin, p.shellFrontZ,
      p.mainOuter.xMax, p.mainOuter.yMax, p.frontFaceBackZ),
    box(innerXMin, p.mainOuter.yMin, p.shellFrontZ,
      innerXMax, innerYMin, p.frontFaceBackZ),
    box(innerXMin, innerYMax, p.shellFrontZ,
      innerXMax, p.mainOuter.yMax, p.frontFaceBackZ),
  ]).simplify();

  // Four back-facing heat-set-insert seats align with three display-PCB holes
  // and the controller-PCB hole above the blocked display-board corner. The
  // boss faces stop just ahead of the PCB front surface.
  p.closureHoleCenters.forEach(([centerX, centerY], index) => {
    const insertFaceZ = p.closureInsertFaceZ[index];
    cover = cover.fuse(cylinderZ(
      insertFaceZ, p.frontFaceBackZ - insertFaceZ + 0.20,
      centerX, centerY, p.enclosureInsertBossRadius,
    ));
    if (index === 3) {
      // Tie the long controller-board insert column into the left extension
      // wall. The web begins just in front of the PCB plane, so it strengthens
      // the insert without touching the board edge.
      cover = cover.fuse(box(
        innerXMin, centerY - 2.00, insertFaceZ,
        centerX, centerY + 2.00, -2.00,
      ));
    }
    cover = cover.cut(cylinderZ(insertFaceZ - 0.10, p.enclosureInsertDepth,
      centerX, centerY, p.enclosureInsertDiameter / 2));
  });

  return cover.simplify();
}

function buildRearShell() {
  const p = parameters;
  let shell = box(
    p.mainOuter.xMin, p.mainOuter.yMin, p.shellBackZ,
    p.mainOuter.xMax, p.mainOuter.yMax, p.shellFrontZ,
  );

  // Two short feet lift the lower edge so a straight USB-C plug can enter the
  // downward-facing connector without the desk blocking it.
  shell = joinAll(shell, [
    box(p.bottomFeet.leftXMin, p.bottomFeet.yMin, p.shellBackZ,
      p.bottomFeet.leftXMax, p.bottomFeet.yMax, p.shellFrontZ),
    box(p.bottomFeet.rightXMin, p.bottomFeet.yMin, p.shellBackZ,
      p.bottomFeet.rightXMax, p.bottomFeet.yMax, p.shellFrontZ),
  ]);

  shell = cutAll(shell, [
    // One continuous rectangular cavity contains both soldered PCBs.
    box(p.mainOuter.xMin + p.wallThickness, p.mainOuter.yMin + p.wallThickness,
      p.shellInnerBackZ, p.mainOuter.xMax - p.wallThickness,
      p.mainOuter.yMax - p.wallThickness, p.shellFrontZ + 0.50),
    // USB-C tunnel to the controller connector on the lower edge.
    box(p.usbOpening.xMin, p.mainOuter.yMin - 0.50, p.usbOpening.zMin,
      p.usbOpening.xMax, 5.25, p.usbOpening.zMax),
    // Open bottom/rear cable bay. It provides room for the plug body and lets
    // the cable turn toward the back between the two feet.
    box(p.usbCableBay.xMin, p.usbCableBay.yMin, p.shellBackZ - 0.40,
      p.usbCableBay.xMax, p.usbCableBay.yMax, p.shellFrontZ + 0.60),
  ]);

  // Rear screw bosses contact the back of the display PCB around its four
  // mounting holes. M2 screws enter from recessed pockets on the rear face,
  // pass through the 3.2 mm PCB holes, and thread into the front-cover inserts.
  p.closureHoleCenters.forEach(([centerX, centerY], index) => {
    const postEndZ = p.closurePostEndZ[index];
    let post = cylinderZ(p.shellInnerBackZ, postEndZ - p.shellInnerBackZ,
      centerX, centerY, 3.20);
    if (index === 0) {
      // The controller PCB begins 1.84 mm above this screw center. Relieve the
      // post above Y=5.20 while retaining the 2.4 mm screw passage.
      post = post.cut(box(centerX - 3.40, 5.20, p.shellInnerBackZ - 0.20,
        centerX + 3.40, centerY + 3.40, postEndZ + 0.20));
    }
    shell = shell.fuse(post);
    shell = shell.cut(cylinderZ(p.shellBackZ - 0.30, 19.40,
      centerX, centerY, p.enclosureScrewClearanceDiameter / 2));
    shell = shell.cut(cylinderZ(p.shellBackZ - 0.30,
      p.enclosureScrewHeadDepth + 0.30,
      centerX, centerY, p.enclosureScrewHeadDiameter / 2));
  });

  // Two upper foam-tipped posts stabilize the far end of the controller PCB.
  // The soldered header and the four PCB-mounted enclosure screws constrain
  // the rest of the stack.
  const controllerPostCenters = [
    [-9.11, 72.40],
  ];
  for (const [centerX, centerY] of controllerPostCenters) {
    shell = shell.fuse(cylinderZ(p.shellInnerBackZ, 10.15,
      centerX, centerY, 2.60));
  }

  // One unobstructed circular opening for the supplied 9.4 mm hinged antenna
  // base. The previous D-flat plus 12 mm counterbore looked like two different
  // holes and clipped one side of the opening.
  const antennaRadius = p.antennaMount.holeDiameter / 2;
  shell = shell.cut(cylinderZ(
    p.shellBackZ - 0.30, 3.40,
    p.antennaMount.x, p.antennaMount.y, antennaRadius,
  ));

  // Shallow exterior pockets let the folded prop sit flush without thinning
  // the 2.6 mm rear wall below 1.6 mm.
  shell = shell.cut(box(-15.10, -11.60, -21.30, -10.40, 44.70, -20.00));
  shell = shell.cut(box(4.40, -11.60, -21.30, 9.10, 44.70, -20.00));

  // External friction-pivot bosses for the folding desk prop.
  const pivotY = p.kickstandPivot.y;
  const pivotZ = p.kickstandPivot.z;
  shell = shell.fuse(cylinderX(-19.00, 4.00, pivotY, pivotZ, 4.00));
  shell = shell.fuse(cylinderX(9.00, 4.00, pivotY, pivotZ, 4.00));
  shell = shell.cut(cylinderX(-19.20, p.kickstandInsertDepth + 0.20,
    pivotY, pivotZ, p.kickstandInsertDiameter / 2));
  shell = shell.cut(cylinderX(13.00 - p.kickstandInsertDepth,
    p.kickstandInsertDepth + 0.20, pivotY, pivotZ, p.kickstandInsertDiameter / 2));

  return shell;
}

function buildKickstand() {
  const p = parameters;
  const radius = 3.50;
  const armRanges = [[-14.75, -10.75], [4.75, 8.75]];
  let stand = null;
  for (const [xMin, xMax] of armRanges) {
    let arm = cylinderX(xMin, xMax - xMin,
      p.kickstandPivot.y, p.kickstandPivot.z, radius);
    arm = arm.fuse(cylinderX(xMin, xMax - xMin,
      p.kickstandFar.y, p.kickstandFar.z, radius));
    const deltaY = p.kickstandFar.y - p.kickstandPivot.y;
    const deltaZ = p.kickstandFar.z - p.kickstandPivot.z;
    const centerDistance = Math.hypot(deltaY, deltaZ);
    const bridgeAngle = Math.atan2(deltaZ, deltaY) * 180 / Math.PI;
    const halfThickness = p.kickstandArmThickness / 2;
    const bridge = box(xMin, p.kickstandPivot.y, p.kickstandPivot.z - halfThickness,
      xMax, p.kickstandPivot.y + centerDistance, p.kickstandPivot.z + halfThickness)
      .rotate(bridgeAngle, [0, p.kickstandPivot.y, p.kickstandPivot.z], [1, 0, 0]);
    arm = arm.fuse(bridge);
    arm = arm.cut(cylinderX(xMin - 0.20, xMax - xMin + 0.40,
      p.kickstandPivot.y, p.kickstandPivot.z,
      p.kickstandPivotHoleDiameter / 2));
    stand = stand ? stand.fuse(arm) : arm;
  }

  // Wide rounded foot spreads load without requiring a desktop base.
  stand = stand.fuse(box(
    -14.75, p.kickstandFar.y - 3.50, -26.80,
    8.75, p.kickstandFar.y + 3.50, -22.80,
  ));
  stand = stand.cut(cylinderX(-15.00, 24.00,
    p.kickstandFar.y, p.kickstandFar.z, 1.20));
  return stand;
}

function buildFoldedAntenna(referenceSolids) {
  const p = parameters.antennaFold;
  const antennaBody = referenceSolids[1];

  // The vendor STEP is supplied in its straight position as one fused antenna
  // body. Split at the knuckle, rotate the whip 90 degrees, then orient the
  // connector through the rear wall. This is a placement reference only; the
  // printed enclosure contains the mounting hole, not printed antenna parts.
  const base = antennaBody.clone().intersect(box(
    -10.00, -20.00, -20.00, p.splitX, 20.00, 20.00,
  ));
  const whip = antennaBody.clone().intersect(box(
    p.splitX, -20.00, -20.00, 150.00, 20.00, 20.00,
  )).rotate(
    p.localFoldDegrees,
    [p.hingeX, 0, p.hingeZ],
    [0, 1, 0],
  );

  return [base, whip].map((solid) => solid
    .rotate(p.globalRotationY, [0, 0, 0], [0, 1, 0])
    .translate(...p.translation));
}

fs.mkdirSync(outputDir, { recursive: true });

const enclosureReference = await importStep(sourceEnclosurePath);
const enclosureSolids = solidChildren(enclosureReference);
const frontBezelReferenceDesign = enclosureSolids
  .slice()
  .sort((a, b) => b.boundingBox.bounds[1][2] - a.boundingBox.bounds[1][2])[0];
const frontCoverDesign = buildFrontCover();
let pcbAssembly = await importStep(sourcePcbPath);
pcbAssembly = pcbAssembly.translate(parameters.pcbTranslation);
const antennaReference = await importStep(sourceAntennaPath);
const antennaSolidsDesign = buildFoldedAntenna(solidChildren(antennaReference));
const antennaAssemblyDesign = replicad.compoundShapes(antennaSolidsDesign);

const rearShellDesign = buildRearShell();
const kickstandDesign = buildKickstand();
const deployedKickstandDesign = kickstandDesign.clone().rotate(
  30.00,
  [0, parameters.kickstandPivot.y, parameters.kickstandPivot.z],
  [1, 0, 0],
);

// Export every enclosure-related body in the supplied PCB STEP's native
// coordinate frame. This makes a zero-position Fusion import align correctly.
const outputTranslation = parameters.enclosureToPcbTranslation;
const frontBezelReference = frontBezelReferenceDesign.clone().translate(...outputTranslation);
const frontCover = frontCoverDesign.clone().translate(...outputTranslation);
const rearShell = rearShellDesign.clone().translate(...outputTranslation);
const kickstand = kickstandDesign.clone().translate(...outputTranslation);
const deployedKickstand = deployedKickstandDesign.clone().translate(...outputTranslation);
const antennaAssembly = antennaAssemblyDesign.clone().translate(...outputTranslation);

const foldedStandOverlap = replicad.measureVolume(
  rearShell.clone().intersect(kickstand.clone()),
);
const bezelShellOverlap = replicad.measureVolume(
  frontCover.clone().intersect(rearShell.clone()),
);
const pcbFrontCoverIntersection = frontCover.clone().intersect(pcbAssembly.clone());
const pcbRearShellIntersection = rearShell.clone().intersect(pcbAssembly.clone());
const pcbOriginalBezelIntersection = frontBezelReference.clone().intersect(pcbAssembly.clone());
const pcbFrontCoverOverlap = replicad.measureVolume(pcbFrontCoverIntersection);
const pcbRearShellOverlap = replicad.measureVolume(pcbRearShellIntersection);
const pcbOriginalBezelOverlap = replicad.measureVolume(pcbOriginalBezelIntersection);

await writeBlob(path.join(outputDir, 'MSV3_display_front_bezel_reference.step'), frontBezelReference.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_front_cover.step'), frontCover.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_rear_shell.step'), rearShell.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_kickstand.step'), kickstand.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_antenna_positioned.step'), antennaAssembly.blobSTEP());
await writeBlob(
  path.join(outputDir, 'MSV3_display_enclosure_assembly.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: rearShell, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: kickstand, name: 'Adjustable Folding Kickstand', color: '#d3a64d' },
    { shape: antennaAssembly, name: 'Folded Rear Antenna Reference', color: '#252a2e' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_fit_check_assembly.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: rearShell, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: kickstand, name: 'Adjustable Folding Kickstand', color: '#d3a64d' },
    { shape: antennaAssembly, name: 'Folded Rear Antenna Reference', color: '#252a2e' },
    { shape: pcbAssembly, name: 'Two-PCB Soldered Assembly Reference', color: '#26733a' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_pcb_alignment_check.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Origin-Aligned Front Cover', color: '#737a80' },
    { shape: pcbAssembly, name: 'Complete Native-Origin Two-PCB Assembly', color: '#26733a' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_enclosure_deployed.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: rearShell, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: deployedKickstand, name: 'Kickstand at 30 Degrees', color: '#d3a64d' },
    { shape: antennaAssembly, name: 'Folded Rear Antenna Reference', color: '#252a2e' },
  ]),
);

const explodedRear = rearShell.clone().translateZ(-18.00);
const explodedStand = kickstand.clone().translateX(30.00);
const explodedAntenna = antennaAssembly.clone().translateX(-25.00);
await writeBlob(
  path.join(outputDir, 'MSV3_display_enclosure_exploded.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: explodedRear, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: explodedStand, name: 'Adjustable Folding Kickstand', color: '#d3a64d' },
    { shape: explodedAntenna, name: 'Folded Rear Antenna', color: '#252a2e' },
  ]),
);

await writeBlob(path.join(outputDir, 'MSV3_display_front_cover_print.stl'),
  frontCover.clone().translateZ(-frontCover.boundingBox.bounds[0][2])
    .blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));
let rearShellForPrint = rearShell.clone().rotate(90, [0, 0, 0], [1, 0, 0]);
rearShellForPrint = rearShellForPrint.translateZ(-rearShellForPrint.boundingBox.bounds[0][2]);
await writeBlob(path.join(outputDir, 'MSV3_display_rear_shell_print.stl'),
  rearShellForPrint.blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));
await writeBlob(path.join(outputDir, 'MSV3_display_kickstand_print.stl'),
  kickstand.clone().rotate(90, [0, 0, 0], [0, 1, 0])
    .translateZ(-kickstand.clone().rotate(90, [0, 0, 0], [0, 1, 0]).boundingBox.bounds[0][2])
    .blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));

const activeCenter = [
  (parameters.bezelOpening.xMin + parameters.bezelOpening.xMax) / 2 + outputTranslation[0],
  (parameters.bezelOpening.yMin + parameters.bezelOpening.yMax) / 2 + outputTranslation[1],
];
const outputClosureHoleCenters = parameters.closureHoleCenters.map(([x, y]) => [
  x + outputTranslation[0], y + outputTranslation[1],
]);
const visibleFrontPlaneZ = parameters.visibleFrontZ + outputTranslation[2];
const visibleFrontFaces = frontCover.faces.filter((face) => {
  const bounds = face.boundingBox.bounds;
  return face.geomType === 'PLANE'
    && Math.abs(bounds[0][2] - visibleFrontPlaneZ) < 0.001
    && Math.abs(bounds[1][2] - visibleFrontPlaneZ) < 0.001;
}).map((face) => ({
  bounds: face.boundingBox.bounds,
  innerWireCount: face.innerWires().length,
}));
const outputCoordinates = {
  bodyEnvelope: {
    xMin: parameters.mainOuter.xMin + outputTranslation[0],
    xMax: parameters.mainOuter.xMax + outputTranslation[0],
    yMin: parameters.mainOuter.yMin + outputTranslation[1],
    yMax: parameters.mainOuter.yMax + outputTranslation[1],
  },
  closureHoleCenters: outputClosureHoleCenters,
  antennaMount: [
    parameters.antennaMount.x + outputTranslation[0],
    parameters.antennaMount.y + outputTranslation[1],
  ],
  kickstandPivot: [
    parameters.kickstandPivot.y + outputTranslation[1],
    parameters.kickstandPivot.z + outputTranslation[2],
  ],
};
const designReport = {
  version: 'prototype-6-native-clean-front',
  units: 'mm',
  parameters,
  outputCoordinates,
  frontCoverConstruction: {
    part: 'new single-piece planar frame using the proven screen-window dimensions from display_enclosure.step',
    modifiedLegacyBody: false,
    construction: 'one face slab, one LCD opening, four continuous perimeter walls, and four blind internal insert bosses',
    originalBounds: frontBezelReference.boundingBox.bounds,
    finishedBounds: frontCover.boundingBox.bounds,
    openingCenter: activeCenter,
    legacySideSlot: false,
    legacyFaceSeams: false,
    visibleFrontFaceCount: visibleFrontFaces.length,
    visibleFrontFaces,
  },
  pcbFit: {
    sourceAssembly: 'MSV3_desktop_display.step',
    solidCount: solidChildren(pcbAssembly).length,
    nativeBounds: pcbAssembly.boundingBox.bounds,
    nativeOriginPreserved: true,
    displayGlassRecessBelowBezelFront: 0.15,
    unifiedRectangle: true,
    backComponentClearance: 0.87,
    displayBoardRetention: 'clamped at three unobstructed 3.2 mm display-board mounting holes',
    controllerBoardRetention: 'fourth enclosure screw passes through the controller board upper-left mounting hole',
    controllerSupport: 'soldered header plus two foam-tipped rear posts at the upper mounting holes',
    frontCoverIntersectionVolume: pcbFrontCoverOverlap,
    originalReferenceIntersectionVolume: pcbOriginalBezelOverlap,
    rearShellIntersectionVolume: pcbRearShellOverlap,
  },
  antenna: {
    source: sourceAntennaPath,
    identifiedStyle: '113 x 10 mm hinged connector-mount whip, shown folded 90 degrees',
    placement: 'high on the rear wall, folded horizontally inside the front-view silhouette',
    attachment: 'single 10.0 mm circular through-bore sized around the supplied 9.4 mm hinged antenna base',
  },
  usb: {
    connectorDirection: 'downward',
    access: 'open bottom/rear cable bay between two 8 mm feet',
    purpose: 'allows a straight USB-C plug to enter and its cable to turn toward the rear above the desk',
  },
  stand: {
    type: 'fold-flat friction kickstand',
    fasteners: 'two M2 screws into 2.9 mm x 3.2 mm heat-set-insert pockets',
    armCenterDistance: Math.hypot(
      parameters.kickstandFar.y - parameters.kickstandPivot.y,
      parameters.kickstandFar.z - parameters.kickstandPivot.z,
    ),
    armThickness: parameters.kickstandArmThickness,
    foldedWithinMainXYFootprint: true,
    adjustment: 'set display lean angle by rotating the prop and tightening both M2 pivots',
    foldedIntersectionVolume: foldedStandOverlap,
  },
  closure: {
    type: 'four M2 screws through three display-PCB holes and one controller-PCB hole into front-cover heat-set inserts',
    screwCount: parameters.closureHoleCenters.length,
    clearanceHoleDiameter: parameters.enclosureScrewClearanceDiameter,
    insertPocket: {
      diameter: parameters.enclosureInsertDiameter,
      depth: parameters.enclosureInsertDepth,
    },
    minimumInsertBlindWall: parameters.visibleFrontZ
      - (parameters.closureInsertFaceZ[0] + parameters.enclosureInsertDepth),
    frontFaceThickness: parameters.visibleFrontZ - parameters.frontFaceBackZ,
    controllerInsertFaceClearance: (
      parameters.closureInsertFaceZ[3] + outputTranslation[2]
    ) - (-4.58852),
    controllerRearPostClearance: -5.00 - (
      parameters.closurePostEndZ[3] + outputTranslation[2]
    ),
    pcbMountingHoleDiameter: 3.20,
    closedBezelIntersectionVolume: bezelShellOverlap,
  },
};
fs.writeFileSync(path.join(outputDir, 'design_report.json'), `${JSON.stringify(designReport, null, 2)}\n`);

console.log(JSON.stringify({
  outputDir,
  frontCoverBounds: frontCover.boundingBox.bounds,
  rearShellBounds: rearShell.boundingBox.bounds,
  kickstandBounds: kickstand.boundingBox.bounds,
  antennaBounds: antennaAssembly.boundingBox.bounds,
  pcbBounds: pcbAssembly.boundingBox.bounds,
  foldedStandOverlap,
  bezelShellOverlap,
  pcbFrontCoverOverlap,
  pcbOriginalBezelOverlap,
  pcbRearShellOverlap,
  pcbFrontCoverIntersectionBounds: pcbFrontCoverOverlap > 0.001
    ? pcbFrontCoverIntersection.boundingBox.bounds : null,
  pcbRearShellIntersectionBounds: pcbRearShellOverlap > 0.001
    ? pcbRearShellIntersection.boundingBox.bounds : null,
}, null, 2));

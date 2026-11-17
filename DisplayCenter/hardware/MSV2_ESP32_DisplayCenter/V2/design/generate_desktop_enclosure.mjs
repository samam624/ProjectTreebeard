import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const outputDir = path.join(projectDir, 'output');
const wasmPath = path.resolve(
  here,
  '../../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.wasm',
);
const sourceEnclosurePath = path.join(projectDir, 'display_enclosure.step');
const sourcePcbPath = path.join(projectDir, 'MSV3_desktop_display.step');

// All coordinates below are in the enclosure DESIGN frame. The PCB STEP's
// native frame is this frame plus enclosureToPcbTranslation.
const parameters = {
  // Keep the supplied PCB STEP at its native origin. The legacy bezel and all
  // generated enclosure parts are moved into that coordinate frame at export.
  pcbTranslation: [0.00, 0.00, 0.00],
  enclosureToPcbTranslation: [-6.39, -13.903, 3.00],

  // --- As-built PCB stack (prototype 9, 2026-11-17) ------------------------
  // MSV3_desktop_display.step places the controller PCB WRONG. It draws the
  // display module's 14-pin header 4.0 mm above the display board's centre
  // line, and the controller hangs on that header. On the real module the
  // header is centred on the mounting-hole pattern and sits ~0.9 mm outboard
  // of the left-hand holes, so the real controller sits lower and further left
  // than the model. Prototype 8 was built on the model and two of its four
  // screws could not be fitted. Evidence, 2026-11-17:
  //   MEASURED  header pads vs display holes, front photo (same plane):  X -3.7, Y -4.0
  //   MEASURED  USB-C receptacle vs the printed 16 mm bay (photo):       X -3.3
  //   CALIPERS  controller upper-left hole to display upper-left hole:  ~10.5 mm c-c
  //   CALIPERS  display lower-left hole to USB-C centre:                ~23.5 mm
  //   FAB DATA  controller top-hole spacing 45.00 mm, holes 2.2 mm (drill file,
  //             pcb/DisplayCenter_controller_fabrication_2026-07-14.zip)
  // The offset is therefore an estimate good to about +/-0.4 mm. Print the
  // fit-check plate before the full enclosure. Z is NOT measured: -0.30 is the
  // midpoint between the model's -8.00 back face and a photo estimate near
  // -8.6, and the controller clamp bosses leave clearance on both faces.
  controllerAsBuiltOffset: [-3.40, -3.80, -0.30],
  controllerModelOutline: { xMin: -57.11, xMax: -6.11, yMin: 5.403, yMax: 75.403 },
  controllerModelBackZ: -8.00,
  controllerThickness: 1.60, // the STEP slab is 0.411 mm; the board edge in the USB photo is ~1.6 mm
  // Controller top-edge mounting holes (drill file (3, 3) and (48, 3)), model frame.
  controllerModelTopHoles: [[-54.11, 72.403], [-9.11, 72.403]],
  // STEP solids that physically move with the controller: the whole
  // controller PCB (0-301) and the display header it hangs on (312-326).
  controllerSolidIndexRanges: [[0, 301], [312, 326]],
  controllerTopClearance: 1.00,

  // Window = the LCD active area exactly (solid 311 of the PCB STEP). The old
  // 88 x 58 mm window was 2.24 mm oversize per side and its right edge sat only
  // 0.26 mm inside the glass's white frame (measured in the front photo), so
  // any sag of the half-mounted stack exposed that frame.
  bezelOpening: { xMin: -44.76, xMax: 38.76, yMin: 3.173, yMax: 58.853 },
  displayActiveArea: { width: 83.52, height: 55.68 },
  // yMin and yMax are derived below: equal top and bottom borders, with the top
  // set by the as-built controller edge.
  mainOuter: { xMin: -64.00, xMax: 58.00, yMin: null, yMax: null },
  previousMainOuterYMin: -4.00, // prototype 8, used to carry the stand geometry down
  shellFrontZ: -6.15,
  shellBackZ: -21.00,
  shellInnerBackZ: -18.40,
  wallThickness: 2.20,
  frontFaceBackZ: 2.08,
  visibleFrontZ: 3.10,

  // Display PCB right-hand holes (unchanged; both fitted on prototype 8).
  displayRightHoles: [[45.16, 3.5645], [45.16, 58.4630]],
  // closureHoleCenters = display right-hand pair + controller top pair (derived).
  closureHoleCenters: null,
  closurePostEndZ: [-4.00, -4.00, -8.90, -8.90],
  closureInsertFaceZ: [-2.15, -2.15, -6.10, -6.10],
  closurePostRadius: [3.20, 3.20, 3.00, 3.00],
  enclosureScrewClearanceDiameter: 2.40,
  enclosureScrewHeadDiameter: 4.60,
  enclosureScrewHeadDepth: 2.00,
  enclosureInsertDiameter: 2.90,
  enclosureInsertDepth: 3.20,
  enclosureInsertBossRadius: 3.20,
  controllerScrewReliefDepth: 1.50,

  // USB-C receptacle (STEP solid 1), model frame; the bay is re-centred on its
  // as-built position.
  usbModelReceptacle: { xMin: -35.583, xMax: -26.637, yMin: 4.80, zMin: -11.263, zMax: -7.097 },
  usbCableBayWidth: 16.50,
  usbBayAboveMouth: 0.80,
  usbOvermoldNotchTopZ: -4.40,
  usbOpening: null,
  usbCableBay: null,
  bottomFeet: null,
  // Prototype-8 stand geometry; moved down with the bottom edge (derived) so the
  // lean geometry relative to the desk is unchanged.
  kickstandPivotPrevious: { y: 41.00, z: -23.80 },
  kickstandFarPrevious: { y: -7.50, z: -24.80 },
  kickstandPocketYPrevious: [-11.60, 44.70],
  kickstandPivot: null,
  kickstandFar: null,
  kickstandArmThickness: 4.00,
  kickstandPivotHoleDiameter: 2.40,
  // One M2 screw per side, head on the shell lug, through the arm, into a
  // captive M2 nut (4.0 mm A/F, 1.6 mm) on the arm's inner face.
  kickstandNutAcrossFlats: 4.30,
  kickstandNutDepth: 1.80,

  fitCheckTubeOuterRadius: 2.50,
  fitCheckTubeHoleDiameter: 2.30,
  fitCheckTubeEndZ: [-2.00, -2.00, -5.50, -5.50],
};

{
  const p = parameters;
  const [dx, dy, dz] = p.controllerAsBuiltOffset;
  p.controllerAsBuilt = {
    xMin: p.controllerModelOutline.xMin + dx,
    xMax: p.controllerModelOutline.xMax + dx,
    yMin: p.controllerModelOutline.yMin + dy,
    yMax: p.controllerModelOutline.yMax + dy,
    backZ: p.controllerModelBackZ + dz,
    frontZ: p.controllerModelBackZ + dz + p.controllerThickness,
  };
  p.controllerTopHoles = p.controllerModelTopHoles.map(([x, y]) => [x + dx, y + dy]);
  p.closureHoleCenters = [...p.displayRightHoles, ...p.controllerTopHoles];

  p.mainOuter.yMax = p.controllerAsBuilt.yMax + p.controllerTopClearance + p.wallThickness;
  p.mainOuter.yMin = p.bezelOpening.yMin - (p.mainOuter.yMax - p.bezelOpening.yMax);
  const drop = p.mainOuter.yMin - p.previousMainOuterYMin;
  p.bottomEdgeDrop = drop;

  const usb = p.usbModelReceptacle;
  const usbCenterX = (usb.xMin + usb.xMax) / 2 + dx;
  const usbCenterZ = (usb.zMin + usb.zMax) / 2 + dz;
  const usbMouthY = usb.yMin + dy;
  p.usbAsBuilt = { centerX: usbCenterX, centerZ: usbCenterZ, mouthY: usbMouthY };
  p.usbOpening = {
    xMin: usbCenterX - 5.20, xMax: usbCenterX + 5.20,
    zMin: usbCenterZ - 2.625, zMax: usbCenterZ + 2.625,
    yMax: usbMouthY + 0.45,
  };
  p.usbCableBay = {
    xMin: usbCenterX - p.usbCableBayWidth / 2,
    xMax: usbCenterX + p.usbCableBayWidth / 2,
    yMin: p.mainOuter.yMin - 8.20,
    yMax: usbMouthY + p.usbBayAboveMouth,
  };
  p.bottomFeet = {
    yMin: p.mainOuter.yMin - 8.00, yMax: p.mainOuter.yMin,
    leftXMin: -62.00, leftXMax: p.usbCableBay.xMin,
    rightXMin: 37.00, rightXMax: 56.00,
  };
  p.kickstandPivot = { y: p.kickstandPivotPrevious.y + drop, z: p.kickstandPivotPrevious.z };
  p.kickstandFar = { y: p.kickstandFarPrevious.y + drop, z: p.kickstandFarPrevious.z };
  p.kickstandPocketY = p.kickstandPocketYPrevious.map((y) => y + drop);
}

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

  // Four back-facing heat-set-insert seats: the display PCB's two right-hand
  // holes and the controller PCB's two top holes. The display bosses stop just
  // ahead of the display PCB; the controller bosses stop short of the
  // controller's front face (its Z is not measured) and get a screw-tip relief
  // below the insert so an M2x16 reaches full engagement.
  p.closureHoleCenters.forEach(([centerX, centerY], index) => {
    const insertFaceZ = p.closureInsertFaceZ[index];
    const controllerHole = index >= 2;
    cover = cover.fuse(cylinderZ(
      insertFaceZ, p.frontFaceBackZ - insertFaceZ + 0.20,
      centerX, centerY, p.enclosureInsertBossRadius,
    ));
    if (controllerHole) {
      // Tie each controller column into the top wall, and the left one into
      // the left wall as well. Both webs sit above the display PCB's top edge.
      cover = cover.fuse(box(
        centerX - 2.00, centerY, insertFaceZ,
        centerX + 2.00, innerYMax + 0.10, p.frontFaceBackZ + 0.10,
      ));
      if (index === 2) {
        cover = cover.fuse(box(
          innerXMin - 0.10, centerY - 2.00, insertFaceZ,
          centerX, centerY + 2.00, p.frontFaceBackZ + 0.10,
        ));
      }
    }
    cover = cover.cut(cylinderZ(insertFaceZ - 0.10, p.enclosureInsertDepth,
      centerX, centerY, p.enclosureInsertDiameter / 2));
    if (controllerHole) {
      cover = cover.cut(cylinderZ(insertFaceZ - 0.10 + p.enclosureInsertDepth - 0.05,
        p.controllerScrewReliefDepth + 0.05, centerX, centerY,
        p.enclosureScrewClearanceDiameter / 2));
    }
  });

  // Relieve the rear edge of the bottom wall above the USB bay so a plug
  // overmold up to ~10 mm tall clears the cover. Not visible from the front.
  cover = cover.cut(box(
    p.usbCableBay.xMin, p.mainOuter.yMin - 0.20, p.shellFrontZ - 0.20,
    p.usbCableBay.xMax, innerYMin + 0.20, p.usbOvermoldNotchTopZ,
  ));

  return cover.simplify();
}

// A 1 mm copy of the front face with guide tubes at the four screw positions.
// Lay it on the screen of the assembled PCB stack and drop M2 screws (or 2 mm
// drill bits) down the tubes: all four should enter their PCB holes while the
// window frames the lit area. Prints in about 20 minutes, face down.
function buildFitCheckPlate() {
  const p = parameters;
  let plate = box(
    p.mainOuter.xMin, p.mainOuter.yMin, p.frontFaceBackZ,
    p.mainOuter.xMax, p.mainOuter.yMax, p.visibleFrontZ,
  ).cut(box(
    p.bezelOpening.xMin, p.bezelOpening.yMin, p.frontFaceBackZ - 0.20,
    p.bezelOpening.xMax, p.bezelOpening.yMax, p.visibleFrontZ + 0.20,
  ));
  p.closureHoleCenters.forEach(([centerX, centerY], index) => {
    const endZ = p.fitCheckTubeEndZ[index];
    plate = plate.fuse(cylinderZ(endZ, p.frontFaceBackZ - endZ + 0.10,
      centerX, centerY, p.fitCheckTubeOuterRadius));
  });
  p.closureHoleCenters.forEach(([centerX, centerY], index) => {
    const endZ = p.fitCheckTubeEndZ[index];
    plate = plate.cut(cylinderZ(endZ - 0.20, p.visibleFrontZ - endZ + 0.40,
      centerX, centerY, p.fitCheckTubeHoleDiameter / 2));
  });
  return plate.simplify();
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
      p.usbOpening.xMax, p.usbOpening.yMax, p.usbOpening.zMax),
    // Open bottom/rear cable bay. It provides room for the plug body and lets
    // the cable turn toward the back between the two feet.
    box(p.usbCableBay.xMin, p.usbCableBay.yMin, p.shellBackZ - 0.40,
      p.usbCableBay.xMax, p.usbCableBay.yMax, p.shellFrontZ + 0.60),
  ]);

  // Rear screw posts behind the four screw positions. M2 screws enter from
  // recessed pockets on the rear face, pass through the PCB holes (3.2 mm on
  // the display PCB, 2.2 mm on the controller), and thread into the
  // front-cover inserts. The display posts bear on the display PCB; the
  // controller posts stop short of the controller's back face.
  p.closureHoleCenters.forEach(([centerX, centerY], index) => {
    const postEndZ = p.closurePostEndZ[index];
    shell = shell.fuse(cylinderZ(p.shellInnerBackZ, postEndZ - p.shellInnerBackZ,
      centerX, centerY, p.closurePostRadius[index]));
    shell = shell.cut(cylinderZ(p.shellBackZ - 0.30, 19.40,
      centerX, centerY, p.enclosureScrewClearanceDiameter / 2));
    shell = shell.cut(cylinderZ(p.shellBackZ - 0.30,
      p.enclosureScrewHeadDepth + 0.30,
      centerX, centerY, p.enclosureScrewHeadDiameter / 2));
  });

  // Shallow exterior pockets let the folded prop sit flush without thinning
  // the 2.6 mm rear wall below 1.6 mm.
  const [pocketYMin, pocketYMax] = p.kickstandPocketY;
  shell = shell.cut(box(-15.10, pocketYMin, -21.30, -10.40, pocketYMax, -20.00));
  shell = shell.cut(box(4.40, pocketYMin, -21.30, 9.10, pocketYMax, -20.00));

  // External pivot lugs for the folding desk prop. The pivot hole goes all
  // the way through each lug (prototype 8 had a blind insert pocket that
  // opened away from the arm, so no screw could reach the arm).
  const pivotY = p.kickstandPivot.y;
  const pivotZ = p.kickstandPivot.z;
  shell = shell.fuse(cylinderX(-19.00, 4.00, pivotY, pivotZ, 4.00));
  shell = shell.fuse(cylinderX(9.00, 4.00, pivotY, pivotZ, 4.00));
  shell = shell.cut(cylinderX(-19.40, 4.80, pivotY, pivotZ,
    p.kickstandPivotHoleDiameter / 2));
  shell = shell.cut(cylinderX(8.60, 4.80, pivotY, pivotZ,
    p.kickstandPivotHoleDiameter / 2));

  return shell;
}

// Hexagonal prism along X. Vertices point along +/-Y, which is vertical when
// the kickstand prints on its foot, so the pocket roof is self-supporting.
function hexPrismX(xMin, length, centerY, centerZ, acrossFlats) {
  const half = acrossFlats / 2;
  const slab = () => box(xMin, centerY - acrossFlats, centerZ - half,
    xMin + length, centerY + acrossFlats, centerZ + half);
  const axisOrigin = [0, centerY, centerZ];
  return slab()
    .intersect(slab().rotate(60, axisOrigin, [1, 0, 0]))
    .intersect(slab().rotate(-60, axisOrigin, [1, 0, 0]));
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
    // Captive M2 nut on the arm's inner face (the face toward the other arm).
    const innerFaceX = xMin < 0 ? xMax : xMin;
    const trapXMin = xMin < 0 ? innerFaceX - p.kickstandNutDepth : innerFaceX - 0.20;
    arm = arm.cut(hexPrismX(trapXMin, p.kickstandNutDepth + 0.20,
      p.kickstandPivot.y, p.kickstandPivot.z, p.kickstandNutAcrossFlats));
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

fs.mkdirSync(outputDir, { recursive: true });
fs.rmSync(path.join(outputDir, 'MSV3_display_antenna_positioned.step'), { force: true });

const enclosureReference = await importStep(sourceEnclosurePath);
const enclosureSolids = solidChildren(enclosureReference);
const frontBezelReferenceDesign = enclosureSolids
  .slice()
  .sort((a, b) => b.boundingBox.bounds[1][2] - a.boundingBox.bounds[1][2])[0];
const frontCoverDesign = buildFrontCover();
let pcbAssembly = await importStep(sourcePcbPath);
pcbAssembly = pcbAssembly.translate(parameters.pcbTranslation);

// As-built PCB reference: the supplied STEP with the controller PCB and the
// display header it hangs on moved by controllerAsBuiltOffset, plus a
// full-thickness controller slab (the STEP's own slab is 0.411 mm).
const pcbSolids = solidChildren(pcbAssembly);
const expectedSolidSizes = { 0: [51.00, 70.00], 1: [8.946, 7.604], 302: [108.04, 61.74], 311: [83.52, 55.68] };
for (const [index, [sizeX, sizeY]] of Object.entries(expectedSolidSizes)) {
  const [min, max] = pcbSolids[index].boundingBox.bounds;
  if (Math.abs(max[0] - min[0] - sizeX) > 0.01 || Math.abs(max[1] - min[1] - sizeY) > 0.01) {
    throw new Error(`PCB STEP solid ${index} is not the expected part; the solid order changed.`);
  }
}
const movesWithController = (index) => parameters.controllerSolidIndexRanges
  .some(([first, last]) => index >= first && index <= last);
const asBuiltPcbParts = pcbSolids.map((solid, index) => (
  movesWithController(index) ? solid.clone().translate(...parameters.controllerAsBuiltOffset) : solid));
const ctl = parameters.controllerAsBuilt;
const asBuiltControllerSlab = box(ctl.xMin, ctl.yMin, ctl.backZ, ctl.xMax, ctl.yMax, ctl.frontZ)
  .translate(...parameters.enclosureToPcbTranslation);
// makeCompound deletes the shapes it is given, so hand it clones.
const asBuiltPcb = replicad.makeCompound(
  [...asBuiltPcbParts, asBuiltControllerSlab].map((shape) => shape.clone()),
);

const rearShellDesign = buildRearShell();
const kickstandDesign = buildKickstand();
const fitCheckPlateDesign = buildFitCheckPlate();
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
const fitCheckPlate = fitCheckPlateDesign.clone().translate(...outputTranslation);

const foldedStandOverlap = replicad.measureVolume(
  rearShell.clone().intersect(kickstand.clone()),
);
const bezelShellOverlap = replicad.measureVolume(
  frontCover.clone().intersect(rearShell.clone()),
);
// Fit is judged against the AS-BUILT stack. The raw STEP is kept for the
// record: prototype 8 passed against it and still did not fit.
const pcbFrontCoverIntersection = frontCover.clone().intersect(asBuiltPcb.clone());
const pcbRearShellIntersection = rearShell.clone().intersect(asBuiltPcb.clone());
const pcbFitPlateIntersection = fitCheckPlate.clone().intersect(asBuiltPcb.clone());
const pcbOriginalBezelIntersection = frontBezelReference.clone().intersect(pcbAssembly.clone());
const pcbFrontCoverOverlap = replicad.measureVolume(pcbFrontCoverIntersection);
const pcbRearShellOverlap = replicad.measureVolume(pcbRearShellIntersection);
const pcbFitPlateOverlap = replicad.measureVolume(pcbFitPlateIntersection);
const pcbOriginalBezelOverlap = replicad.measureVolume(pcbOriginalBezelIntersection);
const rawStepFrontCoverOverlap = replicad.measureVolume(frontCover.clone().intersect(pcbAssembly.clone()));
const rawStepRearShellOverlap = replicad.measureVolume(rearShell.clone().intersect(pcbAssembly.clone()));

await writeBlob(path.join(outputDir, 'MSV3_display_front_bezel_reference.step'), frontBezelReference.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_front_cover.step'), frontCover.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_rear_shell.step'), rearShell.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_kickstand.step'), kickstand.blobSTEP());
await writeBlob(path.join(outputDir, 'MSV3_display_fit_check_plate.step'), fitCheckPlate.blobSTEP());
await writeBlob(
  path.join(outputDir, 'MSV3_display_asbuilt_pcb_reference.step'),
  replicad.exportSTEP([
    ...asBuiltPcbParts.map((shape, index) => ({ shape, name: `As-built PCB solid ${index}`, color: '#26733a' })),
    { shape: asBuiltControllerSlab, name: 'As-built controller PCB 1.6 mm slab', color: '#1f5f30' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_enclosure_assembly.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: rearShell, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: kickstand, name: 'Adjustable Folding Kickstand', color: '#d3a64d' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_fit_check_assembly.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: rearShell, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: kickstand, name: 'Adjustable Folding Kickstand', color: '#d3a64d' },
    { shape: asBuiltPcb, name: 'As-Built Two-PCB Stack Reference', color: '#26733a' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_pcb_alignment_check.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Origin-Aligned Front Cover', color: '#737a80' },
    { shape: asBuiltPcb, name: 'As-Built Two-PCB Stack Reference', color: '#26733a' },
  ]),
);
await writeBlob(
  path.join(outputDir, 'MSV3_display_enclosure_deployed.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: rearShell, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: deployedKickstand, name: 'Kickstand at 30 Degrees', color: '#d3a64d' },
  ]),
);

const explodedRear = rearShell.clone().translateZ(-18.00);
const explodedStand = kickstand.clone().translateX(30.00);
await writeBlob(
  path.join(outputDir, 'MSV3_display_enclosure_exploded.step'),
  replicad.exportSTEP([
    { shape: frontCover, name: 'Extended Front Cover with M2 Inserts', color: '#737a80' },
    { shape: explodedRear, name: 'Single Rectangle Rear Shell', color: '#345f78' },
    { shape: explodedStand, name: 'Adjustable Folding Kickstand', color: '#d3a64d' },
  ]),
);

// Print the front cover with the visible bezel face on the bed.  The previous
// orientation put the insert bosses on the bed and left the top and bottom
// bezel rails as floating regions above the LCD opening.  Face-down removes
// those unsupported bridges and is the stable orientation for a 0.4 mm nozzle.
let frontCoverForPrint = frontCover.clone().rotate(180, [0, 0, 0], [1, 0, 0]);
const frontPrintMin = frontCoverForPrint.boundingBox.bounds[0];
frontCoverForPrint = frontCoverForPrint.translate(
  -frontPrintMin[0], -frontPrintMin[1], -frontPrintMin[2],
);
await writeBlob(path.join(outputDir, 'MSV3_display_front_cover_print.stl'),
  frontCoverForPrint.blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));
// Stand the rear shell on its continuous right side wall.  Printing it on the
// lower edge left the center of the shell suspended between the two USB feet.
// The -90 degree direction also leaves its local X coordinates positive, which
// Bambu Studio's command-line arranger requires before it places the objects.
let rearShellForPrint = rearShell.clone().rotate(-90, [0, 0, 0], [0, 1, 0]);
const rearPrintMin = rearShellForPrint.boundingBox.bounds[0];
rearShellForPrint = rearShellForPrint.translate(
  -rearPrintMin[0], -rearPrintMin[1], -rearPrintMin[2],
);
await writeBlob(path.join(outputDir, 'MSV3_display_rear_shell_print.stl'),
  rearShellForPrint.blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));
let kickstandForPrint = kickstand.clone().rotate(90, [0, 0, 0], [1, 0, 0]);
const kickstandPrintMin = kickstandForPrint.boundingBox.bounds[0];
kickstandForPrint = kickstandForPrint.translate(
  -kickstandPrintMin[0], -kickstandPrintMin[1], -kickstandPrintMin[2],
);
await writeBlob(path.join(outputDir, 'MSV3_display_kickstand_print.stl'),
  kickstandForPrint.blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));
// Fit-check plate prints face down like the front cover; the tubes point up.
let fitCheckPlateForPrint = fitCheckPlate.clone().rotate(180, [0, 0, 0], [1, 0, 0]);
const fitPlatePrintMin = fitCheckPlateForPrint.boundingBox.bounds[0];
fitCheckPlateForPrint = fitCheckPlateForPrint.translate(
  -fitPlatePrintMin[0], -fitPlatePrintMin[1], -fitPlatePrintMin[2],
);
await writeBlob(path.join(outputDir, 'MSV3_display_fit_check_plate_print.stl'),
  fitCheckPlateForPrint.blobSTL({ tolerance: 0.06, angularTolerance: 0.25, binary: true }));

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
  kickstandPivot: [
    parameters.kickstandPivot.y + outputTranslation[1],
    parameters.kickstandPivot.z + outputTranslation[2],
  ],
};
const p = parameters;
const borders = {
  left: p.bezelOpening.xMin - p.mainOuter.xMin,
  right: p.mainOuter.xMax - p.bezelOpening.xMax,
  top: p.mainOuter.yMax - p.bezelOpening.yMax,
  bottom: p.bezelOpening.yMin - p.mainOuter.yMin,
};
// Screw lengths: the head seats at the bottom of the rear pocket; the tip must
// stop 0.10 mm short of the blind end (insert pocket, plus the relief on the
// controller bosses).
const headSeatZ = p.shellBackZ + p.enclosureScrewHeadDepth;
const screwPlan = p.closureHoleCenters.map((center, index) => {
  const insertFaceZ = p.closureInsertFaceZ[index];
  const pocketEndZ = insertFaceZ - 0.10 + p.enclosureInsertDepth;
  const blindEndZ = index >= 2 ? pocketEndZ + p.controllerScrewReliefDepth : pocketEndZ;
  const options = [];
  for (const length of [12, 14, 16, 18, 20, 22]) {
    for (const washer of [0, 0.30]) {
      // A washer lifts the head off the pocket floor, i.e. toward the rear (-Z).
      const tipZ = headSeatZ - washer + length;
      if (tipZ > blindEndZ - 0.10) continue;
      const engagement = Math.min(tipZ, insertFaceZ + p.enclosureInsertDepth) - insertFaceZ;
      options.push({ length, washerMm: washer, tipZ, engagement });
    }
  }
  options.sort((a, b) => b.engagement - a.engagement || a.washerMm - b.washerMm);
  return {
    hole: ['display PCB lower-right', 'display PCB upper-right',
      'controller PCB upper-left', 'controller PCB upper-right'][index],
    center,
    blindEndZ,
    best: options[0],
    noWasher: options.find((option) => option.washerMm === 0),
  };
});
const designReport = {
  version: 'prototype-9-asbuilt-fit',
  units: 'mm',
  parameters,
  outputCoordinates,
  changesFromPrototype8: {
    window: 'cut to the 83.52 x 55.68 mm LCD active area (was 88 x 58 mm)',
    mounting: 'display PCB right-hand pair + controller PCB top pair (was three display holes + one controller hole)',
    controllerPosition: 'enclosure built around the as-built controller position, not the STEP model',
    symmetry: 'top and bottom borders equal',
    usb: 'cable bay re-centred on the as-built receptacle and widened to 16.5 mm; front-cover overmold notch',
    kickstand: 'pivot holes go through both lugs; captive M2 nut trap in each arm; pivot moved down with the bottom edge',
    removed: 'display lower-left post and boss; foam-tipped controller support post',
  },
  borders,
  frontCoverConstruction: {
    part: 'single-piece planar frame with one LCD opening',
    modifiedLegacyBody: false,
    construction: 'one face slab, one LCD opening, four continuous perimeter walls, four blind internal insert bosses, and a USB overmold notch in the rear edge of the bottom wall',
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
    reference: 'AS-BUILT: STEP solids 0-301 and 312-326 moved by controllerAsBuiltOffset, plus a 1.6 mm controller slab (MSV3_display_asbuilt_pcb_reference.step)',
    solidCount: pcbSolids.length,
    nativeBounds: pcbAssembly.boundingBox.bounds,
    asBuiltBounds: asBuiltPcb.boundingBox.bounds,
    nativeOriginPreserved: true,
    controllerAsBuiltOffset: p.controllerAsBuiltOffset,
    controllerAsBuiltOffsetConfidence: 'about +/-0.4 mm in X and Y from photo and caliper evidence; Z not measured',
    controllerAsBuilt: p.controllerAsBuilt,
    usbAsBuilt: p.usbAsBuilt,
    displayGlassRecessBelowBezelFront: p.visibleFrontZ - 1.933,
    displayBoardRetention: 'clamped at the two right-hand 3.2 mm display-board holes',
    controllerBoardRetention: 'two M2 screws through the controller top-edge 2.2 mm holes; bosses stop short of both faces',
    controllerFrontGap: p.closureInsertFaceZ[2] - p.controllerAsBuilt.frontZ,
    controllerRearGap: p.controllerAsBuilt.backZ - p.closurePostEndZ[2],
    frontCoverIntersectionVolume: pcbFrontCoverOverlap,
    rearShellIntersectionVolume: pcbRearShellOverlap,
    fitCheckPlateIntersectionVolume: pcbFitPlateOverlap,
    originalReferenceIntersectionVolume: pcbOriginalBezelOverlap,
    rawStepFrontCoverIntersectionVolume: rawStepFrontCoverOverlap,
    rawStepRearShellIntersectionVolume: rawStepRearShellOverlap,
  },
  antenna: {
    placement: 'internal; mount after the enclosure is confirmed (maintainer 2026-11-17)',
    externalOpening: false,
  },
  usb: {
    connectorDirection: 'downward',
    receptacleCenterX: p.usbAsBuilt.centerX,
    bayCenterX: (p.usbCableBay.xMin + p.usbCableBay.xMax) / 2,
    bayWidth: p.usbCableBay.xMax - p.usbCableBay.xMin,
    mouthToBodyBottom: p.usbAsBuilt.mouthY - p.mainOuter.yMin,
    mouthToDesk: p.usbAsBuilt.mouthY - p.bottomFeet.yMin,
    overmoldClearanceAboveCenter: p.usbOvermoldNotchTopZ - p.usbAsBuilt.centerZ,
    access: 'open bottom/rear cable bay between two 8 mm feet',
  },
  stand: {
    type: 'fold-flat friction kickstand',
    fasteners: "two M2x10 screws, heads on the shell lugs, through 2.4 mm holes into captive M2 nuts on the arms' inner faces",
    nutTrap: { acrossFlats: p.kickstandNutAcrossFlats, depth: p.kickstandNutDepth },
    armCenterDistance: Math.hypot(
      p.kickstandFar.y - p.kickstandPivot.y,
      p.kickstandFar.z - p.kickstandPivot.z,
    ),
    armThickness: p.kickstandArmThickness,
    pivotMovedDownBy: -p.bottomEdgeDrop,
    foldedWithinMainXYFootprint: true,
    adjustment: 'set the lean angle, then tighten both screws against the captive nuts',
    foldedIntersectionVolume: foldedStandOverlap,
  },
  closure: {
    type: 'four M2 screws from the rear: two through the display PCB right-hand holes, two through the controller PCB top holes, into front-cover heat-set inserts',
    screwCount: p.closureHoleCenters.length,
    clearanceHoleDiameter: p.enclosureScrewClearanceDiameter,
    insertPocket: { diameter: p.enclosureInsertDiameter, depth: p.enclosureInsertDepth },
    minimumInsertBlindWall: p.visibleFrontZ
      - (p.closureInsertFaceZ[0] + p.enclosureInsertDepth),
    frontFaceThickness: p.visibleFrontZ - p.frontFaceBackZ,
    pcbMountingHoleDiameter: { display: 3.20, controller: 2.20 },
    screwPlan,
    closedBezelIntersectionVolume: bezelShellOverlap,
  },
  fitCheckPlate: {
    file: 'MSV3_display_fit_check_plate_print.stl',
    tubeHoleDiameter: p.fitCheckTubeHoleDiameter,
    use: 'lay on the screen; drop M2 screws down the four tubes; all four must enter their PCB holes while the window frames the lit area',
  },
  printProfile: {
    printer: 'Bambu Lab P1S',
    nozzleDiameter: 0.40,
    material: 'PLA',
    layerHeight: 0.20,
    nominalLineWidth: 0.42,
    wallLoops: 3,
    supportPlan: 'front cover, kickstand and fit-check plate: none; rear shell: build-plate-only tree supports',
    frontCoverOrientation: 'visible bezel face on bed',
    rearShellOrientation: 'continuous right side wall on bed, shell vertical',
    kickstandOrientation: 'wide foot on bed, arms vertical',
    fitCheckPlateOrientation: 'face on bed, tubes up',
    minimumWallThickness: p.wallThickness,
    frontFaceThickness: p.visibleFrontZ - p.frontFaceBackZ,
    minimumInsertBossRadialWall:
      p.enclosureInsertBossRadius - p.enclosureInsertDiameter / 2,
  },
};
fs.writeFileSync(path.join(outputDir, 'design_report.json'), `${JSON.stringify(designReport, null, 2)}\n`);

console.log(JSON.stringify({
  outputDir,
  frontCoverBounds: frontCover.boundingBox.bounds,
  rearShellBounds: rearShell.boundingBox.bounds,
  kickstandBounds: kickstand.boundingBox.bounds,
  fitCheckPlateBounds: fitCheckPlate.boundingBox.bounds,
  borders,
  foldedStandOverlap,
  bezelShellOverlap,
  pcbFrontCoverOverlap,
  pcbRearShellOverlap,
  pcbFitPlateOverlap,
  rawStepFrontCoverOverlap,
  rawStepRearShellOverlap,
  pcbFrontCoverIntersectionBounds: pcbFrontCoverOverlap > 0.001
    ? pcbFrontCoverIntersection.boundingBox.bounds : null,
  pcbRearShellIntersectionBounds: pcbRearShellOverlap > 0.001
    ? pcbRearShellIntersection.boundingBox.bounds : null,
  screwPlan: screwPlan.map(({ hole, best, noWasher }) => ({ hole, best, noWasher })),
}, null, 2));

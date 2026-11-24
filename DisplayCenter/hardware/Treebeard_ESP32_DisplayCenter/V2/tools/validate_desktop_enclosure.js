const fs = require('fs');
const path = require('path');

const projectDir = path.resolve(__dirname, '..');
const outputDir = path.join(projectDir, 'output');
const sourcePcb = path.join(projectDir, 'MSV3_desktop_display.step');
const occtimportjs = require(path.resolve(
  __dirname,
  '../../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/occt-import-js',
))();

const PCB_TRANSLATION = [0.00, 0.00, 0.00];

// Prototype 9 (2026-11-17): the controller PCB does not sit where the STEP
// draws it. This validator states the as-built offset independently of the
// generator and checks fit against it (see the generator's parameter comment
// for the evidence). STEP meshes 0-301 are the controller PCB; 312-326 are the
// display header it hangs on; both move together.
const AS_BUILT_OFFSET = [-3.40, -3.80, -0.30];
const AS_BUILT_MOVED_MESHES = [[0, 301], [312, 326]];
const meshMovesWithController = (index) => AS_BUILT_MOVED_MESHES
  .some(([first, last]) => index >= first && index <= last);
// Output frame (PCB STEP native frame). Controller slab is 1.6 mm, back face
// at model -8.00 design (-5.00 output) moved by the Z offset.
const CONTROLLER_BACK_Z = -5.00 + AS_BUILT_OFFSET[2];
const CONTROLLER_FRONT_Z = CONTROLLER_BACK_Z + 1.60;
const OUT = { xMin: -70.39, xMax: 51.61, yMin: -26.680, yMax: 60.900 };
const FEET_BOTTOM_Y = OUT.yMin - 8.00;
const WALL = 2.20;
const PIVOT = { y: 18.320, z: -20.80 };
// Prototype 10: 3.5 mm exterior chamfer on every body edge, and a fifth M2
// screw at the corner standoff (design (-56.50, -5.00)), output frame below.
const CHAMFER = 3.50;
const CORNER_SCREW = [-62.89, -18.903];

function boundsForMeshes(meshes) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const mesh of meshes) {
    const values = mesh.attributes.position.array;
    for (let index = 0; index < values.length; index += 3) {
      for (let axis = 0; axis < 3; axis += 1) {
        min[axis] = Math.min(min[axis], values[index + axis]);
        max[axis] = Math.max(max[axis], values[index + axis]);
      }
    }
  }
  return { min, max };
}

function translatedBounds(mesh, translation) {
  const bounds = boundsForMeshes([mesh]);
  return {
    min: bounds.min.map((value, axis) => value + translation[axis]),
    max: bounds.max.map((value, axis) => value + translation[axis]),
  };
}

function readStep(occt, filename) {
  const result = occt.ReadStepFile(fs.readFileSync(filename), {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.08,
    angularDeflection: 0.25,
  });
  if (!result.success) throw new Error(`Could not import ${filename}`);
  return result;
}

function inspectBinaryStl(filename) {
  const data = fs.readFileSync(filename);
  if (data.length < 84) throw new Error(`${filename} is not a binary STL`);
  const triangleCount = data.readUInt32LE(80);
  if (data.length !== 84 + triangleCount * 50) {
    throw new Error(`${filename} has an invalid binary STL byte count`);
  }
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  const edgeUses = new Map();
  let degenerateTriangleCount = 0;
  let bedContactVertexCount = 0;
  const vertexKey = (point) => point.map((value) => value.toFixed(5)).join(',');
  for (let triangle = 0; triangle < triangleCount; triangle += 1) {
    const base = 84 + triangle * 50 + 12;
    const points = [];
    for (let vertex = 0; vertex < 3; vertex += 1) {
      const point = [];
      for (let axis = 0; axis < 3; axis += 1) {
        const value = data.readFloatLE(base + vertex * 12 + axis * 4);
        point.push(value);
        min[axis] = Math.min(min[axis], value);
        max[axis] = Math.max(max[axis], value);
      }
      if (Math.abs(point[2]) <= 0.02) bedContactVertexCount += 1;
      points.push(point);
    }
    const ab = points[1].map((value, axis) => value - points[0][axis]);
    const ac = points[2].map((value, axis) => value - points[0][axis]);
    const cross = [
      ab[1] * ac[2] - ab[2] * ac[1],
      ab[2] * ac[0] - ab[0] * ac[2],
      ab[0] * ac[1] - ab[1] * ac[0],
    ];
    if (Math.hypot(...cross) <= 1e-10) degenerateTriangleCount += 1;
    for (const [from, to] of [[0, 1], [1, 2], [2, 0]]) {
      const ends = [vertexKey(points[from]), vertexKey(points[to])].sort();
      const key = `${ends[0]}|${ends[1]}`;
      edgeUses.set(key, (edgeUses.get(key) || 0) + 1);
    }
  }
  let boundaryEdgeCount = 0;
  let nonManifoldEdgeCount = 0;
  for (const uses of edgeUses.values()) {
    if (uses === 1) boundaryEdgeCount += 1;
    else if (uses !== 2) nonManifoldEdgeCount += 1;
  }
  return {
    triangleCount,
    byteLength: data.length,
    min,
    max,
    bedContactVertexCount,
    degenerateTriangleCount,
    boundaryEdgeCount,
    nonManifoldEdgeCount,
    watertight: boundaryEdgeCount === 0 && nonManifoldEdgeCount === 0,
  };
}

function approx(value, expected, tolerance = 0.05) {
  return Math.abs(value - expected) <= tolerance;
}

function zIntersectionsAt(mesh, x, y) {
  const positions = mesh.attributes.position.array;
  const indices = mesh.index.array;
  const hits = [];
  for (let index = 0; index < indices.length; index += 3) {
    const points = [0, 1, 2].map((offset) => {
      const base = indices[index + offset] * 3;
      return [positions[base], positions[base + 1], positions[base + 2]];
    });
    const denominator = ((points[1][1] - points[2][1]) * (points[0][0] - points[2][0])
      + (points[2][0] - points[1][0]) * (points[0][1] - points[2][1]));
    if (Math.abs(denominator) < 1e-9) continue;
    const a = ((points[1][1] - points[2][1]) * (x - points[2][0])
      + (points[2][0] - points[1][0]) * (y - points[2][1])) / denominator;
    const b = ((points[2][1] - points[0][1]) * (x - points[2][0])
      + (points[0][0] - points[2][0]) * (y - points[2][1])) / denominator;
    const c = 1 - a - b;
    if (a < -1e-6 || b < -1e-6 || c < -1e-6) continue;
    hits.push(a * points[0][2] + b * points[1][2] + c * points[2][2]);
  }
  return [...new Set(hits.map((value) => value.toFixed(4)))].map(Number).sort((a, b) => a - b);
}

function xIntersectionsAt(mesh, y, z) {
  const positions = mesh.attributes.position.array;
  const indices = mesh.index.array;
  const hits = [];
  for (let index = 0; index < indices.length; index += 3) {
    const points = [0, 1, 2].map((offset) => {
      const base = indices[index + offset] * 3;
      return [positions[base], positions[base + 1], positions[base + 2]];
    });
    const denominator = ((points[1][2] - points[2][2]) * (points[0][1] - points[2][1])
      + (points[2][1] - points[1][1]) * (points[0][2] - points[2][2]));
    if (Math.abs(denominator) < 1e-9) continue;
    const a = ((points[1][2] - points[2][2]) * (y - points[2][1])
      + (points[2][1] - points[1][1]) * (z - points[2][2])) / denominator;
    const b = ((points[2][2] - points[0][2]) * (y - points[2][1])
      + (points[0][1] - points[2][1]) * (z - points[2][2])) / denominator;
    const c = 1 - a - b;
    if (a < -1e-6 || b < -1e-6 || c < -1e-6) continue;
    hits.push(a * points[0][0] + b * points[1][0] + c * points[2][0]);
  }
  return [...new Set(hits.map((value) => value.toFixed(4)))].map(Number).sort((a, b) => a - b);
}

occtimportjs.then((occt) => {
  const errors = [];
  if (fs.existsSync(path.join(outputDir, 'MSV3_display_antenna_positioned.step'))) {
    errors.push('The obsolete external antenna reference STEP is still in the output folder.');
  }
  const stepFiles = {
    originalBezel: 'MSV3_display_front_bezel_reference.step',
    frontCover: 'MSV3_display_front_cover.step',
    rearShell: 'MSV3_display_rear_shell.step',
    kickstand: 'MSV3_display_kickstand.step',
    foldedAssembly: 'MSV3_display_enclosure_assembly.step',
    deployedAssembly: 'MSV3_display_enclosure_deployed.step',
    fitCheckAssembly: 'MSV3_display_fit_check_assembly.step',
    pcbAlignmentCheck: 'MSV3_display_pcb_alignment_check.step',
    fitCheckPlate: 'MSV3_display_fit_check_plate.step',
    asBuiltPcbReference: 'MSV3_display_asbuilt_pcb_reference.step',
  };
  const stepReports = {};
  const stepResults = {};
  for (const [name, basename] of Object.entries(stepFiles)) {
    const result = readStep(occt, path.join(outputDir, basename));
    stepResults[name] = result;
    stepReports[name] = {
      meshCount: result.meshes.length,
      triangles: result.meshes.reduce((sum, mesh) => sum + mesh.index.array.length / 3, 0),
      ...boundsForMeshes(result.meshes),
    };
  }

  const expectedMeshCounts = {
    originalBezel: 1,
    frontCover: 1,
    rearShell: 1,
    kickstand: 1,
    foldedAssembly: 3,
    deployedAssembly: 3,
    fitCheckPlate: 1,
    asBuiltPcbReference: 339,
    pcbAlignmentCheck: 340,
  };
  for (const [name, expected] of Object.entries(expectedMeshCounts)) {
    if (stepReports[name].meshCount !== expected) {
      errors.push(`${name} has ${stepReports[name].meshCount} meshes; expected ${expected}`);
    }
  }
  if (stepReports.fitCheckAssembly.meshCount < 342) {
    errors.push('Fit-check assembly does not contain the complete as-built two-PCB reference.');
  }

  const stlReports = {};
  for (const [name, basename] of Object.entries({
    frontCover: 'MSV3_display_front_cover_print.stl',
    rearShell: 'MSV3_display_rear_shell_print.stl',
    kickstand: 'MSV3_display_kickstand_print.stl',
    fitCheckPlate: 'MSV3_display_fit_check_plate_print.stl',
  })) {
    const report = inspectBinaryStl(path.join(outputDir, basename));
    stlReports[name] = report;
    if (Math.abs(report.min[2]) > 0.08) errors.push(`${name} STL is not on the print bed.`);
    if (report.triangleCount < 100) errors.push(`${name} STL has too few triangles to be useful.`);
    if (report.degenerateTriangleCount !== 0) {
      errors.push(`${name} STL contains ${report.degenerateTriangleCount} degenerate triangles.`);
    }
    if (!report.watertight) {
      errors.push(`${name} STL is not watertight (${report.boundaryEdgeCount} boundary edges, ${report.nonManifoldEdgeCount} non-manifold edges).`);
    }
    if (report.bedContactVertexCount < 6) {
      errors.push(`${name} STL has insufficient contact with the print bed.`);
    }
  }

  const pcb = readStep(occt, sourcePcb);
  const allPcbBounds = boundsForMeshes(pcb.meshes);
  const fittedPcbBounds = {
    min: allPcbBounds.min.map((value, axis) => value + PCB_TRANSLATION[axis]),
    max: allPcbBounds.max.map((value, axis) => value + PCB_TRANSLATION[axis]),
  };

  const expectedMeshNames = {
    302: 'PCB_61p74x108p04x1p6_with_3p2_mount_holes',
    311: 'LCD_active_area_55p68x83p52',
    312: 'J2_14pin_header_plastic_approx',
  };
  for (const [index, name] of Object.entries(expectedMeshNames)) {
    if (pcb.meshes[index]?.name !== name) {
      errors.push(`PCB STEP mesh ${index} is not ${name}; the as-built mesh ranges no longer apply.`);
    }
  }
  const meshOffset = (index) => (meshMovesWithController(index)
    ? AS_BUILT_OFFSET.map((value, axis) => value + PCB_TRANSLATION[axis]) : PCB_TRANSLATION);

  const activeMesh = pcb.meshes.find((mesh) => mesh.name === 'LCD_active_area_55p68x83p52');
  if (!activeMesh) errors.push('Could not identify the LCD active-area reference mesh.');
  const activeBounds = activeMesh ? translatedBounds(activeMesh, PCB_TRANSLATION) : null;
  const activeCenter = activeBounds ? [
    (activeBounds.min[0] + activeBounds.max[0]) / 2,
    (activeBounds.min[1] + activeBounds.max[1]) / 2,
  ] : [NaN, NaN];
  const bezelOpeningCenter = [-9.39, 17.110];
  if (!approx(activeCenter[0], bezelOpeningCenter[0], 0.01)
      || !approx(activeCenter[1], bezelOpeningCenter[1], 0.01)) {
    errors.push('LCD active area is not centered in the retained bezel opening.');
  }
  const glassRecess = activeBounds ? 5.08 - activeBounds.max[2] : NaN;
  if (glassRecess < 0.10 || glassRecess > 0.25) {
    errors.push('Display glass recess is outside the 0.10-0.25 mm target.');
  }

  // Screw positions (output frame): display PCB right-hand pair, then the
  // controller's top pair at their as-built positions (45.00 mm apart, drill file).
  const expectedClosureCenters = [
    [38.77, -10.3385], [38.77, 44.5600], [-63.90, 54.700], [-18.90, 54.700],
  ];
  const insertFaceZ = [0.85, 0.85, -3.10, -3.10];
  const postEndZ = [-1.00, -1.00, -5.90, -5.90];

  // Every as-built vertex behind the display front must lie inside the cavity;
  // near the controller screws, nothing may reach the boss faces or post ends.
  let outsideCavityVertexCount = 0;
  let asBuiltMinZ = Infinity;
  let controllerFrontMostNearScrews = CONTROLLER_FRONT_Z;
  let controllerRearMostNearScrews = CONTROLLER_BACK_Z;
  pcb.meshes.forEach((mesh, meshIndex) => {
    const offset = meshOffset(meshIndex);
    const moved = meshMovesWithController(meshIndex);
    const values = mesh.attributes.position.array;
    for (let index = 0; index < values.length; index += 3) {
      const x = values[index] + offset[0];
      const y = values[index + 1] + offset[1];
      const z = values[index + 2] + offset[2];
      asBuiltMinZ = Math.min(asBuiltMinZ, z);
      if (moved) {
        const nearScrew = expectedClosureCenters.slice(2)
          .some(([cx, cy]) => Math.hypot(x - cx, y - cy) <= 3.30);
        if (nearScrew && z > CONTROLLER_FRONT_Z - 0.01) {
          controllerFrontMostNearScrews = Math.max(controllerFrontMostNearScrews, z);
        }
        if (nearScrew && z < CONTROLLER_BACK_Z + 0.01) {
          controllerRearMostNearScrews = Math.min(controllerRearMostNearScrews, z);
        }
      }
      if (z >= -2.70) continue;
      const inMain = x >= OUT.xMin + WALL && x <= OUT.xMax - WALL
        && y >= OUT.yMin + WALL && y <= OUT.yMax - WALL && z >= -15.40;
      if (!inMain) outsideCavityVertexCount += 1;
    }
  });
  if (outsideCavityVertexCount) {
    errors.push(`${outsideCavityVertexCount} as-built PCB vertices fall outside the rear-shell cavity.`);
  }
  // Same cavity depth as prototype 8, which closed over the real stack; the
  // as-built Z offset is an unmeasured estimate, hence the lower 0.50 floor.
  const backComponentClearance = asBuiltMinZ - (-15.40);
  if (backComponentClearance < 0.50) errors.push('As-built rear component clearance is below 0.50 mm.');
  const controllerFrontGap = insertFaceZ[2] - controllerFrontMostNearScrews;
  const controllerRearGap = controllerRearMostNearScrews - postEndZ[2];
  if (controllerFrontGap < 0.30 || controllerFrontGap > 1.00
      || controllerRearGap < 0.30 || controllerRearGap > 1.00) {
    errors.push('Controller boss/post gaps are outside 0.30-1.00 mm at the as-built position.');
  }

  const usbMesh = pcb.meshes[1];
  const usbBounds = translatedBounds(usbMesh, meshOffset(1));
  const usbOpening = { xMin: -46.10, xMax: -35.70, zMin: -9.105, zMax: -3.855, innerY: -12.453 };
  const usbFits = usbBounds.min[0] >= usbOpening.xMin
    && usbBounds.max[0] <= usbOpening.xMax
    && usbBounds.min[2] >= usbOpening.zMin
    && usbBounds.max[2] <= usbOpening.zMax
    && usbBounds.min[1] <= usbOpening.innerY;
  if (!usbFits) errors.push('As-built USB-C connector does not fit through the lower-wall tunnel.');
  const usbCenterX = (usbBounds.min[0] + usbBounds.max[0]) / 2;
  const usbCableBay = { xMin: -49.15, xMax: -32.65, yMin: OUT.yMin - 8.20, yMax: -12.103 };
  if (!approx((usbCableBay.xMin + usbCableBay.xMax) / 2, usbCenterX, 0.05)) {
    errors.push('USB cable bay is not centred on the as-built USB-C receptacle.');
  }
  const usbBaySurroundsTunnel = usbCableBay.xMin <= usbOpening.xMin
    && usbCableBay.xMax >= usbOpening.xMax
    && usbCableBay.yMin <= FEET_BOTTOM_Y
    && usbCableBay.yMax >= usbOpening.innerY;
  if (!usbBaySurroundsTunnel) {
    errors.push('Bottom/rear USB cable bay does not surround the connector tunnel.');
  }

  const formerAntennaBoreCenter = [44.11, 56.097];
  const rearShellMesh = stepResults.rearShell.meshes[0];
  const formerAntennaBoreChecks = [
    // Prototype 9's top wall starts 2.6 mm above the former bore centre, so
    // the +Y sample stays inside the cavity footprint. Prototype 10's back
    // chamfer reaches 3.5 mm in from the right and top edges, so the +X and +Y
    // samples stay inside that.
    [0, 0], [-4.80, 0], [2.00, 0], [0, -4.80], [0, 1.00],
  ].map(([dx, dy]) => ({
    x: formerAntennaBoreCenter[0] + dx,
    y: formerAntennaBoreCenter[1] + dy,
    z: zIntersectionsAt(rearShellMesh, formerAntennaBoreCenter[0] + dx,
      formerAntennaBoreCenter[1] + dy),
  }));
  const formerAntennaBoreIsSolid = formerAntennaBoreChecks.every(({ z }) =>
    z.length >= 2 && approx(z[0], -18.00, 0.05) && approx(z[z.length - 1], -15.40, 0.05));
  if (!formerAntennaBoreIsSolid) {
    errors.push('The former antenna opening is not closed by the full 2.6 mm rear wall.');
  }

  const reportData = JSON.parse(fs.readFileSync(path.join(outputDir, 'design_report.json'), 'utf8'));
  const printProfile = reportData.printProfile;
  if (printProfile?.printer !== 'Bambu Lab P1S'
      || !approx(printProfile?.nozzleDiameter, 0.40, 0.001)
      || printProfile?.material !== 'PLA') {
    errors.push('Design report does not identify the required P1S / 0.4 mm / PLA target.');
  }
  if (printProfile?.minimumWallThickness < 4 * printProfile?.nozzleDiameter) {
    errors.push('Main wall is thinner than four 0.4 mm nozzle widths.');
  }
  if (printProfile?.frontFaceThickness < 4 * printProfile?.layerHeight) {
    errors.push('Front face is thinner than four 0.20 mm layers.');
  }
  if (printProfile?.minimumInsertBossRadialWall < 3 * printProfile?.nominalLineWidth) {
    errors.push('Insert-boss radial wall is thinner than three nominal extrusion lines.');
  }
  if (reportData.frontCoverConstruction.visibleFrontFaceCount !== 1
      || reportData.frontCoverConstruction.visibleFrontFaces[0]?.innerWireCount !== 1) {
    errors.push('The visible front is split into multiple B-rep faces or contains an opening besides the LCD.');
  }
  if (reportData.stand.foldedIntersectionVolume > 0.001) {
    errors.push('Folded kickstand intersects the rear shell.');
  }
  if (reportData.closure.closedBezelIntersectionVolume > 0.001) {
    errors.push('Rear shell intersects the extended front cover when closed.');
  }
  if (reportData.pcbFit.rearShellIntersectionVolume > 0.001) {
    errors.push('Rear shell intersects the PCB assembly.');
  }
  const addedFrontContact = reportData.pcbFit.frontCoverIntersectionVolume
    - reportData.pcbFit.originalReferenceIntersectionVolume;
  if (reportData.pcbFit.frontCoverIntersectionVolume > 0.20) {
    errors.push('Front cover still collides with a PCB substrate or component outside the mounting-hole contacts.');
  }
  if (addedFrontContact > 25.00) {
    errors.push('Front-cover changes add more than 25 mm^3 of PCB contact beyond the accepted reference bezel.');
  }
  if (reportData.pcbFit.fitCheckPlateIntersectionVolume > 0.001) {
    errors.push('Fit-check plate intersects the as-built PCB stack.');
  }
  const reportOffset = reportData.parameters?.controllerAsBuiltOffset || [];
  if (!AS_BUILT_OFFSET.every((value, axis) => approx(reportOffset[axis], value, 0.001))) {
    errors.push('Generator and validator disagree on the as-built controller offset.');
  }
  const standWithinFootprint = stepReports.kickstand.min[0] >= OUT.xMin
    && stepReports.kickstand.max[0] <= OUT.xMax
    && stepReports.kickstand.min[1] >= FEET_BOTTOM_Y
    && stepReports.kickstand.max[1] <= OUT.yMax;
  if (!standWithinFootprint) errors.push('Folded kickstand exceeds the main XY footprint.');

  const unifiedRectangle = approx(stepReports.frontCover.min[0], OUT.xMin)
    && approx(stepReports.frontCover.max[0], OUT.xMax)
    && approx(stepReports.frontCover.min[1], OUT.yMin)
    && approx(stepReports.frontCover.max[1], OUT.yMax)
    && approx(stepReports.rearShell.max[1], OUT.yMax);
  if (!unifiedRectangle) errors.push('Front and rear bodies do not share the unified rectangular envelope.');

  // Issue 5: equal top and bottom borders around the window.
  const activeOut = activeBounds || { min: [NaN, NaN], max: [NaN, NaN] };
  const borders = {
    left: activeOut.min[0] - OUT.xMin,
    right: OUT.xMax - activeOut.max[0],
    top: OUT.yMax - activeOut.max[1],
    bottom: activeOut.min[1] - OUT.yMin,
  };
  if (!approx(borders.top, borders.bottom, 0.02) || !approx(borders.left, borders.right, 0.02)) {
    errors.push('Front borders are not symmetric (top = bottom and left = right).');
  }

  const closureCentersMatch = reportData.outputCoordinates.closureHoleCenters.length === 4
    && reportData.outputCoordinates.closureHoleCenters.every((center, index) => (
      approx(center[0], expectedClosureCenters[index][0], 0.01)
        && approx(center[1], expectedClosureCenters[index][1], 0.01)
    ));
  if (!closureCentersMatch) errors.push('Enclosure screw centers do not match the selected PCB mounting holes.');
  if (reportData.closure.pcbScrewCount !== 4) errors.push('The enclosure must clamp the PCB stack with exactly four screws.');
  if (reportData.closure.screwCount !== 5) errors.push('The enclosure must close with five screws (four PCB + the corner standoff).');
  const reportCorner = reportData.closure.cornerScrewCenter || [];
  if (!approx(reportCorner[0], CORNER_SCREW[0], 0.01) || !approx(reportCorner[1], CORNER_SCREW[1], 0.01)) {
    errors.push('Corner standoff screw is not where the validator expects it.');
  }
  if (reportData.closure.minimumInsertBlindWall < 1.80) {
    errors.push('A front-cover insert pocket is too close to the visible face.');
  }
  if (!approx(reportData.pcbFit.controllerFrontGap, controllerFrontGap, 0.01)
      || !approx(reportData.pcbFit.controllerRearGap, controllerRearGap, 0.01)) {
    errors.push('Design report controller gaps disagree with the validator measurement.');
  }
  if (!approx(stepReports.frontCover.max[2], 6.10, 0.01)) {
    errors.push('A repaired face hole or insert boss protrudes through the visible front plane.');
  }

  const frontCoverMesh = stepResults.frontCover.meshes[0];
  const formerFastenerCenters = [
    [-57.16, -10.533], [-57.16, 44.367], [44.88, -10.533], [44.88, 44.367],
  ];
  const formerFastenerAreaChecks = formerFastenerCenters.map(([x, y]) => {
    const z = zIntersectionsAt(frontCoverMesh, x, y);
    return { center: [x, y], z, thickness: z.length >= 2 ? z[z.length - 1] - z[0] : 0 };
  });
  if (formerFastenerAreaChecks.some((skin) => skin.thickness < 1.00)) {
    errors.push('A former front-fastener area is not part of the continuous 1.02 mm face slab.');
  }

  const cleanFrontFaceChecks = {
    left: zIntersectionsAt(frontCoverMesh, -60.00, 17.11),
    right: zIntersectionsAt(frontCoverMesh, 45.00, 17.11),
    top: zIntersectionsAt(frontCoverMesh, -9.39, 55.00),
    bottom: zIntersectionsAt(frontCoverMesh, -9.39, -15.00),
    screenOpening: zIntersectionsAt(frontCoverMesh, -9.39, 17.11),
  };
  const frontFrameSamplesAreSolid = ['left', 'right', 'top', 'bottom']
    .every((name) => cleanFrontFaceChecks[name].some((z) => approx(z, 6.10, 0.01)));
  if (!frontFrameSamplesAreSolid || cleanFrontFaceChecks.screenOpening.length !== 0) {
    errors.push('The new front face is not one continuous frame around only the LCD opening.');
  }

  // Samples stay clear of the 3.5 mm exterior chamfers (front face at 6.10,
  // top edge at 60.90).
  const leftWallContinuityChecks = [-14.00, 0.00, 30.00, 56.00]
    .flatMap((y) => [-2.00, 0.00, 2.00].map((z) => ({
      y,
      z,
      x: xIntersectionsAt(frontCoverMesh, y, z),
    })));
  const leftWallIsContinuous = leftWallContinuityChecks.every((check) => (
    check.x.length >= 2
      && approx(check.x[0], -70.39, 0.01)
      && check.x[1] >= -68.20
  ));
  if (!leftWallIsContinuous) {
    errors.push('The front cover still contains a gap or slot in its left perimeter wall.');
  }

  const blindInsertChecks = expectedClosureCenters.map(([x, y]) => ({
    center: [x, y],
    z: zIntersectionsAt(frontCoverMesh, x, y),
  }));
  // Display inserts end at the pocket floor; controller inserts continue into
  // a 1.5 mm screw-tip relief.
  const expectedBlindEnds = [3.95, 3.95, 1.50, 1.50];
  if (blindInsertChecks.some((check, index) => check.z.length < 2
      || !approx(check.z[0], expectedBlindEnds[index], 0.03))) {
    errors.push('A heat-set-insert pocket is not blind at the intended depth.');
  }
  // Issue 2: the prototype-8 lower-left display boss must be gone.
  const removedBossCheck = zIntersectionsAt(frontCoverMesh, -63.27, -10.3385);
  if (removedBossCheck.length !== 2) {
    errors.push('Front cover still has material behind the face at the removed lower-left screw.');
  }

  // Issue 6: the prop pivot passes straight through both shell lugs, and each
  // arm has a nut trap on its inner face.
  const pivotAxisHits = xIntersectionsAt(rearShellMesh, PIVOT.y, PIVOT.z);
  const lugBodyHits = xIntersectionsAt(rearShellMesh, PIVOT.y, PIVOT.z - 2.00);
  const pivotHolesThrough = pivotAxisHits.length === 0
    && lugBodyHits.length === 4
    && [-25.39, -21.39, 2.61, 6.61].every((x, index) => approx(lugBodyHits[index], x, 0.05));
  if (!pivotHolesThrough) errors.push('Kickstand pivot holes do not pass through both shell lugs.');
  const kickstandMesh = stepResults.kickstand.meshes[0];
  const nutTrapHits = xIntersectionsAt(kickstandMesh, PIVOT.y + 1.90, PIVOT.z);
  const nutTrapsPresent = nutTrapHits.length === 4
    && [-21.14, -18.94, 0.16, 2.36].every((x, index) => approx(nutTrapHits[index], x, 0.05));
  if (!nutTrapsPresent) errors.push('Kickstand arms are missing their inner-face M2 nut traps.');

  // Prototype 10: exterior chamfers. A point 1 mm in from the outer corner is
  // inside every corner chamfer, so neither part has material there; 1 mm in
  // from a front or back edge, the face is set back by CHAMFER - 1.
  const chamferChecks = {
    frontLeftEdgeX: xIntersectionsAt(frontCoverMesh, 17.11, 6.10 - 1.00),
    cornerCover: zIntersectionsAt(frontCoverMesh, OUT.xMin + 1.00, OUT.yMin + 1.00),
    cornerShell: zIntersectionsAt(rearShellMesh, OUT.xMin + 1.00, OUT.yMax - 1.00),
    backLeftEdgeZ: zIntersectionsAt(rearShellMesh, OUT.xMin + 1.00, 17.11),
    // Through the top-left controller boss's web to the left wall, 0.97 mm
    // behind the face: the web must not poke through the chamfer.
    webUnderFrontChamferX: xIntersectionsAt(frontCoverMesh, 54.697, 5.13),
    // Same for the corner standoff's web to the left wall.
    cornerWebUnderFrontChamferX: xIntersectionsAt(frontCoverMesh, CORNER_SCREW[1], 5.13),
    wallUnderChamfer: reportData.chamfer?.minimumWallUnderChamfer,
  };
  if (!approx(chamferChecks.frontLeftEdgeX[0], OUT.xMin + CHAMFER - 1.00, 0.05)) {
    errors.push('The front perimeter edge is not chamfered 3.5 mm.');
  }
  if (chamferChecks.cornerCover.length !== 0 || chamferChecks.cornerShell.length !== 0) {
    errors.push('A corner edge is not chamfered.');
  }
  if (!approx(chamferChecks.webUnderFrontChamferX[0], OUT.xMin + CHAMFER - 0.97, 0.05)
      || !approx(chamferChecks.cornerWebUnderFrontChamferX[0], OUT.xMin + CHAMFER - 0.97, 0.05)) {
    errors.push('A boss web protrudes through the front chamfer.');
  }
  if (!approx(chamferChecks.backLeftEdgeZ[0], -18.00 + CHAMFER - 1.00, 0.05)) {
    errors.push('The back perimeter edge is not chamfered 3.5 mm.');
  }
  if (!chamferChecks.wallUnderChamfer
      || Object.values(chamferChecks.wallUnderChamfer).some((value) => value < 1.59)) {
    errors.push('A wall is thinner than 1.6 mm under a chamfer.');
  }

  // Corner standoff: blind insert pocket + tip relief in the cover (insert
  // face on the seam at -3.15, blind end at 1.45); a bored post in the shell
  // with a 2 mm head pocket, stopping 0.10 mm short of the seam.
  const cornerChecks = {
    coverAxis: zIntersectionsAt(frontCoverMesh, ...CORNER_SCREW),
    coverBossWall: zIntersectionsAt(frontCoverMesh, CORNER_SCREW[0] + 2.00, CORNER_SCREW[1]),
    shellAxis: zIntersectionsAt(rearShellMesh, ...CORNER_SCREW),
    shellPostWall: zIntersectionsAt(rearShellMesh, CORNER_SCREW[0] + 2.60, CORNER_SCREW[1]),
  };
  const cornerStandoffOk = approx(cornerChecks.coverAxis[0], 1.45, 0.03)
    && approx(cornerChecks.coverBossWall[0], -3.15, 0.03)
    && cornerChecks.shellAxis.length === 0
    && approx(cornerChecks.shellPostWall[0], -18.00, 0.03)
    && approx(cornerChecks.shellPostWall[cornerChecks.shellPostWall.length - 1], -3.25, 0.03);
  if (!cornerStandoffOk) errors.push('The corner standoff boss, insert pocket or post is not as designed.');

  // Fit-check plate: tubes open at all four screw positions.
  const plateMesh = stepResults.fitCheckPlate.meshes[0];
  const fitPlateTubesOpen = expectedClosureCenters
    .every(([x, y]) => zIntersectionsAt(plateMesh, x, y).length === 0)
    && expectedClosureCenters
      .every(([x, y]) => zIntersectionsAt(plateMesh, x + 1.80, y).length >= 2);
  if (!fitPlateTubesOpen) errors.push('Fit-check plate tubes are not open at the four screw positions.');

  const report = {
    passed: errors.length === 0,
    errors,
    stepReports,
    stlReports,
    pcbFit: {
      reference: 'as-built (controller meshes 0-301 and display header 312-326 moved by AS_BUILT_OFFSET)',
      asBuiltOffset: AS_BUILT_OFFSET,
      sourceMeshCount: pcb.meshes.length,
      translation: PCB_TRANSLATION,
      rawStepBounds: fittedPcbBounds,
      bezelOpeningCenter,
      activeCenter,
      glassRecess,
      borders,
      outsideCavityVertexCount,
      backComponentClearance,
      controllerFrontGap,
      controllerRearGap,
      usbBounds,
      usbCenterX,
      usbFits,
      usbCableBay,
      usbBaySurroundsTunnel,
      rearShellIntersectionVolume: reportData.pcbFit.rearShellIntersectionVolume,
      originalReferenceIntersectionVolume: reportData.pcbFit.originalReferenceIntersectionVolume,
      frontCoverIntersectionVolume: reportData.pcbFit.frontCoverIntersectionVolume,
      addedFrontContact,
    },
    closure: {
      type: 'four rear M2 screws: display PCB right-hand pair + controller PCB top pair, into front-cover heat-set inserts',
      closureCenters: reportData.outputCoordinates.closureHoleCenters,
      closureCentersMatch,
      screwClearanceDiameter: 2.40,
      heatSetInsertPocket: { diameter: 2.90, depth: 3.20 },
      screwCount: reportData.closure.screwCount,
      screwPlan: reportData.closure.screwPlan,
      minimumInsertBlindWall: reportData.closure.minimumInsertBlindWall,
      blindInsertChecks,
      removedBossCheck,
      formerFastenerAreaChecks,
      cleanFrontFaceChecks,
      cornerChecks,
      cornerStandoffOk,
      frontRearIntersectionVolume: reportData.closure.closedBezelIntersectionVolume,
    },
    stand: {
      foldedWithinMainXYFootprint: standWithinFootprint,
      foldedIntersectionVolume: reportData.stand.foldedIntersectionVolume,
      pivotClearanceHoleDiameter: 2.40,
      pivotHolesThrough,
      pivotAxisHits,
      lugBodyHits,
      nutTrapsPresent,
      nutTrapHits,
      fasteners: 'M2x10 screw + M2 nut per side',
      deployedPreviewAngle: 30.00,
    },
    fitCheckPlate: {
      tubesOpen: fitPlateTubesOpen,
      intersectionVolume: reportData.pcbFit.fitCheckPlateIntersectionVolume,
    },
    antenna: {
      externalOpening: false,
      formerAntennaBoreIsSolid,
      formerAntennaBoreChecks,
    },
    enclosure: {
      unifiedRectangle,
      leftWallIsContinuous,
      leftWallContinuityChecks,
      exteriorChamfer: CHAMFER,
      chamferChecks,
      bodyEnvelope: OUT,
      bottomFeetYMin: FEET_BOTTOM_Y,
    },
    printability: {
      ...printProfile,
      bedEnvelope: [256, 256, 256],
      allPartsFitP1SBedIndividually: Object.values(stlReports).every((item) => (
        item.max[0] - item.min[0] <= 256
          && item.max[1] - item.min[1] <= 256
          && item.max[2] - item.min[2] <= 256
      )),
      allMeshesWatertight: Object.values(stlReports).every((item) => item.watertight),
      allMeshesNonDegenerate: Object.values(stlReports)
        .every((item) => item.degenerateTriangleCount === 0),
    },
  };
  fs.writeFileSync(path.join(outputDir, 'validation_report.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  if (!report.passed) process.exitCode = 1;
});

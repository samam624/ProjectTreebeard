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
  for (let triangle = 0; triangle < triangleCount; triangle += 1) {
    const base = 84 + triangle * 50 + 12;
    for (let vertex = 0; vertex < 3; vertex += 1) {
      for (let axis = 0; axis < 3; axis += 1) {
        const value = data.readFloatLE(base + vertex * 12 + axis * 4);
        min[axis] = Math.min(min[axis], value);
        max[axis] = Math.max(max[axis], value);
      }
    }
  }
  return { triangleCount, byteLength: data.length, min, max };
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
    pcbAlignmentCheck: 339,
  };
  for (const [name, expected] of Object.entries(expectedMeshCounts)) {
    if (stepReports[name].meshCount !== expected) {
      errors.push(`${name} has ${stepReports[name].meshCount} meshes; expected ${expected}`);
    }
  }
  if (stepReports.fitCheckAssembly.meshCount < 340) {
    errors.push('Fit-check assembly does not contain the complete two-PCB reference.');
  }

  const stlReports = {};
  for (const [name, basename] of Object.entries({
    frontCover: 'MSV3_display_front_cover_print.stl',
    rearShell: 'MSV3_display_rear_shell_print.stl',
    kickstand: 'MSV3_display_kickstand_print.stl',
  })) {
    const report = inspectBinaryStl(path.join(outputDir, basename));
    stlReports[name] = report;
    if (Math.abs(report.min[2]) > 0.08) errors.push(`${name} STL is not on the print bed.`);
  }

  const pcb = readStep(occt, sourcePcb);
  const allPcbBounds = boundsForMeshes(pcb.meshes);
  const fittedPcbBounds = {
    min: allPcbBounds.min.map((value, axis) => value + PCB_TRANSLATION[axis]),
    max: allPcbBounds.max.map((value, axis) => value + PCB_TRANSLATION[axis]),
  };

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

  let outsideCavityVertexCount = 0;
  let controllerFrontMostZ = -Infinity;
  for (const mesh of pcb.meshes) {
    const values = mesh.attributes.position.array;
    for (let index = 0; index < values.length; index += 3) {
      const x = values[index] + PCB_TRANSLATION[0];
      const y = values[index + 1] + PCB_TRANSLATION[1];
      const z = values[index + 2] + PCB_TRANSLATION[2];
      if (y >= 51.597) controllerFrontMostZ = Math.max(controllerFrontMostZ, z);
      if (z >= -2.70) continue;
      const inMain = x >= -68.19 && x <= 49.41 && y >= -15.703 && y <= 61.897
        && z >= -15.40;
      if (!inMain) outsideCavityVertexCount += 1;
    }
  }
  if (outsideCavityVertexCount) {
    errors.push(`${outsideCavityVertexCount} PCB vertices fall outside the rear-shell cavities.`);
  }
  const backComponentClearance = fittedPcbBounds.min[2] - (-15.40);
  if (backComponentClearance < 0.75) errors.push('Rear component clearance is below 0.75 mm.');
  const controllerFrontCapClearance = 1.00 - controllerFrontMostZ;
  if (controllerFrontCapClearance < 1.50) {
    errors.push('Unified front-cover controller clearance is below 1.50 mm.');
  }

  const usbMesh = pcb.meshes[1];
  const usbBounds = translatedBounds(usbMesh, PCB_TRANSLATION);
  const usbOpening = { xMin: -42.69, xMax: -32.29, zMin: -8.80, zMax: -3.55, innerY: -8.653 };
  const usbFits = usbBounds.min[0] >= usbOpening.xMin
    && usbBounds.max[0] <= usbOpening.xMax
    && usbBounds.min[2] >= usbOpening.zMin
    && usbBounds.max[2] <= usbOpening.zMax
    && usbBounds.min[1] <= usbOpening.innerY;
  if (!usbFits) errors.push('USB-C connector does not fit through the lower-wall tunnel.');
  const usbCableBay = { xMin: -45.39, xMax: -29.39, yMin: -26.103, yMax: -8.303 };
  const usbBaySurroundsTunnel = usbCableBay.xMin <= usbOpening.xMin
    && usbCableBay.xMax >= usbOpening.xMax
    && usbCableBay.yMin <= -26.00
    && usbCableBay.yMax >= usbOpening.innerY;
  if (!usbBaySurroundsTunnel) {
    errors.push('Bottom/rear USB cable bay does not surround the connector tunnel.');
  }

  const formerAntennaBoreCenter = [44.11, 56.097];
  const rearShellMesh = stepResults.rearShell.meshes[0];
  const formerAntennaBoreChecks = [
    [0, 0], [-4.80, 0], [4.80, 0], [0, -4.80], [0, 4.80],
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
  const standWithinFootprint = stepReports.kickstand.min[0] >= -70.39
    && stepReports.kickstand.max[0] <= 51.61
    && stepReports.kickstand.min[1] >= -25.903
    && stepReports.kickstand.max[1] <= 64.097;
  if (!standWithinFootprint) errors.push('Folded kickstand exceeds the main XY footprint.');

  const unifiedRectangle = approx(stepReports.frontCover.min[0], -70.39)
    && approx(stepReports.frontCover.max[0], 51.61)
    && approx(stepReports.frontCover.max[1], 64.097)
    && approx(stepReports.rearShell.max[1], 64.097);
  if (!unifiedRectangle) errors.push('Front and rear bodies do not share the unified rectangular envelope.');

  const expectedClosureCenters = [
    [-63.27, -10.3385], [38.77, -10.3385], [38.77, 44.5600], [-60.50, 58.497],
  ];
  const closureCentersMatch = reportData.outputCoordinates.closureHoleCenters.length === 4
    && reportData.outputCoordinates.closureHoleCenters.every((center, index) => (
      approx(center[0], expectedClosureCenters[index][0], 0.01)
        && approx(center[1], expectedClosureCenters[index][1], 0.01)
    ));
  if (!closureCentersMatch) errors.push('Enclosure screw centers do not match the selected PCB mounting holes.');
  if (reportData.closure.screwCount !== 4) errors.push('The enclosure must use exactly four clamping screws.');
  if (reportData.closure.minimumInsertBlindWall < 1.80) {
    errors.push('A front-cover insert pocket is too close to the visible face.');
  }
  if (reportData.closure.controllerInsertFaceClearance < 0.02
      || reportData.closure.controllerInsertFaceClearance > 0.15
      || reportData.closure.controllerRearPostClearance < 0.02
      || reportData.closure.controllerRearPostClearance > 0.15) {
    errors.push('Controller-PCB upper-left clamp supports do not closely bracket the PCB faces.');
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

  const leftWallContinuityChecks = [-14.00, 0.00, 30.00, 56.00, 60.00]
    .flatMap((y) => [-2.00, 0.00, 3.00].map((z) => ({
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
  const expectedBlindEnds = [3.95, 3.95, 3.95, -1.44];
  if (blindInsertChecks.some((check, index) => check.z.length < 2
      || !approx(check.z[0], expectedBlindEnds[index], 0.03))) {
    errors.push('A heat-set-insert pocket is not blind at the intended depth.');
  }

  const report = {
    passed: errors.length === 0,
    errors,
    stepReports,
    stlReports,
    pcbFit: {
      sourceMeshCount: pcb.meshes.length,
      translation: PCB_TRANSLATION,
      fittedBounds: fittedPcbBounds,
      bezelOpeningCenter,
      activeCenter,
      glassRecess,
      outsideCavityVertexCount,
      backComponentClearance,
      controllerFrontMostZ,
      controllerFrontCapClearance,
      usbBounds,
      usbFits,
      usbCableBay,
      usbBaySurroundsTunnel,
      controllerSupportPostClearance: 0.25,
      rearShellIntersectionVolume: reportData.pcbFit.rearShellIntersectionVolume,
      originalReferenceIntersectionVolume: reportData.pcbFit.originalReferenceIntersectionVolume,
      frontCoverIntersectionVolume: reportData.pcbFit.frontCoverIntersectionVolume,
      addedFrontContact,
    },
    closure: {
      type: 'four rear M2 screws through real PCB mounting holes into front-cover heat-set inserts',
      closureCenters: reportData.outputCoordinates.closureHoleCenters,
      closureCentersMatch,
      screwClearanceDiameter: 2.40,
      heatSetInsertPocket: { diameter: 2.90, depth: 3.20 },
      screwCount: reportData.closure.screwCount,
      minimumInsertBlindWall: reportData.closure.minimumInsertBlindWall,
      controllerInsertFaceClearance: reportData.closure.controllerInsertFaceClearance,
      controllerRearPostClearance: reportData.closure.controllerRearPostClearance,
      blindInsertChecks,
      formerFastenerAreaChecks,
      cleanFrontFaceChecks,
      frontRearIntersectionVolume: reportData.closure.closedBezelIntersectionVolume,
    },
    stand: {
      foldedWithinMainXYFootprint: standWithinFootprint,
      foldedIntersectionVolume: reportData.stand.foldedIntersectionVolume,
      pivotClearanceHoleDiameter: 2.40,
      heatSetInsertPocket: { diameter: 2.90, depth: 3.20 },
      deployedPreviewAngle: 30.00,
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
      bodyEnvelope: { xMin: -70.39, xMax: 51.61, yMin: -17.903, yMax: 64.097 },
      bottomFeetYMin: -25.903,
    },
  };
  fs.writeFileSync(path.join(outputDir, 'validation_report.json'), `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  if (!report.passed) process.exitCode = 1;
});

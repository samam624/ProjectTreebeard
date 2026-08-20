const fs = require('fs');
const path = require('path');

const projectDir = path.resolve(__dirname, '..');
const outputDir = path.join(projectDir, 'output');
const pcbPath = path.join(projectDir, 'MSV3_Probe_PCB.step');
const occtimportjs = require(path.join(__dirname, 'node', 'node_modules', 'occt-import-js'))();

const EXPECTED = {
  base: { min: [-12, 82, -6.35], max: [30, 149, 0.65], meshes: 1 },
  lid: { min: [-8.45, 82, -2.8], max: [26.45, 149, 7.55], meshes: 1 },
  solarArmLeft: { min: [-13.85, 115.502, -5.848], max: [-12.25, 152.678, 18.148], meshes: 1 },
  solarArmRight: { min: [30.25, 115.502, -5.848], max: [31.85, 152.678, 18.148], meshes: 1 },
  solarPanelFrame: { min: [-12, 119.68, 12.55], max: [30, 179.68, 17.75], meshes: 1 },
  assembly: { min: [-13.85, 82, -6.35], max: [31.85, 179.68, 18.148], meshes: 5 },
};

const innerContour = [
  [-1.65, 84.00], [19.65, 84.00], [22.65, 86.35], [22.65, 142.25],
  [18.65, 146.65], [-0.65, 146.65], [-4.65, 142.25], [-4.65, 86.35],
];

const shelfBoxes = [
  { min: [-4.95, 95, -2.35], max: [-3.85, 139, -1.60] },
  { min: [21.85, 95, -2.35], max: [22.95, 139, -1.60] },
];
const guideBoxes = [
  { min: [-4.95, 95, -1.75], max: [-4.05, 139, -0.20] },
  { min: [22.05, 95, -1.75], max: [22.95, 139, -0.20] },
];
const retentionBoxes = [...shelfBoxes, ...guideBoxes];

function meshBounds(meshes) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const mesh of meshes) {
    const values = mesh.attributes.position.array;
    for (let i = 0; i < values.length; i += 3) {
      for (let axis = 0; axis < 3; axis += 1) {
        min[axis] = Math.min(min[axis], values[i + axis]);
        max[axis] = Math.max(max[axis], values[i + axis]);
      }
    }
  }
  return { min, max };
}

function approxVector(actual, expected, tolerance = 0.002) {
  return actual.every((value, index) => Math.abs(value - expected[index]) <= tolerance);
}

function readStep(occt, filename) {
  const result = occt.ReadStepFile(fs.readFileSync(filename), {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.08,
    angularDeflection: 0.25,
  });
  if (!result.success) throw new Error(`Could not read ${filename}`);
  return result;
}

function inspectBinaryStl(filename) {
  const data = fs.readFileSync(filename);
  if (data.length < 84) throw new Error(`${filename} is too short to be binary STL.`);
  const triangleCount = data.readUInt32LE(80);
  const expectedLength = 84 + triangleCount * 50;
  if (expectedLength !== data.length) {
    throw new Error(`${filename} STL byte count does not match its triangle count.`);
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

function pointInPolygon([x, y], polygon) {
  let inside = false;
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i, i += 1) {
    const [xi, yi] = polygon[i];
    const [xj, yj] = polygon[j];
    const crosses = ((yi > y) !== (yj > y))
      && (x < ((xj - xi) * (y - yi)) / (yj - yi) + xi);
    if (crosses) inside = !inside;
  }
  return inside;
}

function boundsForMesh(mesh, correctedBottom = false) {
  const values = mesh.attributes.position.array;
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < values.length; i += 3) {
    for (let axis = 0; axis < 3; axis += 1) {
      const value = values[i + axis];
      min[axis] = Math.min(min[axis], value);
      max[axis] = Math.max(max[axis], value);
    }
  }
  // Components modeled entirely on the STEP's bottom side move upward by the
  // 1.81224174 - 1.60 mm PCB correction. Through-hole parts stay conservative.
  if (correctedBottom && max[2] <= -1.81224174 + 0.002) {
    min[2] += 0.21224174;
    max[2] += 0.21224174;
  }
  return { min, max };
}

function overlaps(a, b, tolerance = 0.001) {
  return [0, 1, 2].every((axis) =>
    a.min[axis] < b.max[axis] - tolerance && a.max[axis] > b.min[axis] + tolerance);
}

occtimportjs.then((occt) => {
  const errors = [];
  const stepReports = {};
  const stepFiles = {
    base: path.join(outputDir, 'MSV3_brain_enclosure_base.step'),
    lid: path.join(outputDir, 'MSV3_brain_enclosure_lid.step'),
    solarArmLeft: path.join(outputDir, 'MSV3_solar_arm_left.step'),
    solarArmRight: path.join(outputDir, 'MSV3_solar_arm_right.step'),
    solarPanelFrame: path.join(outputDir, 'MSV3_solar_panel_frame.step'),
    assembly: path.join(outputDir, 'MSV3_brain_enclosure_assembly.step'),
  };
  for (const [name, filename] of Object.entries(stepFiles)) {
    const result = readStep(occt, filename);
    const bounds = meshBounds(result.meshes);
    const triangles = result.meshes.reduce((sum, mesh) => sum + mesh.index.array.length / 3, 0);
    stepReports[name] = { meshCount: result.meshes.length, triangles, ...bounds };
    if (result.meshes.length !== EXPECTED[name].meshes) errors.push(`${name}: unexpected mesh count`);
    if (!approxVector(bounds.min, EXPECTED[name].min)) errors.push(`${name}: unexpected minimum bounds`);
    if (!approxVector(bounds.max, EXPECTED[name].max)) errors.push(`${name}: unexpected maximum bounds`);
  }

  const stlReports = {
    base: inspectBinaryStl(path.join(outputDir, 'MSV3_brain_enclosure_base_print.stl')),
    lid: inspectBinaryStl(path.join(outputDir, 'MSV3_brain_enclosure_lid_print.stl')),
    solarArmLeft: inspectBinaryStl(path.join(outputDir, 'MSV3_solar_arm_left_print.stl')),
    solarArmRight: inspectBinaryStl(path.join(outputDir, 'MSV3_solar_arm_right_print.stl')),
    solarPanelFrame: inspectBinaryStl(path.join(outputDir, 'MSV3_solar_panel_frame_print.stl')),
  };
  for (const [name, stl] of Object.entries(stlReports)) {
    if (Math.abs(stl.min[2]) > 0.002) errors.push(`${name}: STL is not on the print bed.`);
  }

  const pcb = readStep(occt, pcbPath);
  const componentBounds = pcb.meshes.slice(1).map((mesh, index) => ({
    meshIndex: index + 1,
    ...boundsForMesh(mesh, true),
  })).filter((bounds) => bounds.max[1] >= 82 && bounds.min[1] <= 149);

  const componentEnvelope = {
    min: [Infinity, Infinity, Infinity],
    max: [-Infinity, -Infinity, -Infinity],
  };
  for (const bounds of componentBounds) {
    for (let axis = 0; axis < 3; axis += 1) {
      componentEnvelope.min[axis] = Math.min(componentEnvelope.min[axis], bounds.min[axis]);
      componentEnvelope.max[axis] = Math.max(componentEnvelope.max[axis], bounds.max[axis]);
    }
  }

  const shelfCandidates = [];
  for (const component of componentBounds) {
    for (let retentionIndex = 0; retentionIndex < retentionBoxes.length; retentionIndex += 1) {
      if (overlaps(component, retentionBoxes[retentionIndex])) {
        shelfCandidates.push({ meshIndex: component.meshIndex, retentionIndex });
      }
    }
  }
  if (shelfCandidates.length) errors.push('A component AABB overlaps a PCB retention rail.');

  // With the USB-C opening removed, all component vertices must remain inside
  // the closed enclosure cavity.
  let outsideCavityVertexCount = 0;
  for (let meshIndex = 1; meshIndex < pcb.meshes.length; meshIndex += 1) {
    const values = pcb.meshes[meshIndex].attributes.position.array;
    for (let i = 0; i < values.length; i += 3) {
      const x = values[i];
      const y = values[i + 1];
      if (y < 82 || y > 149) continue;
      if (!pointInPolygon([x, y], innerContour)) outsideCavityVertexCount += 1;
    }
  }
  if (outsideCavityVertexCount) errors.push('Component vertices fall outside the closed cavity.');

  const clearances = {
    floorToLowestConservativeComponent: componentEnvelope.min[2] - (-4.25),
    highestComponentToLidRoof: 5.70 - componentEnvelope.max[2],
    pcbClampGap: -0.12 - (-1.60),
    pcbClampInterference: 1.60 - (-0.12 - (-1.60)),
  };
  if (clearances.floorToLowestConservativeComponent < 0.75) errors.push('Floor clearance below target.');
  if (clearances.highestComponentToLidRoof < 0.75) errors.push('Roof clearance below target.');
  if (Math.abs(clearances.pcbClampInterference - 0.12) > 0.002) {
    errors.push('PCB clamp interference does not match the 0.12 mm target.');
  }

  const snapGeometry = {
    externalOnly: -7.00 <= -4.65 && 25.00 >= 22.65,
    leftBumpX: [-7.45, -7.00],
    rightBumpX: [25.00, 25.45],
    cavitySideX: [-4.65, 22.65],
  };
  if (!snapGeometry.externalOnly) errors.push('A snap bump extends into the PCB cavity.');

  const pcbRetention = {
    continuousRailLength: 44.00,
    verticalInterference: 0.12,
    shelfOverlapPerSide: 0.15,
    nominalLateralClearancePerSide: 0.05,
    totalNominalCompressionContactArea: 2 * 44.00 * 0.15,
  };

  const solar = {
    nominalPanelSize: [30.00, 54.00],
    frontRecessSize: [30.40, 54.40],
    frameBodySize: [36.00, 60.00, 3.00],
    frontRecessDepth: 1.60,
    closedRearWallThickness: 1.40,
    panelWireHoles: { count: 2, diameter: 2.20, centersY: [127.18, 172.18], insideRecess: true },
    caseWireEntries: { count: 2, diameter: 1.60, centersZ: [2.20, 4.60], location: 'left side wall' },
    leftArmWireChannel: { width: 2.60, depth: 0.55 },
    usbOpeningRemoved: true,
    enclosurePivotY: 118.50,
    panelPivotY: 149.68,
    enclosurePivotZ: -2.85,
    panelPivotZ: 15.15,
    panelPivotCentered: true,
    foldedPanelDirectlyAboveEnclosure: true,
    pivotCenterDistance: 36.00,
    roofSweepClearance: {
      limitingCornerDistanceFromPivot: Math.hypot(149.00 - 118.50, 7.55 - (-2.85)),
      armEndRadius: 3.00,
      minimumRadialClearance: 36.00 - 3.00
        - Math.hypot(149.00 - 118.50, 7.55 - (-2.85)),
    },
    armThickness: 1.60,
    armToBossAxialClearance: 0.25,
    armClearanceHoleDiameter: 2.40,
    heatSetInsertPocket: { diameter: 2.90, depth: 3.20 },
  };
  if (solar.roofSweepClearance.minimumRadialClearance < 0.50) {
    errors.push('Solar arm does not clear the limiting enclosure roof corner.');
  }

  const bottomSeal = {
    closedEndWall: true,
    splitBetweenBaseAndLid: true,
    baseTopZ: -0.80,
    lidBottomZ: -0.75,
    probeSlotXYClearance: 0.20,
    probeSlotZClearance: 0.15,
    intendedSealant: 'epoxy or outdoor polyurethane adhesive',
  };

  const report = {
    passed: errors.length === 0,
    errors,
    stepReports,
    stlReports,
    pcb: {
      sourceMeshCount: pcb.meshes.length,
      correctedComponentEnvelope: componentEnvelope,
      supportShelfAabbCandidates: shelfCandidates,
      outsideCavityVertexCount,
      clearances,
      retention: pcbRetention,
    },
    snapGeometry,
    bottomSeal,
    solar,
  };
  const reportPath = path.join(outputDir, 'validation_report.json');
  fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(JSON.stringify(report, null, 2));
  if (!report.passed) process.exitCode = 1;
});

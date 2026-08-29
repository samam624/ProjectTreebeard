const fs = require('fs');
const path = require('path');

const input = process.argv[2];
if (!input) throw new Error('Usage: node analyze_mesh_features.js <file.step>');
const probeValues = process.argv.slice(3).map(Number);
const probes = [];
for (let index = 0; index + 1 < probeValues.length; index += 2) {
  probes.push([probeValues[index], probeValues[index + 1]]);
}

const occtimportjs = require(path.resolve(
  __dirname,
  '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/occt-import-js',
))();

function frequency(values, axis) {
  const counts = new Map();
  for (let index = axis; index < values.length; index += 3) {
    const key = values[index].toFixed(2);
    counts.set(key, (counts.get(key) || 0) + 1);
  }
  return [...counts.entries()]
    .map(([value, count]) => ({ value: Number(value), count }))
    .sort((a, b) => b.count - a.count || a.value - b.value)
    .slice(0, 30);
}

function meshBounds(values) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (let index = 0; index < values.length; index += 3) {
    for (let axis = 0; axis < 3; axis += 1) {
      min[axis] = Math.min(min[axis], values[index + axis]);
      max[axis] = Math.max(max[axis], values[index + axis]);
    }
  }
  return { min, max, dimensions: max.map((value, axis) => value - min[axis]) };
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

occtimportjs.then((occt) => {
  const result = occt.ReadStepFile(fs.readFileSync(input), {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.05,
    angularDeflection: 0.20,
  });
  if (!result.success) throw new Error('Could not read STEP file');
  console.log(JSON.stringify({
    meshCount: result.meshes.length,
    meshes: result.meshes.map((mesh, index) => ({
      index,
      name: mesh.name,
      bounds: meshBounds(mesh.attributes.position.array),
      commonX: frequency(mesh.attributes.position.array, 0),
      commonY: frequency(mesh.attributes.position.array, 1),
      commonZ: frequency(mesh.attributes.position.array, 2),
      probes: probes.map(([x, y]) => ({ x, y, z: zIntersectionsAt(mesh, x, y) })),
    })),
  }, null, 2));
});

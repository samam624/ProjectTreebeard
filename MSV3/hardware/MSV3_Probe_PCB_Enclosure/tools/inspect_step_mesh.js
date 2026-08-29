const fs = require('fs');
const path = require('path');

const input = process.argv[2];
if (!input) {
  console.error('Usage: node inspect_step_mesh.js <file.step> [limit]');
  process.exit(2);
}

const modulePath = path.join(__dirname, 'node', 'node_modules', 'occt-import-js');
const occtimportjs = require(modulePath)();

function boundsForPositions(values) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < values.length; i += 3) {
    for (let axis = 0; axis < 3; axis += 1) {
      min[axis] = Math.min(min[axis], values[i + axis]);
      max[axis] = Math.max(max[axis], values[i + axis]);
    }
  }
  return { min, max, size: max.map((value, axis) => value - min[axis]) };
}

function flattenTree(node, depth = 0, rows = []) {
  rows.push({ depth, name: node.name, meshes: node.meshes || [] });
  for (const child of node.children || []) flattenTree(child, depth + 1, rows);
  return rows;
}

occtimportjs.then((occt) => {
  const content = fs.readFileSync(input);
  const result = occt.ReadStepFile(content, {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.12,
    angularDeflection: 0.35,
  });
  if (!result.success) throw new Error('OpenCascade could not read the STEP file.');

  const meshes = result.meshes.map((mesh, index) => {
    const bounds = boundsForPositions(mesh.attributes.position.array);
    return {
      index,
      name: mesh.name,
      color: mesh.color,
      vertexCount: mesh.attributes.position.array.length / 3,
      triangleCount: mesh.index.array.length / 3,
      ...bounds,
      volumeHint: bounds.size[0] * bounds.size[1] * bounds.size[2],
    };
  }).sort((a, b) => b.volumeHint - a.volumeHint);

  const limit = Number(process.argv[3] || 80);
  console.log(JSON.stringify({
    success: result.success,
    meshCount: result.meshes.length,
    hierarchy: flattenTree(result.root).slice(0, 20),
    meshes: meshes.slice(0, limit),
  }, null, 2));
});

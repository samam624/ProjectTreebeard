const fs = require('fs');
const path = require('path');

const input = process.argv[2]
  ? path.resolve(process.argv[2])
  : path.resolve(__dirname, '..', '..', 'MSV4.step');
const modulePath = path.resolve(
  __dirname,
  '..',
  '..',
  '..',
  '..',
  'MSV3',
  'hardware',
  'MSV3_Probe_PCB_Enclosure',
  'tools',
  'node',
  'node_modules',
  'occt-import-js',
);

require(modulePath)().then((occt) => {
  const result = occt.ReadStepFile(fs.readFileSync(input), {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.02,
    angularDeflection: 0.1,
  });
  if (!result.success) throw new Error('Unable to read source STEP.');

  const board = result.meshes.find((mesh) => {
    const values = mesh.attributes.position.array;
    const xs = [];
    const ys = [];
    for (let index = 0; index < values.length; index += 3) {
      xs.push(values[index]);
      ys.push(values[index + 1]);
    }
    return Math.abs(Math.min(...xs) + 13) < 0.002
      && Math.abs(Math.max(...xs) - 13) < 0.002
      && Math.abs(Math.min(...ys)) < 0.002
      && Math.abs(Math.max(...ys) - 145) < 0.002;
  });
  if (!board) throw new Error('Could not identify the 26 x 145 mm PCB substrate.');
  const values = board.attributes.position.array;
  const points = new Map();
  for (let index = 0; index < values.length; index += 3) {
    const x = Number(values[index].toFixed(6));
    const y = Number(values[index + 1].toFixed(6));
    const z = Number(values[index + 2].toFixed(6));
    points.set(`${x},${y},${z}`, { x, y, z });
  }
  console.log(JSON.stringify({
    input,
    meshName: board.name,
    vertexCount: board.attributes.position.array.length / 3,
    uniquePoints: [...points.values()].sort((a, b) => a.y - b.y || a.x - b.x || a.z - b.z),
  }, null, 2));
});

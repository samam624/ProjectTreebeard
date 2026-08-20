const fs = require('fs');

const input = process.argv[2];
const requestedLimit = Number(process.argv[3] || 100);
if (!input) {
  console.error('Usage: node step_solid_bounds.js <file.step>');
  process.exit(2);
}

const source = fs.readFileSync(input, 'utf8');
const statements = source
  .replace(/\r/g, '')
  .split(';')
  .map((statement) => statement.trim())
  .filter(Boolean);

const entities = new Map();
for (const statement of statements) {
  const match = statement.match(/^#(\d+)\s*=\s*([A-Z0-9_]+)\s*\(([\s\S]*)\)$/);
  if (!match) continue;
  const id = Number(match[1]);
  const type = match[2];
  const body = match[3];
  const refs = [...body.matchAll(/#(\d+)/g)].map((item) => Number(item[1]));
  entities.set(id, { id, type, body, refs });
}

function parsePoint(entity) {
  const match = entity.body.match(/\(\s*(-?[\d.Ee+-]+)\s*,\s*(-?[\d.Ee+-]+)\s*,\s*(-?[\d.Ee+-]+)\s*\)\s*$/);
  if (!match) return null;
  return [Number(match[1]), Number(match[2]), Number(match[3])];
}

function solidBounds(rootId) {
  const stack = [rootId];
  const visited = new Set();
  const points = [];
  while (stack.length) {
    const id = stack.pop();
    if (visited.has(id)) continue;
    visited.add(id);
    const entity = entities.get(id);
    if (!entity) continue;
    if (entity.type === 'CARTESIAN_POINT') {
      const point = parsePoint(entity);
      if (point && point.every(Number.isFinite)) points.push(point);
    }
    for (const ref of entity.refs) stack.push(ref);
  }
  if (!points.length) return null;
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const point of points) {
    for (let axis = 0; axis < 3; axis += 1) {
      min[axis] = Math.min(min[axis], point[axis]);
      max[axis] = Math.max(max[axis], point[axis]);
    }
  }
  return {
    id: rootId,
    pointCount: points.length,
    min,
    max,
    size: max.map((value, axis) => value - min[axis]),
  };
}

function solidPoints(rootId) {
  const stack = [rootId];
  const visited = new Set();
  const unique = new Map();
  while (stack.length) {
    const id = stack.pop();
    if (visited.has(id)) continue;
    visited.add(id);
    const entity = entities.get(id);
    if (!entity) continue;
    if (entity.type === 'CARTESIAN_POINT') {
      const point = parsePoint(entity);
      if (point && point.every(Number.isFinite)) {
        unique.set(point.map((value) => value.toFixed(9)).join(','), point);
      }
    }
    for (const ref of entity.refs) stack.push(ref);
  }
  return [...unique.values()].sort((a, b) =>
    a[1] - b[1] || a[0] - b[0] || a[2] - b[2]);
}

if (process.argv[3] === '--solid') {
  const solidId = Number(process.argv[4]);
  console.log(JSON.stringify({ solidId, points: solidPoints(solidId) }, null, 2));
  process.exit(0);
}

const solids = [...entities.values()]
  .filter((entity) => entity.type === 'MANIFOLD_SOLID_BREP')
  .map((entity) => solidBounds(entity.id))
  .filter(Boolean)
  .sort((a, b) => {
    const av = a.size[0] * a.size[1] * a.size[2];
    const bv = b.size[0] * b.size[1] * b.size[2];
    return bv - av;
  });

console.log(JSON.stringify({
  entityCount: entities.size,
  solidCount: solids.length,
  returnedSolidCount: Math.min(solids.length, requestedLimit),
  solids: solids.slice(0, requestedLimit),
}, null, 2));

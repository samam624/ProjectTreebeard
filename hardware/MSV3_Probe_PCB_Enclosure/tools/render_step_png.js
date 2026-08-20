const fs = require('fs');
const path = require('path');
const { PNG } = require(path.join(__dirname, 'node', 'node_modules', 'pngjs'));
const occtimportjs = require(path.join(__dirname, 'node', 'node_modules', 'occt-import-js'))();

const input = process.argv[2];
const outputDir = process.argv[3] || path.dirname(input || '.');
const outputPrefix = process.argv[4] || 'pcb';
if (!input) {
  console.error('Usage: node render_step_png.js <file.step> [output-directory]');
  process.exit(2);
}

const WIDTH = 1100;
const HEIGHT = 1100;
const CROP_Y_MIN = Number(process.argv[5] || 82.0);
const CROP_Y_MAX = Number(process.argv[6] || 147.0);

function normalize(vector) {
  const length = Math.hypot(...vector);
  return vector.map((value) => value / length);
}

function dot(a, b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

function cross(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

function camera(direction, preferredUp = [0, 1, 0]) {
  const depth = normalize(direction);
  const horizontal = normalize(cross(depth, preferredUp));
  const vertical = normalize(cross(horizontal, depth));
  return { horizontal, vertical, depth };
}

function color255(meshColor, normal, lightDirection) {
  const base = meshColor || [0.52, 0.56, 0.62];
  const diffuse = 0.42 + 0.58 * Math.abs(dot(normal, lightDirection));
  return base.map((channel) => {
    const srgb = Math.pow(Math.max(0, Math.min(1, channel * diffuse)), 1 / 2.2);
    return Math.round(255 * srgb);
  });
}

function writeView(meshes, viewName, basis) {
  const triangles = [];
  const projectedPoints = [];
  const light = normalize([0.4, -0.25, 1]);

  for (const mesh of meshes) {
    const positions = mesh.attributes.position.array;
    const indices = mesh.index.array;
    for (let i = 0; i < indices.length; i += 3) {
      const points = [0, 1, 2].map((offset) => {
        const index = indices[i + offset] * 3;
        return [positions[index], positions[index + 1], positions[index + 2]];
      });
      const centerY = (points[0][1] + points[1][1] + points[2][1]) / 3;
      if (centerY < CROP_Y_MIN || centerY > CROP_Y_MAX) continue;
      const edgeA = points[1].map((value, axis) => value - points[0][axis]);
      const edgeB = points[2].map((value, axis) => value - points[0][axis]);
      const rawNormal = cross(edgeA, edgeB);
      const length = Math.hypot(...rawNormal);
      if (length < 1e-9) continue;
      const normal = rawNormal.map((value) => value / length);
      const projected = points.map((point) => [
        dot(point, basis.horizontal),
        dot(point, basis.vertical),
        dot(point, basis.depth),
      ]);
      projectedPoints.push(...projected);
      triangles.push({ projected, color: color255(mesh.color, normal, light) });
    }
  }

  let minX = Infinity;
  let maxX = -Infinity;
  let minY = Infinity;
  let maxY = -Infinity;
  for (const point of projectedPoints) {
    minX = Math.min(minX, point[0]);
    maxX = Math.max(maxX, point[0]);
    minY = Math.min(minY, point[1]);
    maxY = Math.max(maxY, point[1]);
  }
  const padding = 50;
  const scale = Math.min(
    (WIDTH - padding * 2) / (maxX - minX),
    (HEIGHT - padding * 2) / (maxY - minY),
  );
  const centerX = (minX + maxX) / 2;
  const centerY = (minY + maxY) / 2;

  const png = new PNG({ width: WIDTH, height: HEIGHT });
  const zbuffer = new Float64Array(WIDTH * HEIGHT);
  zbuffer.fill(-Infinity);
  for (let i = 0; i < png.data.length; i += 4) {
    png.data[i] = 241;
    png.data[i + 1] = 244;
    png.data[i + 2] = 248;
    png.data[i + 3] = 255;
  }

  function screen(point) {
    return [
      WIDTH / 2 + (point[0] - centerX) * scale,
      HEIGHT / 2 - (point[1] - centerY) * scale,
      point[2],
    ];
  }

  for (const triangle of triangles) {
    const p = triangle.projected.map(screen);
    const area = (p[1][1] - p[2][1]) * (p[0][0] - p[2][0])
      + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]);
    if (Math.abs(area) < 1e-7) continue;
    const minPx = Math.max(0, Math.floor(Math.min(p[0][0], p[1][0], p[2][0])));
    const maxPx = Math.min(WIDTH - 1, Math.ceil(Math.max(p[0][0], p[1][0], p[2][0])));
    const minPy = Math.max(0, Math.floor(Math.min(p[0][1], p[1][1], p[2][1])));
    const maxPy = Math.min(HEIGHT - 1, Math.ceil(Math.max(p[0][1], p[1][1], p[2][1])));
    for (let y = minPy; y <= maxPy; y += 1) {
      for (let x = minPx; x <= maxPx; x += 1) {
        const px = x + 0.5;
        const py = y + 0.5;
        const w0 = ((p[1][1] - p[2][1]) * (px - p[2][0])
          + (p[2][0] - p[1][0]) * (py - p[2][1])) / area;
        const w1 = ((p[2][1] - p[0][1]) * (px - p[2][0])
          + (p[0][0] - p[2][0]) * (py - p[2][1])) / area;
        const w2 = 1 - w0 - w1;
        if (w0 < -1e-7 || w1 < -1e-7 || w2 < -1e-7) continue;
        const depth = w0 * p[0][2] + w1 * p[1][2] + w2 * p[2][2];
        const pixelIndex = y * WIDTH + x;
        if (depth <= zbuffer[pixelIndex]) continue;
        zbuffer[pixelIndex] = depth;
        const dataIndex = pixelIndex * 4;
        png.data[dataIndex] = triangle.color[0];
        png.data[dataIndex + 1] = triangle.color[1];
        png.data[dataIndex + 2] = triangle.color[2];
      }
    }
  }

  const output = path.join(outputDir, `${outputPrefix}_${viewName}.png`);
  fs.writeFileSync(output, PNG.sync.write(png));
  console.log(output);
}

occtimportjs.then((occt) => {
  const content = fs.readFileSync(input);
  const result = occt.ReadStepFile(content, {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.10,
    angularDeflection: 0.30,
  });
  if (!result.success) throw new Error('OpenCascade could not read the STEP file.');
  fs.mkdirSync(outputDir, { recursive: true });
  writeView(result.meshes, 'top', camera([0, 0, 1]));
  writeView(result.meshes, 'bottom', camera([0, 0, -1]));
  writeView(result.meshes, 'isometric', camera([0.8, -1, 0.75]));
});

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../../../../MSV3/hardware/MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const sourcePcbPath = path.resolve(projectDir, '..', 'MSV4.step');
const wasmPath = path.resolve(
  projectDir,
  '..',
  '..',
  '..',
  'MSV3',
  'hardware',
  'MSV3_Probe_PCB_Enclosure',
  'tools',
  'node',
  'node_modules',
  'replicad-opencascadejs',
  'dist',
  'replicad_single.wasm',
);

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

const imported = await replicad.importSTEP(new Blob([
  fs.readFileSync(sourcePcbPath),
], { type: 'application/STEP' }));
const solids = Array.from(
  replicad.iterTopo(imported.wrapped, 'solid'),
  (shape) => replicad.cast(shape),
);
const bottomShift = -(1.60 - 0.41148);
const components = solids.slice(1).map((solid, index) => {
  const clone = solid.clone();
  const isBottom = clone.boundingBox.bounds[1][2] <= -0.41148 + 0.002;
  return {
    index: index + 1,
    isBottom,
    shape: isBottom ? clone.translateZ(bottomShift) : clone,
  };
});

function rectanglesOverlap(a, b) {
  return a.xMin < b.xMax && a.xMax > b.xMin && a.yMin < b.yMax && a.yMax > b.yMin;
}

function componentRect(component) {
  const [min, max] = component.shape.boundingBox.bounds;
  return { xMin: min[0], yMin: min[1], xMax: max[0], yMax: max[1], zMin: min[2], zMax: max[2] };
}

const top = components.filter((component) => !component.isBottom);
const bottom = components.filter((component) => component.isBottom);
const batterySizes = [
  { orientation: '10.4 x 20.4', width: 10.4, length: 20.4 },
  { orientation: '20.4 x 10.4', width: 20.4, length: 10.4 },
];

const candidates = [];
for (const size of batterySizes) {
  for (let centerX = -7.5; centerX <= 7.5001; centerX += 0.5) {
    for (let centerY = 107.5; centerY <= 136.0001; centerY += 0.5) {
      const rect = {
        xMin: centerX - size.width / 2,
        xMax: centerX + size.width / 2,
        yMin: centerY - size.length / 2,
        yMax: centerY + size.length / 2,
      };
      if (rect.xMin < -12.65 || rect.xMax > 12.65 || rect.yMin < 102.35 || rect.yMax > 140.65) continue;
      const overlaps = top
        .map((component) => ({ component, bounds: componentRect(component) }))
        .filter(({ bounds }) => rectanglesOverlap(rect, bounds));
      const supportZ = overlaps.reduce((highest, entry) => Math.max(highest, entry.bounds.zMax), 0.0);
      const overlapArea = overlaps.reduce((total, entry) => {
        const overlapX = Math.max(0, Math.min(rect.xMax, entry.bounds.xMax) - Math.max(rect.xMin, entry.bounds.xMin));
        const overlapY = Math.max(0, Math.min(rect.yMax, entry.bounds.yMax) - Math.max(rect.yMin, entry.bounds.yMin));
        return total + overlapX * overlapY;
      }, 0.0);
      candidates.push({
        orientation: size.orientation,
        centerX,
        centerY,
        ...rect,
        supportZ,
        batteryBottomZ: supportZ + 0.25,
        batteryTopZ: supportZ + 3.45,
        overlapArea,
        overlapIndices: overlaps.map(({ component }) => component.index),
      });
    }
  }
}

candidates.sort((a, b) => (
  a.supportZ - b.supportZ
  || a.overlapArea - b.overlapArea
  || Math.abs(a.centerX) - Math.abs(b.centerX)
  || a.centerY - b.centerY
));

const detail = (component) => ({ index: component.index, bounds: componentRect(component) });
console.log(JSON.stringify({
  sourceSolidCount: solids.length,
  topComponentCount: top.length,
  bottomComponents: bottom.map(detail),
  topEnvelope: {
    zMin: Math.min(...top.map(({ shape }) => shape.boundingBox.bounds[0][2])),
    zMax: Math.max(...top.map(({ shape }) => shape.boundingBox.bounds[1][2])),
  },
  bestCandidates: candidates.slice(0, 30),
}, null, 2));

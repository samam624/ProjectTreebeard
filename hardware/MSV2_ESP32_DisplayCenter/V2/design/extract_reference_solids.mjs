import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.dirname(here);
const sourcePath = path.join(projectDir, 'display_enclosure.step');
const outputDir = path.join(here, 'reference_solids');
const wasmPath = path.resolve(
  here,
  '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.wasm',
);

const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);

const source = await replicad.importSTEP(new Blob([fs.readFileSync(sourcePath)]));
const solids = source._listTopo('solid').map((wrapped) => replicad.cast(wrapped));
fs.mkdirSync(outputDir, { recursive: true });

async function writeBlob(filename, blob) {
  fs.writeFileSync(filename, Buffer.from(await blob.arrayBuffer()));
}

const report = [];
for (let index = 0; index < solids.length; index += 1) {
  const solid = solids[index];
  const bounds = solid.boundingBox.bounds;
  const name = bounds[1][2] > 0 ? 'front_screen_bezel' : 'previous_rear_shell';
  await writeBlob(path.join(outputDir, `${name}.step`), solid.blobSTEP());
  report.push({ index, name, bounds, volume: replicad.measureVolume(solid) });
}

console.log(JSON.stringify(report, null, 2));

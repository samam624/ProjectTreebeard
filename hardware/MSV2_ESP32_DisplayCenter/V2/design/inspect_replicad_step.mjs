import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import opencascade from '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.js';
import * as replicad from '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad/dist/replicad.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = path.resolve(here,
  '../../../MSV3_Probe_PCB_Enclosure/tools/node/node_modules/replicad-opencascadejs/dist/replicad_single.wasm');
const input = process.argv[2];
if (!input) throw new Error('Usage: node inspect_replicad_step.mjs <file.step>');
const oc = await opencascade({ locateFile: () => wasmPath });
replicad.setOC(oc);
const shape = await replicad.importSTEP(new Blob([fs.readFileSync(input)]));
const solids = shape._listTopo('solid').map((wrapped) => replicad.cast(wrapped));
console.log(JSON.stringify({
  compoundBounds: shape.boundingBox.bounds,
  solids: solids.map((solid, index) => ({
    index,
    bounds: solid.boundingBox.bounds,
    volume: replicad.measureVolume(solid),
  })),
}, null, 2));

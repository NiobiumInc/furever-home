// Roundtrip test for the WASM FHE module: keygen -> encrypt -> score -> decrypt,
// checked against the Python plaintext reference for the same answers.
//   node test_module.js [a0..a11]
const fs = require('fs'), path = require('path'), cp = require('child_process');
const createFhe = require('./fhe.js');

const DSL = path.join(__dirname, '..', '..', 'dsl');
const PETS = ['Rex', 'Mochi', 'Smaug', 'Kiwi'];

function loadWeights() {
  const txt = fs.readFileSync(path.join(DSL, 'rubric.dat'), 'utf8');
  const W = new Array(256).fill(0);
  let pet = -1, cat = 0;
  for (const line of txt.split('\n')) {
    const s = line.trim();
    if (!s || s.startsWith('#')) continue;
    if (s.startsWith('PET')) { pet++; cat = 0; continue; }
    const n = s.split(/\s+/).map(Number);          // 12 weights + offset
    const b = pet * 4 + cat;
    for (let q = 0; q < 12; q++) W[b * 16 + q] = n[q];
    W[b * 16 + 12] = n[12];                          // offset -> bias slot
    cat++;
  }
  return W;
}

(async () => {
  const args = process.argv.slice(2).map(Number);
  const ans = args.length === 12 ? args : [3, 4, 1, 1, 0, 3, 3, 2, 4, 2, 4, 3]; // reptile -> Smaug
  const W = loadWeights();

  const M = await createFhe();
  const t0 = Date.now();
  const keys = M.keygen(false);
  const ansB = M.encrypt(keys.cc, keys.pk, ans);
  const res = M.score(keys.cc, keys.mk, keys.rk, ansB, W);     // NOTE: no sk
  const out = M.decrypt(keys.cc, keys.sk, res.scores, res.categories);
  const dt = Date.now() - t0;

  // Python reference (ground truth)
  const py = process.env.PYTHON || 'python3';
  const ref = JSON.parse(cp.execSync(
    `${py} ${path.join(DSL, 'reference', 'score_reference.py')} --json ${ans.join(' ')}`
  ).toString());

  let maxErr = 0;
  console.log('answers:', ans.join(' '));
  console.log('pet'.padEnd(8), 'WASM overall'.padStart(13), 'ref overall'.padStart(13), '|diff|'.padStart(9));
  for (let p = 0; p < 4; p++) {
    const w = out.overall[p], r = ref.pets[p].overall;
    maxErr = Math.max(maxErr, Math.abs(w - r));
    for (let c = 0; c < 4; c++)
      maxErr = Math.max(maxErr, Math.abs(out.categories[p * 4 + c] - ref.pets[p].categories[c]));
    console.log(PETS[p].padEnd(8), w.toFixed(3).padStart(13), r.toFixed(3).padStart(13), Math.abs(w - r).toFixed(4).padStart(9));
  }
  const wasmBest = out.overall.indexOf(Math.max(...out.overall));
  const refBest = ref.pets.reduce((bi, x, i, a) => x.overall > a[bi].overall ? i : bi, 0);
  console.log(`\nbest match — WASM: ${PETS[wasmBest]}   ref: ${PETS[refBest]}`);
  console.log(`max abs error: ${maxErr.toFixed(4)} (tol 0.5)   roundtrip: ${dt} ms`);
  const ok = maxErr < 0.5 && wasmBest === refBest;
  console.log(ok ? 'MODULE_OK' : 'MODULE_FAIL');
  process.exit(ok ? 0 : 1);
})().catch(e => { console.error('ERR', e); process.exit(1); });

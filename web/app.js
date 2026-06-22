// app.js — shared core for the Furever Home web app (survey + checker pages).
// Wraps the WASM FHE engine (web/wasm/fhe.js, global `createFhe`) and the demo's
// data + UX. Exposes window.Furever for the pages and for headless tests.

const QUESTIONS = [
  ["Your housing situation",
   ["Rent, pets NOT allowed", "Rent, informal/unsure", "Rent, pets allowed",
    "Own or long-term pet-friendly", "Own my home, pets fully welcome"]],
  ["Indoor space",
   ["Tiny studio", "Small apartment", "Average apartment", "House with room", "Large house"]],
  ["Outdoor / yard access",
   ["None", "Shared/communal", "Balcony or small patio", "A yard", "Large fenced yard"]],
  ["Hours per day you're home with a pet",
   ["Barely there", "A few hours", "About half the day", "Home most of the day", "Home almost all day"]],
  ["Daily active time you can give (walks/play)",
   ["None", "A short walk", "Moderate play/walks", "Lots of exercise", "Very active, hours a day"]],
  ["Schedule stability (how rarely you travel)",
   ["Travel constantly", "Travel often", "Sometimes away", "Mostly stable", "Very stable, home-based"]],
  ["Monthly budget for pet care",
   ["Very tight", "Modest", "Comfortable", "Generous", "No real limit"]],
  ["Readiness for a surprise vet bill",
   ["None saved", "A little saved", "Some cushion", "Well prepared", "Fully covered / insured"]],
  ["Willing to buy setup & equipment (tank, gear...)",
   ["Not really", "Just the basics", "A fair amount", "Yes, proper gear", "Whatever it takes"]],
  ["Prior pet ownership",
   ["Never", "A little", "Some", "Lots", "Lifelong"]],
  ["Experience with demanding/exotic pets (birds, reptiles)",
   ["None", "A little reading", "Some hands-on", "Experienced", "Expert"]],
  ["Your household is calm & ready for a pet",
   ["Chaotic / conflicts", "Pretty busy", "Average", "Calm", "Calm & fully ready"]],
];

const PETS = [
  { emoji: "🐺", name: "Rex", sub: "high-energy husky", accent: "#5b9bff", img: "public/pets/rex.jpg" },
  { emoji: "🐱", name: "Mochi", sub: "aloof senior cat", accent: "#ff9f43", img: "public/pets/mochi.jpg" },
  { emoji: "🦎", name: "Smaug", sub: "bearded dragon", accent: "#34c98a", img: "public/pets/smaug.jpg" },
  { emoji: "🦜", name: "Kiwi", sub: "parrot", accent: "#c084fc", img: "public/pets/kiwi.jpg" },
];
const CATS = ["Housing", "Time", "Finances", "Experience"];
const VERDICT = {
  Rex: "you've got the space and energy — go meet that husky!",
  Mochi: "low-key and easygoing — you'd make a wonderful cat person.",
  Smaug: "proper setup and know-how — the bearded dragon suits you.",
  Kiwi: "home, engaged, experienced — a parrot would adore you.",
};

let _modPromise = null;
function fhe() { return (_modPromise ||= createFhe()); }

async function loadWeights(rubricUrl = "../dsl/rubric.dat") {
  const txt = await (await fetch(rubricUrl)).text();
  const W = new Array(256).fill(0);
  let pet = -1, cat = 0;
  for (const line of txt.split("\n")) {
    const s = line.trim();
    if (!s || s.startsWith("#")) continue;
    if (s.startsWith("PET")) { pet++; cat = 0; continue; }
    const n = s.split(/\s+/).map(Number);            // 12 weights + offset
    const b = pet * 4 + cat;
    for (let q = 0; q < 12; q++) W[b * 16 + q] = n[q];
    W[b * 16 + 12] = n[12];                            // offset -> bias slot
    cat++;
  }
  return W;
}

// The hand-off artifact given to the checker — public/eval keys + ciphertext,
// NEVER the secret key.
function makeBundle(keys, answersBlob) {
  return { v: 1, cc: keys.cc, pk: keys.pk, mk: keys.mk, rk: keys.rk, answers: answersBlob };
}
function downloadJson(filename, obj) {
  const blob = new Blob([JSON.stringify(obj)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
async function readJsonFile(file) { return JSON.parse(await file.text()); }

function humanBytes(n) {
  for (const u of ["B", "KB", "MB"]) { if (n < 1024) return `${n.toFixed(u === "B" ? 0 : 1)} ${u}`; n /= 1024; }
  return `${n.toFixed(1)} GB`;
}
// rough byte size of a base64 string
function b64bytes(s) { return Math.floor(s.length * 3 / 4); }
// a peek at a ciphertext blob — sampled from the MIDDLE (the start is OpenFHE's
// identical serialization header; the random ciphertext body is deeper).
function cipherPeek(b64, n = 88) { const s = String(b64 || ""); const i = Math.floor(s.length / 2); return s.slice(i, i + n); }
// HTML-escape — use when interpolating any value that could originate off-device
// (e.g. a relayed bundle) into innerHTML, so a crafted blob can't inject markup.
function esc(s) { return String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c])); }

// A round pet avatar: shows the photo (public/pets/<name>.jpg) if present, else
// falls back to the emoji — so it looks intentional before real photos are added.
function petAvatar(pet) {
  return `<span class="avatar" style="--accent:${pet.accent}">` +
    `<span class="emoji">${pet.emoji}</span>` +
    `<img src="${pet.img}" alt="${pet.name}" loading="lazy" onerror="this.remove()">` +
    `</span>`;
}
function catRow(label, v, max) {
  const pct = Math.max(0, Math.min(100, (v / max) * 100));
  return `<div class="catrow"><span class="catlbl">${label}</span>` +
    `<span class="track"><span class="fill" style="width:${pct}%"></span></span>` +
    `<span class="catval">${Math.max(0, v).toFixed(1)}</span></div>`;
}
function renderScorecard(el, overall, categories) {
  const order = [...PETS.keys()].sort((a, b) => overall[b] - overall[a]);
  const best = order[0], bp = PETS[best];
  let html = `<div class="hero" style="--accent:${bp.accent}">${petAvatar(bp)}` +
    `<div class="herotext"><div class="herotag">🏆 your best match</div>` +
    `<div class="heroname">${bp.name} <span class="sub">· ${bp.sub}</span></div>` +
    `<div class="heroverdict">${VERDICT[bp.name]}</div></div></div>` +
    `<div class="cards">`;
  order.forEach(p => {
    const pet = PETS[p];
    html += `<div class="petcard${p === best ? " top" : ""}" style="--accent:${pet.accent}">` +
      `<div class="petcardhead">${petAvatar(pet)}` +
      `<div class="petname"><b>${pet.name}</b>${p === best ? ' <span class="star">⭐</span>' : ""}` +
      `<div class="sub">${pet.sub}</div></div>` +
      `<div class="bigscore">${Math.max(0, overall[p]).toFixed(0)}<span>/40</span></div></div>` +
      CATS.map((c, ci) => catRow(c, categories[p * 4 + ci], 10)).join("") +
      `</div>`;
  });
  el.innerHTML = html + `</div>`;
}

// Programmatic API — pages' buttons and headless tests both call these.
window.Furever = {
  QUESTIONS, PETS, CATS, VERDICT, fhe, loadWeights,
  makeBundle, downloadJson, readJsonFile, humanBytes, b64bytes, cipherPeek, esc, renderScorecard, petAvatar,
  async keygen(hardware = false) { return (await fhe()).keygen(hardware); },
  async encrypt(cc, pk, answers) { return (await fhe()).encrypt(cc, pk, answers); },
  async score(cc, mk, rk, answersBlob, weights) { return (await fhe()).score(cc, mk, rk, answersBlob, weights); },
  async decrypt(cc, sk, scores, categories) {
    const o = await (await fhe()).decrypt(cc, sk, scores, categories);
    return { overall: [...o.overall], categories: [...o.categories] };
  },
};

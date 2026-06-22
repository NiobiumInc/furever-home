# Pet photos

Drop a photo for each pet here to replace the emoji placeholders. Until a file
exists, the UI shows the pet's emoji in a colored circle (graceful fallback), so
the app looks fine with no photos at all.

Expected filenames (referenced by `web/app.js` → `PETS[].img`):

| file | pet |
|------|-----|
| `rex.jpg`   | 🐺 Rex — high-energy husky |
| `mochi.jpg` | 🐱 Mochi — aloof senior cat |
| `smaug.jpg` | 🦎 Smaug — bearded dragon |
| `kiwi.jpg`  | 🦜 Kiwi — parrot |

Tips:
- Square images look best (they're shown in round avatars, `object-fit: cover`).
- ~400×400 px is plenty; keep them small for fast load.
- **Use royalty-free / licensed images** (e.g. Unsplash, Pexels) — these ship in
  the repo. `.jpg`/`.png`/`.webp` all work if you also update the extension in
  `app.js`.

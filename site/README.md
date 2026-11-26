# Kode site

The project's home page, built by Jekyll and published to GitHub Pages
(https://rask-project.github.io/kode/) by `.github/workflows/pages.yml` on
every push to `main` that touches it.

- `index.html` (home), `features.html`, `download.html` and `404.html`.
- `_layouts/` (`default` → `page`) and `_includes/` (menu bar, footer,
  `shot.html` for a screenshot).
- `assets/css/site.css` (all styles; dark, macOS look, from Konsoll's site)
  and `assets/js/site.js` (the Download button points at the latest
  release's `.dmg` once there is one; Copy buttons; the Features sidebar).
- Screenshots, icons and the app icon are **not** kept here:
  `tools/site-assets.sh` copies `docs/images`, `assets/settings` and
  `assets/kode.png` into `assets/` (ignored by git) before a build, with the
  icons only the site uses (`_icons/`).

## Preview

```sh
cd site && bundle install   # once: the github-pages gem (Ruby 3.x)
tools/site-serve.sh         # http://127.0.0.1:4000/kode/
```

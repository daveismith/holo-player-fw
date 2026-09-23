# Releasing

How a release is cut, and how its documentation is published. The pipeline is
`.github/workflows/release.yml` and `docs.yml`.

## Versions

There is **no version constant in the source**. ESP-IDF sets `PROJECT_VER` from
`git describe --always --tags --dirty`, so the tag on the commit is the version compiled into the
image, reported by `version`, and shown per slot by `ota`.

| Thing | Example | Where it comes from |
|---|---|---|
| Release tag | `v0.2.0` | What you push. The only thing to decide |
| Firmware version | `v0.2.0` | `git describe` on the tagged commit, baked into `esp_app_desc` |
| Development build | `v0.2.0-5-gabc1234-dirty` | `git describe` anywhere else — self-labelling on purpose |
| Docs version | `0.2` | `MAJOR.MINOR`: a patch release updates its minor's documentation in place |
| Pre-release | `v0.2.0-rc1` → docs `0.2-rc` | Published, but `latest` does not move |

The site keeps one directory per docs version on the `gh-pages` branch (managed by mike), plus
`latest` (the newest release, which the site root redirects to) and `dev` (built from `main` on
every push).

!!! note
    The firmware version field holds **31 characters**, and ESP-IDF asserts the fit at compile
    time — an over-long version fails the build rather than being truncated. Keep tags to
    `vMAJOR.MINOR.PATCH`, optionally `-rcN`. `tools/check_version.py` enforces this.

## Cutting a release

1. Move the `Unreleased` entries in `CHANGELOG.md` under a new `## [X.Y.Z]` heading with today's
   date. The release notes come from that section, so it must not be empty.
2. Open a PR and merge it.
3. Tag the merge commit and push the tag:

   ```sh
   git tag vX.Y.Z && git push origin vX.Y.Z
   ```

The release workflow then:

1. checks the tag is well formed, is on the commit being built, and fits the version field;
2. builds the firmware in the ESP-IDF v6.1 container for `esp32s3`;
3. **checks the image really reports the tag**, failing the release if it does not;
4. names the four flash images for the release, and stages copies of them for the documentation's
   installer, checked against the release's checksums;
5. publishes the documentation as `X.Y` and moves `latest` to it (not for pre-releases);
6. creates the GitHub Release with the `CHANGELOG.md` section as notes and these assets:

   | Asset | What |
   |---|---|
   | `holo-player-fw-vX.Y.Z.bin` | The application, at `0x10000`; also what `fs_xfer.py ota` sends |
   | `holo-player-fw-vX.Y.Z-bootloader.bin` | The bootloader, at `0x0` |
   | `holo-player-fw-vX.Y.Z-partition-table.bin` | The partition table, at `0x8000` |
   | `holo-player-fw-vX.Y.Z-ota-data-initial.bin` | The initial boot selection, at `0xe000` |
   | `holo-player-fw-vX.Y.Z-docs.zip` | The documentation, offline, with the installer and `serve.py` |
   | `SHA256SUMS` | Checksums of all of the above |

### The installer's firmware

The Install page flashes the release from the browser (`manual/javascripts/installer/`, on the
vendored esptool-js). It can't download from the Release: `github.com/…/releases/download/…` and
the storage host it redirects to send no `Access-Control-Allow-Origin` header, so a page on
another origin can't read the bytes. The GitHub API can find the download URL, but the download
itself is still blocked. So the release copies the four images into the documentation instead:

- `tools/web_install_manifest.py --dist` names them for the Release, taking the offsets from
  the build's `flasher_args.json`.
- `--stage manual/firmware` copies them next to a `manifest.json` (offsets, sizes, SHA-256).
  `sha256sum -c` then checks the copies against the Release's own files.
- Both builds of the documentation — the published version and the offline zip — publish
  `manual/firmware/` unchanged. It is never committed (`.gitignore`).

Each release therefore adds about 1.4 MB to `gh-pages`. A patch release replaces its minor's
copy, as it does the pages. `dev` and pull-request previews carry no firmware, and their Install
page links to `latest` instead. Should GitHub ever serve release downloads with CORS headers,
`manifest.json` already records each part's `release_url` for the installer to fetch instead.

The offline zip also contains `serve.py` (`tools/offline_serve.py`). Web Serial and ES modules
don't run from `file://`, and it serves the unzipped folder on `localhost`, which browsers
treat as secure, without needing the network.

### Updating the vendored JavaScript

esptool-js is committed under `manual/javascripts/vendor/`, never loaded from a CDN, so the
offline copy works with no internet. To move to a new version, change its entry in
`tools/vendor_js.py` (version and `npm view esptool-js@<version> dist.integrity`), then:

```sh
make docs-vendor                # download, verify the integrity, rewrite SHA256SUMS
make docs-check                 # the files match, and nothing loads by absolute URL
```

`make docs-check` also runs the installer's tests with `node --test`, when Node is installed;
the workflows install it.

### Why step 3 exists

Two things silently break the version, and both produce a build that looks fine:

- **A shallow checkout.** `actions/checkout` defaults to `fetch-depth: 1` and fetches no tags, so
  `git describe --always` falls back to a bare commit hash.
- **The container's ownership check.** The IDF image runs as root over a checkout owned by the
  runner, so git refuses the repository as "dubious ownership", `git describe` fails, and
  `PROJECT_VER` falls back the same way. This is
  [esp-idf#9071](https://github.com/espressif/esp-idf/issues/9071); the fix is the
  `IDF_GIT_SAFE_DIR` environment variable.

The workflow sets `fetch-depth: 0` and `IDF_GIT_SAFE_DIR`, and then verifies the result against
`build/project_description.json` rather than trusting either.

## Trying the pipeline

Push a pre-release tag such as `v0.1.0-rc1`. It publishes docs `0.1-rc` and a GitHub pre-release
without touching `latest`. Then:

1. Open the `0.1-rc` documentation's Install page in Chrome, install onto a board, and check that
   the boot log and `version` report `v0.1.0-rc1`.
2. Unzip the docs asset, disconnect from the network, run `python3 serve.py`, and install again
   from there.
3. Check that the images in `gh-pages:0.1-rc/firmware/` match the Release's `SHA256SUMS`.

To remove it afterwards:

```sh
PATH="$PWD/.venv/bin:$PATH" .venv/bin/mike delete --push 0.1-rc
gh release delete v0.1.0-rc1 --cleanup-tag
```

## Previewing locally

```sh
make docs-setup                 # once
make docs-check                 # every registered command is documented
make docs-serve                 # live preview
```

For the version switcher, deploy to the local `gh-pages` branch without pushing:

```sh
PATH="$PWD/.venv/bin:$PATH" .venv/bin/mike deploy --alias-type redirect --update-aliases 0.1 latest
make docs-preview-versions
git branch -D gh-pages          # discard it afterwards
```

mike runs the site builder as a subprocess, so the venv must be on `PATH`; calling
`.venv/bin/mike` by path alone fails with `No such file or directory: 'zensical'`.

To try the installer, stage your own build as its firmware first. `localhost` counts as a secure
page, so Web Serial works there:

```sh
idf.py build
make docs-stage-firmware        # build/dist/, then manual/firmware/
make docs-serve                 # then the Install page, in Chrome
```

Delete `manual/firmware/` afterwards to see the page as `dev` shows it.

## GitHub Pages setup (once)

After the first deploy has created the `gh-pages` branch, set *Settings → Pages → Source* to
**Deploy from a branch**, branch `gh-pages`, folder `/ (root)`.

This requires the repository to be public, or a paid plan: GitHub Pages is not available for
private repositories on the free plan.

## Moving to a custom domain

1. In *Settings → Pages → Custom domain*, enter the domain. GitHub commits a `CNAME` file to the
   root of `gh-pages`; mike leaves root files alone, so it survives later deploys.
2. At your DNS provider, add a `CNAME` record from that name to `daveismith.github.io`. For an
   apex domain, use GitHub's `A`/`AAAA` records instead.
3. Once the certificate is issued, tick *Enforce HTTPS*.
4. Set `site_url` in `mkdocs.yml` to the new address and merge. The next `dev` deploy and every
   release after it use it.

Version paths (`/0.2/…`) stay the same, and the old `github.io` addresses redirect automatically.

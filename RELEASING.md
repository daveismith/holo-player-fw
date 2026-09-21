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
4. publishes the documentation as `X.Y` and moves `latest` to it (not for pre-releases);
5. creates the GitHub Release with the `CHANGELOG.md` section as notes and these assets:
   `holo-player-fw-vX.Y.Z.bin`, an offline copy of the documentation, and `SHA256SUMS`.

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
without touching `latest`. Download the `.bin` from the release, flash it, and check that `ota`
reports `v0.1.0-rc1`.

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

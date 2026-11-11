# Packaging for the openSUSE Build Service

Recipes for [build.opensuse.org](https://build.opensuse.org): one RPM recipe
for Fedora (and openSUSE), one set of Debian recipes for Debian and Ubuntu.
Arch Linux is not covered: Arch users use the AUR, not a build service.

| File | For |
|------|-----|
| `_service` | tells OBS where the source is (this repository, with its submodules) |
| `mbrola-ng.spec` | Fedora, openSUSE |
| `mbrola-ng.dsc`, `debian.control`, `debian.rules`, `debian.changelog`, `debian.copyright` | Debian, Ubuntu |

These files were written without being able to run `rpmbuild` or
`dpkg-buildpackage`: the build log of the first run is the test. Expect to
fix a few things, see "First build" below.

## Setting up the package (once)

1. In the OBS project, create the package `mbrola-ng`.
2. Upload **only** `_service` (*Add file*). OBS runs it at once: it clones the
   repository with its submodules, makes the source archive and takes the
   recipe files from `linux/packaging/` into the package.
3. The repositories of the project (Fedora, Debian, Ubuntu) are built
   automatically from then on. The status is on the package page, *Build
   results* (green = built, red = failed: click it for the log).

Users add the repository of their distribution from the project page
(*Download package* / the repository link), then install `mbrola-ng`.

## Releasing a new version

OBS does not watch git. After a release:

1. Merge `dev` into `master` and tag it (`git tag -a v0.3.0 ...`, push both).
2. Run **Trigger Services** on the package in the OBS web interface, or
   `osc service remoterun home:daremc86:Mbrola-NG mbrola-ng`.
3. OBS takes the version from the tag (`v0.3.0` becomes `0.3.0`), rebuilds for
   every repository and publishes the packages. Users get the update with
   `dnf upgrade` or `apt upgrade`.

A change to the recipes in this folder needs the same trigger, because OBS
reads them from the repository.

## Before the first release that contains this folder

`_service` takes the source from `master`, which does not contain
`linux/packaging/` yet. Until it does, change `revision` to `dev` in the
**OBS copy** of `_service` (the copy in this folder is the final one), and
change it back when the release is on `master`.

## Version numbers

`set_version` writes the version of the tag into the recipes when the package
is built. `debian.changelog` and `mbrola-ng.dsc` carry `0.2.0-1`: if the Debian
build fails because their version differs from the archive, set both to the
version of the tag (for example `0.3.0-1`) and add a changelog entry.

## First build: what to look at

- **Where the module went.** The Speech Dispatcher module folder differs
  between distributions and is read from `pkg-config speech-dispatcher`. The
  build log has the line `Speech Dispatcher module folder: ...`. If it is not
  the folder Speech Dispatcher searches on that distribution, pass
  `-DMBNG_SPEECHD_MODULE_DIR=<folder>` in the recipe. The RPM lists the module
  from what was installed, so the package itself does not depend on a guess.
- **`extract` and `exclude` of `_service`.** If the recipe files do not appear
  in the package, upload them by hand next to `_service` and remove the
  `extract` lines. If the build cannot find `Resources/catalog/catalog.json`,
  remove the `exclude` line (it only leaves the voice samples out of the
  sources).
- **The top folder of the archive** must be `mbrola-ng-<version>`
  (`%autosetup -n %{name}-%{version}` in the spec).
- **The Debian archive name.** If OBS reports that it cannot find the source
  archive for `mbrola-ng.dsc`, the archive has to be named
  `mbrola-ng-<version>.tar.xz` (it is, with the `_service` above).
- **Licenses.** The license expression of the spec and `debian.copyright` are
  a summary of the parts listed in the README of the repository; check them
  before sending the package to an official distribution.

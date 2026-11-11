# Contributing to MBROLA NG

Thank you. The repository is split by how things change:

| Repository | What it holds | Branch |
|------------|---------------|--------|
| [Mbrola-NG](https://github.com/Mbrola-NG/Mbrola-NG) (this one) | the engine and the hosts: NVDA, SAPI, Linux, Android, Apple | `dev` (`master` is for stable releases) |
| [languages](https://github.com/Mbrola-NG/languages) | the language data: one text folder per language | `master` |
| [Resources](https://github.com/Mbrola-NG/Resources) | the voice catalog and the voice samples, **read by the apps at run time** | `main` |

`languages/` and `Resources/` are submodules, so clone with them:

    git clone --recurse-submodules https://github.com/Mbrola-NG/Mbrola-NG
    cd Mbrola-NG && git checkout dev

## Fixing how something is read, or adding a language

This needs no code. Read the README of the
[languages repository](https://github.com/Mbrola-NG/languages): it describes
the files of a language, how to compile and test one, and the short list of
what else a new language needs (an entry in the catalog, a sentence for the
voice sample).

## Checks

Before a pull request:

    python tools/check_catalog.py            validates the voice catalog
    build.cmd x64                            Windows: compiles every language and runs its tests
    linux/build.sh                           Linux: the same, plus the unit tests

The same checks run on GitHub for every pull request. A language whose
`tests.txt` fails does not compile and blocks every build, so add tests for
what you change.

## Rules of thumb

- Work on `dev`. `master` only receives releases.
- What is pushed to the `main` branch of Resources is read by every installed
  copy of the apps within minutes, whatever version it is. Change the catalog
  by pull request and run `tools/check_catalog.py` first. How to replace a
  voice sample is in the README of Resources.
- Contributions are released under the license of the project, the GNU GPL
  version 2 or later. Data from other sources needs a compatible license and
  its notice must stay in the file header.
- MBROLA voices are never added to any repository: each has its own license.

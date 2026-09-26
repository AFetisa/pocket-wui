# Contributing

Thanks for helping. PocketWUI is maintained by one person, so everything goes through review.

- **Bugs:** [open a bug report](https://github.com/AFetisa/pocket-wui/issues/new?template=bug_report.yml). Device, version and connection mode matter most.
- **Features:** [open a feature request](https://github.com/AFetisa/pocket-wui/issues/new?template=feature_request.yml) first. Wait for a 👍 before writing code, so your work isn't wasted on something that won't be merged.
- **Questions:** use [Discussions](https://github.com/AFetisa/pocket-wui/discussions).
- **Security:** report [privately](SECURITY.md), not in an issue.

## Pull requests

1. Fork, branch from `main`, keep the change focused on one issue.
2. Run the tests: `make -C test host` and `make -C test sim` (see [docs/DEVELOPING.md](docs/DEVELOPING.md)).
3. Say in the PR what you tested on: real hardware, the simulator, or host tests only.
4. CI must pass, and the maintainer reviews and merges. By contributing you agree your code is released under the [MIT licence](LICENSE).

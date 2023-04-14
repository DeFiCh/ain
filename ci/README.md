# ci scripts

This directory contains scripts for each build step in each build stage.

Currently defined types:

- `lint`
- `extended_lint`
- `test`
- `sync`

Each stage has its own lifecycle. Every script in here is named
and numbered according to which stage and lifecycle step it belongs to.

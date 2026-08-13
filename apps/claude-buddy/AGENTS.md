## graphify

This project has a graphify knowledge graph at `graphify-out/`.

Rules:
- Before answering architecture or codebase questions, read
  `graphify-out/GRAPH_REPORT.md` for god nodes and community structure.
- If `graphify-out/wiki/index.md` exists, navigate it instead of reading raw
  files.
- After modifying code files in this project, run:

```bash
./scripts/graphify_update.sh
```

The wrapper installs the `graphifyy` PyPI package into the project `.venv` when
needed and then runs the `graphify` CLI. This avoids recurring failures when
`graphify` is not installed globally.

Normal delivery and runtime debugging use root ./cardputer plus Launcher USB MSC and Launcher OTA. scripts/recovery is explicit recovery only.

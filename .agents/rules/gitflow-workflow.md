# GitFlow & Release Workflow Rules

## 1. Branch Hierarchy
- **`main`**: Production releases only. Protected branch. Never commit or push directly.
- **`dev`**: Active development integration. Protected branch. Never commit or push directly.
- **`feat/<feature_name>`** or **`fix/<bug_name>`**: Short-lived feature/bugfix branches branching off `dev`.

## 2. Feature Workflow
1. Fetch latest changes: `git fetch origin dev && git checkout dev && git pull`.
2. Branch out: `git checkout -b feat/<descriptive-name>`.
3. Implement changes, add unit tests, and verify build via devcontainer.
4. Update `README.md` and Z2M converters if hardware/endpoints changed.
5. Commit with Conventional Commits (e.g. `feat(zigbee): add load current reporting`, `fix(sht30): handle crc timeout`).
6. Push and create a Pull Request targeting **`dev`**.

## 3. Release Flow
1. Open a Pull Request from **`dev`** targeting **`main`**.
2. Merging triggers the GitHub Actions workflow to:
   - Run build and test suite.
   - Run semantic-release to bump version and generate changelog.
   - Generate Zigbee OTA firmware artifact (`.zigbee` image) and attach to GitHub Release.

## 4. Pre-Commit Checklist
- [ ] Code builds cleanly with `devcontainer exec --workspace-folder . idf.py -C src build`.
- [ ] Unit tests pass.
- [ ] `README.md` updated if functional behavior or endpoints changed.
- [ ] No temporary debug code or unhandled hardcoded paths.

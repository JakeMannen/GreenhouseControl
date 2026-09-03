---
name: release-and-ota
description: Step-by-step procedure for preparing releases, validating Zigbee OTA firmware artifacts, and ensuring CI compliance.
---

# Release and Zigbee OTA Workflow Guide

This skill guides the preparation of formal releases, semantic version bumping, and Zigbee OTA binary image generation.

---

## 1. Automated Release Pipeline

The project uses GitHub Actions with `semantic-release` to automate version tags and OTA artifact creation when PRs merge from `dev` into `main`.

### Workflow Stages:
1. **Branch Verification**: Ensures PR targets the correct branch (`feat/*` $\rightarrow$ `dev`, `dev` $\rightarrow$ `main`).
2. **Build Verification**: Builds firmware in the container.
3. **OTA Image Packaging**: Converts `greenhouse_controller.bin` into a Zigbee OTA formatted file (with standard OTA header: Manufacturer Code `0x1001`, Image Type `0x1011`, File Version).
4. **GitHub Release**: Creates a GitHub tag and release with changelog and `.zigbee` binary attachment.

---

## 2. Preparing a Feature PR to `dev`

1. Ensure the feature branch is created from latest `dev`:
   ```bash
   git checkout dev
   git pull origin dev
   git checkout -b feat/my-new-feature
   ```
2. Validate the build:
   ```powershell
   devcontainer exec --workspace-folder . idf.py -C src build
   ```
3. Update [README.md](file:///README.md) if pinouts, clusters, or commands were added/changed.
4. Update `Zigbee2Mqtt/external_converters/greenhouse_controller.js` if telemetry was modified.
5. Create a Pull Request with a Conventional Commit title:
   - `feat(sensor): support secondary soil moisture probe`
   - `fix(ota): correct OTA client cluster response`
   - `docs(readme): add wiring schematic for Victron VE.Direct`

---

## 3. Preparing a Production Release to `main`

1. Open a PR from `dev` targeting `main` with title: `chore(release): merge dev to main`.
2. Ensure all automated CI checks pass.
3. Merging triggers the automatic release and publishes the OTA firmware.

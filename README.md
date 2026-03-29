# AirSim-Ubuntu-UE5.7

Thanks [Colosseum](https://github.com/CodexLabsLLC/Colosseum/).

This repository is the release bundle for the Ubuntu + Unreal Engine 5.7 AirSim integration validated in this workspace.

It contains three parts:

- a pip-installable Python client package that keeps the usual `import airsim`
- the Unreal plugin source and content under `Unreal/Plugins/AirSim`
- example settings and integration docs

## Repository Layout

- `airsim/`
  Python client package
- `setup.py`, `pyproject.toml`
  package metadata so `pip install git+...` works directly from this repository
- `Unreal/Plugins/AirSim/`
  Unreal plugin to copy into your UE project
- `examples/settings/`
  validated example settings files
- `docs/`
  integration, extension API, and validation notes

## Python Client

Install directly from GitHub:

```bash
pip install "git+https://github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git"
```

Or over SSH:

```bash
pip install "git+ssh://git@github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git"
```

Minimal usage:

```python
import airsim

client = airsim.VehicleClient()
client.confirmConnection()
```

The module name remains `airsim`, so existing scripts do not need to change imports.

Current local extensions exposed by the Python client include:

- `simAddLabel(pose, size, color_rgba)`
- `simDestroyLabel(label_id)`
- `simDestroyAllLabels()`
- `simNotifyImageCapturesSceneChanged()`

If you need `write_png()`, install the optional OpenCV extra:

```bash
pip install "git+ssh://git@github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git#egg=airsim-ubuntu-ue5-7[opencv]"
```

## Unreal Plugin

Copy:

- `Unreal/Plugins/AirSim`

into:

- `<YourProject>/Plugins/AirSim`

Then build your project and use:

- `/Script/AirSim.AirSimGameMode`

as the generic AirSim game mode for portability validation.

For the first smoke test, prefer:

- `/Engine/Maps/Templates/OpenWorld.OpenWorld`

instead of the plugin sample map.

## Example Settings

Included example files:

- `examples/settings/smoke.multirotor.json`
- `examples/settings/smoke.computervision.json`
- `examples/settings/settings.multirotor.lowq.json`
- `examples/settings/settings.computervision.lowq.json`

Recommended usage:

- use the `smoke.*.json` files for a clean-project first validation
- use the `settings.*.lowq.json` files as a reference for the current CitySample-oriented low-quality profile

## Validation Status

Validated in the UE5.7 Linux workspace through `2026-03-29`:

- `Multirotor` control and capture path working
- `ComputerVision` mode working with live pose-driven image refresh
- clean-project reuse validated on a fresh UE project using `OpenWorld + AirSimGameMode`
- camera-relative transform and additional camera settings exposed through config
- label add/delete APIs and explicit scene-refresh notify API working
- `DepthPlanar` repaired to use native scene depth instead of lighting-coupled post-process output
- `DepthPlanar` metric float readback and synthesized PNG visualization both repaired for UE5.7

## Docs

- [Quickstart](docs/quickstart.md)
- [Camera Config And Label APIs](docs/camera-config-and-label-apis.md)
- [Depth Planar Fix](docs/depth-planar-fix.md)
- [Validation Summary](docs/validation-summary.md)

## Packaging Notes

This repository intentionally excludes generated Unreal build artifacts and unnecessary Win64 prebuilt libraries from the published plugin tree:

- no `Intermediate/`
- no `Binaries/`
- no historical local `.bak*` files
- no unused Win64 `.lib/.pdb/.bsc/.idb` dependency payloads

That keeps the repo smaller while preserving the validated Linux UE5.7 source plugin.

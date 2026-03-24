# AirSim-Ubuntu-UE5.7

Standalone Python client package for the AirSim integration used in this Ubuntu + Unreal Engine 5.7 workspace.

It keeps the usual `import airsim` workflow and includes the locally added RPC wrappers used by the current simulator build:

- `simAddLabel(pose, size, color_rgba)`
- `simDestroyLabel(label_id)`
- `simDestroyAllLabels()`
- `simNotifyImageCapturesSceneChanged()`

The package is intended for direct installation from GitHub:

```bash
pip install "git+ssh://git@github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git"
```

## Install

```bash
pip install "git+ssh://git@github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git"
```

## Usage

```python
import airsim

client = airsim.VehicleClient()
client.confirmConnection()
```

## Notes

- Validated in the UE5.7 workspace on `2026-03-24`.
- Tested with Python `3.10` and `3.12`.
- This repository packages the Python client only. It does not contain the Unreal plugin / C++ simulator code.
- The module name remains `airsim`, so existing scripts can continue using `import airsim`.
- `write_png()` uses OpenCV only when called. If you need that helper, install the optional extra:

```bash
pip install "git+ssh://git@github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git#egg=airsim-ubuntu-ue5-7[opencv]"
```

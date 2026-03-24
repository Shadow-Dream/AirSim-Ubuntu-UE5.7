# AirSim-Ubuntu-UE5.7

Standalone Python client package for the AirSim integration used in this Ubuntu + Unreal Engine 5.7 workspace.

It keeps the usual `import airsim` workflow and includes the locally added RPC wrappers:

- `simAddLabel(pose, size, color_rgba)`
- `simDestroyLabel(label_id)`
- `simDestroyAllLabels()`
- `simNotifyImageCapturesSceneChanged()`

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

- Tested in this workspace with Python `3.10`.
- This repository packages the Python client only. It does not contain the Unreal plugin / C++ simulator code.
- `write_png()` uses OpenCV only when called. If you need that helper, install the optional extra:

```bash
pip install "git+ssh://git@github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git#egg=airsim-ubuntu-ue5-7[opencv]"
```

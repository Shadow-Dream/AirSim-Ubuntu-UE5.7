# Quickstart

## Scope

This guide is for reusing the plugin and Python client outside the original CitySample workspace.

Validated baseline:

- Unreal Engine `5.7`
- Linux
- generic map: `/Engine/Maps/Templates/OpenWorld.OpenWorld`
- generic game mode: `/Script/AirSim.AirSimGameMode`

## 1. Install The Python Client

```bash
pip install "git+https://github.com/Shadow-Dream/AirSim-Ubuntu-UE5.7.git"
```

## 2. Copy The Unreal Plugin

Copy:

- `Unreal/Plugins/AirSim`

into your project:

- `<YourProject>/Plugins/AirSim`

Do not copy generated artifacts from another workspace.

This repository already excludes:

- `Intermediate/`
- `Binaries/`
- local backup files

## 3. Prefer A Small C++ Project Shell

For Linux CLI build and deterministic editor-target generation, a minimal C++ project shell is recommended.

A pure Blueprint blank project is not the best first reuse target for this workflow.

## 4. Minimal Project Config

Add this to `Config/DefaultEngine.ini`:

```ini
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Engine/Maps/Templates/OpenWorld.OpenWorld
EditorStartupMap=/Engine/Maps/Templates/OpenWorld.OpenWorld
GlobalDefaultGameMode=/Script/AirSim.AirSimGameMode
bUseSplitscreen=False

[/Script/PythonScriptPlugin.PythonScriptPluginSettings]
bRemoteExecution=True
RemoteExecutionMulticastBindAddress=0.0.0.0
```

Notes:

- `GlobalDefaultGameMode` should point to the generic AirSim game mode for portability checks
- the `PythonScriptPlugin` section is only needed if you want editor automation like the one used in this workspace

## 5. Choose A Settings File

First smoke test:

- `examples/settings/smoke.multirotor.json`
- `examples/settings/smoke.computervision.json`

Current low-quality profile reference:

- `examples/settings/settings.multirotor.lowq.json`
- `examples/settings/settings.computervision.lowq.json`

## 6. First Validation Path

Recommended first test:

1. launch the project on `OpenWorld`
2. enter PIE
3. confirm AirSim RPC is listening on `41451`
4. connect with the Python client
5. capture one `Scene` image
6. for `ComputerVision`, set a new pose and capture again

Avoid using the plugin sample map as the first portability smoke target.

Known issue:

- `/AirSim/AirSimAssets` carries legacy baggage in this workspace and is not the recommended first smoke map

## 7. Minimal Python Smoke

### Multirotor

```python
import airsim

client = airsim.MultirotorClient()
client.confirmConnection()
client.enableApiControl(True, vehicle_name="drone_1")
client.armDisarm(True, vehicle_name="drone_1")
png = client.simGetImage("0", airsim.ImageType.Scene, vehicle_name="drone_1")
```

### ComputerVision

```python
import airsim

client = airsim.VehicleClient()
client.confirmConnection()

pose = airsim.Pose(
    airsim.Vector3r(0, 0, -30),
    airsim.to_quaternion(0, 0, 0),
)
client.simSetVehiclePose(pose, True, vehicle_name="cv_1")
png = client.simGetImage("0", airsim.ImageType.Scene, vehicle_name="cv_1")
```

## 8. Practical Boundary

What this repository validates:

- the AirSim plugin runtime is not hard-bound to the original CitySample geometry
- `Multirotor` and `ComputerVision` can run in a fresh project

What still depends on your target scene:

- spawn placement
- collision environment
- camera framing
- scene-specific performance and content quality

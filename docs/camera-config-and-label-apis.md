# Camera Config And Label APIs

## Camera Config

This integration keeps the upstream AirSim camera transform model:

- `X`, `Y`, `Z`
- `Pitch`, `Roll`, `Yaw`
- `CaptureSettings`

That means camera pose relative to the drone can be configured from the AirSim settings JSON.

Example:

```json
{
  "Vehicles": {
    "drone_1": {
      "Cameras": {
        "front_custom": {
          "X": 0.46,
          "Y": 0.0,
          "Z": 0.0,
          "Pitch": 0,
          "Roll": 0,
          "Yaw": 0
        }
      }
    }
  }
}
```

## Additional Camera Settings

This workspace also supports an optional local extension block:

- `CineCameraSettings`

Supported keys:

- `LensPresetName`
- `FilmbackPresetName`
- `SensorWidth`
- `SensorHeight`
- `FocalLength`
- `EnableManualFocus`
- `FocusDistance`
- `FocusAperture`
- `EnableFocusPlane`

Example:

```json
{
  "Vehicles": {
    "drone_1": {
      "Cameras": {
        "front_custom": {
          "CineCameraSettings": {
            "FocalLength": 35.0,
            "FocusDistance": 1200.0,
            "FocusAperture": 5.6
          }
        }
      }
    }
  }
}
```

## Label APIs

This repository includes three AirSim-style label APIs plus an explicit scene-refresh API:

- `simAddLabel(pose, size, color_rgba) -> str`
- `simDestroyLabel(label_id) -> bool`
- `simDestroyAllLabels() -> int`
- `simNotifyImageCapturesSceneChanged() -> None`

Label properties:

- simple cube mesh
- unlit
- translucent
- no collision
- no overlap events
- no shadows

## Why Notify Is Explicit

Label mutation and image refresh are intentionally decoupled.

Adding or deleting a label changes the scene, but does not automatically force the next `Scene` capture to refresh.

If you need a guaranteed fresh `Scene` frame after label edits, explicitly call:

```python
client.simNotifyImageCapturesSceneChanged()
```

Recommended sequence:

```python
label_id = client.simAddLabel(
    airsim.Pose(
        airsim.Vector3r(10.0, 0.0, -20.0),
        airsim.to_quaternion(0.0, 0.0, 0.0),
    ),
    airsim.Vector3r(1.0, 1.0, 1.0),
    [0.0, 1.0, 0.8, 1.0],
)

client.simNotifyImageCapturesSceneChanged()
png = client.simGetImage("0", airsim.ImageType.Scene, vehicle_name="cv_1")
```

## Notes

- `color_rgba` uses normalized floats
- `size` is in meters
- nonexistent label ids return `false` from `simDestroyLabel`
- `simDestroyAllLabels` returns the removed count
- `StrictSceneFreshnessAfterPoseChange` remains useful for `ComputerVision` pose-switch workflows

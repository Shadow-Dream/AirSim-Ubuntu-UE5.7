# Validation Summary

## Current Validated State

Validated through `2026-03-29` in the UE5.7 Linux workspace:

- `Multirotor` mode is usable
- `ComputerVision` mode is usable
- `DepthPlanar` is back on a geometric depth path instead of a lighting-coupled image path
- the Python client package installs and imports in a clean Python `3.12` virtual environment
- the plugin can be migrated into a clean Unreal project

## Clean Project Reuse Validation

A fresh smoke project was created outside the original CitySample project and validated with:

- map: `/Engine/Maps/Templates/OpenWorld.OpenWorld`
- game mode: `/Script/AirSim.AirSimGameMode`

Results:

- editor build succeeded
- `Multirotor` entered PIE, opened RPC `41451`, captured images, and responded to takeoff
- `ComputerVision` entered PIE, responded to `simSetVehiclePose`, and returned different images after pose changes

Practical conclusion:

- the current AirSim runtime and capture path are reusable outside the original CitySample scene
- but target scenes still need sane spawn placement and camera framing

## Current Working Extensions

Validated local extensions:

- camera-relative transform from settings
- additional `CineCameraSettings`
- label add/delete APIs
- explicit `simNotifyImageCapturesSceneChanged()`
- native `DepthPlanar` scene-depth capture with local `cm -> m` conversion
- UE5.7-safe PNG depth visualization export without deprecated thumbnail compression

## Important Limits

- do not treat the plugin sample map as the recommended first portability smoke test
- prefer `OpenWorld + AirSimGameMode` for first validation
- scene-agnostic does not mean every arbitrary map will have good default spawn placement

## Python Package Validation

The repository root was installed in a clean Python `3.12` virtual environment and validated for:

- `import airsim`
- `airsim.__version__`
- `VehicleClient.simAddLabel`
- `VehicleClient.simNotifyImageCapturesSceneChanged`
- `VehicleClient.simSetVehiclePose`
- `VehicleClient.simSetCameraPose`

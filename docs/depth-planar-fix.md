# Depth Planar Fix

## Problem

`DepthPlanar` had regressed into a lighting-coupled image path in the UE5.7 CitySample workspace:

- sky brightness and emissive regions distorted the depth output
- reflective water could produce obviously non-geometric depth changes
- a partial intermediate fix stopped the lighting coupling, but float depth values were still clipped too aggressively
- the non-float depth visualization path was also using deprecated UE thumbnail compression

## Published Fix

The published plugin now carries the validated repair:

1. `DepthPlanar` capture uses native `SCS_SceneDepth`
2. native UE scene depth is converted from `cm` back to AirSim `m`
3. UE no-hit scene-depth sentinel values are remapped to a large AirSim-side reject sentinel for visualization
4. synthesized depth PNG export uses `UAirBlueprintLib::CompressImageArray(...)` instead of deprecated thumbnail compression

Other analytic captures keep their previous HDR path:

- `DepthPerspective`
- `DepthVis`
- `DisparityNormalized`

## Practical Effect

Expected behavior after this fix:

- `DepthPlanar` no longer tracks scene lighting
- float `DepthPlanar` values keep a usable metric range instead of collapsing near `0..1`
- non-float depth images are emitted as real PNG payloads
- the same shared code path works for both `Multirotor` and `ComputerVision`

## Files

Core source files in this release:

- `Unreal/Plugins/AirSim/Source/PIPCamera.cpp`
- `Unreal/Plugins/AirSim/Source/UnrealImageCapture.cpp`

## Notes

- this fix does not change the Python API surface
- callers can keep using the usual `simGetImage(...)` or `simGetImages(...)` requests for `DepthPlanar`
- if future depth output starts looking like scene brightness again, check the depth capture source first before changing RGB settings

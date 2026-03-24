#include "ComputerVisionPawnSimApi.h"

#include "AirBlueprintLib.h"

ComputerVisionPawnSimApi::ComputerVisionPawnSimApi(const Params& params)
    : PawnSimApi(params)
{
}

void ComputerVisionPawnSimApi::setPose(const Pose& pose, bool ignore_collision)
{
    UAirBlueprintLib::RunCommandOnGameThread([this, pose, ignore_collision]() {
        setPoseInternal(pose, ignore_collision);

        const UnrealImageCapture* ImageCapture = getImageCapture();
        if (ImageCapture != nullptr) {
            ImageCapture->notifyCameraPoseChanged();
        }
    }, true);
}

#pragma once

#include "PawnSimApi.h"

class ComputerVisionPawnSimApi : public PawnSimApi
{
public:
    explicit ComputerVisionPawnSimApi(const Params& params);

    virtual void setPose(const Pose& pose, bool ignore_collision) override;
};

#pragma once

#include <vector>
#include <algorithm>
#include <limits>
#include "Params.hpp"
#include "interfaces/CommonStructs.hpp"

namespace simple_flight
{

class Mixer
{
public:
    Mixer(const Params* params)
        : params_(params)
    {
    }

    void getMotorOutput(const Axis4r& controls, std::vector<float>& motor_outputs) const
    {
        const float min_motor_output = params_->motor.min_motor_output;
        const float max_motor_output = params_->motor.max_motor_output;

        if (controls.throttle() < params_->motor.min_angling_throttle) {
            motor_outputs.assign(params_->motor.motor_count, controls.throttle());
            return;
        }

        const float base_throttle = std::max(min_motor_output,
                                             std::min(controls.throttle(), max_motor_output));

        float max_attitude = -std::numeric_limits<float>::infinity();
        float min_attitude = std::numeric_limits<float>::infinity();
        float attitude_terms[kMotorCount];

        for (int motor_index = 0; motor_index < kMotorCount; ++motor_index) {
            const float attitude_term = controls.pitch() * mixerQuadX[motor_index].pitch +
                                        controls.roll() * mixerQuadX[motor_index].roll +
                                        controls.yaw() * mixerQuadX[motor_index].yaw;
            attitude_terms[motor_index] = attitude_term;
            max_attitude = std::max(max_attitude, attitude_term);
            min_attitude = std::min(min_attitude, attitude_term);
        }

        float attitude_scale = 1.0f;
        const float upper_headroom = max_motor_output - base_throttle;
        const float lower_headroom = base_throttle - min_motor_output;

        if (max_attitude > upper_headroom && max_attitude > 0.0f)
            attitude_scale = std::min(attitude_scale, upper_headroom / max_attitude);
        if (-min_attitude > lower_headroom && min_attitude < 0.0f)
            attitude_scale = std::min(attitude_scale, lower_headroom / (-min_attitude));

        attitude_scale = std::max(0.0f, attitude_scale);
        motor_outputs.resize(kMotorCount);
        for (int motor_index = 0; motor_index < kMotorCount; ++motor_index) {
            motor_outputs[motor_index] = base_throttle + attitude_terms[motor_index] * attitude_scale;
            motor_outputs[motor_index] = std::max(min_motor_output,
                                                  std::min(motor_outputs[motor_index], max_motor_output));
        }
    }

private:
    static constexpr int kMotorCount = 4;

    const Params* params_;

    // Custom mixer data per motor
    typedef struct motorMixer_t
    {
        float throttle;
        float roll;
        float pitch;
        float yaw;
    } motorMixer_t;

    //only thing that this matrix does is change the sign
    const motorMixer_t mixerQuadX[4] = {
        //QuadX config
        { 1.0f, -1.0f, 1.0f, 1.0f }, // FRONT_R
        { 1.0f, 1.0f, -1.0f, 1.0f }, // REAR_L
        { 1.0f, 1.0f, 1.0f, -1.0f }, // FRONT_L
        { 1.0f, -1.0f, -1.0f, -1.0f }, // REAR_R
    };
};

} //namespace

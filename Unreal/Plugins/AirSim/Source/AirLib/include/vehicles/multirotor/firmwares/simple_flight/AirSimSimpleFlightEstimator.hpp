// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef msr_airlib_AirSimSimpleFlightEstimator_hpp
#define msr_airlib_AirSimSimpleFlightEstimator_hpp

#include "firmware/interfaces/CommonStructs.hpp"
#include "AirSimSimpleFlightCommon.hpp"
#include "physics/Kinematics.hpp"
#include "physics/Environment.hpp"
#include "common/Common.hpp"

#include <chrono>

namespace msr
{
namespace airlib
{

    class AirSimSimpleFlightEstimator : public simple_flight::IStateEstimator
    {
    public:
        virtual ~AirSimSimpleFlightEstimator() {}

        //for now we don't do any state estimation and use ground truth (i.e. assume perfect sensors)
        void setGroundTruthKinematics(const Kinematics::State* kinematics, const Environment* environment)
        {
            const bool source_changed = kinematics_ != kinematics || environment_ != environment;
            kinematics_ = kinematics;
            environment_ = environment;

            if (kinematics_ == nullptr) {
                linear_velocity_initialized_ = false;
                cached_linear_velocity_ = simple_flight::Axis3r();
                return;
            }

            if (source_changed) {
                last_position_ = kinematics_->pose.position;
                last_position_sample_time_ = std::chrono::steady_clock::now();
                cached_linear_velocity_ = AirSimSimpleFlightCommon::toAxis3r(kinematics_->twist.linear);
                linear_velocity_initialized_ = true;
            }
        }

        virtual simple_flight::Axis3r getAngles() const override
        {
            simple_flight::Axis3r angles;
            VectorMath::toEulerianAngle(kinematics_->pose.orientation,
                                        angles.pitch(),
                                        angles.roll(),
                                        angles.yaw());

            //Utils::log(Utils::stringf("Ang Est:\t(%f, %f, %f)", angles.pitch(), angles.roll(), angles.yaw()));

            return angles;
        }

        virtual simple_flight::Axis3r getAngularVelocity() const override
        {
            const auto& anguler = kinematics_->twist.angular;

            simple_flight::Axis3r conv;
            conv.x() = anguler.x();
            conv.y() = anguler.y();
            conv.z() = anguler.z();

            return conv;
        }

        virtual simple_flight::Axis3r getPosition() const override
        {
            return AirSimSimpleFlightCommon::toAxis3r(kinematics_->pose.position);
        }

        virtual simple_flight::Axis3r transformToBodyFrame(const simple_flight::Axis3r& world_frame_val) const override
        {
            const Vector3r& vec = AirSimSimpleFlightCommon::toVector3r(world_frame_val);
            const Vector3r& trans = VectorMath::transformToBodyFrame(vec, kinematics_->pose.orientation);
            return AirSimSimpleFlightCommon::toAxis3r(trans);
        }

        virtual simple_flight::Axis3r getLinearVelocity() const override
        {
            if (kinematics_ == nullptr)
                return simple_flight::Axis3r();

            const simple_flight::Axis3r raw_velocity = AirSimSimpleFlightCommon::toAxis3r(kinematics_->twist.linear);
            const Vector3r& position = kinematics_->pose.position;
            const auto now = std::chrono::steady_clock::now();

            if (!linear_velocity_initialized_) {
                last_position_ = position;
                last_position_sample_time_ = now;
                cached_linear_velocity_ = raw_velocity;
                linear_velocity_initialized_ = true;
                return cached_linear_velocity_;
            }

            const double dt = std::chrono::duration<double>(now - last_position_sample_time_).count();
            if (dt <= 1.0e-4)
                return cached_linear_velocity_;

            const Vector3r delta = position - last_position_;
            const double distance = delta.norm();
            constexpr double kPositionEpsilon = 1.0e-4;

            if (distance <= kPositionEpsilon) {
                if (dt >= 0.1) {
                    cached_linear_velocity_.x() *= 0.5f;
                    cached_linear_velocity_.y() *= 0.5f;
                    cached_linear_velocity_.z() *= 0.5f;
                    last_position_sample_time_ = now;
                }
                return cached_linear_velocity_;
            }

            last_position_ = position;
            last_position_sample_time_ = now;

            if (distance > 5.0) {
                cached_linear_velocity_ = raw_velocity;
                return cached_linear_velocity_;
            }

            const Vector3r estimated_velocity = delta / dt;
            constexpr float alpha = 0.6f;
            cached_linear_velocity_.x() = cached_linear_velocity_.x() + alpha * static_cast<float>(estimated_velocity.x() - cached_linear_velocity_.x());
            cached_linear_velocity_.y() = cached_linear_velocity_.y() + alpha * static_cast<float>(estimated_velocity.y() - cached_linear_velocity_.y());
            cached_linear_velocity_.z() = cached_linear_velocity_.z() + alpha * static_cast<float>(estimated_velocity.z() - cached_linear_velocity_.z());
            return cached_linear_velocity_;
        }

        virtual simple_flight::Axis4r getOrientation() const override
        {
            return AirSimSimpleFlightCommon::toAxis4r(kinematics_->pose.orientation);
        }

        virtual simple_flight::GeoPoint getGeoPoint() const override
        {
            return AirSimSimpleFlightCommon::toSimpleFlightGeoPoint(environment_->getState().geo_point);
        }

        virtual simple_flight::GeoPoint getHomeGeoPoint() const override
        {
            return AirSimSimpleFlightCommon::toSimpleFlightGeoPoint(environment_->getHomeGeoPoint());
        }

        virtual simple_flight::KinematicsState getKinematicsEstimated() const override
        {
            simple_flight::KinematicsState state;
            state.position = getPosition();
            state.orientation = getOrientation();
            state.linear_velocity = getLinearVelocity();
            state.angular_velocity = getAngularVelocity();
            state.linear_acceleration = AirSimSimpleFlightCommon::toAxis3r(kinematics_->accelerations.linear);
            state.angular_acceleration = AirSimSimpleFlightCommon::toAxis3r(kinematics_->accelerations.angular);

            return state;
        }

    private:
        const Kinematics::State* kinematics_ = nullptr;
        const Environment* environment_ = nullptr;
        mutable bool linear_velocity_initialized_ = false;
        mutable Vector3r last_position_ = Vector3r::Zero();
        mutable std::chrono::steady_clock::time_point last_position_sample_time_;
        mutable simple_flight::Axis3r cached_linear_velocity_ = simple_flight::Axis3r();
    };
}
} //namespace
#endif

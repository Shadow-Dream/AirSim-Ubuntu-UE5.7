#pragma once

#include <cstdint>
#include "interfaces/ICommLink.hpp"
#include "interfaces/IGoal.hpp"
#include "interfaces/IOffboardApi.hpp"
#include "interfaces/IUpdatable.hpp"
#include "interfaces/CommonStructs.hpp"
#include "RemoteControl.hpp"
#include "Params.hpp"

namespace simple_flight
{

class OffboardApi : public IUpdatable
    , public IOffboardApi
{
public:
    OffboardApi(const Params* params, const IBoardClock* clock, const IBoardInputPins* board_inputs,
                IStateEstimator* state_estimator, ICommLink* comm_link)
        : params_(params), rc_(params, clock, board_inputs, &vehicle_state_, state_estimator, comm_link), board_inputs_(board_inputs), state_estimator_(state_estimator), comm_link_(comm_link), clock_(clock), has_api_control_(false), is_api_timedout_(false), landed_(true), takenoff_(false), grounded_position_z_(0)
    {
    }

    virtual void reset() override
    {
        IUpdatable::reset();

        vehicle_state_.setState(params_->default_vehicle_state, state_estimator_->getGeoPoint());
        rc_.reset();
        has_api_control_ = false;
        is_api_timedout_ = false;
        landed_ = true;
        takenoff_ = false;
        grounded_position_z_ = state_estimator_->getPosition().z();
        goal_timestamp_ = clock_->millis();
        updateGoalFromRcOrHold();
    }

    virtual void update() override
    {
        IUpdatable::update();

        rc_.update();
        if (!has_api_control_)
            updateGoalFromRcOrHold();
        else {
            if (takenoff_ &&
                (clock_->millis() - goal_timestamp_ > params_->api_goal_timeout)) {
                if (!is_api_timedout_) {
                    comm_link_->log("API call was not received, entering hover mode for safety");
                    goal_mode_ = GoalMode::getPositionMode();
                    goal_ = Axis4r::xyzToAxis4(state_estimator_->getPosition(), true);
                    is_api_timedout_ = true;
                }

                //do not update goal_timestamp_
            }
        }
        //else leave the goal set by IOffboardApi API

        detectLanding();
        detectTakingOff();
    }

    /**************** IOffboardApi ********************/

    virtual const Axis4r& getGoalValue() const override
    {
        return goal_;
    }

    virtual const GoalMode& getGoalMode() const override
    {
        return goal_mode_;
    }

    virtual bool canRequestApiControl(std::string& message) override
    {
        if (rc_.allowApiControl())
            return true;
        else {
            message = "Remote Control switch position disallows API control";
            comm_link_->log(message, ICommLink::kLogLevelError);
            return false;
        }
    }
    virtual bool hasApiControl() override
    {
        return has_api_control_;
    }
    virtual bool requestApiControl(std::string& message) override
    {
        if (canRequestApiControl(message)) {
            has_api_control_ = true;
            is_api_timedout_ = false;
            goal_timestamp_ = clock_->millis();

            //with API-only local-input disablement, preserve current hover/position instead of switching to neutral RC throttle
            updateGoalFromRcOrHold();

            comm_link_->log("requestApiControl was successful", ICommLink::kLogLevelInfo);

            return true;
        }
        else {
            comm_link_->log("requestApiControl failed", ICommLink::kLogLevelError);
            return false;
        }
    }
    virtual void releaseApiControl() override
    {
        has_api_control_ = false;
        is_api_timedout_ = false;
        goal_timestamp_ = clock_->millis();
        updateGoalFromRcOrHold();
        comm_link_->log("releaseApiControl was successful", ICommLink::kLogLevelInfo);
    }
    virtual bool setGoalAndMode(const Axis4r* goal, const GoalMode* goal_mode, std::string& message) override
    {
        if (has_api_control_) {
            if (goal != nullptr)
                goal_ = *goal;
            if (goal_mode != nullptr)
                goal_mode_ = *goal_mode;
            goal_timestamp_ = clock_->millis();
            is_api_timedout_ = false;
            return true;
        }
        else {
            message = "requestApiControl() must be called before using API control";
            comm_link_->log(message, ICommLink::kLogLevelError);
            return false;
        }
    }

    virtual bool arm(std::string& message) override
    {
        if (has_api_control_) {
            if (vehicle_state_.getState() == VehicleStateType::Armed) {
                message = "Vehicle is already armed";
                comm_link_->log(message, ICommLink::kLogLevelInfo);
                return true;
            }
            else if ((vehicle_state_.getState() == VehicleStateType::Inactive || vehicle_state_.getState() == VehicleStateType::Disarmed || vehicle_state_.getState() == VehicleStateType::BeingDisarmed)) {

                vehicle_state_.setState(VehicleStateType::Armed, state_estimator_->getHomeGeoPoint());
                goal_ = Axis4r(0, 0, 0, params_->rc.min_angling_throttle);
                goal_mode_ = GoalMode::getAllRateMode();

                message = "Vehicle is armed";
                comm_link_->log(message, ICommLink::kLogLevelInfo);
                return true;
            }
            else {
                message = "Vehicle cannot be armed because it is not in Inactive, Disarmed or BeingDisarmed state";
                comm_link_->log(message, ICommLink::kLogLevelError);
                return false;
            }
        }
        else {
            message = "Vehicle cannot be armed via API because API has not been given control";
            comm_link_->log(message, ICommLink::kLogLevelError);
            return false;
        }
    }

    virtual bool disarm(std::string& message) override
    {
        if (has_api_control_ && (vehicle_state_.getState() == VehicleStateType::Active || vehicle_state_.getState() == VehicleStateType::Armed || vehicle_state_.getState() == VehicleStateType::BeingArmed)) {

            vehicle_state_.setState(VehicleStateType::Disarmed);
            goal_ = Axis4r(0, 0, 0, 0);
            goal_mode_ = GoalMode::getAllRateMode();

            message = "Vehicle is disarmed";
            comm_link_->log(message, ICommLink::kLogLevelInfo);
            return true;
        }
        else {
            message = "Vehicle cannot be disarmed because it is not in Active, Armed or BeingArmed state";
            comm_link_->log(message, ICommLink::kLogLevelError);
            return false;
        }
    }

    virtual VehicleStateType getVehicleState() const override
    {
        return vehicle_state_.getState();
    }

    virtual const IStateEstimator& getStateEstimator() override
    {
        return *state_estimator_;
    }

    virtual GeoPoint getHomeGeoPoint() const override
    {
        return state_estimator_->getHomeGeoPoint();
    }

    virtual GeoPoint getGeoPoint() const override
    {
        return state_estimator_->getGeoPoint();
    }

    virtual bool getLandedState() const override
    {
        return landed_;
    }

private:
    bool shouldHoldCurrentPosition() const
    {
        return board_inputs_->isLocalInputDisabled() && (takenoff_ || !landed_);
    }

    void updateGoalFromRc()
    {
        goal_ = rc_.getGoalValue();
        goal_mode_ = rc_.getGoalMode();
    }

    void updateGoalFromRcOrHold()
    {
        if (shouldHoldCurrentPosition()) {
            goal_mode_ = GoalMode::getPositionMode();
            goal_ = Axis4r::xyzToAxis4(state_estimator_->getPosition(), true);
        }
        else
            updateGoalFromRc();
    }

    bool isNearGroundAltitude() const
    {
        return state_estimator_->getPosition().z() >= grounded_position_z_ - kLandingAltitudeTolerance;
    }

    void detectLanding()
    {

        // if we are not trying to move by setting motor outputs
        if (takenoff_) {
            float checkThrottle = rc_.getMotorOutput();
            auto angular = state_estimator_->getAngularVelocity();
            auto velocity = state_estimator_->getLinearVelocity();
            const bool is_stationary = isAlmostZero(angular.roll()) && isAlmostZero(angular.pitch()) && isAlmostZero(angular.yaw()) &&
                                       isAlmostZero(velocity.x()) && isAlmostZero(velocity.y()) && isAlmostZero(velocity.z());
            const bool near_ground = isNearGroundAltitude();
            const bool low_throttle = !isGreaterThanArmedThrottle(checkThrottle);
            if (near_ground && is_stationary && (low_throttle || !has_api_control_)) {
                landed_ = true;
                takenoff_ = false;
                grounded_position_z_ = state_estimator_->getPosition().z();
            }
        }
    }

    void detectTakingOff()
    {
        // if we are not trying to move by setting motor outputs
        if (!takenoff_) {
            float checkThrottle = rc_.getMotorOutput();
            const bool has_lifted_from_ground = state_estimator_->getPosition().z() < grounded_position_z_ - kLandingAltitudeTolerance;
            const bool has_meaningful_vertical_motion = std::abs(state_estimator_->getLinearVelocity().z()) > 0.01f;
            //TODO: better handling of landed & takenoff states
            if (has_lifted_from_ground ||
                (isGreaterThanArmedThrottle(checkThrottle) && has_meaningful_vertical_motion)) {
                takenoff_ = true;
                landed_ = false;
            }
        }
    }

    bool isAlmostZero(float v)
    {
        return std::abs(v) < kMovementTolerance;
    }
    bool isGreaterThanArmedThrottle(float throttle)
    {
        return throttle > params_->min_armed_throttle();
    }

private:
    const TReal kMovementTolerance = (TReal)0.08;
    const TReal kLandingAltitudeTolerance = (TReal)0.25;
    const Params* params_;
    RemoteControl rc_;
    const IBoardInputPins* board_inputs_;
    IStateEstimator* state_estimator_;
    ICommLink* comm_link_;
    const IBoardClock* clock_;

    VehicleState vehicle_state_;

    Axis4r goal_;
    GoalMode goal_mode_;
    uint64_t goal_timestamp_;

    bool has_api_control_;
    bool is_api_timedout_;
    bool landed_, takenoff_;
    TReal grounded_position_z_;
};

} //namespace

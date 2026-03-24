#include "MultirotorPawnSimApi.h"
#include "AirBlueprintLib.h"
#include "vehicles/multirotor/MultiRotorParamsFactory.hpp"
#include "UnrealSensors/UnrealSensorFactory.h"
#include "vehicles/multirotor/api/MultirotorCommon.hpp"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include <exception>

using namespace msr::airlib;

DEFINE_LOG_CATEGORY_STATIC(LogAirSimMultirotorControl, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarAirSimMultirotorControlLog(
    TEXT("AirSim.Multirotor.ControlLog"),
    0,
    TEXT("Enable multirotor control-source and applied-RC logs in the Output Log."));

TAutoConsoleVariable<int32> CVarAirSimMultirotorStateLog(
    TEXT("AirSim.Multirotor.StateLog"),
    0,
    TEXT("Enable multirotor state logs in the Output Log."));

TAutoConsoleVariable<float> CVarAirSimMultirotorControlLogPeriod(
    TEXT("AirSim.Multirotor.ControlLogPeriod"),
    0.20f,
    TEXT("Minimum time in seconds between applied-control logs."));

TAutoConsoleVariable<float> CVarAirSimMultirotorStateLogPeriod(
    TEXT("AirSim.Multirotor.StateLogPeriod"),
    0.50f,
    TEXT("Minimum time in seconds between multirotor state logs."));

TAutoConsoleVariable<float> CVarAirSimMultirotorVisualSyncCorrectionThreshold(
    TEXT("AirSim.Multirotor.VisualSyncCorrectionThreshold"),
    0.10f,
    TEXT("Maximum allowed positional error in meters between AirSim multirotor physics pose and the visible UE pawn before physics is corrected back to the visible pawn pose."));

FString LandedStateToString(LandedState state)
{
    return state == LandedState::Landed ? TEXT("Landed") : TEXT("Flying");
}
}

MultirotorPawnSimApi::MultirotorPawnSimApi(const Params& params, const msr::airlib::RCData& keyboard_controls)
    : PawnSimApi(params)
    , pawn_events_(static_cast<MultirotorPawnEvents*>(params.pawn_events))
    , keyboard_controls_(keyboard_controls)
    , last_control_log_time_(0.0)
    , last_state_log_time_(0.0)
    , last_logged_control_(msr::airlib::RCData())
    , last_logged_using_keyboard_(false)
{
    last_logged_control_.pitch = 0.0f;
    last_logged_control_.roll = 0.0f;
    last_logged_control_.yaw = 0.0f;
    last_logged_control_.throttle = 0.0f;
    last_logged_control_.is_initialized = false;
    last_logged_control_.is_valid = false;
}

void MultirotorPawnSimApi::initialize()
{
    PawnSimApi::initialize();

    //create vehicle API
    std::shared_ptr<UnrealSensorFactory> sensor_factory = std::make_shared<UnrealSensorFactory>(getPawn(), &getNedTransform());
    vehicle_params_ = MultiRotorParamsFactory::createConfig(getVehicleSetting(), sensor_factory);
    vehicle_api_ = vehicle_params_->createMultirotorApi();
    //setup physics vehicle
    multirotor_physics_body_ = std::unique_ptr<MultiRotor>(new MultiRotorPhysicsBody(vehicle_params_.get(), vehicle_api_.get(), getKinematics(), getEnvironment()));
    rotor_count_ = multirotor_physics_body_->wrenchVertexCount();
    rotor_actuator_info_.assign(rotor_count_, RotorActuatorInfo());

    vehicle_api_->setSimulatedGroundTruth(getGroundTruthKinematics(), getGroundTruthEnvironment());

    //initialize private vars
    last_phys_pose_ = Pose::nanPose();
    pending_pose_status_ = PendingPoseStatus::NonePending;
    reset_pending_ = false;
    did_reset_ = false;
    rotor_states_.rotors.assign(rotor_count_, RotorParameters());

    //reset roll & pitch of vehicle as multirotors required to be on plain surface at start
    Pose pose = getPose();
    float pitch, roll, yaw;
    VectorMath::toEulerianAngle(pose.orientation, pitch, roll, yaw);
    pose.orientation = VectorMath::toQuaternion(0, 0, yaw);
    setPose(pose, false);
}

void MultirotorPawnSimApi::pawnTick(float dt)
{
    unused(dt);
    //calls to update* are handled by physics engine and in SimModeWorldBase
}

void MultirotorPawnSimApi::updateRenderedState(float dt)
{
    //Utils::log("------Render tick-------");

    //if reset is pending then do it first, no need to do other things until next tick
    if (reset_pending_) {
        reset_task_();
        did_reset_ = true;
        return;
    }

    //move collision info from rendering engine to vehicle
    const CollisionInfo& collision_info = getCollisionInfo();
    multirotor_physics_body_->setCollisionInfo(collision_info);

    last_phys_pose_ = multirotor_physics_body_->getPose();

    collision_response = multirotor_physics_body_->getCollisionResponseInfo();

    //update rotor poses
    for (unsigned int i = 0; i < rotor_count_; ++i) {
        const auto& rotor_output = multirotor_physics_body_->getRotorOutput(i);
        // update private rotor variable
        rotor_states_.rotors[i].update(rotor_output.thrust, rotor_output.torque_scaler, rotor_output.speed);
        RotorActuatorInfo* info = &rotor_actuator_info_[i];
        info->rotor_speed = rotor_output.speed;
        info->rotor_direction = static_cast<int>(rotor_output.turning_direction);
        info->rotor_thrust = rotor_output.thrust;
        info->rotor_control_filtered = rotor_output.control_signal_filtered;
    }

    vehicle_api_messages_.clear();
    vehicle_api_->getStatusMessages(vehicle_api_messages_);

    const auto rc_data = getRCData();
    const bool using_keyboard = !(rc_data.is_initialized && rc_data.is_valid);
    const auto& applied_rc = using_keyboard ? keyboard_controls_ : rc_data;
    vehicle_api_->setRCData(applied_rc);
    logAppliedControl(applied_rc, using_keyboard);
    rotor_states_.timestamp = clock()->nowNanos();
    vehicle_api_->setRotorStates(rotor_states_);
}

void MultirotorPawnSimApi::updateRendering(float dt)
{
    //if we did reset then don't worry about synchronizing states for this tick
    if (reset_pending_) {
        // Continue to wait for reset
        if (!did_reset_) {
            return;
        }
        else {
            reset_pending_ = false;
            did_reset_ = false;
            return;
        }
    }

    if (!VectorMath::hasNan(last_phys_pose_)) {
        const Pose requested_pose = last_phys_pose_;

        if (pending_pose_status_ == PendingPoseStatus::RenderPending) {
            PawnSimApi::setPose(requested_pose, pending_pose_collisions_);
            pending_pose_status_ = PendingPoseStatus::NonePending;
        }
        else {
            PawnSimApi::setPose(requested_pose, false);
        }

        const Pose actual_pose = PawnSimApi::getPose();
        const Vector3r visual_delta = actual_pose.position - requested_pose.position;
        const real_T correction_threshold = static_cast<real_T>(FMath::Max(0.01f, CVarAirSimMultirotorVisualSyncCorrectionThreshold.GetValueOnGameThread()));

        if (visual_delta.squaredNorm() > correction_threshold * correction_threshold) {
            multirotor_physics_body_->lock();
            auto corrected_state = multirotor_physics_body_->getKinematics();
            corrected_state.pose = actual_pose;
            corrected_state.twist = Twist::zero();
            corrected_state.accelerations = Accelerations::zero();
            multirotor_physics_body_->updateKinematics(corrected_state);
            multirotor_physics_body_->setGrounded(false);
            multirotor_physics_body_->unlock();

            last_phys_pose_ = actual_pose;

            if (CVarAirSimMultirotorStateLog.GetValueOnGameThread() > 0) {
                UE_LOG(LogAirSimMultirotorControl, Warning,
                    TEXT("[AirSimVisual] requested=(%.2f, %.2f, %.2f) actual=(%.2f, %.2f, %.2f) delta=(%.2f, %.2f, %.2f) collision_raw=%u collision_non_resting=%u"),
                    requested_pose.position.x(), requested_pose.position.y(), requested_pose.position.z(),
                    actual_pose.position.x(), actual_pose.position.y(), actual_pose.position.z(),
                    visual_delta.x(), visual_delta.y(), visual_delta.z(),
                    collision_response.collision_count_raw,
                    collision_response.collision_count_non_resting);
            }
        }
    }

    const auto collision_info = getCollisionInfo();
    UAirBlueprintLib::LogMessage(TEXT("Collision Count:"),
                                 FString::Printf(TEXT("hits=%u raw=%u active=%u"), collision_info.collision_count, collision_response.collision_count_raw, collision_response.collision_count_non_resting),
                                 LogDebugLevel::Informational);

    for (auto i = 0; i < vehicle_api_messages_.size(); ++i) {
        const FString Message = FString(vehicle_api_messages_[i].c_str());
        UAirBlueprintLib::LogMessage(Message, TEXT(""), LogDebugLevel::Success, 30);
        UE_LOG(LogAirSimMultirotorControl, Log, TEXT("[AirSimStatus] %s"), *Message);
    }
    vehicle_api_messages_.clear();

    logVehicleState();

    try {
        vehicle_api_->sendTelemetry(dt);
    }
    catch (std::exception& e) {
        UAirBlueprintLib::LogMessage(FString(e.what()), TEXT(""), LogDebugLevel::Failure, 30);
    }

    pawn_events_->getActuatorSignal().emit(rotor_actuator_info_);
}

void MultirotorPawnSimApi::logAppliedControl(const msr::airlib::RCData& rc_data, bool using_keyboard)
{
    if (CVarAirSimMultirotorControlLog.GetValueOnGameThread() <= 0)
        return;

    const double now = FPlatformTime::Seconds();
    const float log_period = FMath::Max(0.05f, CVarAirSimMultirotorControlLogPeriod.GetValueOnGameThread());
    const bool source_changed = using_keyboard != last_logged_using_keyboard_;
    const bool values_changed =
        FMath::Abs(static_cast<float>(rc_data.pitch - last_logged_control_.pitch)) >= 0.02f ||
        FMath::Abs(static_cast<float>(rc_data.roll - last_logged_control_.roll)) >= 0.02f ||
        FMath::Abs(static_cast<float>(rc_data.yaw - last_logged_control_.yaw)) >= 0.02f ||
        FMath::Abs(static_cast<float>(rc_data.throttle - last_logged_control_.throttle)) >= 0.02f;

    if (!source_changed && !values_changed && (now - last_control_log_time_) < log_period)
        return;

    const bool local_input_disabled = rc_data.vendor_id == "LocalInputDisabled";
    const TCHAR* source_label = using_keyboard
        ? (local_input_disabled ? TEXT("LocalInputDisabled") : TEXT("Keyboard"))
        : TEXT("Joystick");

    UE_LOG(LogAirSimMultirotorControl, Log,
        TEXT("[AirSimControl] source=%s pitch=%.2f roll=%.2f yaw=%.2f throttle=%.3f valid=%d init=%d switches=%u"),
        source_label,
        rc_data.pitch,
        rc_data.roll,
        rc_data.yaw,
        rc_data.throttle,
        rc_data.is_valid ? 1 : 0,
        rc_data.is_initialized ? 1 : 0,
        rc_data.switches);

    last_logged_control_ = rc_data;
    last_logged_using_keyboard_ = using_keyboard;
    last_control_log_time_ = now;
}

void MultirotorPawnSimApi::logVehicleState()
{
    if (CVarAirSimMultirotorStateLog.GetValueOnGameThread() <= 0)
        return;

    const double now = FPlatformTime::Seconds();
    const float log_period = FMath::Max(0.05f, CVarAirSimMultirotorStateLogPeriod.GetValueOnGameThread());
    if ((now - last_state_log_time_) < log_period)
        return;

    const auto state = vehicle_api_->getMultirotorState();
    const auto& pose = state.kinematics_estimated.pose;
    const auto& velocity = state.kinematics_estimated.twist.linear;

    float pitch = 0.0f;
    float roll = 0.0f;
    float yaw = 0.0f;
    VectorMath::toEulerianAngle(pose.orientation, pitch, roll, yaw);

    const float rotor0 = rotor_states_.rotors.size() > 0 ? rotor_states_.rotors[0].speed : 0.0f;
    const float rotor1 = rotor_states_.rotors.size() > 1 ? rotor_states_.rotors[1].speed : 0.0f;
    const float rotor2 = rotor_states_.rotors.size() > 2 ? rotor_states_.rotors[2].speed : 0.0f;
    const float rotor3 = rotor_states_.rotors.size() > 3 ? rotor_states_.rotors[3].speed : 0.0f;

    const FString ReadyMessage = state.ready_message.empty() ? TEXT("") : FString(state.ready_message.c_str());

    UE_LOG(LogAirSimMultirotorControl, Log,
        TEXT("[AirSimState] landed=%s ready=%d can_arm=%d pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f) euler_deg=(p=%.1f,r=%.1f,y=%.1f) rc_throttle=%.3f rotors=(%.1f, %.1f, %.1f, %.1f) msg=%s"),
        *LandedStateToString(state.landed_state),
        state.ready ? 1 : 0,
        state.can_arm ? 1 : 0,
        pose.position.x(),
        pose.position.y(),
        pose.position.z(),
        velocity.x(),
        velocity.y(),
        velocity.z(),
        FMath::RadiansToDegrees(pitch),
        FMath::RadiansToDegrees(roll),
        FMath::RadiansToDegrees(yaw),
        state.rc_data.throttle,
        rotor0,
        rotor1,
        rotor2,
        rotor3,
        *ReadyMessage);

    last_state_log_time_ = now;
}
void MultirotorPawnSimApi::setPose(const Pose& pose, bool ignore_collision)
{
    multirotor_physics_body_->lock();
    multirotor_physics_body_->setPose(pose);
    multirotor_physics_body_->setGrounded(false);
    multirotor_physics_body_->unlock();
    pending_pose_collisions_ = ignore_collision;
    pending_pose_status_ = PendingPoseStatus::RenderPending;
}

void MultirotorPawnSimApi::setKinematics(const Kinematics::State& state, bool ignore_collision)
{
    multirotor_physics_body_->lock();
    multirotor_physics_body_->updateKinematics(state);
    multirotor_physics_body_->setGrounded(false);
    multirotor_physics_body_->unlock();
    pending_pose_collisions_ = ignore_collision;
    pending_pose_status_ = PendingPoseStatus::RenderPending;
}

//*** Start: UpdatableState implementation ***//
void MultirotorPawnSimApi::resetImplementation()
{
    PawnSimApi::resetImplementation();

    vehicle_api_->reset();
    multirotor_physics_body_->reset();
    vehicle_api_messages_.clear();
}

//this is high frequency physics tick, flier gets ticked at rendering frame rate
void MultirotorPawnSimApi::update()
{
    //environment update for current position
    PawnSimApi::update();

    //update forces on vertices
    multirotor_physics_body_->update();

    //update to controller must be done after kinematics have been updated by physics engine
}

void MultirotorPawnSimApi::reportState(StateReporter& reporter)
{
    PawnSimApi::reportState(reporter);

    multirotor_physics_body_->reportState(reporter);
}

MultirotorPawnSimApi::UpdatableObject* MultirotorPawnSimApi::getPhysicsBody()
{
    return multirotor_physics_body_->getPhysicsBody();
}
//*** End: UpdatableState implementation ***//






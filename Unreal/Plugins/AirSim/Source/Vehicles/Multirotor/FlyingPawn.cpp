#include "FlyingPawn.h"
#include "Components/StaticMeshComponent.h"
#include "AirBlueprintLib.h"
#include "common/CommonStructs.hpp"
#include "common/Common.hpp"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAirSimKeyboardInput, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarAirSimMultirotorKeyboardInputLog(
    TEXT("AirSim.Multirotor.KeyboardInputLog"),
    0,
    TEXT("Enable multirotor keyboard input logs in the Output Log."));

TAutoConsoleVariable<float> CVarAirSimMultirotorKeyboardThrottleLogPeriod(
    TEXT("AirSim.Multirotor.KeyboardThrottleLogPeriod"),
    0.25f,
    TEXT("Minimum time in seconds between keyboard throttle logs."));
}

AFlyingPawn::AFlyingPawn()
{
    init_id_ = pawn_events_.getActuatorSignal().connect_member(this, &AFlyingPawn::initializeRotors);
    pawn_events_.getActuatorSignal().connect_member(this, &AFlyingPawn::setRotorSpeed);

    keyboard_controls_ = msr::airlib::RCData();
    keyboard_controls_.is_initialized = true;
    keyboard_controls_.is_valid = true;
    keyboard_controls_.vendor_id = "LocalInputDisabled";
    keyboard_controls_.switches = 0;
    keyboard_controls_.throttle = 0.0f;
    keyboard_controls_.pitch = 0.0f;
    keyboard_controls_.roll = 0.0f;
    keyboard_controls_.yaw = 0.0f;

    throttle_input_ = 0.0f;
    throttle_ramp_rate_ = 0.35f;
    hover_throttle_ = 0.58f;
    last_logged_throttle_ = -1.0f;
    last_throttle_log_time_ = 0.0;
}

void AFlyingPawn::BeginPlay()
{
    Super::BeginPlay();
}

void AFlyingPawn::initializeForBeginPlay()
{
    //get references of existing camera
    camera_front_right_ = Cast<APIPCamera>(
        (UAirBlueprintLib::GetActorComponent<UChildActorComponent>(this, TEXT("FrontRightCamera")))->GetChildActor());
    camera_front_left_ = Cast<APIPCamera>(
        (UAirBlueprintLib::GetActorComponent<UChildActorComponent>(this, TEXT("FrontLeftCamera")))->GetChildActor());
    camera_front_center_ = Cast<APIPCamera>(
        (UAirBlueprintLib::GetActorComponent<UChildActorComponent>(this, TEXT("FrontCenterCamera")))->GetChildActor());
    camera_back_center_ = Cast<APIPCamera>(
        (UAirBlueprintLib::GetActorComponent<UChildActorComponent>(this, TEXT("BackCenterCamera")))->GetChildActor());
    camera_bottom_center_ = Cast<APIPCamera>(
        (UAirBlueprintLib::GetActorComponent<UChildActorComponent>(this, TEXT("BottomCenterCamera")))->GetChildActor());

    UAirBlueprintLib::LogMessageString("Multirotor local input:", "disabled (API-only mode)", LogDebugLevel::Informational, 20);
}

void AFlyingPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    pawn_events_.getPawnTickSignal().emit(DeltaSeconds);
}

void AFlyingPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    camera_front_right_ = nullptr;
    camera_front_left_ = nullptr;
    camera_front_center_ = nullptr;
    camera_back_center_ = nullptr;
    camera_bottom_center_ = nullptr;

    pawn_events_.getActuatorSignal().disconnect_all();
    rotating_movements_.Empty();

    Super::EndPlay(EndPlayReason);
}

const common_utils::UniqueValueMap<std::string, APIPCamera*> AFlyingPawn::getCameras() const
{
    common_utils::UniqueValueMap<std::string, APIPCamera*> cameras;
    cameras.insert_or_assign("front_center", camera_front_center_);
    cameras.insert_or_assign("front_right", camera_front_right_);
    cameras.insert_or_assign("front_left", camera_front_left_);
    cameras.insert_or_assign("bottom_center", camera_bottom_center_);
    cameras.insert_or_assign("back_center", camera_back_center_);

    cameras.insert_or_assign("0", camera_front_center_);
    cameras.insert_or_assign("1", camera_front_right_);
    cameras.insert_or_assign("2", camera_front_left_);
    cameras.insert_or_assign("3", camera_bottom_center_);
    cameras.insert_or_assign("4", camera_back_center_);

    cameras.insert_or_assign("", camera_front_center_);
    cameras.insert_or_assign("fpv", camera_front_center_);

    return cameras;
}

void AFlyingPawn::NotifyHit(class UPrimitiveComponent* MyComp, class AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation,
                            FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
    pawn_events_.getCollisionSignal().emit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
}

void AFlyingPawn::setRotorSpeed(const std::vector<MultirotorPawnEvents::RotorActuatorInfo>& rotor_infos)
{
    for (auto rotor_index = 0; rotor_index < rotor_infos.size(); ++rotor_index) {
        auto comp = rotating_movements_[rotor_index];
        if (comp != nullptr) {
            comp->RotationRate.Yaw =
                rotor_infos.at(rotor_index).rotor_speed * rotor_infos.at(rotor_index).rotor_direction *
                180.0f / M_PIf * RotatorFactor;
        }
    }
}

void AFlyingPawn::initializeRotors(const std::vector<MultirotorPawnEvents::RotorActuatorInfo>& rotor_infos)
{
    for (auto i = 0; i < rotor_infos.size(); ++i) {
        rotating_movements_.Add(UAirBlueprintLib::GetActorComponent<URotatingMovementComponent>(this, TEXT("Rotation") + FString::FromInt(i)));
    }
    pawn_events_.getActuatorSignal().disconnect(init_id_);
}

void AFlyingPawn::setupInputBindings()
{
    UAirBlueprintLib::EnableInput(this);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMovePitch", EKeys::W, 1), this, this, &AFlyingPawn::onMovePitch);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMovePitch", EKeys::S, -1), this, this, &AFlyingPawn::onMovePitch);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMovePitch", EKeys::Up, 1), this, this, &AFlyingPawn::onMovePitch);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMovePitch", EKeys::Down, -1), this, this, &AFlyingPawn::onMovePitch);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveRoll", EKeys::D, 1), this, this, &AFlyingPawn::onMoveRoll);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveRoll", EKeys::A, -1), this, this, &AFlyingPawn::onMoveRoll);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveRoll", EKeys::Right, 1), this, this, &AFlyingPawn::onMoveRoll);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveRoll", EKeys::Left, -1), this, this, &AFlyingPawn::onMoveRoll);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveYaw", EKeys::E, 1), this, this, &AFlyingPawn::onMoveYaw);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveYaw", EKeys::Q, -1), this, this, &AFlyingPawn::onMoveYaw);

    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveThrottle", EKeys::SpaceBar, 1), this, this, &AFlyingPawn::onMoveThrottle);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveThrottle", EKeys::PageUp, 1), this, this, &AFlyingPawn::onMoveThrottle);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveThrottle", EKeys::LeftControl, -1), this, this, &AFlyingPawn::onMoveThrottle);
    UAirBlueprintLib::BindAxisToKey(FInputAxisKeyMapping("MultirotorMoveThrottle", EKeys::PageDown, -1), this, this, &AFlyingPawn::onMoveThrottle);

    UAirBlueprintLib::BindActionToKey("MultirotorHoverThrottle", EKeys::H, this, &AFlyingPawn::onHoverThrottle, true);
    UAirBlueprintLib::BindActionToKey("MultirotorCutThrottle", EKeys::BackSpace, this, &AFlyingPawn::onCutThrottle, true);
}

void AFlyingPawn::updateKeyboardControls(float DeltaSeconds)
{
    const float previous_throttle = keyboard_controls_.throttle;

    if (!FMath::IsNearlyZero(throttle_input_)) {
        keyboard_controls_.throttle = FMath::Clamp(keyboard_controls_.throttle + throttle_input_ * throttle_ramp_rate_ * DeltaSeconds, 0.0f, 1.0f);
    }
    else if (keyboard_controls_.throttle < KINDA_SMALL_NUMBER) {
        keyboard_controls_.throttle = 0.0f;
    }

    if (CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0 &&
        !FMath::IsNearlyEqual(previous_throttle, keyboard_controls_.throttle, KINDA_SMALL_NUMBER)) {
        const double now = FPlatformTime::Seconds();
        const float log_period = FMath::Max(0.05f, CVarAirSimMultirotorKeyboardThrottleLogPeriod.GetValueOnGameThread());
        if (FMath::Abs(keyboard_controls_.throttle - last_logged_throttle_) >= 0.02f ||
            (now - last_throttle_log_time_) >= log_period) {
            UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] throttle=%.3f throttle_axis=%.2f"), keyboard_controls_.throttle, throttle_input_);
            last_logged_throttle_ = keyboard_controls_.throttle;
            last_throttle_log_time_ = now;
        }
    }
}

void AFlyingPawn::onMovePitch(float Val)
{
    if (!FMath::IsNearlyEqual(keyboard_controls_.pitch, Val) && CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0)
        UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] pitch=%.2f"), Val);

    keyboard_controls_.pitch = Val;
}

void AFlyingPawn::onMoveRoll(float Val)
{
    if (!FMath::IsNearlyEqual(keyboard_controls_.roll, Val) && CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0)
        UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] roll=%.2f"), Val);

    keyboard_controls_.roll = Val;
}

void AFlyingPawn::onMoveYaw(float Val)
{
    if (!FMath::IsNearlyEqual(keyboard_controls_.yaw, Val) && CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0)
        UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] yaw=%.2f"), Val);

    keyboard_controls_.yaw = Val;
}

void AFlyingPawn::onMoveThrottle(float Val)
{
    if (!FMath::IsNearlyEqual(throttle_input_, Val) && CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0)
        UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] throttle_axis=%.2f"), Val);

    throttle_input_ = Val;
}

void AFlyingPawn::onHoverThrottle()
{
    keyboard_controls_.throttle = hover_throttle_;
    UAirBlueprintLib::LogMessageString("Keyboard throttle:", std::to_string(keyboard_controls_.throttle), LogDebugLevel::Informational, 5);
    if (CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0)
        UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] hover_throttle=%.3f"), keyboard_controls_.throttle);
}

void AFlyingPawn::onCutThrottle()
{
    keyboard_controls_.throttle = 0.0f;
    UAirBlueprintLib::LogMessageString("Keyboard throttle:", "0.0", LogDebugLevel::Informational, 5);
    if (CVarAirSimMultirotorKeyboardInputLog.GetValueOnGameThread() > 0)
        UE_LOG(LogAirSimKeyboardInput, Log, TEXT("[AirSimInput] cut_throttle"));
}



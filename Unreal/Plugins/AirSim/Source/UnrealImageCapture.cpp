#include "UnrealImageCapture.h"
#include "Engine/World.h"
#include "ImageUtils.h"
#include "CoreGlobals.h"

#include "AirBlueprintLib.h"
#include "RenderRequest.h"
#include "common/ClockFactory.hpp"
#include <chrono>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace
{
using ImageRequest = msr::airlib::ImageCaptureBase::ImageRequest;
using ImageResponse = msr::airlib::ImageCaptureBase::ImageResponse;
using ImageType = msr::airlib::ImageCaptureBase::ImageType;

constexpr bool kEnableLatestFrameCache = true;
constexpr float kDepthVisualizationRejectSentinelMeters = 60000.0f;
constexpr float kNativeSceneDepthNoHitThresholdCentimeters = 65000.0f;
constexpr float kNativeSceneDepthCentimetersToMeters = 0.01f;
constexpr float kDepthVisualizationNearPercentile = 0.01f;
constexpr float kDepthVisualizationFarPercentile = 0.97f;
constexpr msr::airlib::TTimePoint kSceneLatestFrameFallbackIntervalNanos = 50ll * 1000ll * 1000ll;
constexpr bool kUseSafeCaptureReadback = true;
constexpr auto kStrictSceneFreshnessWaitTimeout = std::chrono::milliseconds(1000);

uint64 GetCurrentCaptureFrame()
{
    return GFrameCounter;
}

bool ShouldSynthesizeDepthImage(const ImageRequest& Request)
{
    if (Request.pixels_as_float) {
        return false;
    }

    switch (Request.image_type) {
    case ImageType::DepthPlanar:
    case ImageType::DepthPerspective:
    case ImageType::DepthVis:
        return true;

    default:
        return false;
    }
}

ImageType GetDepthCaptureSourceType(const ImageRequest& Request)
{
    return Request.image_type == ImageType::DepthVis ? ImageType::DepthPlanar : Request.image_type;
}

bool IsRenderableDepthValue(const float Depth)
{
    return FMath::IsFinite(Depth) && Depth > KINDA_SMALL_NUMBER && Depth < kDepthVisualizationRejectSentinelMeters;
}

float ConvertNativeSceneDepthCmToAirSimMeters(const float DepthCm)
{
    if (!FMath::IsFinite(DepthCm)) {
        return DepthCm;
    }

    if (DepthCm >= kNativeSceneDepthNoHitThresholdCentimeters) {
        return kDepthVisualizationRejectSentinelMeters + 1.0f;
    }

    return DepthCm * kNativeSceneDepthCentimetersToMeters;
}

bool NeedsNativeSceneDepthUnitFix(const ImageRequest& CaptureRequest)
{
    return CaptureRequest.pixels_as_float && CaptureRequest.image_type == ImageType::DepthPlanar;
}

void ConvertNativeSceneDepthPixelsToAirSimMeters(const TArray<FFloat16Color>& InPixels, TArray<FFloat16Color>& OutPixels)
{
    OutPixels = InPixels;
    for (FFloat16Color& Pixel : OutPixels) {
        Pixel.R = ConvertNativeSceneDepthCmToAirSimMeters(Pixel.R.GetFloat());
    }
}

bool AreCaptureRequestsEquivalent(const ImageRequest& A, const ImageRequest& B)
{
    return A.camera_name == B.camera_name
           && A.image_type == B.image_type
           && A.pixels_as_float == B.pixels_as_float
           && (A.pixels_as_float || A.compress == B.compress);
}

int32 FindEquivalentCaptureRequest(const std::vector<ImageRequest>& Requests, const ImageRequest& Request)
{
    for (int32 Index = 0; Index < static_cast<int32>(Requests.size()); ++Index) {
        if (AreCaptureRequestsEquivalent(Requests[Index], Request)) {
            return Index;
        }
    }

    return INDEX_NONE;
}

bool NeedsSceneWarmupPass(const common_utils::UniqueValueMap<std::string, APIPCamera*>* Cameras,
                          const std::vector<ImageRequest>& Requests)
{
    for (const ImageRequest& Request : Requests) {
        if (Request.image_type != ImageType::Scene || Request.pixels_as_float) {
            continue;
        }

        APIPCamera* Camera = Cameras->at(Request.camera_name);
        if (Camera == nullptr) {
            continue;
        }

        USceneCaptureComponent2D* Capture = Camera->getCaptureComponent(ImageType::Scene, false);
        if (!Camera->getCameraTypeEnabled(ImageType::Scene)
            || Capture == nullptr
            || !Capture->bCaptureEveryFrame
            || !Capture->bCaptureOnMovement
            || !Capture->bAlwaysPersistRenderingState
            || Capture->bExcludeFromSceneTextureExtents) {
            return true;
        }
    }

    return false;
}

bool ShouldUseContinuousSceneReadback(const ImageRequest& Request)
{
    return Request.image_type == ImageType::Scene && !Request.pixels_as_float;
}

bool IsPureContinuousSceneBatch(const std::vector<ImageRequest>& Requests)
{
    if (Requests.empty()) {
        return false;
    }

    for (const ImageRequest& Request : Requests) {
        if (!ShouldUseContinuousSceneReadback(Request)) {
            return false;
        }
    }

    return true;
}

bool HasContinuousSceneRequest(const std::vector<ImageRequest>& Requests)
{
    for (const ImageRequest& Request : Requests) {
        if (ShouldUseContinuousSceneReadback(Request)) {
            return true;
        }
    }

    return false;
}

bool HasUsableSceneResponse(const std::vector<ImageRequest>& Requests, const std::vector<ImageResponse>& Responses)
{
    const size_t Count = FMath::Min(Requests.size(), Responses.size());
    for (size_t Index = 0; Index < Count; ++Index) {
        if (Requests[Index].image_type != ImageType::Scene || Requests[Index].pixels_as_float) {
            continue;
        }

        const ImageResponse& Response = Responses[Index];
        if (Response.width > 0 && Response.height > 0
            && (!Response.image_data_uint8.empty() || !Response.image_data_float.empty())) {
            return true;
        }
    }

    return false;
}

ImageRequest MakeCaptureRequest(const ImageRequest& Request)
{
    if (ShouldSynthesizeDepthImage(Request)) {
        return ImageRequest(Request.camera_name, GetDepthCaptureSourceType(Request), true, false);
    }

    return Request;
}

float GetPercentileValue(const TArray<float>& Values, float Fraction)
{
    if (Values.Num() == 0) {
        return 0.0f;
    }

    const float ClampedFraction = FMath::Clamp(Fraction, 0.0f, 1.0f);
    const int32 Index = FMath::Clamp(FMath::FloorToInt((Values.Num() - 1) * ClampedFraction), 0, Values.Num() - 1);
    return Values[Index];
}

void BuildDepthVisImage(const TArray<FFloat16Color>& DepthPixels, int32 Width, int32 Height, bool bCompress, std::vector<uint8_t>& OutImageData)
{
    TArray<float> ValidDepths;
    ValidDepths.Reserve(DepthPixels.Num());
    for (const FFloat16Color& Pixel : DepthPixels) {
        const float Depth = Pixel.R.GetFloat();
        if (IsRenderableDepthValue(Depth)) {
            ValidDepths.Add(Depth);
        }
    }

    float NearDepth = 0.0f;
    float FarDepth = 1.0f;
    if (ValidDepths.Num() > 0) {
        ValidDepths.Sort();
        NearDepth = GetPercentileValue(ValidDepths, kDepthVisualizationNearPercentile);
        FarDepth = GetPercentileValue(ValidDepths, kDepthVisualizationFarPercentile);
        if (!(FarDepth > NearDepth)) {
            FarDepth = NearDepth + FMath::Max(1.0f, NearDepth * 0.05f);
        }
    }

    TArray<FColor> DepthBitmap;
    DepthBitmap.SetNumUninitialized(Width * Height);
    for (int32 Index = 0; Index < DepthPixels.Num(); ++Index) {
        const float Depth = DepthPixels[Index].R.GetFloat();
        uint8 Intensity = 0;
        if (IsRenderableDepthValue(Depth)) {
            const float Normalized = FMath::Clamp((Depth - NearDepth) / (FarDepth - NearDepth), 0.0f, 1.0f);
            Intensity = static_cast<uint8>(FMath::RoundToInt((1.0f - Normalized) * 255.0f));
        }
        DepthBitmap[Index] = FColor(Intensity, Intensity, Intensity, 255);
    }

    if (bCompress) {
        TArray<uint8> CompressedPng;
        UAirBlueprintLib::CompressImageArray(Width, Height, DepthBitmap, CompressedPng);
        OutImageData.assign(CompressedPng.GetData(), CompressedPng.GetData() + CompressedPng.Num());
        return;
    }

    OutImageData.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 3ull);
    uint8* Dest = OutImageData.data();
    for (const FColor& Pixel : DepthBitmap) {
        *Dest++ = Pixel.B;
        *Dest++ = Pixel.G;
        *Dest++ = Pixel.R;
    }
}
}

UnrealImageCapture::UnrealImageCapture(const common_utils::UniqueValueMap<std::string, APIPCamera*>* cameras,
                                       bool strict_scene_freshness_after_pose_change)
    : cameras_(cameras)
    , strict_scene_freshness_after_pose_change_(strict_scene_freshness_after_pose_change)
{
    //TODO: explore screenshot option
    //addScreenCaptureHandler(camera->GetWorld());
}

UnrealImageCapture::~UnrealImageCapture()
{
}

void UnrealImageCapture::getImages(const std::vector<msr::airlib::ImageCaptureBase::ImageRequest>& requests,
                                   std::vector<msr::airlib::ImageCaptureBase::ImageResponse>& responses) const
{
    if (cameras_->valsSize() == 0) {
        for (unsigned int i = 0; i < requests.size(); ++i) {
            responses.push_back(ImageResponse());
            responses[responses.size() - 1].message = "camera is not set";
        }
        return;
    }

    const uint64 RequiredSceneGeneration = getRequiredSceneGeneration(requests);
    const bool bForceSceneSnapshot =
        RequiredSceneGeneration > 0 && !waitForRequiredSceneGeneration(requests, RequiredSceneGeneration);

    if (!bForceSceneSnapshot && shouldUseLatestFrameCache(requests) && tryServeCachedResponses(requests, responses)) {
        return;
    }

    const bool needsSceneWarmupPass = NeedsSceneWarmupPass(cameras_, requests);

    if (UAirBlueprintLib::IsInGameThread()) {
        if (needsSceneWarmupPass) {
            std::vector<ImageResponse> warmup_responses;
            getSceneCaptureImage(requests, warmup_responses, kUseSafeCaptureReadback, bForceSceneSnapshot);
        }
        getSceneCaptureImage(requests, responses, kUseSafeCaptureReadback, bForceSceneSnapshot);
        if (RequiredSceneGeneration > 0 && HasUsableSceneResponse(requests, responses)) {
            markSceneGenerationComplete(RequiredSceneGeneration);
        }
        return;
    }

    if (needsSceneWarmupPass) {
        UAirBlueprintLib::RunCommandOnGameThread([this, &requests]() {
            std::vector<ImageResponse> warmup_responses;
            getSceneCaptureImage(requests, warmup_responses, kUseSafeCaptureReadback, false);
        }, true);
        std::this_thread::sleep_for(std::chrono::duration<double>(0.1));
    }

    UAirBlueprintLib::RunCommandOnGameThread([this, &requests, &responses, bForceSceneSnapshot]() {
        getSceneCaptureImage(requests, responses, kUseSafeCaptureReadback, bForceSceneSnapshot);
    }, true);

    if (RequiredSceneGeneration > 0 && HasUsableSceneResponse(requests, responses)) {
        markSceneGenerationComplete(RequiredSceneGeneration);
    }

    if (shouldUseLatestFrameCache(requests)) {
        updateCachedResponses(requests, responses, GetCurrentCaptureFrame());
    }
}

void UnrealImageCapture::getSceneCaptureImage(const std::vector<msr::airlib::ImageCaptureBase::ImageRequest>& requests,
                                              std::vector<msr::airlib::ImageCaptureBase::ImageResponse>& responses,
                                              bool use_safe_method,
                                              bool force_scene_snapshot) const
{
    std::vector<ImageRequest> capture_requests;
    std::vector<int32> request_to_capture_indices;
    capture_requests.reserve(requests.size());
    request_to_capture_indices.reserve(requests.size());
    for (const ImageRequest& Request : requests) {
        const ImageRequest CaptureRequest = MakeCaptureRequest(Request);
        int32 CaptureIndex = FindEquivalentCaptureRequest(capture_requests, CaptureRequest);
        if (CaptureIndex == INDEX_NONE) {
            CaptureIndex = static_cast<int32>(capture_requests.size());
            capture_requests.push_back(CaptureRequest);
        }

        request_to_capture_indices.push_back(CaptureIndex);
    }

    std::vector<std::shared_ptr<RenderRequest::RenderParams>> render_params;
    std::vector<std::shared_ptr<RenderRequest::RenderResult>> render_results;
    std::vector<int32> capture_to_render_indices(capture_requests.size(), INDEX_NONE);
    std::vector<std::string> capture_messages(capture_requests.size());

    bool visibilityChanged = false;
    for (unsigned int i = 0; i < capture_requests.size(); ++i) {
        APIPCamera* camera = cameras_->at(capture_requests.at(i).camera_name);
        const bool requestVisibilityChanged = const_cast<UnrealImageCapture*>(this)->updateCameraVisibility(camera, capture_requests[i]);
        visibilityChanged = requestVisibilityChanged || visibilityChanged;
    }

    if (use_safe_method && visibilityChanged) {
        std::this_thread::sleep_for(std::chrono::duration<double>(0.2));
    }

    UGameViewportClient* gameViewport = nullptr;
    responses.reserve(responses.size() + requests.size());
    for (unsigned int i = 0; i < requests.size(); ++i) {
        responses.push_back(ImageResponse());
    }

    for (unsigned int i = 0; i < capture_requests.size(); ++i) {
        APIPCamera* camera = cameras_->at(capture_requests.at(i).camera_name);
        if (gameViewport == nullptr) {
            gameViewport = camera->GetWorld()->GetGameViewport();
        }

        UTextureRenderTarget2D* textureTarget = nullptr;
        USceneCaptureComponent2D* capture = camera->getCaptureComponent(capture_requests[i].image_type, false);
        if (capture == nullptr) {
            capture_messages[i] = "Can't take screenshot because none camera type is not active";
        }
        else if (capture->TextureTarget == nullptr) {
            capture_messages[i] = "Can't take screenshot because texture target is null";
        }
        else {
            textureTarget = capture->TextureTarget;
            capture_to_render_indices[i] = static_cast<int32>(render_params.size());
            render_params.push_back(std::make_shared<RenderRequest::RenderParams>(
                capture,
                textureTarget,
                capture_requests[i].image_type,
                capture_requests[i].pixels_as_float,
                capture_requests[i].compress,
                ShouldUseContinuousSceneReadback(capture_requests[i]) && !force_scene_snapshot));
        }
    }

    if (nullptr == gameViewport) {
        return;
    }

    if (!render_params.empty()) {
        auto query_camera_pose_cb = [this, &requests, &responses]() {
            size_t count = requests.size();
            for (size_t i = 0; i < count; i++) {
                const ImageRequest& request = requests.at(i);
                APIPCamera* camera = cameras_->at(request.camera_name);
                ImageResponse& response = responses.at(i);
                auto camera_pose = camera->getPose();
                response.camera_position = camera_pose.position;
                response.camera_orientation = camera_pose.orientation;
            }
        };
        RenderRequest render_request{ gameViewport, std::move(query_camera_pose_cb) };

        render_request.getScreenshot(render_params.data(), render_results, render_params.size(), use_safe_method);
    }

    for (unsigned int i = 0; i < requests.size(); ++i) {
        const ImageRequest& request = requests.at(i);
        const int32 CaptureIndex = request_to_capture_indices.at(i);
        const int32 RenderIndex = capture_to_render_indices.at(CaptureIndex);
        ImageResponse& response = responses.at(i);

        response.camera_name = request.camera_name;
        response.message = capture_messages[CaptureIndex];

        if (RenderIndex != INDEX_NONE) {
            const auto& RenderResult = render_results[RenderIndex];
            response.time_stamp = RenderResult->time_stamp;
            const ImageRequest& CaptureRequest = capture_requests[CaptureIndex];
            const bool bNeedsNativeSceneDepthUnitFix = NeedsNativeSceneDepthUnitFix(CaptureRequest);

            if (ShouldSynthesizeDepthImage(request)) {
                if (bNeedsNativeSceneDepthUnitFix) {
                    TArray<FFloat16Color> ConvertedDepthPixels;
                    ConvertNativeSceneDepthPixelsToAirSimMeters(RenderResult->bmp_float, ConvertedDepthPixels);
                    BuildDepthVisImage(ConvertedDepthPixels, RenderResult->width, RenderResult->height, request.compress, response.image_data_uint8);
                }
                else {
                    BuildDepthVisImage(RenderResult->bmp_float, RenderResult->width, RenderResult->height, request.compress, response.image_data_uint8);
                }
                response.image_data_float.clear();
            }
            else {
                response.image_data_uint8 = std::vector<uint8_t>(RenderResult->image_data_uint8.GetData(), RenderResult->image_data_uint8.GetData() + RenderResult->image_data_uint8.Num());
                response.image_data_float = std::vector<float>(RenderResult->image_data_float.GetData(), RenderResult->image_data_float.GetData() + RenderResult->image_data_float.Num());
                if (bNeedsNativeSceneDepthUnitFix) {
                    for (float& DepthMeters : response.image_data_float) {
                        DepthMeters = ConvertNativeSceneDepthCmToAirSimMeters(DepthMeters);
                    }
                }
            }

            response.width = RenderResult->width;
            response.height = RenderResult->height;
        }

        if (use_safe_method) {
            APIPCamera* camera = cameras_->at(request.camera_name);
            msr::airlib::Pose pose = camera->getPose();
            response.camera_position = pose.position;
            response.camera_orientation = pose.orientation;
        }
        response.pixels_as_float = request.pixels_as_float;
        response.compress = request.compress;
        response.image_type = request.image_type;
    }
}

bool UnrealImageCapture::updateCameraVisibility(APIPCamera* camera, const msr::airlib::ImageCaptureBase::ImageRequest& request)
{
    bool captureStateChanged = false;
    if (!camera->getCameraTypeEnabled(request.image_type)) {
        camera->setCameraTypeEnabled(request.image_type, true);
        captureStateChanged = true;
    }

    if (ShouldUseContinuousSceneReadback(request)) {
        USceneCaptureComponent2D* Capture = camera->getCaptureComponent(request.image_type, false);
        if (Capture != nullptr
            && (!Capture->bCaptureEveryFrame
                || !Capture->bCaptureOnMovement
                || !Capture->bAlwaysPersistRenderingState
                || Capture->bExcludeFromSceneTextureExtents)) {
            // For Scene RGB, read back the latest continuously rendered target instead
            // of forcing a fresh CaptureScene() snapshot on every RPC call.
            camera->setCameraTypeUpdate(request.image_type, false);
            Capture->CaptureScene();
            captureStateChanged = true;
        }
    }

    return captureStateChanged;
}

bool UnrealImageCapture::shouldUseLatestFrameCache(const std::vector<ImageRequest>& requests) const
{
    return kEnableLatestFrameCache && !requests.empty();
}

bool UnrealImageCapture::shouldEnforceStrictSceneFreshness(const std::vector<ImageRequest>& requests) const
{
    return strict_scene_freshness_after_pose_change_ && HasContinuousSceneRequest(requests);
}

uint64 UnrealImageCapture::getRequiredSceneGeneration(const std::vector<ImageRequest>& requests) const
{
    if (!shouldEnforceStrictSceneFreshness(requests)) {
        return 0;
    }

    std::lock_guard<std::mutex> Lock(response_cache_mutex_);
    if (latest_pose_change_generation_ == 0
        || latest_completed_scene_generation_ >= latest_pose_change_generation_) {
        return 0;
    }

    return latest_pose_change_generation_;
}

bool UnrealImageCapture::waitForRequiredSceneGeneration(const std::vector<ImageRequest>& requests,
                                                        uint64 required_scene_generation) const
{
    if (required_scene_generation == 0) {
        return true;
    }

    queueDeferredSceneRefresh(requests, required_scene_generation);

    if (UAirBlueprintLib::IsInGameThread()) {
        return false;
    }

    std::unique_lock<std::mutex> Lock(response_cache_mutex_);
    return scene_generation_cv_.wait_for(
        Lock,
        kStrictSceneFreshnessWaitTimeout,
        [this, required_scene_generation]() {
            return latest_completed_scene_generation_ >= required_scene_generation;
        });
}

void UnrealImageCapture::markSceneGenerationComplete(uint64 scene_generation) const
{
    if (scene_generation == 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> Lock(response_cache_mutex_);
        latest_completed_scene_generation_ = FMath::Max(latest_completed_scene_generation_, scene_generation);
    }
    scene_generation_cv_.notify_all();
}

std::string UnrealImageCapture::makeRequestBatchKey(const std::vector<ImageRequest>& requests) const
{
    std::ostringstream Stream;
    for (const ImageRequest& Request : requests) {
        Stream << Request.camera_name
               << '|'
               << static_cast<int>(Request.image_type)
               << '|'
               << (Request.pixels_as_float ? 1 : 0)
               << '|'
               << (Request.compress ? 1 : 0)
               << ';';
    }

    return Stream.str();
}

bool UnrealImageCapture::tryServeCachedResponses(const std::vector<ImageRequest>& requests,
                                                 std::vector<ImageResponse>& responses) const
{
    const std::string CacheKey = makeRequestBatchKey(requests);
    const uint64 CurrentFrame = GetCurrentCaptureFrame();
    const msr::airlib::TTimePoint CurrentTime = msr::airlib::ClockFactory::get()->nowNanos();
    const bool bPureContinuousSceneBatch = IsPureContinuousSceneBatch(requests);
    const bool bHasContinuousSceneRequest = HasContinuousSceneRequest(requests);
    bool shouldQueueRefresh = false;
    bool shouldQueueDeferredSceneRefresh = false;
    bool shouldBypassCache = false;

    {
        std::lock_guard<std::mutex> Lock(response_cache_mutex_);
        auto It = response_cache_.find(CacheKey);
        if (It == response_cache_.end() || !It->second.has_value) {
            return false;
        }

        CachedBatch& Entry = It->second;

        if (bHasContinuousSceneRequest
            && Entry.scene_refresh_pending
            && CurrentFrame > Entry.pending_scene_readback_frame) {
            Entry.scene_refresh_pending = false;
            Entry.last_refresh_request_time = CurrentTime;
            return false;
        }

        if (bPureContinuousSceneBatch && Entry.time_stamp > 0) {
            const msr::airlib::TTimePoint CacheAge =
                CurrentTime > Entry.time_stamp ? CurrentTime - Entry.time_stamp : 0;
            const msr::airlib::TTimePoint SinceLastRefreshRequest =
                CurrentTime > Entry.last_refresh_request_time ? CurrentTime - Entry.last_refresh_request_time : 0;

            // In some NoDisplay sessions the Scene target keeps updating, but the
            // frame-gated cache refresh never re-arms. For Scene-only latest-frame
            // batches, fall back to a direct refresh once the cached image is old
            // enough, while rate-limiting that bypass to avoid same-frame storms.
            if (!Entry.refresh_in_progress
                && CacheAge >= kSceneLatestFrameFallbackIntervalNanos
                && SinceLastRefreshRequest >= kSceneLatestFrameFallbackIntervalNanos) {
                Entry.last_refresh_request_time = CurrentTime;
                shouldBypassCache = true;
            }
        }

        if (shouldBypassCache) {
            return false;
        }

        responses = Entry.responses;

        // Vulkan temp upload blocks are only reclaimed after the renderer reaches
        // its frame-end maintenance. Re-rendering the same request multiple times
        // inside one engine frame keeps ratcheting the temp-block high-water mark,
        // so only queue one real refresh per cache key per engine frame.
        if (!Entry.refresh_in_progress
            && !Entry.scene_refresh_pending
            && CurrentFrame > Entry.capture_frame
            && Entry.last_refresh_request_frame != CurrentFrame) {
            Entry.last_refresh_request_frame = CurrentFrame;
            Entry.last_refresh_request_time = CurrentTime;
            if (bHasContinuousSceneRequest) {
                Entry.pending_scene_readback_frame = CurrentFrame;
                Entry.scene_refresh_pending = true;
                shouldQueueDeferredSceneRefresh = true;
            }
            else {
                Entry.refresh_in_progress = true;
                shouldQueueRefresh = true;
            }
        }
    }

    if (shouldQueueDeferredSceneRefresh) {
        queueDeferredSceneRefresh(requests);
    }

    if (shouldQueueRefresh) {
        queueAsyncRefresh(requests);
    }

    return true;
}

void UnrealImageCapture::updateCachedResponses(const std::vector<ImageRequest>& requests,
                                               const std::vector<ImageResponse>& responses,
                                               uint64 capture_frame) const
{
    CachedBatch Entry;
    Entry.requests = requests;
    Entry.responses = responses;
    Entry.has_value = !responses.empty();
    Entry.scene_refresh_pending = false;
    Entry.refresh_in_progress = false;
    Entry.capture_frame = capture_frame;
    Entry.last_refresh_request_frame = capture_frame;
    Entry.pending_scene_readback_frame = 0;
    for (const ImageResponse& Response : responses) {
        Entry.time_stamp = FMath::Max(Entry.time_stamp, Response.time_stamp);
    }
    Entry.last_refresh_request_time = Entry.time_stamp;

    std::lock_guard<std::mutex> Lock(response_cache_mutex_);
    response_cache_[makeRequestBatchKey(requests)] = std::move(Entry);
}

void UnrealImageCapture::notifySceneContentChanged() const
{
    notifySceneChanged(true);
}

void UnrealImageCapture::notifyCameraPoseChanged() const
{
    notifySceneChanged(true);
}

void UnrealImageCapture::notifySceneChanged(bool advance_scene_generation) const
{
    uint64 RequiredSceneGeneration = 0;
    {
        std::lock_guard<std::mutex> Lock(response_cache_mutex_);
        response_cache_.clear();
        if (advance_scene_generation && strict_scene_freshness_after_pose_change_) {
            RequiredSceneGeneration = ++latest_pose_change_generation_;
        }
    }

    std::vector<ImageRequest> SceneRequests;
    for (const auto& Pair : cameras_->getMap()) {
        const std::string& CameraName = Pair.first;
        APIPCamera* Camera = Pair.second;
        if (Camera == nullptr || !Camera->getCameraTypeEnabled(ImageType::Scene)) {
            continue;
        }

        USceneCaptureComponent2D* Capture = Camera->getCaptureComponent(ImageType::Scene, false);
        if (Capture == nullptr
            || Capture->TextureTarget == nullptr
            || !Capture->bCaptureEveryFrame
            || !Capture->bCaptureOnMovement
            || !Capture->bAlwaysPersistRenderingState
            || Capture->bExcludeFromSceneTextureExtents) {
            continue;
        }

        SceneRequests.emplace_back(CameraName, ImageType::Scene, false, false);
    }

    if (SceneRequests.empty()) {
        return;
    }

    if (UAirBlueprintLib::IsInGameThread()) {
        scheduleDeferredSceneRefresh(SceneRequests, RequiredSceneGeneration);
    }
    else {
        UAirBlueprintLib::RunCommandOnGameThread([this, SceneRequests, RequiredSceneGeneration]() {
            scheduleDeferredSceneRefresh(SceneRequests, RequiredSceneGeneration);
        }, false);
    }
}

void UnrealImageCapture::queueAsyncRefresh(const std::vector<ImageRequest>& requests) const
{
    const std::string CacheKey = makeRequestBatchKey(requests);
    UAirBlueprintLib::RunCommandOnGameThread([this, requests, CacheKey]() {
        std::vector<ImageResponse> refreshedResponses;
        getSceneCaptureImage(requests, refreshedResponses, true, false);
        const uint64 CaptureFrame = GetCurrentCaptureFrame();

        std::lock_guard<std::mutex> Lock(response_cache_mutex_);
        CachedBatch& Entry = response_cache_[CacheKey];
        Entry.requests = requests;
        if (!refreshedResponses.empty()) {
            Entry.responses = refreshedResponses;
            Entry.has_value = true;
            Entry.time_stamp = 0;
            Entry.capture_frame = CaptureFrame;
            for (const ImageResponse& Response : refreshedResponses) {
                Entry.time_stamp = FMath::Max(Entry.time_stamp, Response.time_stamp);
            }
            Entry.last_refresh_request_time = Entry.time_stamp;
        }
        Entry.refresh_in_progress = false;
    }, false);
}

void UnrealImageCapture::queueDeferredSceneRefresh(const std::vector<ImageRequest>& requests,
                                                   uint64 required_scene_generation) const
{
    UAirBlueprintLib::RunCommandOnGameThread([this, requests, required_scene_generation]() {
        scheduleDeferredSceneRefresh(requests, required_scene_generation);
    }, false);
}

void UnrealImageCapture::scheduleDeferredSceneRefresh(const std::vector<ImageRequest>& requests,
                                                      uint64 required_scene_generation) const
{
    UGameViewportClient* GameViewport = nullptr;
    for (const ImageRequest& Request : requests) {
        if (!ShouldUseContinuousSceneReadback(Request)) {
            continue;
        }

        APIPCamera* Camera = cameras_->at(Request.camera_name);
        if (Camera != nullptr && Camera->GetWorld() != nullptr) {
            GameViewport = Camera->GetWorld()->GetGameViewport();
            if (GameViewport != nullptr) {
                break;
            }
        }
    }

    if (GameViewport == nullptr) {
        return;
    }

    const bool bSavedDisableWorldRendering = GameViewport->bDisableWorldRendering;
    if (bSavedDisableWorldRendering) {
        // Under NoDisplay, CaptureSceneDeferred() needs one real viewport draw to
        // push the Scene capture through the normal deferred render path.
        GameViewport->bDisableWorldRendering = false;
    }

    const bool bTrackSceneGeneration = required_scene_generation > 0;
    if (bSavedDisableWorldRendering || bTrackSceneGeneration) {
        auto EndDrawHandle = std::make_shared<FDelegateHandle>();
        *EndDrawHandle = GameViewport->OnEndDraw().AddLambda(
            [this, GameViewport, EndDrawHandle, bSavedDisableWorldRendering, required_scene_generation]() {
                GameViewport->bDisableWorldRendering = bSavedDisableWorldRendering;
                if (EndDrawHandle->IsValid()) {
                    GameViewport->OnEndDraw().Remove(*EndDrawHandle);
                }

                if (required_scene_generation > 0) {
                    ENQUEUE_RENDER_COMMAND(AirSimMarkStrictSceneGenerationComplete)
                    ([this, required_scene_generation](FRHICommandListImmediate& RHICmdList) {
                        markSceneGenerationComplete(required_scene_generation);
                    });
                }
            });
    }

    std::unordered_set<std::string> RefreshedCameras;
    for (const ImageRequest& Request : requests) {
        if (!ShouldUseContinuousSceneReadback(Request)) {
            continue;
        }

        if (!RefreshedCameras.insert(Request.camera_name).second) {
            continue;
        }

        APIPCamera* Camera = cameras_->at(Request.camera_name);
        if (Camera == nullptr) {
            continue;
        }

        USceneCaptureComponent2D* Capture = Camera->getCaptureComponent(Request.image_type, false);
        if (Capture == nullptr || Capture->TextureTarget == nullptr) {
            continue;
        }

        if (!Capture->bCaptureEveryFrame
            || !Capture->bCaptureOnMovement
            || !Capture->bAlwaysPersistRenderingState
            || Capture->bExcludeFromSceneTextureExtents) {
            continue;
        }

        Capture->CaptureSceneDeferred();
    }
}

bool UnrealImageCapture::getScreenshotScreen(ImageType image_type, std::vector<uint8_t>& compressedPng)
{
    FScreenshotRequest::RequestScreenshot(false); // This is an async operation
    return true;
}

void UnrealImageCapture::addScreenCaptureHandler(UWorld* world)
{
    static bool is_installed = false;

    if (!is_installed) {
        UGameViewportClient* ViewportClient = world->GetGameViewport();
        ViewportClient->OnScreenshotCaptured().Clear();
        ViewportClient->OnScreenshotCaptured().AddLambda(
            [this](int32 SizeX, int32 SizeY, const TArray<FColor>& Bitmap) {
                TArray<FColor>& RefBitmap = const_cast<TArray<FColor>&>(Bitmap);
                for (auto& Color : RefBitmap)
                    Color.A = 255;

                TArray<uint8_t> last_compressed_png;
                UAirBlueprintLib::CompressImageArray(SizeX, SizeY, RefBitmap, last_compressed_png);
                last_compressed_png_ = std::vector<uint8_t>(last_compressed_png.GetData(), last_compressed_png.GetData() + last_compressed_png.Num());
            });

        is_installed = true;
    }
}

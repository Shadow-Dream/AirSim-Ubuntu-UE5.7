#pragma once

#include "CoreMinimal.h"
#include "PIPCamera.h"
#include "common/ImageCaptureBase.hpp"
#include "common/common_utils/UniqueValueMap.hpp"
#include <condition_variable>
#include <mutex>
#include <unordered_map>

class UnrealImageCapture : public msr::airlib::ImageCaptureBase
{
public:
    typedef msr::airlib::ImageCaptureBase::ImageType ImageType;

    UnrealImageCapture(const common_utils::UniqueValueMap<std::string, APIPCamera*>* cameras,
                       bool strict_scene_freshness_after_pose_change = false);
    virtual ~UnrealImageCapture();

    virtual void getImages(const std::vector<ImageRequest>& requests, std::vector<ImageResponse>& responses) const override;
    void notifySceneContentChanged() const;
    void notifyCameraPoseChanged() const;

private:
    struct CachedBatch
    {
        std::vector<ImageRequest> requests;
        std::vector<ImageResponse> responses;
        msr::airlib::TTimePoint time_stamp = 0;
        uint64 capture_frame = 0;
        uint64 last_refresh_request_frame = 0;
        uint64 pending_scene_readback_frame = 0;
        msr::airlib::TTimePoint last_refresh_request_time = 0;
        bool has_value = false;
        bool scene_refresh_pending = false;
        bool refresh_in_progress = false;
    };

    void getSceneCaptureImage(const std::vector<msr::airlib::ImageCaptureBase::ImageRequest>& requests,
                              std::vector<msr::airlib::ImageCaptureBase::ImageResponse>& responses,
                              bool use_safe_method,
                              bool force_scene_snapshot) const;
    bool tryServeCachedResponses(const std::vector<ImageRequest>& requests,
                                 std::vector<ImageResponse>& responses) const;
    void updateCachedResponses(const std::vector<ImageRequest>& requests,
                               const std::vector<ImageResponse>& responses,
                               uint64 capture_frame) const;
    void queueAsyncRefresh(const std::vector<ImageRequest>& requests) const;
    void queueDeferredSceneRefresh(const std::vector<ImageRequest>& requests,
                                   uint64 required_scene_generation = 0) const;
    std::string makeRequestBatchKey(const std::vector<ImageRequest>& requests) const;
    bool shouldUseLatestFrameCache(const std::vector<ImageRequest>& requests) const;
    bool shouldEnforceStrictSceneFreshness(const std::vector<ImageRequest>& requests) const;
    uint64 getRequiredSceneGeneration(const std::vector<ImageRequest>& requests) const;
    bool waitForRequiredSceneGeneration(const std::vector<ImageRequest>& requests,
                                        uint64 required_scene_generation) const;
    void markSceneGenerationComplete(uint64 scene_generation) const;
    void scheduleDeferredSceneRefresh(const std::vector<ImageRequest>& requests,
                                      uint64 required_scene_generation = 0) const;
    void notifySceneChanged(bool advance_scene_generation) const;

    void addScreenCaptureHandler(UWorld* world);
    bool getScreenshotScreen(ImageType image_type, std::vector<uint8_t>& compressedPng);

    bool updateCameraVisibility(APIPCamera* camera, const msr::airlib::ImageCaptureBase::ImageRequest& request);

private:
    const common_utils::UniqueValueMap<std::string, APIPCamera*>* cameras_;
    std::vector<uint8_t> last_compressed_png_;
    const bool strict_scene_freshness_after_pose_change_;
    mutable std::mutex response_cache_mutex_;
    mutable std::condition_variable scene_generation_cv_;
    mutable std::unordered_map<std::string, CachedBatch> response_cache_;
    mutable uint64 latest_pose_change_generation_ = 0;
    mutable uint64 latest_completed_scene_generation_ = 0;
};

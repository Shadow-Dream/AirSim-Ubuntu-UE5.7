#include "RenderRequest.h"
#include "TextureResource.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Async/TaskGraphInterfaces.h"
#include "ImageUtils.h"

#include "AirBlueprintLib.h"
#include "Async/Async.h"

RenderRequest::RenderRequest(UGameViewportClient* game_viewport, std::function<void()>&& query_camera_pose_cb)
    : params_(nullptr), results_(nullptr), req_size_(0), wait_signal_(new msr::airlib::WorkerThreadSignal), game_viewport_(game_viewport), query_camera_pose_cb_(std::move(query_camera_pose_cb))
{
}

RenderRequest::~RenderRequest()
{
}

// read pixels from render target using render thread, then compress the result into PNG
// argument on the thread that calls this method.
void RenderRequest::getScreenshot(std::shared_ptr<RenderParams> params[], std::vector<std::shared_ptr<RenderResult>>& results, unsigned int req_size, bool use_safe_method)
{
    //TODO: is below really needed?
    for (unsigned int i = 0; i < req_size; ++i) {
        results.push_back(std::make_shared<RenderResult>());

        if (!params[i]->pixels_as_float)
            results[i]->bmp.Reset();
        else
            results[i]->bmp_float.Reset();
        results[i]->time_stamp = 0;
    }

    //make sure we are not on the rendering thread
    CheckNotBlockedOnRenderThread();

    if (use_safe_method) {
        for (unsigned int i = 0; i < req_size; ++i) {
            //TODO: below doesn't work right now because it must be running in game thread
            FIntPoint img_size;
            if (!params[i]->pixels_as_float) {
                if (params[i]->read_latest_without_capture) {
                    auto param = params[i];
                    auto result = results[i];
                    auto readback_signal = std::make_shared<msr::airlib::WorkerThreadSignal>();
                    ENQUEUE_RENDER_COMMAND(ReadLatestSceneRenderTarget)
                    (
                        [param, result, readback_signal](FRHICommandListImmediate& RHICmdList) {
                            auto* rt_resource = param->render_target->GetRenderTargetResource();
                            if (rt_resource != nullptr) {
                                FIntPoint size;
                                auto flags = RenderRequest::setupRenderResource(rt_resource, param.get(), result.get(), size);
                                RHICmdList.ReadSurfaceData(
                                    rt_resource->GetRenderTargetTexture(),
                                    FIntRect(0, 0, size.X, size.Y),
                                    result->bmp,
                                    flags);
                                result->time_stamp = msr::airlib::ClockFactory::get()->nowNanos();
                            }
                            readback_signal->signal();
                        });

                    while (!readback_signal->waitFor(5)) {
                        UE_LOG(LogTemp, Warning, TEXT("Failed: timeout waiting for latest render-target readback"));
                    }
                }
                else {
                    //below is documented method but more expensive because it forces flush
                    if (params[i]->render_component != nullptr) {
                        // AirSim RPC image requests are explicit snapshots. Keep them out of
                        // persistent render history so repeated capture stays RSS-stable.
                        params[i]->render_component->bCameraCutThisFrame = true;
                        params[i]->render_component->bAlwaysPersistRenderingState = false;
                        params[i]->render_component->bExcludeFromSceneTextureExtents = true;
                        params[i]->render_component->CaptureScene();
                    }
                    FTextureRenderTargetResource* rt_resource = params[i]->render_target->GameThread_GetRenderTargetResource();
                    auto flags = setupRenderResource(rt_resource, params[i].get(), results[i].get(), img_size);
                    rt_resource->ReadPixels(results[i]->bmp, flags);
                    results[i]->time_stamp = msr::airlib::ClockFactory::get()->nowNanos();
                }
            }
            else {
                if (params[i]->read_latest_without_capture) {
                    auto param = params[i];
                    auto result = results[i];
                    auto readback_signal = std::make_shared<msr::airlib::WorkerThreadSignal>();
                    ENQUEUE_RENDER_COMMAND(ReadLatestFloatRenderTarget)
                    (
                        [param, result, readback_signal](FRHICommandListImmediate& RHICmdList) {
                            auto* rt_resource = param->render_target->GetRenderTargetResource();
                            if (rt_resource != nullptr) {
                                FIntPoint size;
                                RenderRequest::setupRenderResource(rt_resource, param.get(), result.get(), size);
                                RHICmdList.ReadSurfaceFloatData(
                                    rt_resource->GetRenderTargetTexture(),
                                    FIntRect(0, 0, size.X, size.Y),
                                    result->bmp_float,
                                    CubeFace_PosX,
                                    0,
                                    0);
                                result->time_stamp = msr::airlib::ClockFactory::get()->nowNanos();
                            }
                            readback_signal->signal();
                        });

                    while (!readback_signal->waitFor(5)) {
                        UE_LOG(LogTemp, Warning, TEXT("Failed: timeout waiting for latest float render-target readback"));
                    }
                }
                else {
                    if (params[i]->render_component != nullptr) {
                        params[i]->render_component->bCameraCutThisFrame = true;
                        params[i]->render_component->bAlwaysPersistRenderingState = false;
                        params[i]->render_component->bExcludeFromSceneTextureExtents = true;
                        params[i]->render_component->CaptureScene();
                    }
                    FTextureRenderTargetResource* rt_resource = params[i]->render_target->GameThread_GetRenderTargetResource();
                    setupRenderResource(rt_resource, params[i].get(), results[i].get(), img_size);
                    rt_resource->ReadFloat16Pixels(results[i]->bmp_float);
                    results[i]->time_stamp = msr::airlib::ClockFactory::get()->nowNanos();
                }
            }
        }
    }
    else {
        //wait for render thread to pick up our task
        params_ = params;
        results_ = results.data();
        req_size_ = req_size;

        // Queue up the task of querying camera pose in the game thread and synchronizing render thread with camera pose
        AsyncTask(ENamedThreads::GameThread, [this]() {
            check(IsInGameThread());

            saved_DisableWorldRendering_ = game_viewport_->bDisableWorldRendering;
            game_viewport_->bDisableWorldRendering = 0;
            end_draw_handle_ = game_viewport_->OnEndDraw().AddLambda([this] {
                check(IsInGameThread());

                // capture CameraPose for this frame
                query_camera_pose_cb_();

                // The completion is called immeidately after GameThread sends the
                // rendering commands to RenderThread. Hence, our ExecuteTask will
                // execute *immediately* after RenderThread renders the scene!
                RenderRequest* This = this;
                ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)
                (
                    [This](FRHICommandListImmediate& RHICmdList) {
                        This->ExecuteTask();
                    });

                game_viewport_->bDisableWorldRendering = saved_DisableWorldRendering_;

                assert(end_draw_handle_.IsValid());
                game_viewport_->OnEndDraw().Remove(end_draw_handle_);
            });

            // while we're still on GameThread, enqueue request for capture the scene!
            for (unsigned int i = 0; i < req_size_; ++i) {
                params_[i]->render_component->CaptureSceneDeferred();
            }
        });

        // wait for this task to complete
        while (!wait_signal_->waitFor(5)) {
            // log a message and continue wait
            // lamda function still references a few objects for which there is no refcount.
            // Walking away will cause memory corruption, which is much more difficult to debug.
            UE_LOG(LogTemp, Warning, TEXT("Failed: timeout waiting for screenshot"));
        }
    }

    for (unsigned int i = 0; i < req_size; ++i) {
        if (!params[i]->pixels_as_float) {
            if (results[i]->width != 0 && results[i]->height != 0) {
                results[i]->image_data_uint8.SetNumUninitialized(results[i]->width * results[i]->height * 3, false);
                if (params[i]->compress)
                    UAirBlueprintLib::CompressImageArray(results[i]->width, results[i]->height, results[i]->bmp, results[i]->image_data_uint8);
                else {
                    uint8* ptr = results[i]->image_data_uint8.GetData();
                    for (const auto& item : results[i]->bmp) {
                        *ptr++ = item.B;
                        *ptr++ = item.G;
                        *ptr++ = item.R;
                    }
                }
            }
        }
        else {
            results[i]->image_data_float.SetNumUninitialized(results[i]->width * results[i]->height);
            float* ptr = results[i]->image_data_float.GetData();
            for (const auto& item : results[i]->bmp_float) {
                *ptr++ = item.R.GetFloat();
            }
        }
    }
}

FReadSurfaceDataFlags RenderRequest::setupRenderResource(const FTextureRenderTargetResource* rt_resource, const RenderParams* params, RenderResult* result, FIntPoint& size)
{
    size = rt_resource->GetSizeXY();
    result->width = size.X;
    result->height = size.Y;
    FReadSurfaceDataFlags flags(RCM_UNorm, CubeFace_MAX);
    flags.SetLinearToGamma(false);

    return flags;
}

void RenderRequest::ExecuteTask()
{
    if (params_ != nullptr && req_size_ > 0) {
        for (unsigned int i = 0; i < req_size_; ++i) {
            FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
            auto rt_resource = params_[i]->render_target->GetRenderTargetResource();
            if (rt_resource != nullptr) {
                const FTextureRHIRef& rhi_texture = rt_resource->GetRenderTargetTexture();
                FIntPoint size;
                auto flags = setupRenderResource(rt_resource, params_[i].get(), results_[i].get(), size);

                //should we be using ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER which was in original commit by @saihv
                //https://github.com/Microsoft/AirSim/pull/162/commits/63e80c43812300a8570b04ed42714a3f6949e63f#diff-56b790f9394f7ca1949ddbb320d8456fR64
                if (!params_[i]->pixels_as_float) {
                    //below is undocumented method that avoids flushing, but it seems to segfault every 2000 or so calls
                    RHICmdList.ReadSurfaceData(
                        rhi_texture,
                        FIntRect(0, 0, size.X, size.Y),
                        results_[i]->bmp,
                        flags);
                }
                else {
                    RHICmdList.ReadSurfaceFloatData(
                        rhi_texture,
                        FIntRect(0, 0, size.X, size.Y),
                        results_[i]->bmp_float,
                        CubeFace_PosX,
                        0,
                        0);
                }
            }

            results_[i]->time_stamp = msr::airlib::ClockFactory::get()->nowNanos();
        }

        req_size_ = 0;
        params_ = nullptr;
        results_ = nullptr;

        wait_signal_->signal();
    }
}

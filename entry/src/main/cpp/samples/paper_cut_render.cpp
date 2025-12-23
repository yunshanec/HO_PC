//
// Created on 2025/12/21.
// 剪纸渲染器实现 - 将PaperCutEngine集成到NAPI架构
//

#include "paper_cut_render.h"
#include "common/log_common.h"
#include <hilog/log.h>
#include <unordered_map>

#define LOG_TAG "PaperCutRender"
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, LOG_TAG, __VA_ARGS__))

std::unordered_map<std::string, PaperCutRender *> PaperCutRender::g_instance;

PaperCutRender::~PaperCutRender()
{
    LOGI("~PaperCutRender");
    engine_.reset();
    nativeWindow_ = nullptr;
    previewWindow_ = nullptr;
}

void PaperCutRender::SetNativeWindow(OHNativeWindow *nativeWindow)
{
    nativeWindow_ = nativeWindow;
    if (nativeWindow_ && engine_) {
        uint64_t width = 2048;
        uint64_t height = 2048;
        engine_->Initialize(nativeWindow_, width, height);
        LOGI("NativeWindow set for editor");
    }
}

void PaperCutRender::SetPreviewWindow(OHNativeWindow *nativeWindow)
{
    previewWindow_ = nativeWindow;
    if (previewWindow_ && engine_) {
        engine_->SetPreviewWindow(previewWindow_);
        LOGI("PreviewWindow set");
    }
}

PaperCutRender *PaperCutRender::GetInstance(std::string &id)
{
    if (g_instance.find(id) == g_instance.end()) {
        PaperCutRender *render = new PaperCutRender(id);
        g_instance[id] = render;
        return render;
    } else {
        return g_instance[id];
    }
}

void PaperCutRender::Release(std::string &id)
{
    auto it = g_instance.find(id);
    if (it != g_instance.end()) {
        delete it->second;
        g_instance.erase(it);
    }
}

PaperCutRender *PaperCutRender::GetRenderFromArgs(napi_env env, napi_callback_info info)
{
    napi_value thisArg = nullptr;
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, &thisArg, nullptr);
    
    napi_value exportInstance = nullptr;
    if (napi_get_named_property(env, thisArg, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) != napi_ok) {
        LOGE("GetRenderFromArgs: napi_get_named_property fail");
        return nullptr;
    }
    
    OH_NativeXComponent *nativeXComponent = nullptr;
    if (napi_unwrap(env, exportInstance, reinterpret_cast<void **>(&nativeXComponent)) != napi_ok) {
        LOGE("GetRenderFromArgs: napi_unwrap fail");
        return nullptr;
    }
    
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    if (OH_NativeXComponent_GetXComponentId(nativeXComponent, idStr, &idSize) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        LOGE("GetRenderFromArgs: OH_NativeXComponent_GetXComponentId fail");
        return nullptr;
    }
    
    std::string id(idStr);
    return GetInstance(id);
}

// NAPI回调函数
static void OnSurfaceCreatedCB(OH_NativeXComponent *component, void *window)
{
    LOGI("PaperCutRender OnSurfaceCreatedCB");
    if ((component == nullptr) || (window == nullptr)) {
        LOGE("OnSurfaceCreatedCB: component or window is null");
        return;
    }
    char idStr[OH_XCOMPONENT_ID_LEN_MAX + 1] = {'\0'};
    uint64_t idSize = OH_XCOMPONENT_ID_LEN_MAX + 1;
    if (OH_NativeXComponent_GetXComponentId(component, idStr, &idSize) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS) {
        LOGE("OnSurfaceCreatedCB: Unable to get XComponent id");
        return;
    }
    std::string id(idStr);
    auto render = PaperCutRender::GetInstance(id);
    OHNativeWindow *nativeWindow = static_cast<OHNativeWindow *>(window);
    
    // 根据ID判断是编辑器还是预览器
    if (id.find("preview") != std::string::npos) {
        render->SetPreviewWindow(nativeWindow);
    } else {
        render->SetNativeWindow(nativeWindow);
    }
    
    // 获取XComponent尺寸
    uint64_t width;
    uint64_t height;
    int32_t xSize = OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
    if ((xSize == OH_NATIVEXCOMPONENT_RESULT_SUCCESS) && (render != nullptr)) {
        LOGI("PaperCutRender xComponent width = %{public}llu, height = %{public}llu", width, height);
    }
}

static void OnSurfaceDestroyedCB(OH_NativeXComponent *component, void *window)
{
    LOGI("PaperCutRender OnSurfaceDestroyedCB");
    // 清理资源
}

void PaperCutRender::RegisterCallback(OH_NativeXComponent *nativeXComponent)
{
    LOGI("PaperCutRender register callback");
    renderCallback_.OnSurfaceCreated = OnSurfaceCreatedCB;
    renderCallback_.OnSurfaceDestroyed = OnSurfaceDestroyedCB;
    // Callback must be initialized
    renderCallback_.DispatchTouchEvent = nullptr;
    renderCallback_.OnSurfaceChanged = nullptr;
    OH_NativeXComponent_RegisterCallback(nativeXComponent, &renderCallback_);
}

void PaperCutRender::Export(napi_env env, napi_value exports)
{
    if ((env == nullptr) || (exports == nullptr)) {
        LOGE("Export: env or exports is null");
        return;
    }
    
    napi_property_descriptor desc[] = {
        {"drawPaperCut", nullptr, DrawPaperCut, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"drawPaperCutPreview", nullptr, DrawPaperCutPreview, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"initializeEngine", nullptr, InitializeEngine, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startDrawing", nullptr, StartDrawing, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"addPoint", nullptr, AddPoint, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"finishDrawing", nullptr, FinishDrawing, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setToolMode", nullptr, SetToolMode, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setFoldMode", nullptr, SetFoldMode, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setPaperType", nullptr, SetPaperType, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setPaperColor", nullptr, SetPaperColor, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"undo", nullptr, Undo, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"redo", nullptr, Redo, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"clear", nullptr, Clear, nullptr, nullptr, nullptr, napi_default, nullptr}
    };
    
    if (napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc) != napi_ok) {
        LOGE("Export: napi_define_properties failed");
    }
}

napi_value PaperCutRender::DrawPaperCut(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (render && render->engine_ && render->nativeWindow_) {
        render->engine_->Render();
    }
    return nullptr;
}

napi_value PaperCutRender::DrawPaperCutPreview(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (render && render->engine_ && render->previewWindow_) {
        render->engine_->RenderPreview();
    }
    return nullptr;
}

napi_value PaperCutRender::InitializeEngine(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 3) {
        LOGE("InitializeEngine: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        LOGE("InitializeEngine: render or engine is null");
        return nullptr;
    }
    
    // 获取nativeWindow (从第一个参数，但实际上应该从XComponent获取)
    // 这里我们假设nativeWindow已经通过SetNativeWindow设置
    int32_t width = 2048;
    int32_t height = 2048;
    
    napi_get_value_int32(env, args[1], &width);
    napi_get_value_int32(env, args[2], &height);
    
    if (render->nativeWindow_) {
        render->engine_->Initialize(render->nativeWindow_, width, height);
        LOGI("Engine initialized: %dx%d", width, height);
    }
    
    return nullptr;
}

napi_value PaperCutRender::StartDrawing(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 2) {
        LOGE("StartDrawing: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    double x, y;
    napi_get_value_double(env, args[0], &x);
    napi_get_value_double(env, args[1], &y);
    
    render->engine_->StartDrawing(static_cast<float>(x), static_cast<float>(y));
    return nullptr;
}

napi_value PaperCutRender::AddPoint(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 2) {
        LOGE("AddPoint: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    double x, y;
    napi_get_value_double(env, args[0], &x);
    napi_get_value_double(env, args[1], &y);
    
    render->engine_->AddPoint(static_cast<float>(x), static_cast<float>(y));
    return nullptr;
}

napi_value PaperCutRender::FinishDrawing(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (render && render->engine_) {
        render->engine_->FinishDrawing();
    }
    return nullptr;
}

napi_value PaperCutRender::SetToolMode(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        LOGE("SetToolMode: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    int32_t mode;
    napi_get_value_int32(env, args[0], &mode);
    
    render->engine_->SetToolMode(static_cast<ToolMode>(mode));
    return nullptr;
}

napi_value PaperCutRender::SetFoldMode(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        LOGE("SetFoldMode: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    int32_t mode;
    napi_get_value_int32(env, args[0], &mode);
    
    render->engine_->SetFoldMode(static_cast<FoldMode>(mode));
    return nullptr;
}

napi_value PaperCutRender::SetPaperType(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        LOGE("SetPaperType: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    int32_t type;
    napi_get_value_int32(env, args[0], &type);
    
    render->engine_->SetPaperType(static_cast<PaperType>(type));
    return nullptr;
}

napi_value PaperCutRender::SetPaperColor(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        LOGE("SetPaperColor: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    uint32_t color;
    napi_get_value_uint32(env, args[0], &color);
    
    render->engine_->SetPaperColor(color);
    return nullptr;
}

napi_value PaperCutRender::Undo(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (render && render->engine_) {
        render->engine_->Undo();
    }
    return nullptr;
}

napi_value PaperCutRender::Redo(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (render && render->engine_) {
        render->engine_->Redo();
    }
    return nullptr;
}

napi_value PaperCutRender::Clear(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (render && render->engine_) {
        render->engine_->Clear();
    }
    return nullptr;
}




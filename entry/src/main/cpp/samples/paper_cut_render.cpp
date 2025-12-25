//
// Created on 2025/12/21.
// 剪纸渲染器实现 - 将PaperCutEngine集成到NAPI架构
//

#include "paper_cut_render.h"
#include "common/log_common.h"
#include <hilog/log.h>
#include <unordered_map>

// hilog/log.h 已定义 LOG_TAG（可能为 NULL），避免宏重定义带来的告警/潜在 Werror
static constexpr const char* LOG_LABEL = "PaperCutRender";
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, LOG_LABEL, __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, LOG_LABEL, __VA_ARGS__))

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
        // %{public}llu 期望 unsigned long long，避免不同架构下 uint64_t typedef 导致的格式告警
        LOGI("PaperCutRender xComponent width = %{public}llu, height = %{public}llu",
             static_cast<unsigned long long>(width),
             static_cast<unsigned long long>(height));
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
        {"clear", nullptr, Clear, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getActions", nullptr, GetActions, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setActions", nullptr, SetActions, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setZoom", nullptr, SetZoom, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setPan", nullptr, SetPan, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"setPreviewWindow", nullptr, SetPreviewWindow, nullptr, nullptr, nullptr, napi_default, nullptr}
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

napi_value PaperCutRender::GetActions(napi_env env, napi_callback_info info)
{
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        napi_value emptyArray;
        napi_create_array(env, &emptyArray);
        return emptyArray;
    }
    
    std::vector<Action> actions = render->engine_->GetActions();
    napi_value result;
    napi_create_array(env, &result);
    
    for (size_t i = 0; i < actions.size(); i++) {
        const Action& action = actions[i];
        
        napi_value actionObj;
        napi_create_object(env, &actionObj);
        
        // id
        napi_value idValue;
        napi_create_string_utf8(env, action.id.c_str(), NAPI_AUTO_LENGTH, &idValue);
        napi_set_named_property(env, actionObj, "id", idValue);
        
        // type
        napi_value typeValue;
        napi_create_int32(env, static_cast<int32_t>(action.type), &typeValue);
        napi_set_named_property(env, actionObj, "type", typeValue);
        
        // tool
        napi_value toolValue;
        napi_create_int32(env, static_cast<int32_t>(action.tool), &toolValue);
        napi_set_named_property(env, actionObj, "tool", toolValue);
        
        // timestamp
        napi_value timestampValue;
        napi_create_int64(env, action.timestamp, &timestampValue);
        napi_set_named_property(env, actionObj, "timestamp", timestampValue);
        
        // points
        napi_value pointsArray;
        napi_create_array(env, &pointsArray);
        for (size_t j = 0; j < action.points.size(); j++) {
            napi_value pointObj;
            napi_create_object(env, &pointObj);
            
            napi_value xValue;
            napi_create_double(env, action.points[j].x, &xValue);
            napi_set_named_property(env, pointObj, "x", xValue);
            
            napi_value yValue;
            napi_create_double(env, action.points[j].y, &yValue);
            napi_set_named_property(env, pointObj, "y", yValue);
            
            napi_set_element(env, pointsArray, j, pointObj);
        }
        napi_set_named_property(env, actionObj, "points", pointsArray);
        
        napi_set_element(env, result, i, actionObj);
    }
    
    return result;
}

napi_value PaperCutRender::SetActions(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    if (argc < 1) {
        LOGE("SetActions: insufficient arguments");
        return nullptr;
    }
    
    bool isArray = false;
    if (napi_is_array(env, args[0], &isArray) != napi_ok || !isArray) {
        LOGE("SetActions: arg0 is not array");
        return nullptr;
    }
    
    uint32_t length = 0;
    napi_get_array_length(env, args[0], &length);
    
    std::vector<Action> actions;
    actions.reserve(length);
    
    for (uint32_t i = 0; i < length; i++) {
        napi_value actionObj;
        if (napi_get_element(env, args[0], i, &actionObj) != napi_ok) {
            continue;
        }
        
        Action action;
        action.id = std::to_string(i);
        action.type = ActionType::STROKE;
        action.tool = ToolMode::DRAFT_PEN;
        action.timestamp = 0;
        
        // id
        napi_value idValue;
        if (napi_get_named_property(env, actionObj, "id", &idValue) == napi_ok) {
            size_t strLen = 0;
            napi_get_value_string_utf8(env, idValue, nullptr, 0, &strLen);
            if (strLen > 0) {
                std::vector<char> buf(strLen + 1, '\0');
                size_t readLen = 0;
                napi_get_value_string_utf8(env, idValue, buf.data(), buf.size(), &readLen);
                action.id = std::string(buf.data(), readLen);
            }
        }
        
        // type (0=CUT, 1=STROKE)
        napi_value typeValue;
        int32_t typeInt = 1;
        if (napi_get_named_property(env, actionObj, "type", &typeValue) == napi_ok) {
            napi_get_value_int32(env, typeValue, &typeInt);
        }
        action.type = (typeInt == 0) ? ActionType::CUT : ActionType::STROKE;
        
        // tool (0..3)
        napi_value toolValue;
        int32_t toolInt = 2;
        if (napi_get_named_property(env, actionObj, "tool", &toolValue) == napi_ok) {
            napi_get_value_int32(env, toolValue, &toolInt);
        }
        action.tool = static_cast<ToolMode>(toolInt);
        
        // timestamp
        napi_value tsValue;
        int64_t tsInt = 0;
        if (napi_get_named_property(env, actionObj, "timestamp", &tsValue) == napi_ok) {
            napi_get_value_int64(env, tsValue, &tsInt);
        }
        action.timestamp = tsInt;
        
        // points
        napi_value pointsValue;
        bool pointsIsArray = false;
        if (napi_get_named_property(env, actionObj, "points", &pointsValue) == napi_ok &&
            napi_is_array(env, pointsValue, &pointsIsArray) == napi_ok && pointsIsArray) {
            uint32_t pLen = 0;
            napi_get_array_length(env, pointsValue, &pLen);
            action.points.reserve(pLen);
            for (uint32_t j = 0; j < pLen; j++) {
                napi_value pObj;
                if (napi_get_element(env, pointsValue, j, &pObj) != napi_ok) continue;
                
                napi_value xVal;
                napi_value yVal;
                double x = 0;
                double y = 0;
                if (napi_get_named_property(env, pObj, "x", &xVal) == napi_ok) {
                    napi_get_value_double(env, xVal, &x);
                }
                if (napi_get_named_property(env, pObj, "y", &yVal) == napi_ok) {
                    napi_get_value_double(env, yVal, &y);
                }
                action.points.push_back(Point{static_cast<float>(x), static_cast<float>(y)});
            }
        }
        
        actions.push_back(action);
    }
    
    render->engine_->SetActions(actions);
    return nullptr;
}

napi_value PaperCutRender::SetZoom(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 1) {
        LOGE("SetZoom: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    double zoom;
    napi_get_value_double(env, args[0], &zoom);
    
    render->engine_->SetZoom(static_cast<float>(zoom));
    return nullptr;
}

napi_value PaperCutRender::SetPan(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    if (argc < 2) {
        LOGE("SetPan: insufficient arguments");
        return nullptr;
    }
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        return nullptr;
    }
    
    double x, y;
    napi_get_value_double(env, args[0], &x);
    napi_get_value_double(env, args[1], &y);
    
    render->engine_->SetPan(static_cast<float>(x), static_cast<float>(y));
    return nullptr;
}

napi_value PaperCutRender::SetPreviewWindow(napi_env env, napi_callback_info info)
{
    // 注意: previewWindow实际上是通过OnSurfaceCreatedCB回调设置的
    // 这个函数主要是为了保持接口一致性，接受nativeWindow ID参数但不做实际处理
    // 因为窗口已经在回调中设置了
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    
    PaperCutRender *render = GetRenderFromArgs(env, info);
    if (!render || !render->engine_) {
        LOGE("SetPreviewWindow: render or engine is null");
        return nullptr;
    }
    
    // 窗口已经通过OnSurfaceCreatedCB回调设置，这里只记录日志
    LOGI("SetPreviewWindow called (window already set via callback)");
    
    return nullptr;
}




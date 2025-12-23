//
// Created on 2025/12/21.
// 剪纸渲染器头文件 - 将PaperCutEngine集成到NAPI架构
//

#ifndef PAPERCUTTING_PAPER_CUT_RENDER_H
#define PAPERCUTTING_PAPER_CUT_RENDER_H

#include <ace/xcomponent/native_interface_xcomponent.h>
#include <native_window/external_window.h>
#include <string>
#include <memory>
#include "paper_cut_engine.h"
#include "napi/native_api.h"

class PaperCutRender {
public:
    PaperCutRender() = default;
    ~PaperCutRender();
    explicit PaperCutRender(std::string id) : id_(id), engine_(std::make_unique<PaperCutEngine>()) {}
    
    // NAPI静态方法
    static napi_value DrawPaperCut(napi_env env, napi_callback_info info);
    static napi_value DrawPaperCutPreview(napi_env env, napi_callback_info info);
    static napi_value InitializeEngine(napi_env env, napi_callback_info info);
    static napi_value StartDrawing(napi_env env, napi_callback_info info);
    static napi_value AddPoint(napi_env env, napi_callback_info info);
    static napi_value FinishDrawing(napi_env env, napi_callback_info info);
    static napi_value SetToolMode(napi_env env, napi_callback_info info);
    static napi_value SetFoldMode(napi_env env, napi_callback_info info);
    static napi_value SetPaperType(napi_env env, napi_callback_info info);
    static napi_value SetPaperColor(napi_env env, napi_callback_info info);
    static napi_value Undo(napi_env env, napi_callback_info info);
    static napi_value Redo(napi_env env, napi_callback_info info);
    static napi_value Clear(napi_env env, napi_callback_info info);
    static napi_value GetActions(napi_env env, napi_callback_info info);
    static napi_value SetZoom(napi_env env, napi_callback_info info);
    static napi_value SetPan(napi_env env, napi_callback_info info);
    
    // 导出NAPI接口
    void Export(napi_env env, napi_value exports);
    void RegisterCallback(OH_NativeXComponent *nativeXComponent);
    
    // 设置NativeWindow
    void SetNativeWindow(OHNativeWindow *nativeWindow);
    void SetPreviewWindow(OHNativeWindow *nativeWindow);
    
    // 获取实例
    static PaperCutRender *GetInstance(std::string &id);
    static void Release(std::string &id);
    
    std::string id_;
    
private:
    static std::unordered_map<std::string, PaperCutRender *> g_instance;
    
    std::unique_ptr<PaperCutEngine> engine_;
    OHNativeWindow *nativeWindow_ = nullptr;
    OHNativeWindow *previewWindow_ = nullptr;
    OH_NativeXComponent_Callback renderCallback_;
    
    // 从NAPI参数获取实例
    static PaperCutRender *GetRenderFromArgs(napi_env env, napi_callback_info info);
};

#endif // PAPERCUTTING_PAPER_CUT_RENDER_H




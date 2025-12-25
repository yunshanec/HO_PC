//
// Created on 2025/12/21.
// 剪纸绘制引擎实现
//

#include "paper_cut_engine.h"
#include <hilog/log.h>
#include <cmath>
#include <algorithm>
#include <functional>
#include <native_drawing/drawing_bitmap.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_matrix.h>
#include <sstream>
#include <chrono>
#include <sys/mman.h>

// LOG_TAG is already defined in hilog/log.h, so we don't redefine it
#define LOGI(...) ((void)OH_LOG_Print(LOG_APP, LOG_INFO, LOG_DOMAIN, "PaperCutEngine", __VA_ARGS__))
#define LOGE(...) ((void)OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, "PaperCutEngine", __VA_ARGS__))

namespace {
// 用真圆弧生成扇形 wedge，避免 lineTo 采样造成的“锯齿/折线感”
OH_Drawing_Path* CreateWedgePath(float radius, float startAngleRad, float sweepAngleRad)
{
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    const float startX = cos(startAngleRad) * radius;
    const float startY = sin(startAngleRad) * radius;
    const float startDeg = startAngleRad * 180.0f / static_cast<float>(M_PI);
    const float sweepDeg = sweepAngleRad * 180.0f / static_cast<float>(M_PI);
    
    OH_Drawing_PathMoveTo(path, 0, 0);
    OH_Drawing_PathLineTo(path, startX, startY);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(-radius, -radius, radius, radius);
    OH_Drawing_PathAddArc(path, rect, startDeg, sweepDeg);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_PathLineTo(path, 0, 0);
    OH_Drawing_PathClose(path);
    return path;
}
} // namespace

PaperCutEngine::PaperCutEngine()
    : nativeWindow_(nullptr)
    , previewWindow_(nullptr)
    , canvasWidth_(CANVAS_SIZE)
    , canvasHeight_(CANVAS_SIZE)
    , currentToolMode_(ToolMode::SCISSORS)
    , foldMode_(FoldMode::FOUR)
    , paperType_(PaperType::CIRCLE)
    , paperColor_(0xFFC4161C)  // 默认红色
    , isDrawing_(false)
    , bezierClosed_(false)
    , bezierSharpMode_(false)
    , inputBitmap_(nullptr)
    , inputCanvas_(nullptr)
    , inputDirty_(false)
    , offscreenBitmap_(nullptr)
    , offscreenCanvas_(nullptr)
    , offscreenDirty_(false)
    , layersInitialized_(false)
{
}

PaperCutEngine::~PaperCutEngine()
{
    DestroyLayers();
    nativeWindow_ = nullptr;
}

bool PaperCutEngine::Initialize(OHNativeWindow* window, int width, int height)
{
    if (!window) {
        LOGE("NativeWindow is null");
        return false;
    }
    
    nativeWindow_ = window;
    canvasWidth_ = width > 0 ? width : CANVAS_SIZE;
    canvasHeight_ = height > 0 ? height : CANVAS_SIZE;
    
    // 初始化3层画布架构
    InitializeLayers(canvasWidth_, canvasHeight_);
    
    LOGI("Engine initialized: %dx%d", canvasWidth_, canvasHeight_);
    return true;
}

void PaperCutEngine::SetPreviewWindow(OHNativeWindow* window)
{
    previewWindow_ = window;
    LOGI("Preview window set");
}

void PaperCutEngine::Render()
{
    if (!nativeWindow_) {
        LOGE("NativeWindow not initialized");
        return;
    }
    
    // 请求Buffer
    OHNativeWindowBuffer* buffer = nullptr;
    int fenceFd;
    int ret = OH_NativeWindow_NativeWindowRequestBuffer(nativeWindow_, &buffer, &fenceFd);
    if (ret != 0 || !buffer) {
        LOGE("Failed to request buffer");
        return;
    }
    
    // 获取Buffer信息
    BufferHandle* handle = OH_NativeWindow_GetBufferHandleFromNative(buffer);
    if (!handle) {
        LOGE("Failed to get buffer handle");
        return;
    }
    
    // 使用mmap映射内存（按照官方示例的方式）
    void* mappedAddr = mmap(handle->virAddr, handle->size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);
    if (mappedAddr == MAP_FAILED) {
        LOGE("mmap failed");
        return;
    }
    
    // 创建Bitmap
    uint32_t width = static_cast<uint32_t>(handle->stride / 4);
    uint32_t height = static_cast<uint32_t>(handle->height);
    
    OH_Drawing_Bitmap* bitmap = OH_Drawing_BitmapCreate();
    OH_Drawing_BitmapFormat format{COLOR_FORMAT_RGBA_8888, ALPHA_FORMAT_OPAQUE};
    OH_Drawing_BitmapBuild(bitmap, width, height, &format);
    
    // 创建Canvas
    OH_Drawing_Canvas* canvas = OH_Drawing_CanvasCreate();
    OH_Drawing_CanvasBind(canvas, bitmap);
    
    // 清空画布
    OH_Drawing_CanvasClear(canvas, 0xFFFDF6E3);  // 米色背景
    
    // 使用3层架构：先应用视图变换，然后合成InputCanvas + OffscreenCanvas
    // WebEditor 对齐：逻辑画布固定为 2048（canvasWidth_/canvasHeight_），并以中心为原点进行渲染
    float centerX = canvasWidth_ * 0.5f;
    float centerY = canvasHeight_ * 0.5f;
    float paperRadius = std::min(canvasWidth_, canvasHeight_) * PAPER_RADIUS_RATIO;
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    // WebEditor 对齐：折叠模式使用 2*N 段（旋转 + 镜像）来生成完整图案
    int totalSegments = isFullPaper ? 1 : (static_cast<int>(foldMode_) * 2);
    float sectorAngle = isFullPaper ? (2.0f * M_PI) : ((2.0f * M_PI) / totalSegments);
    
    // 保存状态并应用视图变换（中心原点坐标系）
    OH_Drawing_CanvasSave(canvas);
    ApplyViewTransform(canvas, centerX, centerY, static_cast<float>(canvasHeight_));
    
    // 设置裁剪区域 - 纸张逻辑层（中心原点）
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Path* clipPath = OH_Drawing_PathCreate();
    if (isFullPaper) {
        // 0 折：整张纸可操作
        if (paperType_ == PaperType::CIRCLE) {
            OH_Drawing_PathAddCircle(clipPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
        } else {
            OH_Drawing_PathAddRect(clipPath, -paperRadius, -paperRadius, paperRadius, paperRadius, PATH_DIRECTION_CCW);
        }
    } else {
        // WebEditor 对齐：折叠模式仅显示/操作 wedge（扇形裁剪）
        const float clipRadius = std::min(canvasWidth_, canvasHeight_) * CLIP_RADIUS_RATIO;
        const float startAngle = -M_PI / 2.0f;
        OH_Drawing_PathDestroy(clipPath);
        clipPath = CreateWedgePath(clipRadius, startAngle, sectorAngle);
    }
    OH_Drawing_CanvasClipPath(canvas, clipPath, OH_Drawing_CanvasClipOp::INTERSECT, true);
    OH_Drawing_PathDestroy(clipPath);

    // 合成 Offscreen + Input（两层 bitmap 都以中心为原点贴回）
    CompositeLayers(canvas);

    OH_Drawing_CanvasRestore(canvas);  // 结束纸张裁剪
    
    // 不再绘制折叠/扇形边界引导线（用户要求去掉黑线）
    OH_Drawing_CanvasRestore(canvas);  // 结束全局变换
    
    // 获取bitmap的像素数据
    void* bitmapAddr = OH_Drawing_BitmapGetPixels(bitmap);
    uint32_t* value = static_cast<uint32_t*>(bitmapAddr);
    uint32_t* pixel = static_cast<uint32_t*>(mappedAddr);
    
    if (pixel == nullptr || value == nullptr) {
        LOGE("pixel or value is null");
        munmap(mappedAddr, handle->size);
        OH_Drawing_CanvasDestroy(canvas);
        OH_Drawing_BitmapDestroy(bitmap);
        return;
    }
    
    // 手动复制像素数据（按照官方示例的方式）
    for (uint32_t x = 0; x < width; x++) {
        for (uint32_t y = 0; y < height; y++) {
            *pixel++ = *value++;
        }
    }
    
    // 设置刷新区域
    Region region{nullptr, 0};
    
    // 提交Buffer
    OH_NativeWindow_NativeWindowFlushBuffer(nativeWindow_, buffer, fenceFd, region);
    
    // 释放内存映射
    int result = munmap(mappedAddr, handle->size);
    if (result == -1) {
        LOGE("munmap failed!");
    }
    
    // 释放资源
    OH_Drawing_CanvasDestroy(canvas);
    OH_Drawing_BitmapDestroy(bitmap);
}

void PaperCutEngine::RenderPreview()
{
    if (!previewWindow_) {
        LOGE("PreviewWindow not initialized");
        return;
    }
    
    // 请求Buffer
    OHNativeWindowBuffer* buffer = nullptr;
    int fenceFd;
    int ret = OH_NativeWindow_NativeWindowRequestBuffer(previewWindow_, &buffer, &fenceFd);
    if (ret != 0 || !buffer) {
        LOGE("Failed to request preview buffer");
        return;
    }
    
    // 获取Buffer信息
    BufferHandle* handle = OH_NativeWindow_GetBufferHandleFromNative(buffer);
    if (!handle) {
        LOGE("Failed to get preview buffer handle");
        return;
    }
    
    // 使用mmap映射内存
    void* mappedAddr = mmap(handle->virAddr, handle->size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd, 0);
    if (mappedAddr == MAP_FAILED) {
        LOGE("mmap failed for preview");
        return;
    }
    
    // 创建Bitmap
    uint32_t width = static_cast<uint32_t>(handle->stride / 4);
    uint32_t height = static_cast<uint32_t>(handle->height);
    
    OH_Drawing_Bitmap* bitmap = OH_Drawing_BitmapCreate();
    OH_Drawing_BitmapFormat format{COLOR_FORMAT_RGBA_8888, ALPHA_FORMAT_OPAQUE};
    OH_Drawing_BitmapBuild(bitmap, width, height, &format);
    
    // 创建Canvas
    OH_Drawing_Canvas* canvas = OH_Drawing_CanvasCreate();
    OH_Drawing_CanvasBind(canvas, bitmap);
    
    // 清空画布
    OH_Drawing_CanvasClear(canvas, 0xFFFDF6E3);  // 米色背景
    
    // ③ PreviewCanvas - 展示层：只读 OffscreenCanvas，并进行旋转/镜像/对称展开
    RenderPreviewCanvas(canvas);
    
    // 获取bitmap的像素数据
    void* bitmapAddr = OH_Drawing_BitmapGetPixels(bitmap);
    uint32_t* value = static_cast<uint32_t*>(bitmapAddr);
    uint32_t* pixel = static_cast<uint32_t*>(mappedAddr);
    
    if (pixel == nullptr || value == nullptr) {
        LOGE("pixel or value is null for preview");
        munmap(mappedAddr, handle->size);
        OH_Drawing_CanvasDestroy(canvas);
        OH_Drawing_BitmapDestroy(bitmap);
        return;
    }
    
    // 手动复制像素数据
    for (uint32_t x = 0; x < width; x++) {
        for (uint32_t y = 0; y < height; y++) {
            *pixel++ = *value++;
        }
    }
    
    // 设置刷新区域
    Region region{nullptr, 0};
    
    // 提交Buffer
    OH_NativeWindow_NativeWindowFlushBuffer(previewWindow_, buffer, fenceFd, region);
    
    // 释放内存映射
    int result = munmap(mappedAddr, handle->size);
    if (result == -1) {
        LOGE("munmap failed for preview!");
    }
    
    // 释放资源
    OH_Drawing_CanvasDestroy(canvas);
    OH_Drawing_BitmapDestroy(bitmap);
}

void PaperCutEngine::RenderInputCanvas(OH_Drawing_Canvas* canvas)
{
    // 注意：RenderInputCanvas现在主要用于兼容旧代码
    // 新的架构中，主渲染通过Render()方法使用CompositeLayers完成
    // 这个方法保留用于可能需要单独渲染InputCanvas的场景
    
    if (!canvas) return;
    
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float centerX = width * 0.5f;
    float paperRadius = std::min(width, height) * PAPER_RADIUS_RATIO;
    float clipRadius = std::min(width, height) * CLIP_RADIUS_RATIO;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    float centerY = isFullPaper ? (height * 0.5f) : (height - paperRadius);
    
    // 保存状态并应用视图变换（使用统一的变换函数）
    OH_Drawing_CanvasSave(canvas);
    ApplyViewTransform(canvas, centerX, centerY, static_cast<float>(height));
    
    // 设置裁剪区域 - 纸张逻辑层（在变换后的坐标系统中，中心是(0,0)）
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Path* clipPath = OH_Drawing_PathCreate();
    
    if (isFullPaper) {
        if (paperType_ == PaperType::CIRCLE) {
            OH_Drawing_PathAddCircle(clipPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
        } else {
            OH_Drawing_PathAddRect(clipPath, -paperRadius, -paperRadius,
                                  paperRadius, paperRadius, PATH_DIRECTION_CCW);
        }
    } else {
        int totalSegments = static_cast<int>(foldMode_) * 2;
        float sectorAngle = (2.0f * M_PI) / totalSegments;
        float startAngle = -M_PI / 2.0f;
        float endAngle = startAngle + sectorAngle;
        OH_Drawing_PathMoveTo(clipPath, 0, 0);
        for (float angle = startAngle; angle <= endAngle; angle += 0.1f) {
            float x = cos(angle) * clipRadius;
            float y = sin(angle) * clipRadius;
            OH_Drawing_PathLineTo(clipPath, x, y);
        }
        OH_Drawing_PathLineTo(clipPath, 0, 0);
        OH_Drawing_PathClose(clipPath);
    }
    
    OH_Drawing_CanvasClipPath(canvas, clipPath, OH_Drawing_CanvasClipOp::INTERSECT, true);
    OH_Drawing_PathDestroy(clipPath);
    
    // 绘制纸张底色（在模型坐标系统中，中心是(0,0)）
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, paperColor_);
    
    OH_Drawing_Path* paperPath = OH_Drawing_PathCreate();
    if (paperType_ == PaperType::CIRCLE) {
        OH_Drawing_PathAddCircle(paperPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
    } else {
        OH_Drawing_PathAddRect(paperPath, -paperRadius, -paperRadius,
                               paperRadius, paperRadius, PATH_DIRECTION_CCW);
    }
    
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawPath(canvas, paperPath);
    OH_Drawing_CanvasDetachBrush(canvas);
    
    OH_Drawing_BrushDestroy(brush);
    OH_Drawing_PathDestroy(paperPath);
    
    // 绘制已确认的裁剪（兼容旧代码，新架构中由OffscreenCanvas处理）
    DrawActions(canvas);
    
    OH_Drawing_CanvasRestore(canvas);  // 结束裁剪
    
    // 绘制实时剪刀线预览（在变换后的坐标系统中）
    if (isDrawing_ && currentToolMode_ == ToolMode::SCISSORS && currentPoints_.size() > 1) {
        OH_Drawing_Path* previewPath = OH_Drawing_PathCreate();
        if (currentPoints_.size() > 0) {
            OH_Drawing_PathMoveTo(previewPath, currentPoints_[0].x, currentPoints_[0].y);
            for (size_t i = 1; i < currentPoints_.size(); i++) {
                OH_Drawing_PathLineTo(previewPath, currentPoints_[i].x, currentPoints_[i].y);
            }
            OH_Drawing_PathClose(previewPath);
        }
        
        OH_Drawing_Brush* previewBrush = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetColor(previewBrush, 0x59FFD700);  // 半透明金色
        OH_Drawing_CanvasAttachBrush(canvas, previewBrush);
        OH_Drawing_CanvasDrawPath(canvas, previewPath);
        OH_Drawing_CanvasDetachBrush(canvas);
        
        OH_Drawing_BrushDestroy(previewBrush);
        OH_Drawing_PathDestroy(previewPath);
    }
    
    OH_Drawing_CanvasRestore(canvas);  // 结束视图变换
}

void PaperCutEngine::RenderOutputCanvas(OH_Drawing_Canvas* canvas)
{
    // RenderOutputCanvas用于预览渲染，应用对称展开变换
    if (!canvas) return;
    
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float centerX = width * 0.5f;
    float paperRadius = std::min(width, height) * PAPER_RADIUS_RATIO;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    float centerY = isFullPaper ? (height * 0.5f) : (height - paperRadius);
    
    // 保存状态并应用基础变换（预览使用简化的变换，只应用缩放和平移到中心）
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasTranslate(canvas, centerX, centerY);
    OH_Drawing_CanvasScale(canvas, drawState_.zoom, drawState_.zoom);
    
    // 绘制纸张底色（在模型坐标系统中，中心是(0,0)）
    DrawPaperBase(canvas);
    
    // 应用对称变换绘制裁剪（镜像对称展开）
    if (isFullPaper) {
        // 全纸模式：直接绘制Actions
        DrawActions(canvas);
    } else {
        // 折叠模式：应用镜像对称展开
        // WebEditor 对齐：使用 2*N 段（偶数段旋转，奇数段按边界镜像）
        int totalSegments = static_cast<int>(foldMode_) * 2;
        float sectorAngle = (2.0f * M_PI) / totalSegments;
        float startAngle = -M_PI / 2.0f;
        
        for (int i = 0; i < totalSegments; i++) {
            OH_Drawing_CanvasSave(canvas);
            
            if (i % 2 == 0) {
                // 偶数段：旋转 i * sectorAngle
                OH_Drawing_CanvasRotate(canvas, i * sectorAngle * 180.0f / M_PI, 0, 0);
            } else {
                // 奇数段：绕边界做镜像（rotate 2*boundary + scaleY(-1)）
                float boundary = startAngle + ((i + 1) / 2) * sectorAngle;
                OH_Drawing_CanvasRotate(canvas, (2.0f * boundary) * 180.0f / M_PI, 0, 0);
                OH_Drawing_CanvasScale(canvas, 1.0f, -1.0f);
            }
            
            DrawActions(canvas);
            OH_Drawing_CanvasRestore(canvas);
        }
    }
    
    OH_Drawing_CanvasRestore(canvas);
}

// 应用视图变换矩阵（根据参考代码的坐标映射逻辑）
void PaperCutEngine::ApplyViewTransform(OH_Drawing_Canvas* canvas, float centerX, float centerY, float height)
{
    if (!canvas) return;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    int totalSegments = isFullPaper ? 1 : (static_cast<int>(foldMode_) * 2);
    float sectorAngle = isFullPaper ? (2.0f * M_PI) : ((2.0f * M_PI) / totalSegments);
    float baseRotation = isFullPaper ? 0 : -sectorAngle / 2.0f;
    float totalRotation = baseRotation + drawState_.rotation;
    
    // WebEditor 对齐：以中心为原点的坐标系（不再 translate(-center) 回到左上角）
    // translate(center) -> scale(flip) -> scale(zoom) -> translate(pan + VIEW_OFFSET_Y) -> rotate
    OH_Drawing_CanvasTranslate(canvas, centerX, centerY);
    if (drawState_.isFlipped) {
        OH_Drawing_CanvasScale(canvas, -1.0f, 1.0f);
    }
    OH_Drawing_CanvasScale(canvas, VIEW_SCALE * drawState_.zoom, VIEW_SCALE * drawState_.zoom);
    // VIEW_OFFSET_Y 以逻辑画布(2048)为基准
    OH_Drawing_CanvasTranslate(canvas, drawState_.pan.x, drawState_.pan.y + canvasHeight_ * VIEW_OFFSET_Y_RATIO);
    OH_Drawing_CanvasRotate(canvas, totalRotation * 180.0f / M_PI, 0, 0);
}

void PaperCutEngine::DrawPaperBase(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    
    // 注意：这个函数在RenderInputCanvas和RenderOutputCanvas中被调用
    // RenderInputCanvas中坐标系统未变换，需要使用实际坐标
    // RenderOutputCanvas中坐标系统已变换到(0,0)为中心，需要使用相对坐标
    // 这里通过检查canvas的当前变换状态来判断，或者使用固定值
    // 为了简化，我们假设在RenderInputCanvas中调用时使用实际坐标
    // 在RenderOutputCanvas中调用时使用相对坐标(0,0)
    
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float paperRadius = std::min(width, height) * PAPER_RADIUS_RATIO;
    
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, paperColor_);
    
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    
    if (paperType_ == PaperType::CIRCLE) {
        // 圆形纸张 - 使用相对坐标，因为可能已经变换
        OH_Drawing_PathAddCircle(path, 0, 0, paperRadius, PATH_DIRECTION_CCW);
    } else {
        // 方形纸张
        OH_Drawing_PathAddRect(path, -paperRadius, -paperRadius, 
                               paperRadius, paperRadius, PATH_DIRECTION_CCW);
    }
    
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachBrush(canvas);
    
    OH_Drawing_BrushDestroy(brush);
    OH_Drawing_PathDestroy(path);
}

void PaperCutEngine::DrawFoldLines(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    
    // 注意：这个函数在RenderInputCanvas中被调用，此时坐标系统已经变换
    // 中心点已经移动到(0,0)，所以使用相对坐标
    // WebEditor 对齐：几何以逻辑画布尺寸(2048)为基准，不依赖屏幕 buffer 尺寸
    float paperRadius = std::min(canvasWidth_, canvasHeight_) * PAPER_RADIUS_RATIO;
    float clipRadius = std::min(canvasWidth_, canvasHeight_) * CLIP_RADIUS_RATIO;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    int totalSegments = isFullPaper ? 1 : (static_cast<int>(foldMode_) * 2);
    float sectorAngle = isFullPaper ? (2.0f * M_PI) : ((2.0f * M_PI) / totalSegments);
    
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetColor(pen, 0x80000000);  // 半透明黑色
    OH_Drawing_PenSetWidth(pen, 2.0f);
    
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    
    if (isFullPaper) {
        if (paperType_ == PaperType::CIRCLE) {
            OH_Drawing_PathAddCircle(path, 0, 0, paperRadius, PATH_DIRECTION_CCW);
        } else {
            OH_Drawing_PathAddRect(path, -paperRadius, -paperRadius,
                                  paperRadius, paperRadius, PATH_DIRECTION_CCW);
        }
    } else {
        float startAngle = -M_PI / 2.0f;
        float endAngle = startAngle + sectorAngle;
        OH_Drawing_PathMoveTo(path, 0, 0);
        // 使用圆弧绘制扇形
        for (float angle = startAngle; angle <= endAngle; angle += 0.1f) {
            float x = cos(angle) * clipRadius;
            float y = sin(angle) * clipRadius;
            OH_Drawing_PathLineTo(path, x, y);
        }
        OH_Drawing_PathLineTo(path, 0, 0);
        OH_Drawing_PathClose(path);
    }
    
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    
    OH_Drawing_PenDestroy(pen);
    OH_Drawing_PathDestroy(path);
}

void PaperCutEngine::DrawActions(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    
    float paperRadius = std::min(canvasWidth_, canvasHeight_) * PAPER_RADIUS_RATIO;
    
    // 设置裁剪区域
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Path* clipPath = OH_Drawing_PathCreate();
    
    if (paperType_ == PaperType::CIRCLE) {
        OH_Drawing_PathAddCircle(clipPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
    } else {
        OH_Drawing_PathAddRect(clipPath, -paperRadius, -paperRadius,
                              paperRadius, paperRadius, PATH_DIRECTION_CCW);
    }
    
    OH_Drawing_CanvasClipPath(canvas, clipPath, OH_Drawing_CanvasClipOp::INTERSECT, true);
    OH_Drawing_PathDestroy(clipPath);
    
    // 绘制所有动作
    for (const auto& action : actions_) {
        if (action.type == ActionType::CUT) {
            // 裁剪操作：使用CanvasClipPath的DIFFERENCE模式实现镂空
            OH_Drawing_CanvasSave(canvas);
            OH_Drawing_Path* cutPath = OH_Drawing_PathCreate();
            if (action.points.size() > 0) {
                OH_Drawing_PathMoveTo(cutPath, action.points[0].x, action.points[0].y);
                for (size_t i = 1; i < action.points.size(); i++) {
                    OH_Drawing_PathLineTo(cutPath, action.points[i].x, action.points[i].y);
                }
                OH_Drawing_PathClose(cutPath);
            }
            // 使用DIFFERENCE模式裁剪，实现镂空效果
            OH_Drawing_CanvasClipPath(canvas, cutPath, OH_Drawing_CanvasClipOp::DIFFERENCE, true);
            OH_Drawing_PathDestroy(cutPath);
            // 绘制一个大的矩形来清除裁剪区域
            OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
            OH_Drawing_BrushSetColor(brush, 0x00000000);  // 透明色
            OH_Drawing_CanvasAttachBrush(canvas, brush);
            OH_Drawing_Rect* clearRect = OH_Drawing_RectCreate(-10000, -10000, 10000, 10000);
            OH_Drawing_CanvasDrawRect(canvas, clearRect);
            OH_Drawing_CanvasDetachBrush(canvas);
            OH_Drawing_BrushDestroy(brush);
            OH_Drawing_RectDestroy(clearRect);
            OH_Drawing_CanvasRestore(canvas);
        } else if (action.type == ActionType::STROKE) {
            // 笔触操作
            if (action.tool == ToolMode::DRAFT_PEN) {
                DrawPencilStroke(canvas, action.points);
            } else if (action.tool == ToolMode::DRAFT_ERASER) {
                ErasePencilStroke(canvas, action.points, 0xFFFDF6E3);  // 使用米色背景擦除
            }
        }
    }
    
    // 绘制当前正在绘制的笔触
    if (isDrawing_ && currentPoints_.size() > 1) {
        if (currentToolMode_ == ToolMode::SCISSORS) {
            // 实时预览裁剪区域
            OH_Drawing_Path* previewPath = OH_Drawing_PathCreate();
            if (currentPoints_.size() > 0) {
                OH_Drawing_PathMoveTo(previewPath, currentPoints_[0].x, currentPoints_[0].y);
                for (size_t i = 1; i < currentPoints_.size(); i++) {
                    OH_Drawing_PathLineTo(previewPath, currentPoints_[i].x, currentPoints_[i].y);
                }
                OH_Drawing_PathClose(previewPath);
            }
            
            OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
            OH_Drawing_BrushSetColor(brush, 0x59FFD700);  // 半透明金色
            OH_Drawing_CanvasAttachBrush(canvas, brush);
            OH_Drawing_CanvasDrawPath(canvas, previewPath);
            OH_Drawing_CanvasDetachBrush(canvas);
            
            OH_Drawing_BrushDestroy(brush);
            OH_Drawing_PathDestroy(previewPath);
        } else if (currentToolMode_ == ToolMode::DRAFT_PEN) {
            DrawPencilStroke(canvas, currentPoints_);
        } else if (currentToolMode_ == ToolMode::DRAFT_ERASER) {
            ErasePencilStroke(canvas, currentPoints_, 0xFFFDF6E3);  // 使用米色背景擦除
        }
    }
    
    OH_Drawing_CanvasRestore(canvas);
}

void PaperCutEngine::DrawPath(OH_Drawing_Canvas* canvas, const std::vector<Point>& points, bool closePath)
{
    if (!canvas || points.size() < 2) return;
    
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, points[0].x, points[0].y);
    
    if (points.size() == 2) {
        OH_Drawing_PathLineTo(path, points[1].x, points[1].y);
    } else {
        // 使用二次贝塞尔曲线平滑连接
        for (size_t i = 1; i < points.size() - 1; i++) {
            float xc = (points[i].x + points[i + 1].x) * 0.5f;
            float yc = (points[i].y + points[i + 1].y) * 0.5f;
            OH_Drawing_PathQuadTo(path, points[i].x, points[i].y, xc, yc);
        }
        OH_Drawing_PathLineTo(path, points.back().x, points.back().y);
    }
    
    if (closePath) {
        OH_Drawing_PathClose(path);
    }
    
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetColor(pen, 0xFFFFD700);  // 金色
    OH_Drawing_PenSetWidth(pen, 5.0f);
    
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    
    OH_Drawing_PenDestroy(pen);
    OH_Drawing_PathDestroy(path);
}

void PaperCutEngine::DrawPencilStroke(OH_Drawing_Canvas* canvas, const std::vector<Point>& points)
{
    if (!canvas || points.size() < 2) return;
    
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, points[0].x, points[0].y);
    
    for (size_t i = 1; i < points.size(); i++) {
        if (i < points.size() - 1) {
            float xc = (points[i].x + points[i + 1].x) * 0.5f;
            float yc = (points[i].y + points[i + 1].y) * 0.5f;
            OH_Drawing_PathQuadTo(path, points[i].x, points[i].y, xc, yc);
        } else {
            OH_Drawing_PathLineTo(path, points[i].x, points[i].y);
        }
    }
    
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetColor(pen, 0xFFFFFFFF);  // 白色铅笔
    OH_Drawing_PenSetWidth(pen, 3.0f);
    // 注意：ROUND可能不存在，使用BUTT或移除这些设置
    // OH_Drawing_PenSetCap(pen, OH_Drawing_PenLineCapStyle::BUTT);
    // OH_Drawing_PenSetJoin(pen, OH_Drawing_PenLineJoinStyle::MITER);
    
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    
    OH_Drawing_PenDestroy(pen);
    OH_Drawing_PathDestroy(path);
}

void PaperCutEngine::ErasePencilStroke(OH_Drawing_Canvas* canvas, const std::vector<Point>& points, uint32_t backgroundColor)
{
    if (!canvas || points.size() < 2) return;
    
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, points[0].x, points[0].y);
    
    for (size_t i = 1; i < points.size(); i++) {
        if (i < points.size() - 1) {
            float xc = (points[i].x + points[i + 1].x) * 0.5f;
            float yc = (points[i].y + points[i + 1].y) * 0.5f;
            OH_Drawing_PathQuadTo(path, points[i].x, points[i].y, xc, yc);
        } else {
            OH_Drawing_PathLineTo(path, points[i].x, points[i].y);
        }
    }
    
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetColor(pen, backgroundColor);  // 使用背景色擦除
    OH_Drawing_PenSetWidth(pen, 8.0f);
    // 注意：ROUND和DST_OUT可能不存在，移除这些设置
    // OH_Drawing_PenSetCap(pen, OH_Drawing_PenLineCapStyle::BUTT);
    // OH_Drawing_PenSetJoin(pen, OH_Drawing_PenLineJoinStyle::MITER);
    // OH_Drawing_PenSetBlendMode(pen, OH_Drawing_BlendMode::CLEAR);
    
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    
    OH_Drawing_PenDestroy(pen);
    OH_Drawing_PathDestroy(path);
}

void PaperCutEngine::SetToolMode(ToolMode mode)
{
    currentToolMode_ = mode;
    if (mode != ToolMode::BEZIER) {
        CancelBezier();
    }
}

void PaperCutEngine::SetFoldMode(FoldMode mode)
{
    foldMode_ = mode;
}

void PaperCutEngine::SetPaperType(PaperType type)
{
    paperType_ = type;
}

void PaperCutEngine::SetPaperColor(uint32_t color)
{
    paperColor_ = color;
}

void PaperCutEngine::StartDrawing(float x, float y)
{
    if (isDrawing_) return;
    // InputCanvas：必须限制在当前扇形区域内（否则不启动一次绘制）
    if (!IsPointInSector(x, y)) {
        return;
    }
    
    isDrawing_ = true;
    currentPoints_.clear();
    currentPoints_.push_back(Point(x, y));
    // InputCanvas 需要立即更新（临时路径）
    MarkInputDirty();
}

void PaperCutEngine::AddPoint(float x, float y)
{
    if (!isDrawing_) return;
    // InputCanvas：丢弃扇形外点，保证 Offscreen（数据层）永不被污染
    if (!IsPointInSector(x, y)) {
        return;
    }
    
    // 检查距离，避免点太密集
    if (!currentPoints_.empty()) {
        const Point& last = currentPoints_.back();
        float dx = x - last.x;
        float dy = y - last.y;
        if (dx * dx + dy * dy < 100.0f) {  // 距离小于10像素
            return;
        }
    }
    
    currentPoints_.push_back(Point(x, y));
    // InputCanvas 需要更新（临时路径）
    MarkInputDirty();
}

void PaperCutEngine::FinishDrawing()
{
    if (!isDrawing_ || currentPoints_.size() < 2) {
        isDrawing_ = false;
        currentPoints_.clear();
        // 结束时清空 InputCanvas
        if (inputCanvas_) {
            OH_Drawing_CanvasClear(inputCanvas_, 0x00000000);
            inputDirty_ = false;
        }
        return;
    }
    
    // 创建命令并应用到OffscreenCanvas
    std::unique_ptr<ICommand> cmd;
    if (currentToolMode_ == ToolMode::SCISSORS) {
        cmd = std::make_unique<CutCommand>(currentPoints_);
    } else if (currentToolMode_ == ToolMode::DRAFT_PEN) {
        cmd = std::make_unique<PencilCommand>(currentPoints_);
    } else if (currentToolMode_ == ToolMode::DRAFT_ERASER) {
        cmd = std::make_unique<EraserCommand>(currentPoints_, paperColor_);
    }
    
    if (cmd) {
        // 应用到OffscreenCanvas
        ApplyCommandToOffscreenCanvas(std::move(cmd));
    }
    
    isDrawing_ = false;
    currentPoints_.clear();
    // InputCanvas 在抬笔必须立即清空
    if (inputCanvas_) {
        OH_Drawing_CanvasClear(inputCanvas_, 0x00000000);
        inputDirty_ = false;
    }
}

void PaperCutEngine::CancelDrawing()
{
    isDrawing_ = false;
    currentPoints_.clear();
}

void PaperCutEngine::AddBezierPoint(float x, float y)
{
    bezierPoints_.push_back(Point(x, y));
}

void PaperCutEngine::UpdateBezierPoint(int index, float x, float y)
{
    if (index >= 0 && index < static_cast<int>(bezierPoints_.size())) {
        bezierPoints_[index] = Point(x, y);
    }
}

void PaperCutEngine::CloseBezier()
{
    if (bezierPoints_.size() < 3) return;
    
    std::vector<Point> finalPoints;
    if (bezierSharpMode_) {
        // 直线模式
        finalPoints = bezierPoints_;
    } else {
        // 曲线模式
        finalPoints = CalculateSplinePoints(bezierPoints_, true);
    }
    
    // 创建CutCommand并应用到OffscreenCanvas
    auto cmd = std::make_unique<CutCommand>(finalPoints);
    ApplyCommandToOffscreenCanvas(std::move(cmd));
    
    CancelBezier();
}

void PaperCutEngine::CancelBezier()
{
    bezierPoints_.clear();
    bezierClosed_ = false;
}

void PaperCutEngine::Undo()
{
    // 使用命令模式的撤销
    if (!commandHistory_.empty()) {
        RevertCommandFromOffscreenCanvas();
    }
}

void PaperCutEngine::Redo()
{
    // 使用命令模式的重做
    if (!redoStack_.empty()) {
        std::unique_ptr<ICommand> cmd = std::move(redoStack_.back());
        redoStack_.pop_back();
        ApplyCommandToOffscreenCanvas(std::move(cmd));
    }
}

void PaperCutEngine::Clear()
{
    // 命令型清空（可撤销）：通过 ClearCommand 作为“分界点”，RenderOffscreenCanvas 会只渲染最后一次 clear 之后的命令
    auto cmd = std::make_unique<ClearCommand>();
    ApplyCommandToOffscreenCanvas(std::move(cmd));
    CancelDrawing();
    CancelBezier();
}

void PaperCutEngine::SetZoom(float zoom)
{
    drawState_.zoom = std::max(0.2f, std::min(8.0f, zoom));
}

void PaperCutEngine::SetPan(float x, float y)
{
    drawState_.pan = Point(x, y);
}

void PaperCutEngine::SetRotation(float rotation)
{
    drawState_.rotation = rotation;
}

void PaperCutEngine::SetFlip(bool flipped)
{
    drawState_.isFlipped = flipped;
}

void PaperCutEngine::AddAction(const Action& action)
{
    // 兼容接口：把 Action 转成 Command 并写入 OffscreenCanvas（单向数据流）
    std::unique_ptr<ICommand> cmd;
    if (action.points.empty()) {
        // 约定：空 points 视为 Clear
        cmd = std::make_unique<ClearCommand>();
    } else if (action.tool == ToolMode::SCISSORS) {
        cmd = std::make_unique<CutCommand>(action.points);
    } else if (action.tool == ToolMode::DRAFT_PEN) {
        cmd = std::make_unique<PencilCommand>(action.points);
    } else if (action.tool == ToolMode::DRAFT_ERASER) {
        cmd = std::make_unique<EraserCommand>(action.points, paperColor_);
    }
    if (cmd) {
        ApplyCommandToOffscreenCanvas(std::move(cmd));
    }
}

std::vector<Action> PaperCutEngine::GetActions() const
{
    // 从命令历史生成动作列表（单一事实来源）
    std::vector<Action> out;
    out.reserve(commandHistory_.size());
    for (const auto& cmd : commandHistory_) {
        if (cmd) {
            out.push_back(cmd->ToAction());
        }
    }
    return out;
}

void PaperCutEngine::SetActions(const std::vector<Action>& actions)
{
    // 从 Action 列表重建命令历史
    commandHistory_.clear();
    redoStack_.clear();
    for (const auto& action : actions) {
        std::unique_ptr<ICommand> cmd;
        if (action.points.empty()) {
            cmd = std::make_unique<ClearCommand>();
        } else if (action.tool == ToolMode::SCISSORS) {
            cmd = std::make_unique<CutCommand>(action.points);
        } else if (action.tool == ToolMode::DRAFT_PEN) {
            cmd = std::make_unique<PencilCommand>(action.points);
        } else if (action.tool == ToolMode::DRAFT_ERASER) {
            cmd = std::make_unique<EraserCommand>(action.points, paperColor_);
        }
        if (cmd) {
            commandHistory_.push_back(std::move(cmd));
        }
    }
    offscreenDirty_ = true;
    RenderOffscreenCanvas();
}

Point PaperCutEngine::ScreenToModel(float x, float y) const
{
    // 坐标转换逻辑
    float centerX = canvasWidth_ * 0.5f;
    float paperRadius = std::min(canvasWidth_, canvasHeight_) * PAPER_RADIUS_RATIO;
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    // 0折：圆心在画布中心；其他折：圆心在x轴居中，y轴在底部
    float centerY = isFullPaper ? (canvasHeight_ * 0.5f) : (canvasHeight_ - paperRadius);
    
    float modelX = (x - centerX) / (VIEW_SCALE * drawState_.zoom) - drawState_.pan.x;
    float modelY = (y - centerY) / (VIEW_SCALE * drawState_.zoom) - drawState_.pan.y;
    
    return Point(modelX, modelY);
}

Point PaperCutEngine::ModelToScreen(float x, float y) const
{
    float centerX = canvasWidth_ * 0.5f;
    float paperRadius = std::min(canvasWidth_, canvasHeight_) * PAPER_RADIUS_RATIO;
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    // 0折：圆心在画布中心；其他折：圆心在x轴居中，y轴在底部
    float centerY = isFullPaper ? (canvasHeight_ * 0.5f) : (canvasHeight_ - paperRadius);
    
    float screenX = (x + drawState_.pan.x) * (VIEW_SCALE * drawState_.zoom) + centerX;
    float screenY = (y + drawState_.pan.y) * (VIEW_SCALE * drawState_.zoom) + centerY;
    
    return Point(screenX, screenY);
}

std::vector<Point> PaperCutEngine::CalculateSplinePoints(const std::vector<Point>& points, bool closed) const
{
    if (points.size() < 2) return points;
    
    std::vector<Point> result;
    std::vector<Point> pts = points;
    
    if (closed) {
        pts.push_back(points[0]);
        pts.push_back(points[1]);
        pts.insert(pts.begin(), points.back());
    } else {
        pts.insert(pts.begin(), points[0]);
        pts.push_back(points.back());
    }
    
    const int numSegments = 16;
    for (size_t i = 1; i < pts.size() - 2; i++) {
        const Point& p0 = pts[i - 1];
        const Point& p1 = pts[i];
        const Point& p2 = pts[i + 1];
        const Point& p3 = pts[i + 2];
        
        for (int t = 0; t <= numSegments; t++) {
            float tNorm = t / static_cast<float>(numSegments);
            float t2 = tNorm * tNorm;
            float t3 = t2 * tNorm;
            
            float x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * tNorm +
                             (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
                             (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
            float y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * tNorm +
                             (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
                             (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
            
            result.push_back(Point(x, y));
        }
    }
    
    return result;
}

// ========== 命令类实现 ==========

// CutCommand 实现
CutCommand::CutCommand(const std::vector<Point>& points)
    : points_(points)
{
    id_ = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

void CutCommand::Apply(OH_Drawing_Canvas* canvas)
{
    if (!canvas || points_.size() < 2) return;
    
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Path* cutPath = OH_Drawing_PathCreate();
    if (points_.size() > 0) {
        OH_Drawing_PathMoveTo(cutPath, points_[0].x, points_[0].y);
        for (size_t i = 1; i < points_.size(); i++) {
            OH_Drawing_PathLineTo(cutPath, points_[i].x, points_[i].y);
        }
        OH_Drawing_PathClose(cutPath);
    }
    // 使用DIFFERENCE模式裁剪，实现镂空效果
    OH_Drawing_CanvasClipPath(canvas, cutPath, OH_Drawing_CanvasClipOp::DIFFERENCE, true);
    OH_Drawing_PathDestroy(cutPath);
    
    // 绘制一个大的矩形来清除裁剪区域
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, 0x00000000);  // 透明色
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_Rect* clearRect = OH_Drawing_RectCreate(-10000, -10000, 10000, 10000);
    OH_Drawing_CanvasDrawRect(canvas, clearRect);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_BrushDestroy(brush);
    OH_Drawing_RectDestroy(clearRect);
    OH_Drawing_CanvasRestore(canvas);
}

void CutCommand::Revert(OH_Drawing_Canvas* canvas)
{
    // 撤销裁剪操作需要重新渲染整个OffscreenCanvas，所以这里不做任何操作
    // 实际的撤销由引擎的RenderOffscreenCanvas完成
}

Action CutCommand::ToAction() const
{
    Action action;
    action.id = id_;
    action.type = ActionType::CUT;
    action.tool = ToolMode::SCISSORS;
    action.points = points_;
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return action;
}

// PencilCommand 实现
PencilCommand::PencilCommand(const std::vector<Point>& points)
    : points_(points)
{
    id_ = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

void PencilCommand::Apply(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    PaperCutEngine::DrawPencilStroke(canvas, points_);
}

void PencilCommand::Revert(OH_Drawing_Canvas* canvas)
{
    // 撤销铅笔操作需要重新渲染整个OffscreenCanvas，所以这里不做任何操作
    // 实际的撤销由引擎的RenderOffscreenCanvas完成
}

Action PencilCommand::ToAction() const
{
    Action action;
    action.id = id_;
    action.type = ActionType::STROKE;
    action.tool = ToolMode::DRAFT_PEN;
    action.points = points_;
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return action;
}

// EraserCommand 实现
EraserCommand::EraserCommand(const std::vector<Point>& points, uint32_t backgroundColor)
    : points_(points), backgroundColor_(backgroundColor)
{
    id_ = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

void EraserCommand::Apply(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    // 使用纸张颜色作为背景色来擦除
    PaperCutEngine::ErasePencilStroke(canvas, points_, backgroundColor_);
}

void EraserCommand::Revert(OH_Drawing_Canvas* canvas)
{
    // 撤销橡皮操作需要重新渲染整个OffscreenCanvas，所以这里不做任何操作
    // 实际的撤销由引擎的RenderOffscreenCanvas完成
}

Action EraserCommand::ToAction() const
{
    Action action;
    action.id = id_;
    action.type = ActionType::STROKE;
    action.tool = ToolMode::DRAFT_ERASER;
    action.points = points_;
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return action;
}

// ClearCommand 实现
ClearCommand::ClearCommand()
{
    id_ = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

void ClearCommand::Apply(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    // 清空画布
    OH_Drawing_CanvasClear(canvas, 0x00000000);  // 透明
}

void ClearCommand::Revert(OH_Drawing_Canvas* canvas)
{
    // 撤销清空操作需要重新渲染整个OffscreenCanvas，所以这里不做任何操作
    // 实际的撤销由引擎的RenderOffscreenCanvas完成
}

Action ClearCommand::ToAction() const
{
    Action action;
    action.id = id_;
    action.type = ActionType::CUT;  // 使用CUT类型表示清空操作
    action.tool = ToolMode::SCISSORS;
    action.points.clear();
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return action;
}

void ClearCommand::SetPreviousCommands(std::vector<std::unique_ptr<ICommand>> commands)
{
    previousCommands_ = std::move(commands);
}

// ========== 离屏Canvas实现 ==========

void PaperCutEngine::InitializeLayers(int width, int height)
{
    if (layersInitialized_) {
        DestroyLayers();
    }
    
    // 初始化InputCanvas - 交互层
    inputBitmap_ = OH_Drawing_BitmapCreate();
    OH_Drawing_BitmapFormat inputFormat{COLOR_FORMAT_RGBA_8888, ALPHA_FORMAT_PREMUL};
    OH_Drawing_BitmapBuild(inputBitmap_, width, height, &inputFormat);
    inputCanvas_ = OH_Drawing_CanvasCreate();
    OH_Drawing_CanvasBind(inputCanvas_, inputBitmap_);
    inputDirty_ = true;
    
    // 初始化OffscreenCanvas - 数据层（真实数据存储）
    offscreenBitmap_ = OH_Drawing_BitmapCreate();
    OH_Drawing_BitmapFormat offscreenFormat{COLOR_FORMAT_RGBA_8888, ALPHA_FORMAT_PREMUL};
    OH_Drawing_BitmapBuild(offscreenBitmap_, width, height, &offscreenFormat);
    offscreenCanvas_ = OH_Drawing_CanvasCreate();
    OH_Drawing_CanvasBind(offscreenCanvas_, offscreenBitmap_);
    
    // 清空OffscreenCanvas为透明
    OH_Drawing_CanvasClear(offscreenCanvas_, 0x00000000);
    
    offscreenDirty_ = true;
    layersInitialized_ = true;
    
    LOGI("Layers initialized: %dx%d", width, height);
}

void PaperCutEngine::DestroyLayers()
{
    if (inputCanvas_) {
        OH_Drawing_CanvasDestroy(inputCanvas_);
        inputCanvas_ = nullptr;
    }
    if (inputBitmap_) {
        OH_Drawing_BitmapDestroy(inputBitmap_);
        inputBitmap_ = nullptr;
    }
    
    if (offscreenCanvas_) {
        OH_Drawing_CanvasDestroy(offscreenCanvas_);
        offscreenCanvas_ = nullptr;
    }
    if (offscreenBitmap_) {
        OH_Drawing_BitmapDestroy(offscreenBitmap_);
        offscreenBitmap_ = nullptr;
    }
    
    layersInitialized_ = false;
    LOGI("Layers destroyed");
}

void PaperCutEngine::RenderOffscreenCanvas()
{
    if (!offscreenCanvas_ || !layersInitialized_) return;
    
    // 清空OffscreenCanvas
    OH_Drawing_CanvasClear(offscreenCanvas_, 0x00000000);  // 透明背景
    
    // OffscreenCanvas使用模型坐标系统(以canvas中心为原点)
    float centerX = canvasWidth_ * 0.5f;
    float centerY = canvasHeight_ * 0.5f;
    float paperRadius = std::min(canvasWidth_, canvasHeight_) * PAPER_RADIUS_RATIO;
    
    // 转换到模型坐标系统
    OH_Drawing_CanvasSave(offscreenCanvas_);
    OH_Drawing_CanvasTranslate(offscreenCanvas_, centerX, centerY);
    
    // 绘制纸张底色(在模型坐标系统中,中心是(0,0))
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, paperColor_);
    
    OH_Drawing_Path* paperPath = OH_Drawing_PathCreate();
    if (paperType_ == PaperType::CIRCLE) {
        OH_Drawing_PathAddCircle(paperPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
    } else {
        OH_Drawing_PathAddRect(paperPath, -paperRadius, -paperRadius,
                              paperRadius, paperRadius, PATH_DIRECTION_CCW);
    }
    
    OH_Drawing_CanvasAttachBrush(offscreenCanvas_, brush);
    OH_Drawing_CanvasDrawPath(offscreenCanvas_, paperPath);
    OH_Drawing_CanvasDetachBrush(offscreenCanvas_);
    
    OH_Drawing_BrushDestroy(brush);
    OH_Drawing_PathDestroy(paperPath);
    
    // 设置裁剪区域（纸张边界）
    OH_Drawing_CanvasSave(offscreenCanvas_);
    OH_Drawing_Path* clipPath = OH_Drawing_PathCreate();
    if (paperType_ == PaperType::CIRCLE) {
        OH_Drawing_PathAddCircle(clipPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
    } else {
        OH_Drawing_PathAddRect(clipPath, -paperRadius, -paperRadius,
                              paperRadius, paperRadius, PATH_DIRECTION_CCW);
    }
    OH_Drawing_CanvasClipPath(offscreenCanvas_, clipPath, OH_Drawing_CanvasClipOp::INTERSECT, true);
    OH_Drawing_PathDestroy(clipPath);
    
    // ClearCommand 作为“分界点”：只渲染最后一次 clear 之后的命令
    size_t startIndex = 0;
    for (size_t i = 0; i < commandHistory_.size(); i++) {
        if (dynamic_cast<ClearCommand*>(commandHistory_[i].get()) != nullptr) {
            startIndex = i + 1;
        }
    }
    for (size_t i = startIndex; i < commandHistory_.size(); i++) {
        const auto& cmd = commandHistory_[i];
        if (cmd) {
            cmd->Apply(offscreenCanvas_);
        }
    }
    
    OH_Drawing_CanvasRestore(offscreenCanvas_);  // 结束裁剪
    OH_Drawing_CanvasRestore(offscreenCanvas_);  // 结束坐标转换
    offscreenDirty_ = false;
}

void PaperCutEngine::ApplyCommandToOffscreenCanvas(std::unique_ptr<ICommand> cmd)
{
    if (!cmd || !layersInitialized_) return;
    
    // 将命令添加到历史
    commandHistory_.push_back(std::move(cmd));
    
    // 清空重做栈(新命令后不能再重做)
    redoStack_.clear();
    
    // 标记需要重新渲染
    offscreenDirty_ = true;
    
    // 重新渲染整个OffscreenCanvas（保证一致性）
    RenderOffscreenCanvas();
}

void PaperCutEngine::RevertCommandFromOffscreenCanvas()
{
    if (commandHistory_.empty() || !layersInitialized_) return;
    
    // 将最后一个命令移到重做栈
    redoStack_.push_back(std::move(commandHistory_.back()));
    commandHistory_.pop_back();
    
    // 标记需要重新渲染
    offscreenDirty_ = true;
    
    // 重新渲染整个OffscreenCanvas（保证一致性）
    RenderOffscreenCanvas();
}

void PaperCutEngine::CompositeLayers(OH_Drawing_Canvas* targetCanvas)
{
    if (!targetCanvas || !layersInitialized_) return;
    const bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    
    // 如果OffscreenCanvas需要更新，先渲染它
    if (offscreenDirty_) {
        RenderOffscreenCanvas();
    }
    
    // 目标 canvas 已经处在“中心原点 + 视图变换”坐标系中：
    // bitmap（2048x2048）内容本身以像素坐标存储，因此需要以 (-CENTER,-CENTER) 贴回
    const float halfW = canvasWidth_ * 0.5f;
    const float halfH = canvasHeight_ * 0.5f;
    if (offscreenBitmap_) {
        OH_Drawing_CanvasDrawBitmap(targetCanvas, offscreenBitmap_, -halfW, -halfH);
    }
    
    // 更新InputCanvas（交互层，临时绘制）
    if (inputDirty_) {
        // 清空InputCanvas
        OH_Drawing_CanvasClear(inputCanvas_, 0x00000000);  // 透明
        
        // 绘制当前正在进行的绘制（实时预览）- 使用模型坐标
        if (isDrawing_ && currentPoints_.size() > 1) {
            // 转换模型坐标到屏幕坐标进行绘制
            OH_Drawing_CanvasSave(inputCanvas_);
            OH_Drawing_CanvasTranslate(inputCanvas_, halfW, halfH);

            // WebEditor 对齐：
            // - 草稿(铅笔/橡皮)必须被扇形 clip 约束
            // - 剪刀金色预览线应在 clip 外也可见（Web 在 restore clip 后绘制）
            if (!isFullPaper && currentToolMode_ != ToolMode::SCISSORS) {
                float clipRadius = std::min(canvasWidth_, canvasHeight_) * CLIP_RADIUS_RATIO;
                float sectorAngle2 = (2.0f * M_PI) / (static_cast<int>(foldMode_) * 2);
                float startAngle = -M_PI / 2.0f;
                OH_Drawing_Path* sectorPath = CreateWedgePath(clipRadius, startAngle, sectorAngle2);
                OH_Drawing_CanvasClipPath(inputCanvas_, sectorPath, OH_Drawing_CanvasClipOp::INTERSECT, true);
                OH_Drawing_PathDestroy(sectorPath);
            }
            
            if (currentToolMode_ == ToolMode::SCISSORS) {
                OH_Drawing_Path* previewPath = OH_Drawing_PathCreate();
                if (currentPoints_.size() > 0) {
                    OH_Drawing_PathMoveTo(previewPath, currentPoints_[0].x, currentPoints_[0].y);
                    for (size_t i = 1; i < currentPoints_.size(); i++) {
                        OH_Drawing_PathLineTo(previewPath, currentPoints_[i].x, currentPoints_[i].y);
                    }
                    OH_Drawing_PathClose(previewPath);
                }
                
                // WebEditor：金色半透明填充 + 描边
                OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
                OH_Drawing_BrushSetColor(brush, 0x59FFD700);
                OH_Drawing_CanvasAttachBrush(inputCanvas_, brush);
                OH_Drawing_CanvasDrawPath(inputCanvas_, previewPath);
                OH_Drawing_CanvasDetachBrush(inputCanvas_);
                OH_Drawing_BrushDestroy(brush);

                OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
                OH_Drawing_PenSetColor(pen, 0xFFFFD700);
                OH_Drawing_PenSetWidth(pen, 5.0f);
                OH_Drawing_CanvasAttachPen(inputCanvas_, pen);
                OH_Drawing_CanvasDrawPath(inputCanvas_, previewPath);
                OH_Drawing_CanvasDetachPen(inputCanvas_);
                OH_Drawing_PenDestroy(pen);
                OH_Drawing_PathDestroy(previewPath);
            } else if (currentToolMode_ == ToolMode::DRAFT_PEN) {
                DrawPencilStroke(inputCanvas_, currentPoints_);
            } else if (currentToolMode_ == ToolMode::DRAFT_ERASER) {
                ErasePencilStroke(inputCanvas_, currentPoints_, paperColor_);
            }
            
            OH_Drawing_CanvasRestore(inputCanvas_);
        }
        inputDirty_ = false;
    }
    
    // 绘制InputCanvas（交互层）
    if (inputBitmap_) {
        OH_Drawing_CanvasDrawBitmap(targetCanvas, inputBitmap_, -halfW, -halfH);
    }
}

bool PaperCutEngine::IsPointInSector(float x, float y) const
{
    // 0 折：不限制扇形（整张纸）
    if (foldMode_ == FoldMode::ZERO) {
        return true;
    }
    
    // 扇形半径与 InputCanvas clip 逻辑保持一致
    float clipRadius = std::min(canvasWidth_, canvasHeight_) * CLIP_RADIUS_RATIO;
    float r2 = x * x + y * y;
    if (r2 > clipRadius * clipRadius) {
        return false;
    }
    
    // 扇形角度范围：[-pi/2, -pi/2 + sectorAngle]，WebEditor：sectorAngle = 2pi/(2*fold)
    float sectorAngle = (2.0f * M_PI) / (static_cast<int>(foldMode_) * 2);
    float angle = atan2(y, x);
    // 平移到 [0, 2pi) 再以 startAngle 为零基准
    float start = -M_PI / 2.0f;
    float shifted = angle - start;
    while (shifted < 0) shifted += 2.0f * M_PI;
    while (shifted >= 2.0f * M_PI) shifted -= 2.0f * M_PI;
    
    return shifted >= 0.0f && shifted <= sectorAngle;
}

void PaperCutEngine::RenderPreviewCanvas(OH_Drawing_Canvas* canvas)
{
    // WebEditor 对齐的 PreviewCanvas：
    // - 只渲染“纸张 + CUT 镂空”（不显示铅笔草稿）
    // - 使用 2*N 段旋转/镜像展开
    if (!canvas || !layersInitialized_) return;
    
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float centerX = width * 0.5f;
    float centerY = height * 0.5f;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    int totalSegments = isFullPaper ? 1 : (static_cast<int>(foldMode_) * 2);
    float sectorAngle = isFullPaper ? (2.0f * M_PI) : ((2.0f * M_PI) / totalSegments);
    
    // Offscreen(固定 2048) -> Preview(窗口尺寸) 的比例映射，保证预览缩放一致
    float srcSize = static_cast<float>(std::min(canvasWidth_, canvasHeight_));
    float dstSize = static_cast<float>(std::min(width, height));
    float scale = (srcSize > 0.0f) ? (dstSize / srcSize) : 1.0f;
    
    // ClearCommand 分界：只渲染最后一次 clear 之后的裁剪
    size_t startIndex = 0;
    for (size_t i = 0; i < commandHistory_.size(); i++) {
        if (dynamic_cast<ClearCommand*>(commandHistory_[i].get()) != nullptr) {
            startIndex = i + 1;
        }
    }

    // 预览以画布中心为原点进行展开
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasTranslate(canvas, centerX, centerY);
    OH_Drawing_CanvasScale(canvas, scale, scale);

    // 纸张底色（只绘制一次）
    DrawPaperBase(canvas);
    
    float startAngle = -M_PI / 2.0f;
    for (int i = 0; i < totalSegments; i++) {
        OH_Drawing_CanvasSave(canvas);
        
        // WebEditor：偶数段旋转；奇数段按边界镜像（rotate 2*boundary + scaleY(-1)）
        if (isFullPaper) {
            // no-op
        } else if (i % 2 == 0) {
            OH_Drawing_CanvasRotate(canvas, (i * sectorAngle) * 180.0f / M_PI, 0, 0);
        } else {
            float boundary = startAngle + ((i + 1) / 2) * sectorAngle;
            OH_Drawing_CanvasRotate(canvas, (2.0f * boundary) * 180.0f / M_PI, 0, 0);
            OH_Drawing_CanvasScale(canvas, 1.0f, -1.0f);
        }

        // WebEditor：每段只在“扇形 wedge”内应用裁剪（CLIP_RADIUS 足够大）
        if (!isFullPaper) {
            float clipRadius = std::min(canvasWidth_, canvasHeight_) * CLIP_RADIUS_RATIO;
            OH_Drawing_Path* sectorPath = CreateWedgePath(clipRadius, startAngle, sectorAngle);
            OH_Drawing_CanvasClipPath(canvas, sectorPath, OH_Drawing_CanvasClipOp::INTERSECT, true);
            OH_Drawing_PathDestroy(sectorPath);
        }

        // 应用所有 CUT 命令（destination-out 等价效果）
        for (size_t k = startIndex; k < commandHistory_.size(); k++) {
            auto* cut = dynamic_cast<CutCommand*>(commandHistory_[k].get());
            if (!cut) continue;
            // 复用 CutCommand::Apply 的逻辑（clip DIFFERENCE + 透明填充）
            cut->Apply(canvas);
        }

        // WebEditor：实时剪刀预览（在预览层做 cut 模拟），但仍只在 wedge 内生效
        if (isDrawing_ && currentToolMode_ == ToolMode::SCISSORS && currentPoints_.size() > 1) {
            CutCommand liveCut(currentPoints_);
            liveCut.Apply(canvas);
        }
        
        OH_Drawing_CanvasRestore(canvas);
    }
    
    OH_Drawing_CanvasRestore(canvas);
}


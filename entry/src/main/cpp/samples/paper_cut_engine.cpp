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
{
}

PaperCutEngine::~PaperCutEngine()
{
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
    
    // 绘制输入画布（编辑区）
    RenderInputCanvas(canvas);
    
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
    
    // 绘制输出画布（预览区）
    RenderOutputCanvas(canvas);
    
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
    if (!canvas) return;
    
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float centerX = width * 0.5f;
    float paperRadius = std::min(width, height) * PAPER_RADIUS_RATIO;
    float clipRadius = std::min(width, height) * CLIP_RADIUS_RATIO;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    // 0折：圆心在画布中心；其他折：圆心在x轴居中，y轴在底部（圆心y = height - radius）
    float centerY = isFullPaper ? (height * 0.5f) : (height - paperRadius);
    float sectorAngle = isFullPaper ? (2.0f * M_PI) : ((2.0f * M_PI) / (static_cast<int>(foldMode_) + 1));
    float baseRotation = isFullPaper ? 0 : -sectorAngle / 2.0f;
    
    // 保存状态
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasTranslate(canvas, centerX, centerY);
    
    // 应用视图变换
    if (drawState_.isFlipped) {
        OH_Drawing_CanvasScale(canvas, -1.0f, 1.0f);
    }
    OH_Drawing_CanvasScale(canvas, VIEW_SCALE * drawState_.zoom, VIEW_SCALE * drawState_.zoom);
    OH_Drawing_CanvasTranslate(canvas, drawState_.pan.x, drawState_.pan.y + height * VIEW_OFFSET_Y_RATIO);
    // 使用CanvasRotate实现旋转（需要4个参数：canvas, degrees, px, py）
    OH_Drawing_CanvasRotate(canvas, (baseRotation + drawState_.rotation) * 180.0f / M_PI, 0, 0);
    OH_Drawing_CanvasTranslate(canvas, -centerX, -centerY);
    
    // 设置裁剪区域 - 纸张逻辑层
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Path* clipPath = OH_Drawing_PathCreate();
    
    // 在变换后的坐标系统中，中心是(0,0)
    if (isFullPaper) {
        if (paperType_ == PaperType::CIRCLE) {
            OH_Drawing_PathAddCircle(clipPath, 0, 0, paperRadius, PATH_DIRECTION_CCW);
        } else {
            OH_Drawing_PathAddRect(clipPath, -paperRadius, -paperRadius,
                                  paperRadius, paperRadius, PATH_DIRECTION_CCW);
        }
    } else {
        float startAngle = -M_PI / 2.0f;
        float endAngle = startAngle + sectorAngle;
        OH_Drawing_PathMoveTo(clipPath, 0, 0);
        // 使用圆弧绘制扇形
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
    
    // 绘制纸张底色（在变换后的坐标系统中，中心是(0,0)）
    // 使用已定义的paperRadius变量
    
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
    
    // 绘制已确认的裁剪
    DrawActions(canvas);
    
    OH_Drawing_CanvasRestore(canvas);  // 结束裁剪
    
    // 绘制实时剪刀线（放在此处，使其在全屏可见）
    if (isDrawing_ && currentToolMode_ == ToolMode::SCISSORS && currentPoints_.size() > 1) {
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
    }
    
    // 绘制扇形/纸张边界线（最后画，压在最上面）
    DrawFoldLines(canvas);
    
    OH_Drawing_CanvasRestore(canvas);  // 结束全局变换
}

void PaperCutEngine::RenderOutputCanvas(OH_Drawing_Canvas* canvas)
{
    if (!canvas) return;
    
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float centerX = width * 0.5f;
    float paperRadius = std::min(width, height) * PAPER_RADIUS_RATIO;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    // 0折：圆心在画布中心；其他折：圆心在x轴居中，y轴在底部（圆心y = height - radius）
    float centerY = isFullPaper ? (height * 0.5f) : (height - paperRadius);
    
    // 保存状态
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasTranslate(canvas, centerX, centerY);
    OH_Drawing_CanvasScale(canvas, drawState_.zoom, drawState_.zoom);
    
    // 绘制纸张底色
    DrawPaperBase(canvas);
    
    // 应用对称变换绘制裁剪
    if (isFullPaper) {
        DrawActions(canvas);
    } else {
        float sectorAngle = (2.0f * M_PI) / (static_cast<int>(foldMode_) + 1);
        int totalSegments = static_cast<int>(foldMode_) + 1;
        
        for (int i = 0; i < totalSegments; i++) {
            OH_Drawing_CanvasSave(canvas);
            
            if (i % 2 == 0) {
                // 使用CanvasRotate实现旋转（需要4个参数：canvas, degrees, px, py）
                OH_Drawing_CanvasRotate(canvas, i * sectorAngle * 180.0f / M_PI, 0, 0);
            } else {
                float boundary = -M_PI / 2.0f + ((i + 1) / 2) * sectorAngle;
                // 使用CanvasRotate实现旋转
                OH_Drawing_CanvasRotate(canvas, 2 * boundary * 180.0f / M_PI, 0, 0);
                OH_Drawing_CanvasScale(canvas, 1.0f, -1.0f);
            }
            
            DrawActions(canvas);
            OH_Drawing_CanvasRestore(canvas);
        }
    }
    
    OH_Drawing_CanvasRestore(canvas);
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
    int width = OH_Drawing_CanvasGetWidth(canvas);
    int height = OH_Drawing_CanvasGetHeight(canvas);
    float paperRadius = std::min(width, height) * PAPER_RADIUS_RATIO;
    float clipRadius = std::min(width, height) * CLIP_RADIUS_RATIO;
    
    bool isFullPaper = (foldMode_ == FoldMode::ZERO);
    float sectorAngle = isFullPaper ? (2.0f * M_PI) : ((2.0f * M_PI) / (static_cast<int>(foldMode_) + 1));
    
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
    
    isDrawing_ = true;
    currentPoints_.clear();
    currentPoints_.push_back(Point(x, y));
}

void PaperCutEngine::AddPoint(float x, float y)
{
    if (!isDrawing_) return;
    
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
}

void PaperCutEngine::FinishDrawing()
{
    if (!isDrawing_ || currentPoints_.size() < 2) {
        isDrawing_ = false;
        currentPoints_.clear();
        return;
    }
    
    Action action;
    action.id = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    action.type = (currentToolMode_ == ToolMode::SCISSORS) ? ActionType::CUT : ActionType::STROKE;
    action.tool = currentToolMode_;
    action.points = currentPoints_;
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    actions_.push_back(action);
    actionRedoStack_.clear();
    redoStack_.clear();
    
    isDrawing_ = false;
    currentPoints_.clear();
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
    
    Action action;
    action.id = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    action.type = ActionType::CUT;
    action.tool = ToolMode::SCISSORS;
    action.points = finalPoints;
    action.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    actions_.push_back(action);
    actionRedoStack_.clear();
    redoStack_.clear();
    
    CancelBezier();
}

void PaperCutEngine::CancelBezier()
{
    bezierPoints_.clear();
    bezierClosed_ = false;
}

void PaperCutEngine::Undo()
{
    if (actions_.empty()) return;
    
    Action last = actions_.back();
    actions_.pop_back();
    actionRedoStack_.insert(actionRedoStack_.begin(), last);
}

void PaperCutEngine::Redo()
{
    if (actionRedoStack_.empty()) return;
    
    Action next = actionRedoStack_[0];
    actionRedoStack_.erase(actionRedoStack_.begin());
    actions_.push_back(next);
}

void PaperCutEngine::Clear()
{
    actions_.clear();
    actionRedoStack_.clear();
    redoStack_.clear();
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
    actions_.push_back(action);
}

std::vector<Action> PaperCutEngine::GetActions() const
{
    return actions_;
}

void PaperCutEngine::SetActions(const std::vector<Action>& actions)
{
    actions_ = actions;
    actionRedoStack_.clear();
    redoStack_.clear();
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


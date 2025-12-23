//
// Created on 2025/12/21.
// 剪纸绘制引擎头文件
//

#ifndef PAPERCUTTING_PAPER_CUT_ENGINE_H
#define PAPERCUTTING_PAPER_CUT_ENGINE_H

#include <native_drawing/drawing_canvas.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_color.h>
#include <native_window/external_window.h>
#include <vector>
#include <string>
#include <memory>

// 点结构
struct Point {
    float x;
    float y;
    
    Point() : x(0), y(0) {}
    Point(float x, float y) : x(x), y(y) {}
};

// 工具类型
enum class ToolMode {
    SCISSORS = 0,      // 剪刀（裁剪）
    BEZIER = 1,        // 贝塞尔曲线
    DRAFT_PEN = 2,     // 铅笔
    DRAFT_ERASER = 3   // 橡皮
};

// 折叠模式
enum class FoldMode {
    ZERO = 0,  // 不折叠（全纸）
    ONE = 1,
    TWO = 2,
    THREE = 3,
    FOUR = 4,
    FIVE = 5,
    SIX = 6,
    SEVEN = 7,
    EIGHT = 8
};

// 纸张类型
enum class PaperType {
    CIRCLE = 0,   // 圆形
    SQUARE = 1    // 方形
};

// 动作类型
enum class ActionType {
    CUT = 0,      // 裁剪
    STROKE = 1    // 笔触
};

// 动作结构
struct Action {
    std::string id;
    ActionType type;
    ToolMode tool;
    std::vector<Point> points;
    int64_t timestamp;
    
    Action() : type(ActionType::CUT), tool(ToolMode::SCISSORS), timestamp(0) {}
};

// 绘制状态
struct DrawState {
    float zoom;
    Point pan;
    float rotation;
    bool isFlipped;
    
    DrawState() : zoom(1.0f), pan(0, 0), rotation(0), isFlipped(false) {}
};

// 剪纸绘制引擎类
class PaperCutEngine {
public:
    PaperCutEngine();
    ~PaperCutEngine();
    
    // 初始化画布
    bool Initialize(OHNativeWindow* window, int width, int height);
    void SetPreviewWindow(OHNativeWindow* window);  // 设置预览窗口
    
    // 绘制主函数
    void Render();
    void RenderPreview();  // 渲染预览画布
    void RenderInputCanvas(OH_Drawing_Canvas* canvas);  // 绘制输入画布（编辑区）
    void RenderOutputCanvas(OH_Drawing_Canvas* canvas);  // 绘制输出画布（预览区）
    
    // 工具操作
    void SetToolMode(ToolMode mode);
    ToolMode GetToolMode() const { return currentToolMode_; }
    
    // 折叠模式
    void SetFoldMode(FoldMode mode);
    FoldMode GetFoldMode() const { return foldMode_; }
    
    // 纸张设置
    void SetPaperType(PaperType type);
    void SetPaperColor(uint32_t color);
    
    // 绘制操作
    void StartDrawing(float x, float y);
    void AddPoint(float x, float y);
    void FinishDrawing();
    void CancelDrawing();
    
    // 贝塞尔曲线操作
    void AddBezierPoint(float x, float y);
    void UpdateBezierPoint(int index, float x, float y);
    void CloseBezier();
    void CancelBezier();
    
    // 撤销/重做
    void Undo();
    void Redo();
    void Clear();
    
    // 变换操作
    void SetZoom(float zoom);
    void SetPan(float x, float y);
    void SetRotation(float rotation);
    void SetFlip(bool flipped);
    
    // 动作管理
    void AddAction(const Action& action);
    std::vector<Action> GetActions() const { return actions_; }
    void SetActions(const std::vector<Action>& actions);
    
    // 坐标转换
    Point ScreenToModel(float x, float y) const;
    Point ModelToScreen(float x, float y) const;
    
    // 获取画布尺寸
    int GetCanvasWidth() const { return canvasWidth_; }
    int GetCanvasHeight() const { return canvasHeight_; }
    
private:
    // 绘制函数
    void DrawPaperBase(OH_Drawing_Canvas* canvas);
    void DrawActions(OH_Drawing_Canvas* canvas);
    void DrawCurrentStroke(OH_Drawing_Canvas* canvas);
    void DrawBezierUI(OH_Drawing_Canvas* canvas);
    void DrawFoldLines(OH_Drawing_Canvas* canvas);
    
    // 路径绘制
    void DrawPath(OH_Drawing_Canvas* canvas, const std::vector<Point>& points, bool closePath);
    void DrawPencilStroke(OH_Drawing_Canvas* canvas, const std::vector<Point>& points);
    void ErasePencilStroke(OH_Drawing_Canvas* canvas, const std::vector<Point>& points);
    
    // 对称变换
    Point TransformPoint(const Point& p, int segment, float sectorAngle) const;
    
    // 贝塞尔曲线计算
    std::vector<Point> CalculateSplinePoints(const std::vector<Point>& points, bool closed) const;
    
    // 画布管理
    OHNativeWindow* nativeWindow_;
    OHNativeWindow* previewWindow_;  // 预览窗口
    int canvasWidth_;
    int canvasHeight_;
    
    // 状态
    ToolMode currentToolMode_;
    FoldMode foldMode_;
    PaperType paperType_;
    uint32_t paperColor_;
    DrawState drawState_;
    
    // 动作列表
    std::vector<Action> actions_;
    std::vector<Action> redoStack_;
    
    // 当前绘制
    bool isDrawing_;
    std::vector<Point> currentPoints_;
    
    // 贝塞尔曲线
    std::vector<Point> bezierPoints_;
    bool bezierClosed_;
    bool bezierSharpMode_;  // true=直线, false=曲线
    
    // 常量
    static constexpr int CANVAS_SIZE = 2048;
    static constexpr float PAPER_RADIUS_RATIO = 0.40f;
    static constexpr float CLIP_RADIUS_RATIO = 1.5f;
    static constexpr float VIEW_SCALE = 1.2f;
    static constexpr float VIEW_OFFSET_Y_RATIO = 0.25f;
};

#endif // PAPERCUTTING_PAPER_CUT_ENGINE_H


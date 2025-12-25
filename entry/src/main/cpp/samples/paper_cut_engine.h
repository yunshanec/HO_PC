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
#include <chrono>

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

// 动作结构（兼容旧代码）
struct Action {
    std::string id;
    ActionType type;
    ToolMode tool;
    std::vector<Point> points;
    int64_t timestamp;
    
    Action() : type(ActionType::CUT), tool(ToolMode::SCISSORS), timestamp(0) {}
};

// 命令接口（Command Pattern）
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Apply(OH_Drawing_Canvas* canvas) = 0;  // 应用命令到画布
    virtual void Revert(OH_Drawing_Canvas* canvas) = 0;  // 撤销命令
    virtual Action ToAction() const = 0;  // 转换为Action（用于序列化）
};

// 裁剪命令
class CutCommand : public ICommand {
private:
    std::vector<Point> points_;
    std::string id_;
    
public:
    CutCommand(const std::vector<Point>& points);
    void Apply(OH_Drawing_Canvas* canvas) override;
    void Revert(OH_Drawing_Canvas* canvas) override;
    Action ToAction() const override;
};

// 铅笔命令
class PencilCommand : public ICommand {
private:
    std::vector<Point> points_;
    std::string id_;
    
public:
    PencilCommand(const std::vector<Point>& points);
    void Apply(OH_Drawing_Canvas* canvas) override;
    void Revert(OH_Drawing_Canvas* canvas) override;
    Action ToAction() const override;
};

// 橡皮命令
class EraserCommand : public ICommand {
private:
    std::vector<Point> points_;
    std::string id_;
    uint32_t backgroundColor_ = 0xFFC4161C;
    
public:
    EraserCommand(const std::vector<Point>& points, uint32_t backgroundColor);
    void Apply(OH_Drawing_Canvas* canvas) override;
    void Revert(OH_Drawing_Canvas* canvas) override;
    Action ToAction() const override;
};

// 清空命令
class ClearCommand : public ICommand {
private:
    std::vector<std::unique_ptr<ICommand>> previousCommands_;
    std::string id_;
    
public:
    ClearCommand();
    void Apply(OH_Drawing_Canvas* canvas) override;
    void Revert(OH_Drawing_Canvas* canvas) override;
    Action ToAction() const override;
    void SetPreviousCommands(std::vector<std::unique_ptr<ICommand>> commands);
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
    void Render();  // 渲染主画布（InputCanvas + OffscreenCanvas合成）
    void RenderPreview();  // 渲染预览画布（PreviewCanvas）
    
    // 标记层需要更新
    void MarkInputDirty() { inputDirty_ = true; }
    void MarkOffscreenDirty() { offscreenDirty_ = true; }
    
    // 工具操作
    void SetToolMode(ToolMode mode);
    ToolMode GetToolMode() const { return currentToolMode_; }
    
    // 折叠模式
    void SetFoldMode(FoldMode mode);
    FoldMode GetFoldMode() const { return foldMode_; }
    
    // 纸张设置
    void SetPaperType(PaperType type);
    void SetPaperColor(uint32_t color);
    uint32_t GetPaperColor() const { return paperColor_; }
    
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
    
    // 动作管理（兼容旧接口）
    void AddAction(const Action& action);  // 兼容性接口：将Action转换为Command
    std::vector<Action> GetActions() const;  // 从命令历史生成Action列表
    void SetActions(const std::vector<Action>& actions);  // 从Action列表重建命令历史
    
    // 坐标转换
    Point ScreenToModel(float x, float y) const;
    Point ModelToScreen(float x, float y) const;
    
    // 获取画布尺寸
    int GetCanvasWidth() const { return canvasWidth_; }
    int GetCanvasHeight() const { return canvasHeight_; }
    
private:
    // 3层画布架构（按照refactor.md重构）
    void InitializeLayers(int width, int height);
    void DestroyLayers();
    
    // ① InputCanvas - 交互层（只处理输入和临时绘制）
    void RenderInputCanvas(OH_Drawing_Canvas* canvas);
    
    // ② OffscreenCanvas - 数据层（存储真实数据，通过命令应用）
    void RenderOffscreenCanvas();  // 重新渲染整个OffscreenCanvas（从所有命令）
    void ApplyCommandToOffscreenCanvas(std::unique_ptr<ICommand> cmd);
    void RevertCommandFromOffscreenCanvas();
    
    // ③ PreviewCanvas - 展示层（只渲染预览，应用旋转/镜像/对称展开）
    void RenderPreviewCanvas(OH_Drawing_Canvas* canvas);
    
    // 旧版兼容函数
    void RenderOutputCanvas(OH_Drawing_Canvas* canvas);
    void DrawActions(OH_Drawing_Canvas* canvas);
    
    // 合成层（用于主渲染）
    void CompositeLayers(OH_Drawing_Canvas* targetCanvas);  // 合成InputCanvas + OffscreenCanvas到目标画布
    
    // 输入约束：判定点是否在当前扇形(sector)范围内（用于 InputCanvas 数据约束）
    bool IsPointInSector(float x, float y) const;
    
    // 视图变换辅助函数
    void ApplyViewTransform(OH_Drawing_Canvas* canvas, float centerX, float centerY, float height);
    
    // 绘制辅助函数
    void DrawPaperBase(OH_Drawing_Canvas* canvas);
    void DrawFoldLines(OH_Drawing_Canvas* canvas);
    
    // 绘制扇形路径（用于裁剪）
    OH_Drawing_Path* CreateSectorPath(float centerX, float centerY, float radius, bool isFullPaper, float sectorAngle);
    
public:
    // 路径绘制（用于命令，需要public以便命令类访问）
    static void DrawPath(OH_Drawing_Canvas* canvas, const std::vector<Point>& points, bool closePath);
    static void DrawPencilStroke(OH_Drawing_Canvas* canvas, const std::vector<Point>& points);
    static void ErasePencilStroke(OH_Drawing_Canvas* canvas, const std::vector<Point>& points, uint32_t backgroundColor);

private:
    
    
    // 贝塞尔曲线计算
    std::vector<Point> CalculateSplinePoints(const std::vector<Point>& points, bool closed) const;
    
    // 画布管理
    OHNativeWindow* nativeWindow_;
    OHNativeWindow* previewWindow_;  // 预览窗口
    int canvasWidth_;
    int canvasHeight_;
    
    // 3层画布架构（按照refactor.md）
    // ① InputCanvas - 交互层（临时绘制）
    OH_Drawing_Bitmap* inputBitmap_;           // InputCanvas bitmap
    OH_Drawing_Canvas* inputCanvas_;           // InputCanvas canvas
    bool inputDirty_;                          // InputCanvas是否需要重绘
    
    // ② OffscreenCanvas - 数据层（真实数据存储）
    OH_Drawing_Bitmap* offscreenBitmap_;       // OffscreenCanvas bitmap（数据真相层）
    OH_Drawing_Canvas* offscreenCanvas_;       // OffscreenCanvas canvas
    bool offscreenDirty_;                      // OffscreenCanvas是否需要重绘
    
    // ③ PreviewCanvas - 展示层（预览渲染，在RenderPreview时使用）
    // 注意：PreviewCanvas不需要离屏bitmap，它直接从OffscreenCanvas读取并应用变换
    
    bool layersInitialized_;                   // 层是否已初始化
    
    // 状态
    ToolMode currentToolMode_;
    FoldMode foldMode_;
    PaperType paperType_;
    uint32_t paperColor_;
    DrawState drawState_;
    
    // 命令历史（使用命令模式）
    std::vector<std::unique_ptr<ICommand>> commandHistory_;  // 已应用的命令
    std::vector<std::unique_ptr<ICommand>> redoStack_;       // 可重做的命令
    
    // 动作列表（兼容旧接口，从命令生成）
    std::vector<Action> actions_;
    std::vector<Action> actionRedoStack_;  // Action类型的重做栈（兼容旧代码）
    
    // 当前绘制
    bool isDrawing_;
    std::vector<Point> currentPoints_;
    
    // 贝塞尔曲线
    std::vector<Point> bezierPoints_;
    bool bezierClosed_;
    bool bezierSharpMode_;  // true=直线, false=曲线
    
    // 常量
    static constexpr int CANVAS_SIZE = 2048;
    static constexpr float SECTOR_RADIUS_RATIO = 0.5f;  // 扇形半径为画布高度的一半
    static constexpr float PAPER_RADIUS_RATIO = 0.4f;  // 纸张半径为画布尺寸的比例
    // WebEditor 对齐：扇形裁剪半径需要足够大，保证在缩放/旋转/平移下扇形覆盖整个纸张区域
    static constexpr float CLIP_RADIUS_RATIO = 1.5f;    // 扇形裁剪半径为画布尺寸的比例
    static constexpr float VIEW_SCALE = 1.2f;
    static constexpr float VIEW_OFFSET_Y_RATIO = 0.25f;
};

#endif // PAPERCUTTING_PAPER_CUT_ENGINE_H


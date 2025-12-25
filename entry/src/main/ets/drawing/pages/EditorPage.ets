// 编辑器页面
import { router, window } from '@kit.ArkUI';
import { preferences } from '@kit.ArkData';
import XComponentContext from '../../interface/XComponentContext';
import { SavedWork, RouterParams, NativeModule, GlobalObject, TouchPoint, Action } from '../../common/types';

// 鼠标事件常量（HarmonyOS API）
const MOUSE_BUTTON_LEFT = 0;
const MOUSE_BUTTON_RIGHT = 2;
const MOUSE_ACTION_PRESS = 0;
const MOUSE_ACTION_MOVE = 1;
const MOUSE_ACTION_RELEASE = 2;

@Entry
@Component
export struct EditorPage {
  @State currentTool: number = 0; // 0=剪刀, 1=铅笔, 2=橡皮
  @State scissorMode: number = 0; // 0=手工, 1=直线, 2=曲线
  @State foldMode: number = 4;
  @State paperColor: string = '#C4161C';
  @State paperType: string = 'CIRCLE';
  @State isDrawing: boolean = false;
  @State lastPoint: TouchPoint | null = null;
  @State showScissorMenu: boolean = false;
  @State showPreview: boolean = false;
  @State isSplitMode: boolean = false;
  @State scaleValue: number = 1.0;  // 缩放值
  @State pinchValue: number = 1.0;  // 捏合手势的缩放值
  @State panX: number = 0;  // 平移X
  @State panY: number = 0;  // 平移Y
  @State isPinching: boolean = false;  // 是否正在捏合
  @State isPanning: boolean = false;  // 是否正在平移
  @State isRightMouseDown: boolean = false;  // 是否按下鼠标右键
  @State lastMouseX: number = 0;  // 上次鼠标X位置
  @State lastMouseY: number = 0;  // 上次鼠标Y位置

  private xComponentController: XComponentController = new XComponentController();
  private previewXComponentController: XComponentController = new XComponentController();
  private papercutModule: NativeModule | null = null;
  private nativeWindow: string | number | null = null;
  private previewNativeWindow: string | number | null = null;
  private work: SavedWork | null = null;
  private viewMode: boolean = false;
  private lastPanX: number = 0;
  private lastPanY: number = 0;
  // 预览渲染：与输入解耦，合并/节流到每帧一次
  private previewRenderPending: boolean = false;
  private previewNeedsRender: boolean = false;

  aboutToAppear() {
    // 获取传递的参数
    const params: RouterParams | undefined = router.getParams() as RouterParams | undefined;
    if (params) {
      // 检查是否是新建作品（从CreateModalPage传递）
      if (params.paperType && params.paperColor) {
        this.paperType = params.paperType as string;
        this.paperColor = params.paperColor as string;
        this.foldMode = 4; // 默认4瓣
        console.info('EditorPage: New work - paperType:', this.paperType, 'paperColor:', this.paperColor);
      } else if (params.work) {
        // 编辑已有作品
        this.work = params.work;
        this.viewMode = params.viewMode === true;
        if (this.work) {
          this.foldMode = this.work.foldMode || 4;
          this.paperColor = this.work.paperColor || '#C4161C';
          this.paperType = this.work.paperType || 'CIRCLE';
        }
        console.info('EditorPage: Edit work - foldMode:', this.foldMode);
      }
    }
    
    // 注意：Native 模块必须通过 XComponent 的 onLoad 回调获取
    // requireNativeModule 无法获取，因为模块方法是导出到 XComponent context 的
    console.info('EditorPage: Waiting for XComponent onLoad to get Native module...');
  }

  onXComponentLoad() {
    console.info('EditorPage: onXComponentLoad - Starting initialization');
    
    if (!this.papercutModule) {
      console.error('EditorPage: papercutModule is null - cannot initialize!');
      return;
    }
    
    try {
      const nativeWindow = this.xComponentController.getXComponentSurfaceId();
      console.info('EditorPage: Got surfaceId:', nativeWindow);

      if (nativeWindow) {
        this.nativeWindow = nativeWindow;

        // 获取屏幕尺寸（异步获取，先使用默认值）
        // Native层会根据实际的NativeWindow buffer尺寸进行适配
        let width: number = 2048;
        let height: number = 2048;

        // 使用默认值初始化引擎（Native层会根据实际buffer尺寸适配）
        console.info('EditorPage: Initializing engine with size:', width, 'x', height);
        this.papercutModule.initializeEngine(nativeWindow, width, height);

        // 设置纸张类型和颜色（必须在初始化引擎之后）
        // 只支持圆形团花
        const paperTypeValue = 0; // 0=CIRCLE
        console.info('EditorPage: Setting paperType:', paperTypeValue, '(CIRCLE)');
        this.papercutModule.setPaperType(paperTypeValue);
        
        const colorArgb = this.hexToArgb(this.paperColor);
        console.info('EditorPage: Setting paperColor:', this.paperColor, '-> ARGB:', colorArgb.toString(16));
        this.papercutModule.setPaperColor(colorArgb);

        // 设置折叠模式和工具
        console.info('EditorPage: Setting foldMode:', this.foldMode);
        this.papercutModule.setFoldMode(this.foldMode);
        this.papercutModule.setToolMode(this.currentTool);

        // 如果是编辑已有作品，恢复 actions（命令历史/离屏数据层）
        if (this.work && this.work.actions && this.work.actions.length > 0) {
          console.info('EditorPage: Restoring actions:', this.work.actions.length);
          this.papercutModule.setActions(this.work.actions);
        }

        // 延迟绘制，确保所有设置都已完成
        setTimeout(() => {
          console.info('EditorPage: Rendering initial canvas...');
          this.render();
          this.schedulePreviewRender();
        }, 200);
      }
    } catch (error) {
      console.error('EditorPage: Error in onXComponentLoad:', JSON.stringify(error));
    }
  }

  onPreviewXComponentLoad() {
    console.info('EditorPage: Preview XComponent loaded');
    try {
      const nativeWindow = this.previewXComponentController.getXComponentSurfaceId();
      console.info('EditorPage: Got preview surfaceId:', nativeWindow);
      if (nativeWindow && this.papercutModule) {
        this.previewNativeWindow = nativeWindow;
        this.papercutModule.setPreviewWindow(nativeWindow);
        setTimeout(() => {
          this.schedulePreviewRender();
        }, 200);
      }
    } catch (error) {
      console.error('EditorPage: Error getting preview surfaceId:', error);
    }
  }

  // 渲染草稿和裁剪层
  render() {
    if (this.nativeWindow && this.papercutModule) {
      this.papercutModule.drawPaperCut(this.nativeWindow);
    }
  }

  // 渲染参考层
  renderPreview() {
    if (this.previewNativeWindow && this.papercutModule) {
      this.papercutModule.drawPaperCutPreview(this.previewNativeWindow);
    }
  }

  // 预览渲染调度：不要在 pointer move 上直接 renderPreview；只标记 dirty，并在下一帧合并刷新
  schedulePreviewRender() {
    if (!this.papercutModule) return;
    if (!(this.showPreview || this.isSplitMode)) return;
    this.previewNeedsRender = true;
    if (this.previewRenderPending) return;
    this.previewRenderPending = true;
    setTimeout(() => {
      this.previewRenderPending = false;
      if (!this.previewNeedsRender) return;
      this.previewNeedsRender = false;
      this.renderPreview();
    }, 16);
  }

  onTouchStart(event: TouchEvent) {
    if (this.viewMode || !this.papercutModule) return;
    
    // 如果正在缩放或平移，不处理绘制
    if (this.isPinching || this.isPanning || (event.touches && event.touches.length > 1)) {
      return;
    }
    
    if (event.touches && event.touches.length > 0) {
      const touch = event.touches[0];
      // 获取XComponent的实际尺寸（从触摸事件获取）
      // 画布尺寸是2048x2048，需要将屏幕坐标转换为画布坐标
      const canvasSize = 2048;
      const viewScale = 1.2 * this.scaleValue; // VIEW_SCALE * 缩放值
      
      // 获取触摸点相对于XComponent的位置
      // 考虑缩放和平移
      const modelX = (touch.x - canvasSize / 2 - this.panX) / viewScale;
      const modelY = (touch.y - canvasSize / 2 - this.panY) / viewScale;
      
      this.papercutModule.startDrawing(modelX, modelY);
      this.isDrawing = true;
      const point: TouchPoint = { x: modelX, y: modelY };
      this.lastPoint = point;
    }
  }

  onTouchMove(event: TouchEvent) {
    if (this.viewMode || !this.papercutModule) return;
    
    // 如果正在缩放或平移，不处理绘制
    if (this.isPinching || this.isPanning || (event.touches && event.touches.length > 1)) {
      return;
    }
    
    if (this.isDrawing && this.lastPoint && event.touches && event.touches.length > 0) {
      const touch = event.touches[0];
      const canvasSize = 2048;
      const viewScale = 1.2 * this.scaleValue; // VIEW_SCALE * 缩放值
      
      // 转换为模型坐标（考虑缩放和平移）
      const modelX = (touch.x - canvasSize / 2 - this.panX) / viewScale;
      const modelY = (touch.y - canvasSize / 2 - this.panY) / viewScale;
      
      // 检查距离，避免点太密集
      const dx = modelX - this.lastPoint.x;
      const dy = modelY - this.lastPoint.y;
      if (dx * dx + dy * dy > 100) {
        this.papercutModule.addPoint(modelX, modelY);
        const point: TouchPoint = { x: modelX, y: modelY };
        this.lastPoint = point;
        this.render();
      }
    }
  }

  onTouchEnd(event: TouchEvent) {
    if (this.viewMode || !this.isDrawing || !this.papercutModule) return;
    
    this.papercutModule.finishDrawing();
    this.isDrawing = false;
    this.lastPoint = null;
    this.render();
    // 同时更新预览
    if (this.showPreview || this.isSplitMode) {
      this.schedulePreviewRender();
    }
  }

  onToolChange(tool: number) {
    this.currentTool = tool;
    if (this.papercutModule) {
      // 工具映射：0=SCISSORS, 1=BEZIER, 2=DRAFT_PEN, 3=DRAFT_ERASER
      this.papercutModule.setToolMode(tool);
    }
    this.showScissorMenu = false;
    this.render();
  }

  onScissorModeChange(mode: number) {
    this.scissorMode = mode;
    this.showScissorMenu = false;
    // 0=手工(SCISSORS), 1=直线(BEZIER SHARP), 2=曲线(BEZIER CURVED)
    if (mode === 0) {
      this.currentTool = 0; // SCISSORS
    } else {
      this.currentTool = 1; // BEZIER
    }
    if (this.papercutModule) {
      this.papercutModule.setToolMode(this.currentTool);
    }
  }

  onFoldChange(mode: number) {
    this.foldMode = mode;
    if (this.papercutModule) {
      this.papercutModule.setFoldMode(mode);
      this.render();
      if (this.showPreview || this.isSplitMode) {
        this.schedulePreviewRender();
      }
    }
  }

  onUndo() {
    if (this.papercutModule) {
      this.papercutModule.undo();
      this.render();
      this.schedulePreviewRender();
    }
  }

  onRedo() {
    if (this.papercutModule) {
      this.papercutModule.redo();
      this.render();
      this.schedulePreviewRender();
    }
  }

  onClear() {
    if (this.papercutModule) {
      this.papercutModule.clear();
      this.render();
      this.schedulePreviewRender();
    }
  }

  onBack() {
    router.back();
  }

  async onSave() {
    if (!this.papercutModule) {
      console.error('EditorPage: Cannot save - papercutModule is null');
      return;
    }

    try {
      // 获取actions
      const actions: Action[] = this.papercutModule.getActions();
      
      // 创建或更新作品
      const work: SavedWork = {
        id: this.work?.id || `work_${Date.now()}`,
        paperType: this.paperType,
        paperColor: this.paperColor,
        foldMode: this.foldMode,
        date: Date.now(),
        actions: actions
      };

      // 加载现有作品列表
      const prefs = await preferences.getPreferences(this.getContext(), 'papercut_works');
      const worksJson: string = await prefs.get('works', '[]') as string;
      let works: SavedWork[] = JSON.parse(worksJson) as SavedWork[];

      // 更新或添加作品
      const existingIndex = works.findIndex(w => w.id === work.id);
      if (existingIndex >= 0) {
        works[existingIndex] = work;
      } else {
        works.push(work);
      }

      // 保存作品列表
      await prefs.put('works', JSON.stringify(works));
      await prefs.flush();

      console.info('EditorPage: Work saved successfully');
      
      // 跳转到Gallery页面
      router.replaceUrl({
        url: 'drawing/pages/GalleryPage'
      });
    } catch (error) {
      console.error('EditorPage: Failed to save work:', error);
    }
  }

  getContext() {
    return getContext(this) as Context;
  }

  onTogglePreview() {
    // 如果当前是分屏模式，切换为预览模式
    if (this.isSplitMode) {
      this.isSplitMode = false;
      this.showPreview = true;
    } else {
      // 切换预览模式：true=左右分屏预览，false=仅编辑
      this.showPreview = !this.showPreview;
    }
    // 当切换到预览模式时，渲染预览
    if (this.showPreview) {
      setTimeout(() => {
        this.schedulePreviewRender();
      }, 100);
    }
  }

  onToggleSplit() {
    // 分屏模式：同时显示编辑和预览
    this.showPreview = false;
    this.isSplitMode = !this.isSplitMode;
    if (this.isSplitMode) {
      setTimeout(() => {
        this.schedulePreviewRender();
      }, 100);
    }
  }

  build() {
    Column() {
      // 标题栏
      Row() {
        Button('返回')
          .type(ButtonType.Normal)
          .backgroundColor(Color.Transparent)
          .fontColor('#333333')
          .onClick(() => this.onBack())
        
        Blank()
        
        // 折法选择（在标题栏中）
        if (!this.viewMode) {
          Row() {
            Text('折法:')
              .fontSize(12)
              .fontColor('#666666')
              .margin({ right: 8 })
            
            ForEach([0, 1, 2, 3, 4, 5, 6, 7, 8], (mode: number) => {
              Stack() {
                Circle()
                .width(28)
                .height(28)
                  .fill(this.foldMode === mode ? this.paperColor : '#E0E0E0')
                
                if (this.foldMode === mode) {
                  Text(mode === 0 ? '0' : mode.toString())
                .fontSize(11)
                    .fontColor(Color.White)
                }
              }
              .width(28)
              .height(28)
                .onClick(() => this.onFoldChange(mode))
              .margin({ left: 2, right: 2 })
            }, (mode: number) => mode.toString())
          }
          .height(50)
          .justifyContent(FlexAlign.Center)
          .alignItems(VerticalAlign.Center)
        }
        
        Blank()
        
        if (!this.viewMode) {
          Button('保存')
            .type(ButtonType.Normal)
            .backgroundColor(this.paperColor)
            .fontColor(Color.White)
            .onClick(() => {
              this.onSave();
            })
        }
      }
      .width('100%')
      .height(50)
      .padding({ left: 10, right: 10 })
      .backgroundColor('#FDF6E3')

      // 主内容区域
      Row() {
        // 左侧编辑区
        Column() {
          // 画布
          Stack() {
            XComponent({
              id: 'xcomponent_editor',
              type: XComponentType.SURFACE,
              libraryname: 'entry',  // 必须与 Native 模块注册名称一致
              controller: this.xComponentController
            })
              .onLoad((xComponentContext: XComponentContext | null) => {
                // 通过 onLoad 回调获取 XComponentContext（这是 Native 模块导出的接口）
                console.info('EditorPage: XComponent onLoad callback triggered');
                
                if (xComponentContext) {
                  // XComponentContext 就是 Native 模块的接口，可以直接使用
                  this.papercutModule = xComponentContext as NativeModule;
                  console.info('EditorPage: ✓ Got Native module from XComponentContext');
                  
                  // 延迟调用初始化，确保 Surface 已创建
                  setTimeout(() => {
                    this.onXComponentLoad();
                  }, 100);
                } else {
                  console.error('EditorPage: ✗ XComponentContext is null - Native module failed to load!');
                  console.error('EditorPage: Check if libraryname="entry" matches the native module name');
                }
              })
              .width('100%')
              .height('100%')
              .backgroundColor('#FDF6E3')
              .scale({ x: this.scaleValue, y: this.scaleValue, z: 1 })
              .translate({ x: this.panX, y: this.panY, z: 0 })
              .gesture(
                GestureGroup(GestureMode.Exclusive,
                  // 捏合手势 - 缩放
                  PinchGesture()
                    .onActionStart(() => {
                      this.isPinching = true;
                      this.isDrawing = false;
                    })
                    .onActionUpdate((event: GestureEvent) => {
                      if (event && event.scale) {
                        this.scaleValue = this.pinchValue * event.scale;
                        // 限制缩放范围
                        if (this.scaleValue < 0.5) this.scaleValue = 0.5;
                        if (this.scaleValue > 3.0) this.scaleValue = 3.0;
                        
                        // 更新 C++ 引擎的缩放
                        if (this.papercutModule) {
                          this.papercutModule.setZoom(this.scaleValue);
                          this.render();
                        }
                      }
                    })
                    .onActionEnd(() => {
                      this.pinchValue = this.scaleValue;
                      this.isPinching = false;
                      if (this.papercutModule) {
                        this.render();
                      }
                    }),
                  // 平移手势
                  PanGesture()
                    .onActionStart(() => {
                      if (!this.isPinching) {
                        this.isPanning = true;
                        this.isDrawing = false;
                        this.lastPanX = this.panX;
                        this.lastPanY = this.panY;
                      }
                    })
                    .onActionUpdate((event: GestureEvent) => {
                      if (!this.isPinching && event && event.offsetX !== undefined && event.offsetY !== undefined) {
                        this.panX = this.lastPanX + event.offsetX;
                        this.panY = this.lastPanY + event.offsetY;
                        
                        // 更新 C++ 引擎的平移
                        if (this.papercutModule) {
                          this.papercutModule.setPan(this.panX, this.panY);
                          this.render();
                        }
                      }
                    })
                    .onActionEnd(() => {
                      this.isPanning = false;
                      if (this.papercutModule) {
                        this.render();
                      }
                    })
                )
              )
              .onTouch((event: TouchEvent) => {
                // 如果正在缩放或平移，不处理绘制
                if (this.isPinching || this.isPanning || (event.touches && event.touches.length > 1)) {
                  return;
                }
                
                if (event.type === TouchType.Down) {
                  this.onTouchStart(event);
                } else if (event.type === TouchType.Move) {
                  this.onTouchMove(event);
                } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
                  this.onTouchEnd(event);
                }
              })
              .onMouse((event: MouseEvent) => {
                // 鼠标交互：右键拖拽平移，左键绘制
                // 注意：禁用鼠标滚轮，不允许上下移动（通过不处理滚轮事件实现）
                if (this.viewMode || !this.papercutModule) return;
                
                if (event.button === MOUSE_BUTTON_RIGHT) {
                  // 右键：拖拽平移画布
                  if (event.action === MOUSE_ACTION_PRESS) {
                    this.isRightMouseDown = true;
                    this.lastMouseX = event.x;
                    this.lastMouseY = event.y;
                  } else if (event.action === MOUSE_ACTION_MOVE && this.isRightMouseDown) {
                    const deltaX = event.x - this.lastMouseX;
                    const deltaY = event.y - this.lastMouseY;
                    this.panX += deltaX;
                    this.panY += deltaY;
                    this.lastMouseX = event.x;
                    this.lastMouseY = event.y;
                    
                    if (this.papercutModule) {
                      this.papercutModule.setPan(this.panX, this.panY);
                      this.render();
                    }
                  } else if (event.action === MOUSE_ACTION_RELEASE) {
                    this.isRightMouseDown = false;
                  }
                } else if (event.button === MOUSE_BUTTON_LEFT) {
                  // 左键：绘制/裁剪/橡皮擦
                  const canvasSize = 2048;
                  const viewScale = 1.2 * this.scaleValue;
                  
                  const modelX = (event.x - canvasSize / 2 - this.panX) / viewScale;
                  const modelY = (event.y - canvasSize / 2 - this.panY) / viewScale;
                  
                  if (event.action === MOUSE_ACTION_PRESS) {
                    this.papercutModule.startDrawing(modelX, modelY);
                    this.isDrawing = true;
                    const point: TouchPoint = { x: modelX, y: modelY };
                    this.lastPoint = point;
                  } else if (event.action === MOUSE_ACTION_MOVE && this.isDrawing && this.lastPoint) {
                    const dx = modelX - this.lastPoint.x;
                    const dy = modelY - this.lastPoint.y;
                    if (dx * dx + dy * dy > 100) {
                      this.papercutModule.addPoint(modelX, modelY);
                      const point: TouchPoint = { x: modelX, y: modelY };
                      this.lastPoint = point;
                      this.render();
                    }
                  } else if (event.action === MOUSE_ACTION_RELEASE && this.isDrawing) {
                    this.papercutModule.finishDrawing();
                    this.isDrawing = false;
                    this.lastPoint = null;
                    this.render();
                    if (this.showPreview || this.isSplitMode) {
                      this.renderPreview();
                    }
                  }
                }
              })

            // 左侧工具栏（撤销/重做/清空）
            if (!this.viewMode) {
              Column() {
                Button() {
                  Text('↶')
                    .fontSize(20)
                    .fontColor('#333333')
                }
                  .width(36)
                  .height(36)
                  .borderRadius(18)
                  .backgroundColor(Color.White)
                  .onClick(() => this.onUndo())
                
                Button() {
                  Text('↷')
                    .fontSize(20)
                    .fontColor('#333333')
                }
                  .width(36)
                  .height(36)
                  .borderRadius(18)
                  .backgroundColor(Color.White)
                  .onClick(() => this.onRedo())
                
                Button() {
                  Text('×')
                    .fontSize(20)
                    .fontColor('#333333')
                }
                  .width(36)
                  .height(36)
                  .borderRadius(18)
                  .backgroundColor(Color.White)
                  .onClick(() => this.onClear())
              }
              .width(40)
              .height(120)
              .justifyContent(FlexAlign.SpaceBetween)
              .alignItems(HorizontalAlign.Center)
              .margin({ left: 10 })
              .position({ x: 0, y: '50%' })
              .offset({ y: -60 })
            }
          }
          .width('100%')
          .layoutWeight(1)
        }
        .width((this.isSplitMode || this.showPreview) ? '50%' : '100%')
        .layoutWeight(1)
        .backgroundColor('#FDF6E3')

        // 分隔线（仅在预览或分屏模式下显示）
        if (this.showPreview || this.isSplitMode) {
          Divider()
            .vertical(true)
            .strokeWidth(2)
            .color('#CCCCCC')
            .width(2)
        }

        // 右侧预览区（仅在预览或分屏模式下显示）
        if (this.showPreview || this.isSplitMode) {
          Column() {
            Stack() {
              // 预览画布
              XComponent({
                id: 'xcomponent_preview',
                type: XComponentType.SURFACE,
                libraryname: 'entry',  // 必须与 Native 模块注册名称一致
                controller: this.previewXComponentController
              })
                .onLoad((xComponentContext: XComponentContext | null) => {
                  // 通过 onLoad 回调获取 XComponentContext
                  console.info('EditorPage: Preview XComponent onLoad callback triggered');
                  
                  if (xComponentContext) {
                    // 预览可以使用相同的模块
                    if (!this.papercutModule) {
                      this.papercutModule = xComponentContext as NativeModule;
                      console.info('EditorPage: ✓ Got Native module from preview XComponent');
                    } else {
                      console.info('EditorPage: Reusing existing Native module for preview');
                    }
                    
                    // 延迟调用初始化
                    setTimeout(() => {
                      this.onPreviewXComponentLoad();
                    }, 100);
                  } else {
                    console.error('EditorPage: ✗ Preview XComponentContext is null');
                  }
                })
                .width('100%')
                .height('100%')
                .backgroundColor('#FDF6E3')
            }
            .width('100%')
            .layoutWeight(1)
            .backgroundColor('#FDF6E3')
          }
          .width(this.isSplitMode ? '50%' : '100%')
          .layoutWeight(1)
        }
      }
      .width('100%')
      .layoutWeight(1)

      // 底部工具栏
      if (!this.viewMode) {
        Row() {
          // 剪刀工具（带子菜单）
          Stack() {
            Button(this.getScissorLabel())
              .type(ButtonType.Normal)
              .width(80)
              .height(40)
              .backgroundColor((this.currentTool === 0 || this.currentTool === 1) ? this.paperColor : '#E0E0E0')
              .fontColor((this.currentTool === 0 || this.currentTool === 1) ? Color.White : '#666666')
              .onClick(() => {
                this.showScissorMenu = !this.showScissorMenu;
              })

            // 剪刀子菜单
            if (this.showScissorMenu) {
              Column() {
                Button('手工')
                  .type(ButtonType.Normal)
                  .width(80)
                  .height(32)
                  .backgroundColor(this.scissorMode === 0 ? '#FFE0E0' : Color.White)
                  .fontColor(this.scissorMode === 0 ? '#E60012' : '#666666')
                  .onClick(() => this.onScissorModeChange(0))
                
                Button('直线')
                  .type(ButtonType.Normal)
                  .width(80)
                  .height(32)
                  .backgroundColor(this.scissorMode === 1 ? '#FFE0E0' : Color.White)
                  .fontColor(this.scissorMode === 1 ? '#E60012' : '#666666')
                  .onClick(() => this.onScissorModeChange(1))
                
                Button('曲线')
                  .type(ButtonType.Normal)
                  .width(80)
                  .height(32)
                  .backgroundColor(this.scissorMode === 2 ? '#FFE0E0' : Color.White)
                  .fontColor(this.scissorMode === 2 ? '#E60012' : '#666666')
                  .onClick(() => this.onScissorModeChange(2))
              }
              .width(80)
              .backgroundColor(Color.White)
              .borderRadius(4)
              .shadow({ radius: 4, color: '#33000000', offsetX: 0, offsetY: 2 })
              .position({ x: 0, y: -110 })
            }
          }
          .width(80)
          .height(40)

          Button('铅笔')
            .type(ButtonType.Normal)
            .width(80)
            .height(40)
            .backgroundColor(this.currentTool === 2 ? this.paperColor : '#E0E0E0')
            .fontColor(this.currentTool === 2 ? Color.White : '#666666')
            .onClick(() => {
              this.onToolChange(2); // DRAFT_PEN
            })

          Button('橡皮')
            .type(ButtonType.Normal)
            .width(80)
            .height(40)
            .backgroundColor(this.currentTool === 3 ? this.paperColor : '#E0E0E0')
            .fontColor(this.currentTool === 3 ? Color.White : '#666666')
            .onClick(() => {
              this.onToolChange(3); // DRAFT_ERASER
            })
          
          Blank()
          
          // 预览按钮
          Button(this.showPreview ? '编辑' : '预览')
            .type(ButtonType.Normal)
            .width(80)
            .height(40)
            .backgroundColor(this.paperColor)
            .fontColor(Color.White)
            .onClick(() => this.onTogglePreview())
        }
        .width('100%')
        .height(60)
        .padding({ left: 10, right: 10, top: 10, bottom: 10 })
        .justifyContent(FlexAlign.SpaceBetween)
        .alignItems(VerticalAlign.Center)
        .backgroundColor('#FDF6E3')
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#FDF6E3')
  }

  getScissorLabel(): string {
    if (this.scissorMode === 0) return '手工';
    if (this.scissorMode === 1) return '直线';
    if (this.scissorMode === 2) return '曲线';
    return '剪刀';
  }

  // 将十六进制颜色转换为ARGB格式
  hexToArgb(hex: string): number {
    const cleanHex = hex.replace('#', '');
    return parseInt('FF' + cleanHex, 16);
  }
}

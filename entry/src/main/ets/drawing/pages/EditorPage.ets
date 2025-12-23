// 编辑器页面
import { router } from '@kit.ArkUI';
import XComponentContext from '../../interface/XComponentContext';
import { SavedWork, RouterParams, NativeModule, GlobalObject, TouchPoint } from '../../common/types';

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

  private xComponentController: XComponentController = new XComponentController();
  private previewXComponentController: XComponentController = new XComponentController();
  private papercutModule: NativeModule | null = null;
  private nativeWindow: string | number | null = null;
  private previewNativeWindow: string | number | null = null;
  private work: SavedWork | null = null;
  private viewMode: boolean = false;

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

        // 初始化引擎
        const width = 2048;
        const height = 2048;
        console.info('EditorPage: Initializing engine with size:', width, 'x', height);
        this.papercutModule.initializeEngine(nativeWindow, width, height);

        // 设置纸张类型和颜色（必须在初始化引擎之后）
        const paperTypeValue = this.paperType === 'CIRCLE' ? 0 : 1; // 0=CIRCLE, 1=SQUARE
        console.info('EditorPage: Setting paperType:', paperTypeValue, '(', this.paperType, ')');
        this.papercutModule.setPaperType(paperTypeValue);
        
        const colorArgb = this.hexToArgb(this.paperColor);
        console.info('EditorPage: Setting paperColor:', this.paperColor, '-> ARGB:', colorArgb.toString(16));
        this.papercutModule.setPaperColor(colorArgb);

        // 设置折叠模式和工具
        console.info('EditorPage: Setting foldMode:', this.foldMode);
        this.papercutModule.setFoldMode(this.foldMode);
        this.papercutModule.setToolMode(this.currentTool);

        // 延迟绘制，确保所有设置都已完成
        setTimeout(() => {
          console.info('EditorPage: Rendering initial canvas...');
          this.render();
        }, 300);
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
        
        // 初始化预览引擎（使用相同的配置）
        const width = 2048;
        const height = 2048;
        this.papercutModule.initializeEngine(nativeWindow, width, height);

        // 延迟绘制预览
        setTimeout(() => {
          this.renderPreview();
        }, 200);
      }
    } catch (error) {
      console.error('EditorPage: Error getting preview surfaceId:', error);
    }
  }

  render() {
    if (this.nativeWindow && this.papercutModule) {
      this.papercutModule.drawPaperCut(this.nativeWindow);
    }
  }

  renderPreview() {
    if (this.previewNativeWindow && this.papercutModule) {
      this.papercutModule.drawPaperCutPreview(this.previewNativeWindow);
    }
  }

  onTouchStart(event: TouchEvent) {
    if (this.viewMode || !this.papercutModule) return;
    
    if (event.touches && event.touches.length > 0) {
      const touch = event.touches[0];
      // 获取XComponent的实际尺寸（从触摸事件获取）
      // 画布尺寸是2048x2048，需要将屏幕坐标转换为画布坐标
      const canvasSize = 2048;
      const viewScale = 1.2; // VIEW_SCALE
      const viewOffsetY = canvasSize * 0.25; // VIEW_OFFSET_Y
      
      // 获取触摸点相对于XComponent的位置
      // 注意：touch.x和touch.y已经是相对于XComponent的坐标
      // 需要转换为画布坐标（考虑视图变换）
      const modelX = (touch.x - canvasSize / 2) / viewScale;
      const modelY = (touch.y - canvasSize / 2 - viewOffsetY) / viewScale;
      
      this.papercutModule.startDrawing(modelX, modelY);
      this.isDrawing = true;
      const point: TouchPoint = { x: modelX, y: modelY };
      this.lastPoint = point;
    }
  }

  onTouchMove(event: TouchEvent) {
    if (this.viewMode || !this.isDrawing || !this.papercutModule || !this.lastPoint) return;
    
    if (event.touches && event.touches.length > 0) {
      const touch = event.touches[0];
      const canvasSize = 2048;
      const viewScale = 1.2; // VIEW_SCALE
      const viewOffsetY = canvasSize * 0.25; // VIEW_OFFSET_Y
      
      // 转换为模型坐标
      const modelX = (touch.x - canvasSize / 2) / viewScale;
      const modelY = (touch.y - canvasSize / 2 - viewOffsetY) / viewScale;
      
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
      this.renderPreview();
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
        this.renderPreview();
      }
    }
  }

  onUndo() {
    if (this.papercutModule) {
      this.papercutModule.undo();
      this.render();
    }
  }

  onRedo() {
    if (this.papercutModule) {
      this.papercutModule.redo();
      this.render();
    }
  }

  onClear() {
    if (this.papercutModule) {
      this.papercutModule.clear();
      this.render();
    }
  }

  onBack() {
    router.back();
  }

  onTogglePreview() {
    if (this.isSplitMode) {
      this.isSplitMode = false;
    }
    this.showPreview = !this.showPreview;
    // 当切换到预览模式时，渲染预览
    if (this.showPreview) {
      setTimeout(() => {
        this.renderPreview();
      }, 100);
    }
  }

  onToggleSplit() {
    this.showPreview = false;
    this.isSplitMode = !this.isSplitMode;
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
        
        if (!this.viewMode) {
          Button('保存')
            .type(ButtonType.Normal)
            .backgroundColor(this.paperColor)
            .fontColor(Color.White)
            .onClick(() => {
              // TODO: 实现保存功能
              this.onBack();
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
          // 折叠模式选择（顶部居中）
          if (!this.viewMode) {
            Row() {
              Text('折法:')
                .fontSize(12)
                .fontColor('#666666')
                .margin({ right: 8 })
              
              ForEach([0, 1, 2, 3, 4, 5, 6, 7, 8], (mode: number) => {
                Button(mode === 0 ? '0' : mode.toString())
                  .type(ButtonType.Normal)
                  .width(32)
                  .height(32)
                  .borderRadius(16)
                  .fontSize(12)
                  .backgroundColor(this.foldMode === mode ? this.paperColor : '#E0E0E0')
                  .fontColor(this.foldMode === mode ? Color.White : '#666666')
                  .onClick(() => this.onFoldChange(mode))
              }, (mode: number) => mode.toString())
            }
            .width('100%')
            .height(40)
            .justifyContent(FlexAlign.Center)
            .alignItems(VerticalAlign.Center)
            .margin({ top: 10 })
          }

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
              .onTouch((event: TouchEvent) => {
                if (event.type === TouchType.Down) {
                  this.onTouchStart(event);
                } else if (event.type === TouchType.Move) {
                  this.onTouchMove(event);
                } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
                  this.onTouchEnd(event);
                }
              })

            // 左侧工具栏（撤销/重做/清空）
            if (!this.viewMode) {
              Column() {
                Button()
                  .width(36)
                  .height(36)
                  .borderRadius(18)
                  .backgroundColor(Color.White)
                  .onClick(() => this.onUndo())
                
                Button()
                  .width(36)
                  .height(36)
                  .borderRadius(18)
                  .backgroundColor(Color.White)
                  .onClick(() => this.onRedo())
                
                Button()
                  .width(36)
                  .height(36)
                  .borderRadius(18)
                  .backgroundColor(Color.White)
                  .onClick(() => this.onClear())
              }
              .width(40)
              .height(120)
              .justifyContent(FlexAlign.SpaceBetween)
              .alignItems(HorizontalAlign.Start)
              .margin({ left: 10, bottom: 20 })
              .position({ x: 0, y: '50%' })
            }
          }
          .width('100%')
          .layoutWeight(1)
        }
        .width(this.isSplitMode ? '50%' : (this.showPreview ? '0%' : '100%'))
        .layoutWeight(1)
        .backgroundColor('#FDF6E3')

        // 右侧预览区
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

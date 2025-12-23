// entry/src/main/ets/pages/PaperCutPage.ets
import { XComponentContext } from '../../interface/XComponentContext'

@Entry
@Component
export struct PaperCutPage {
  @State message: string = '剪纸艺术';
  private xComponentController: XComponentController = new XComponentController();
  private xComponentContext: XComponentContext = new XComponentContext();

  aboutToAppear(): void {
    console.info('PaperCutPage aboutToAppear');
  }

  // XComponent初始化完成回调
  onXComponentLoad(): void {
    console.info('PaperCutPage: XComponent loaded');

    // 获取NativeWindow
    try {
      const nativeWindow = this.xComponentController.getXComponentSurfaceId();
      console.info('PaperCutPage: Got surfaceId:', nativeWindow);

      if (nativeWindow) {
        this.xComponentContext.setNativeWindow(nativeWindow);

        // 延迟绘制
        setTimeout(() => {
          this.xComponentContext.drawPaperCut();
        }, 200);
      }
    } catch (error) {
      console.error('PaperCutPage: Error getting surfaceId:', error);
    }
  }

  // 重新绘制
  onRedrawClick(): void {
    this.xComponentContext.drawPaperCut();
  }

  build() {
    Column() {
      Text(this.message)
        .fontSize(24)
        .fontWeight(FontWeight.Bold)
        .margin({ top: 20 })
        .fontColor('#E84C3D')

      XComponent({
        id: 'xcomponent_paper_cut',
        type: 'surface',
        controller: this.xComponentController
      })
        .onLoad(() => {
          this.onXComponentLoad();
        })
        .width('90%')
        .height(400)
        .backgroundColor('#FFF0E0')
        .margin({ top: 20 })

      Button('重新绘制')
        .onClick(() => this.onRedrawClick())
        .backgroundColor('#E84C3D')
        .fontColor(Color.White)
        .margin({ top: 20 })
        .width(120)
        .height(40)
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Start)
    .alignItems(HorizontalAlign.Center)
  }
}
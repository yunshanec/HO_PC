// 创建新作品模态页面
import { router } from '@kit.ArkUI';

interface PaperColorData {
  name: string;
  hex: string;
  meaning: string;
}

const PAPER_COLORS: PaperColorData[] = [
  { name: "大红", hex: "#FF2121", meaning: "吉祥、热烈" },
  { name: "银朱", hex: "#bf242a", meaning: "庄重、典雅" },
  { name: "胭脂", hex: "#9d2933", meaning: "柔美、哀婉" },
  { name: "朱砂", hex: "#ff461f", meaning: "正统、护佑" },
  { name: "朱膘", hex: "#f36838", meaning: "温暖、活力" },
  { name: "赭石", hex: "#845a33", meaning: "稳重、质朴" },
  { name: "石青", hex: "#1685a9", meaning: "清冷、高远" },
  { name: "石绿", hex: "#16a951", meaning: "生机、和谐" },
  { name: "白粉", hex: "#fff2df", meaning: "纯净、留白" },
  { name: "花青", hex: "#003472", meaning: "沉静、隽永" },
  { name: "藤黄", hex: "#ffb61e", meaning: "明亮、鲜活" },
  { name: "雄黄", hex: "#e9bb1d", meaning: "驱邪、守护" },
  { name: "洋红", hex: "#ff4777", meaning: "热烈、外放" },
  { name: "墨色", hex: "#50616d", meaning: "万象归一" }
];

@Entry
@Component
export struct CreateModalPage {
  @State activeTab: 'BLANK' | 'AI' = 'BLANK';
  @State selectedPaper: 'CIRCLE' | 'SQUARE' = 'CIRCLE';
  @State selectedColor: string = PAPER_COLORS[0].hex;

  onClose() {
    router.back();
  }

  onConfirm() {
    // 跳转到编辑器页面，传递选择的参数
    router.pushUrl({
      url: 'drawing/pages/EditorPage',
      params: {
        paperType: this.selectedPaper,
        paperColor: this.selectedColor
      }
    });
  }

  // 将十六进制颜色转换为ARGB格式
  hexToArgb(hex: string): number {
    // 移除#号
    const cleanHex = hex.replace('#', '');
    // 转换为ARGB (0xFF + RGB)
    return parseInt('FF' + cleanHex, 16);
  }

  build() {
    Column() {
      // 标题栏 - 红色背景
      Row() {
        Row() {
          Text('✂')
            .fontSize(20)
            .fontColor(Color.White)
            .margin({ right: 8 })
          
          Text('开始新创作')
            .fontSize(18)
            .fontWeight(FontWeight.Bold)
            .fontColor(Color.White)
        }
        .margin({ left: 16 })
        
        Blank()
        
        Button() {
          Text('✕')
            .fontSize(20)
            .fontColor(Color.White)
        }
        .type(ButtonType.Normal)
        .width(32)
        .height(32)
        .borderRadius(16)
        .backgroundColor(Color.Transparent)
        .onClick(() => this.onClose())
        .margin({ right: 16 })
      }
      .width('100%')
      .height(56)
      .backgroundColor('#E60012')
      .justifyContent(FlexAlign.SpaceBetween)
      .alignItems(VerticalAlign.Center)

      // 标签页
      Row() {
        Button('自由裁剪')
          .type(ButtonType.Normal)
          .layoutWeight(1)
          .height(48)
          .backgroundColor(Color.Transparent)
          .fontColor(this.activeTab === 'BLANK' ? '#E60012' : '#666666')
          .fontWeight(this.activeTab === 'BLANK' ? FontWeight.Bold : FontWeight.Normal)
          .border({ width: { bottom: this.activeTab === 'BLANK' ? 2 : 0 }, color: '#E60012' })
          .onClick(() => {
            this.activeTab = 'BLANK';
          })
        
        Button('AI 灵感生成')
          .type(ButtonType.Normal)
          .layoutWeight(1)
          .height(48)
          .backgroundColor(Color.Transparent)
          .fontColor(this.activeTab === 'AI' ? '#E60012' : '#666666')
          .fontWeight(this.activeTab === 'AI' ? FontWeight.Bold : FontWeight.Normal)
          .border({ width: { bottom: this.activeTab === 'AI' ? 2 : 0 }, color: '#E60012' })
          .onClick(() => {
            this.activeTab = 'AI';
          })
      }
      .width('100%')
      .backgroundColor(Color.White)

      // 内容区域
      if (this.activeTab === 'BLANK') {
        Column() {
          // 选择纸张颜色
          Column() {
            Row() {
              Text('选择纸张颜色')
                .fontSize(14)
                .fontWeight(FontWeight.Bold)
                .fontColor('#333333')
            }
            .width('100%')
            .margin({ bottom: 12 })

            Flex({ wrap: FlexWrap.Wrap, justifyContent: FlexAlign.Start }) {
              ForEach(PAPER_COLORS, (color: PaperColorData) => {
                Stack() {
                  // 使用Column+backgroundColor模拟圆形
                  Column()
                    .width(48)
                    .height(48)
                    .borderRadius(24)
                    .backgroundColor(color.hex)
                  
                  if (this.selectedColor === color.hex) {
                    Column()
                      .width(8)
                      .height(8)
                      .borderRadius(4)
                      .backgroundColor(Color.White)
                  }
                }
                .width(48)
                .height(48)
                .margin(6)
                .border({
                  width: this.selectedColor === color.hex ? 2 : 0,
                  color: Color.White,
                  radius: 24
                })
                .onClick(() => {
                  this.selectedColor = color.hex;
                })
              }, (color: PaperColorData) => color.hex)
            }
            .width('100%')
            .padding({ top: 12, bottom: 12 })
          }
          .width('100%')
          .padding({ left: 20, right: 20, top: 20, bottom: 20 })
          .backgroundColor(Color.White)
          .borderRadius(12)
          .margin({ top: 20, left: 20, right: 20 })

          // 选择纸张形状
          Column() {
            Row() {
              Text('选择纸张形状')
                .fontSize(14)
                .fontWeight(FontWeight.Bold)
                .fontColor('#333333')
            }
            .width('100%')
            .margin({ bottom: 12 })

            Row() {
              // 圆形
              Column() {
                Column()
                  .width(48)
                  .height(48)
                  .borderRadius(24)
                  .backgroundColor(this.selectedColor)
                  .margin({ bottom: 8 })

                Text('圆形团花')
                  .fontSize(14)
                  .fontWeight(FontWeight.Medium)
                  .fontColor('#333333')
                  .margin({ bottom: 4 })

                Text('零-八角团花')
                  .fontSize(12)
                  .fontColor('#999999')
              }
              .layoutWeight(1)
              .padding(20)
              .borderRadius(12)
              .border({
                width: this.selectedPaper === 'CIRCLE' ? 2 : 1,
                color: this.selectedPaper === 'CIRCLE' ? '#E60012' : '#E0E0E0'
              })
              .backgroundColor(this.selectedPaper === 'CIRCLE' ? '#FFE0E0' : Color.White)
              .onClick(() => {
                this.selectedPaper = 'CIRCLE';
              })
              .margin({ right: 12 })

              // 方形
              Column() {
                Column()
                  .width(48)
                  .height(48)
                  .borderRadius(8)
                  .backgroundColor(this.selectedColor)
                  .margin({ bottom: 8 })

                Text('方形团花')
                  .fontSize(14)
                  .fontWeight(FontWeight.Medium)
                  .fontColor('#333333')
                  .margin({ bottom: 4 })

                Text('零-八角团花')
                  .fontSize(12)
                  .fontColor('#999999')
              }
              .layoutWeight(1)
              .padding(20)
              .borderRadius(12)
              .border({
                width: this.selectedPaper === 'SQUARE' ? 2 : 1,
                color: this.selectedPaper === 'SQUARE' ? '#E60012' : '#E0E0E0'
              })
              .backgroundColor(this.selectedPaper === 'SQUARE' ? '#FFE0E0' : Color.White)
              .onClick(() => {
                this.selectedPaper = 'SQUARE';
              })
            }
            .width('100%')
          }
          .width('100%')
          .padding({ left: 20, right: 20, top: 20, bottom: 20 })
          .backgroundColor(Color.White)
          .borderRadius(12)
          .margin({ top: 12, left: 20, right: 20 })
        }
        .layoutWeight(1)
        .width('100%')
      } else {
        // AI模式（暂时显示提示）
        Column() {
          Text('AI 灵感生成功能')
            .fontSize(16)
            .fontColor('#666666')
            .margin({ top: 100 })
          
          Text('即将支持')
            .fontSize(14)
            .fontColor('#999999')
            .margin({ top: 12 })
        }
        .width('100%')
        .layoutWeight(1)
        .justifyContent(FlexAlign.Center)
        .alignItems(HorizontalAlign.Center)
      }

      // 底部按钮
      Row() {
        Button('取消')
          .type(ButtonType.Normal)
          .layoutWeight(1)
          .height(44)
          .backgroundColor(Color.Transparent)
          .fontColor('#666666')
          .onClick(() => this.onClose())
        
        Button('开始创作')
          .type(ButtonType.Normal)
          .layoutWeight(1)
          .height(44)
          .backgroundColor('#E60012')
          .fontColor(Color.White)
          .fontWeight(FontWeight.Bold)
          .borderRadius(22)
          .onClick(() => this.onConfirm())
      }
      .width('100%')
      .padding({ left: 20, right: 20, top: 12, bottom: 12 })
      .backgroundColor(Color.White)
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#FDF6E3')
    .borderRadius(16)
    .shadow({
      radius: 20,
      color: '#40000000',
      offsetX: 0,
      offsetY: 4
    })
  }
}


// 画廊页面
import { router } from '@kit.ArkUI';
import { preferences } from '@kit.ArkData';
import { SavedWork } from '../../common/types';

@Entry
@Component
export struct GalleryPage {
  @State works: SavedWork[] = [];
  @State isLoading: boolean = true;

  aboutToAppear() {
    this.loadWorks();
  }

  async loadWorks() {
    try {
      const prefs = await preferences.getPreferences(this.getContext(), 'papercut_works');
      const worksJson: string = await prefs.get('works', '[]') as string;
      const parsedWorks: SavedWork[] = JSON.parse(worksJson) as SavedWork[];
      this.works = parsedWorks;
    } catch (e) {
      console.error('Failed to load works:', e);
      this.works = [];
    } finally {
      this.isLoading = false;
    }
  }

  onCreateNew() {
    router.pushUrl({
      url: 'drawing/pages/CreateModalPage'
    });
  }

  onEdit(work: SavedWork) {
    router.pushUrl({
      url: 'drawing/pages/EditorPage',
      params: { work: work }
    });
  }

  onView(work: SavedWork) {
    router.pushUrl({
      url: 'drawing/pages/EditorPage',
      params: { work: work, viewMode: true }
    });
  }

  async onDelete(id: string) {
    this.works = this.works.filter(w => w.id !== id);
    try {
      const prefs = await preferences.getPreferences(this.getContext(), 'papercut_works');
      await prefs.put('works', JSON.stringify(this.works));
      await prefs.flush();
    } catch (e) {
      console.error('Failed to save works:', e);
    }
  }

  getContext() {
    return getContext(this) as Context;
  }

  build() {
    Column() {
      // 标题栏 - 红色背景
      Column() {
        Text('中华剪纸')
          .fontSize(36)
          .fontWeight(FontWeight.Bold)
          .fontColor(Color.White)
          .margin({ top: 40, bottom: 8 })
        
        Text('每一剪都是故事，每一纸都是传承。')
          .fontSize(14)
          .fontColor(Color.White)
          .opacity(0.9)
          .margin({ bottom: 30 })
        
        Button('+ 创作新剪纸')
          .type(ButtonType.Normal)
          .width(200)
          .height(44)
          .borderRadius(22)
          .backgroundColor(Color.White)
          .fontColor('#E60012')
          .fontSize(16)
          .fontWeight(FontWeight.Medium)
          .margin({ bottom: 30 })
          .onClick(() => this.onCreateNew())
      }
      .width('100%')
      .backgroundColor('#E60012')
      .alignItems(HorizontalAlign.Center)

      // 作品列表区域
      Column() {
        // 标题
        Row() {
          Column()
            .width(4)
            .height(20)
            .backgroundColor('#E60012')
            .margin({ right: 8 })
          
          Text('我的作品')
            .fontSize(18)
            .fontWeight(FontWeight.Medium)
            .fontColor('#333333')
        }
        .width('100%')
        .padding({ left: 20, top: 20, bottom: 16 })
        .alignItems(VerticalAlign.Center)
        
        if (this.isLoading) {
          LoadingProgress()
            .width(50)
            .height(50)
            .margin({ top: 100 })
        } else if (this.works.length === 0) {
          Column() {
            Text('暂无作品')
              .fontSize(20)
              .fontColor('#666666')
              .margin({ top: 100, bottom: 20 })
            
            Button('立即开始创作')
              .type(ButtonType.Normal)
              .backgroundColor('#E60012')
              .fontColor(Color.White)
              .onClick(() => this.onCreateNew())
          }
          .width('100%')
          .alignItems(HorizontalAlign.Center)
        } else {
          Scroll() {
            Row() {
              ForEach(this.works, (work: SavedWork) => {
                this.WorkCard(work)
              }, (work: SavedWork) => work.id)
            }
            .padding({ left: 20, right: 20 })
            .justifyContent(FlexAlign.Start)
          }
          .layoutWeight(1)
          .scrollBar(BarState.Auto)
        }
      }
      .width('100%')
      .layoutWeight(1)
      .backgroundColor('#FDF6E3')
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#FDF6E3')
  }

  @Builder
  WorkCard(work: SavedWork) {
    Column() {
      // 预览图
      Stack() {
        if (work.previewImage) {
          Image(work.previewImage)
            .width(120)
            .height(120)
            .objectFit(ImageFit.Contain)
            .backgroundColor('#FFFFFF')
        } else {
          Column() {
            Text('无预览')
              .fontSize(12)
              .fontColor('#999999')
          }
          .width(120)
          .height(120)
          .backgroundColor('#FFFFFF')
          .justifyContent(FlexAlign.Center)
          .alignItems(HorizontalAlign.Center)
        }

        // 删除按钮
        Button() {
          Text('×')
            .fontSize(18)
            .fontColor(Color.White)
            .fontWeight(FontWeight.Bold)
        }
        .type(ButtonType.Circle)
        .width(24)
        .height(24)
        .backgroundColor('#80000000')
        .position({ x: 100, y: 4 })
        .onClick(() => {
          if (work.id) {
            this.onDelete(work.id);
          }
        })
      }
      .width(120)
      .height(120)
      .margin({ bottom: 8 })

      // 信息
      Column() {
        Text('剪纸作品 ' + new Date(work.date || Date.now()).toLocaleDateString('zh-CN', {
          year: 'numeric',
          month: '2-digit',
          day: '2-digit'
        }).replace(/\//g, '/'))
          .fontSize(12)
          .fontColor('#333333')
          .maxLines(1)
          .textOverflow({ overflow: TextOverflow.Ellipsis })
          .width('100%')
        
        Text(`${new Date(work.date || Date.now()).toLocaleDateString('zh-CN', {
          year: 'numeric',
          month: '2-digit',
          day: '2-digit'
        }).replace(/\//g, '/')} · ${work.foldMode === 0 ? '自由' : work.foldMode + '瓣'}`)
          .fontSize(10)
          .fontColor('#999999')
          .margin({ top: 4 })
          .width('100%')
      }
      .width(120)
      .alignItems(HorizontalAlign.Start)
      .onClick(() => this.onView(work))
    }
    .width(120)
    .margin({ right: 16, bottom: 16 })
  }
}


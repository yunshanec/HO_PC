// 通用类型定义
export interface SavedWork {
  id?: string;
  title?: string;
  paperType?: string;
  paperColor?: string;
  date?: number;
  foldMode?: number;
  actions?: Action[];
  previewImage?: string;
}

export interface Action {
  id: string;
  type: number; // 0=CUT, 1=STROKE
  tool: number; // 0=SCISSORS, 1=BEZIER, 2=DRAFT_PEN, 3=DRAFT_ERASER
  points: Point[];
  timestamp: number;
}

export interface Point {
  x: number;
  y: number;
}

export interface RouterParams {
  work?: SavedWork;
  viewMode?: boolean;
  paperType?: string;
  paperColor?: string;
}

export interface NativeModule {
  drawPaperCut: (nativeWindow: string | number) => void;
  drawPaperCutPreview: (nativeWindow: string | number) => void;
  initializeEngine: (nativeWindow: string | number, width: number, height: number) => void;
  setPreviewWindow: (nativeWindow: string | number) => void;
  startDrawing: (x: number, y: number) => void;
  addPoint: (x: number, y: number) => void;
  finishDrawing: () => void;
  setToolMode: (mode: number) => void;
  setFoldMode: (mode: number) => void;
  setPaperType: (type: number) => void;
  setPaperColor: (color: number) => void;
  undo: () => void;
  redo: () => void;
  clear: () => void;
  getActions: () => Action[];
  setActions: (actions: Action[]) => void;
  setZoom: (zoom: number) => void;
  setPan: (x: number, y: number) => void;
}

export interface GlobalObject {
  requireNativeModule?: (name: string) => NativeModule | undefined;
  papercut?: NativeModule;
}

export interface TouchPoint {
  x: number;
  y: number;
}


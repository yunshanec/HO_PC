/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
import { image } from '@kit.ImageKit';
import { Action } from '../common/types';

export default interface XComponentContext {
  draw(canvasType:string, shapeType: string):void;
  drawImage(canvasType:string, shapeType: string, pixelmap: image.PixelMap):void;
  drawPaperCut(nativeWindow: string | number): void;
  drawPaperCutPreview(nativeWindow: string | number): void;
  initializeEngine(nativeWindow: string | number, width: number, height: number): void;
  setPreviewWindow(nativeWindow: string | number): void;
  startDrawing(x: number, y: number): void;
  addPoint(x: number, y: number): void;
  finishDrawing(): void;
  setToolMode(mode: number): void;
  setFoldMode(mode: number): void;
  setPaperType(type: number): void;
  setPaperColor(color: number): void;
  undo(): void;
  redo(): void;
  clear(): void;
  getActions(): Action[];
  setActions(actions: Action[]): void;
  setZoom(zoom: number): void;
  setPan(x: number, y: number): void;
};

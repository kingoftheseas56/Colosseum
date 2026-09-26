import type { BrowserWindowConstructorOptions } from "electron";
import type { WindowState } from "./stateStore";

export function createSecureWindowOptions(
  preloadPath: string,
  state: WindowState
): BrowserWindowConstructorOptions {
  return {
    width: state.width,
    height: state.height,
    ...(state.x === undefined ? {} : { x: state.x }),
    ...(state.y === undefined ? {} : { y: state.y }),
    minWidth: 1000,
    minHeight: 680,
    frame: false,
    show: false,
    backgroundColor: "#090b11",
    title: "Colosseum Harness",
    webPreferences: {
      preload: preloadPath,
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true
    }
  };
}

import { ipcMain, type BrowserWindow } from "electron";
import {
  IPC_CHANNELS,
  type ShellState,
  type WindowAction
} from "../shared/desktopApi";
import { loadShellState, sanitizeShellState, writeJsonAtomic } from "./stateStore";

const validActions = new Set<WindowAction>(["minimize", "toggle-maximize", "close"]);

export function registerDesktopIpc(
  shellStatePath: string,
  getWindow: () => BrowserWindow | null
): void {
  ipcMain.handle(IPC_CHANNELS.shellStateGet, () => loadShellState(shellStatePath));

  ipcMain.handle(IPC_CHANNELS.shellStateSet, (_event, patch: Partial<ShellState>) => {
    const next = sanitizeShellState({ ...loadShellState(shellStatePath), ...patch });
    writeJsonAtomic(shellStatePath, next);
    return next;
  });

  ipcMain.handle(IPC_CHANNELS.windowAction, (_event, action: WindowAction) => {
    if (!validActions.has(action)) {
      throw new Error("Unsupported window action");
    }

    const window = getWindow();
    if (!window) {
      return;
    }

    if (action === "minimize") {
      window.minimize();
    } else if (action === "toggle-maximize") {
      window.isMaximized() ? window.unmaximize() : window.maximize();
    } else {
      window.close();
    }
  });
}

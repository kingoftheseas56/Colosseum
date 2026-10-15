import { contextBridge, ipcRenderer } from "electron";
import type { DesktopApi, ShellState, WindowAction } from "../shared/desktopApi";

const CHANNELS = {
  shellStateGet: "desktop:shell-state:get",
  shellStateSet: "desktop:shell-state:set",
  windowAction: "desktop:window:action"
} as const;

const api: DesktopApi = {
  async getShellState() {
    return await ipcRenderer.invoke(CHANNELS.shellStateGet) as ShellState;
  },
  async updateShellState(patch) {
    return await ipcRenderer.invoke(CHANNELS.shellStateSet, patch) as ShellState;
  },
  async windowAction(action: WindowAction) {
    await ipcRenderer.invoke(CHANNELS.windowAction, action);
  }
};

contextBridge.exposeInMainWorld("harnessDesktop", api);

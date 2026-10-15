import {
  IPC_CHANNELS,
  type DesktopApi,
  type ShellState,
  type WindowAction
} from "../shared/desktopApi";

export interface IpcInvoker {
  invoke(channel: string, ...args: unknown[]): Promise<unknown>;
}

export function createDesktopApi(ipc: IpcInvoker): DesktopApi {
  return {
    async getShellState() {
      return await ipc.invoke(IPC_CHANNELS.shellStateGet) as ShellState;
    },
    async updateShellState(patch) {
      return await ipc.invoke(IPC_CHANNELS.shellStateSet, patch) as ShellState;
    },
    async windowAction(action: WindowAction) {
      await ipc.invoke(IPC_CHANNELS.windowAction, action);
    }
  };
}

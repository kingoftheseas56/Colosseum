// @vitest-environment node
import { describe, expect, it, vi } from "vitest";
import { IPC_CHANNELS } from "../shared/desktopApi";
import { createDesktopApi } from "./desktopApi";

describe("preload contract", () => {
  it("exposes only bounded desktop operations", async () => {
    const invoke = vi.fn(async () => ({}));
    const api = createDesktopApi({ invoke });

    expect(Object.keys(api).sort()).toEqual([
      "getShellState",
      "updateShellState",
      "windowAction"
    ]);

    await api.getShellState();
    await api.updateShellState({ inspectorCollapsed: true });
    await api.windowAction("minimize");

    expect(invoke).toHaveBeenNthCalledWith(1, IPC_CHANNELS.shellStateGet);
    expect(invoke).toHaveBeenNthCalledWith(
      2,
      IPC_CHANNELS.shellStateSet,
      { inspectorCollapsed: true }
    );
    expect(invoke).toHaveBeenNthCalledWith(3, IPC_CHANNELS.windowAction, "minimize");
  });
});

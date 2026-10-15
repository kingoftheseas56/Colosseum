// @vitest-environment node
import { describe, expect, it } from "vitest";
import { createSecureWindowOptions } from "./windowOptions";

describe("BrowserWindow security boundary", () => {
  it("keeps Node out of the renderer", () => {
    const options = createSecureWindowOptions("C:\\preload.js", {
      width: 1400,
      height: 900,
      maximized: false
    });

    expect(options.frame).toBe(false);
    expect(options.webPreferences).toMatchObject({
      preload: "C:\\preload.js",
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true
    });
  });
});

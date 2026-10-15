// @vitest-environment node
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { describe, expect, it } from "vitest";
import {
  DEFAULT_WINDOW_STATE,
  loadShellState,
  sanitizeShellState,
  sanitizeWindowState,
  writeJsonAtomic
} from "./stateStore";

describe("state persistence", () => {
  it("sanitizes unsafe shell values", () => {
    expect(sanitizeShellState({
      leftPanelWidth: 5,
      rightPanelWidth: 9000,
      inspectorCollapsed: true,
      selectedSettingsSection: "wat",
      theme: "neon"
    })).toMatchObject({
      leftPanelWidth: 210,
      rightPanelWidth: 520,
      inspectorCollapsed: true,
      selectedSettingsSection: "appearance",
      theme: "dark"
    });
  });

  it("sanitizes window geometry", () => {
    expect(sanitizeWindowState({ width: 12, height: 99999, maximized: true })).toEqual({
      width: 1000,
      height: 2160,
      maximized: true
    });
    expect(sanitizeWindowState(undefined)).toEqual(DEFAULT_WINDOW_STATE);
  });

  it("round-trips shell state atomically", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "harness-shell-state-"));
    const file = path.join(dir, "state.json");
    writeJsonAtomic(file, {
      leftPanelWidth: 300,
      rightPanelWidth: 410,
      inspectorCollapsed: true,
      selectedSettingsSection: "harness",
      theme: "system"
    });
    expect(loadShellState(file)).toMatchObject({
      leftPanelWidth: 300,
      rightPanelWidth: 410,
      inspectorCollapsed: true,
      selectedSettingsSection: "harness",
      theme: "system"
    });
  });
});

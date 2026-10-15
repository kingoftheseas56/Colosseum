import fs from "node:fs";
import path from "node:path";
import {
  DEFAULT_SHELL_STATE,
  type SettingsSection,
  type ShellState,
  type ThemePreference
} from "../shared/desktopApi";

export interface WindowState {
  width: number;
  height: number;
  x?: number;
  y?: number;
  maximized: boolean;
}

export const DEFAULT_WINDOW_STATE: WindowState = {
  width: 1440,
  height: 900,
  maximized: false
};

const settingsSections = new Set<SettingsSection>([
  "appearance",
  "codex",
  "behaviour",
  "repository",
  "harness"
]);

const themes = new Set<ThemePreference>(["dark", "system"]);

function finiteNumber(value: unknown, fallback: number): number {
  return typeof value === "number" && Number.isFinite(value) ? value : fallback;
}

export function sanitizeWindowState(input: unknown): WindowState {
  const value = input && typeof input === "object" ? input as Record<string, unknown> : {};
  const width = Math.max(1000, Math.min(3840, finiteNumber(value.width, DEFAULT_WINDOW_STATE.width)));
  const height = Math.max(680, Math.min(2160, finiteNumber(value.height, DEFAULT_WINDOW_STATE.height)));
  const x = typeof value.x === "number" && Number.isFinite(value.x) ? Math.trunc(value.x) : undefined;
  const y = typeof value.y === "number" && Number.isFinite(value.y) ? Math.trunc(value.y) : undefined;

  return {
    width: Math.trunc(width),
    height: Math.trunc(height),
    ...(x === undefined ? {} : { x }),
    ...(y === undefined ? {} : { y }),
    maximized: value.maximized === true
  };
}

export function sanitizeShellState(input: unknown): ShellState {
  const value = input && typeof input === "object" ? input as Record<string, unknown> : {};
  const selected = settingsSections.has(value.selectedSettingsSection as SettingsSection)
    ? value.selectedSettingsSection as SettingsSection
    : DEFAULT_SHELL_STATE.selectedSettingsSection;
  const theme = themes.has(value.theme as ThemePreference)
    ? value.theme as ThemePreference
    : DEFAULT_SHELL_STATE.theme;

  return {
    leftPanelWidth: Math.max(210, Math.min(420, finiteNumber(value.leftPanelWidth, DEFAULT_SHELL_STATE.leftPanelWidth))),
    rightPanelWidth: Math.max(280, Math.min(520, finiteNumber(value.rightPanelWidth, DEFAULT_SHELL_STATE.rightPanelWidth))),
    inspectorCollapsed: value.inspectorCollapsed === true,
    selectedSettingsSection: selected,
    theme
  };
}

function readJson(filePath: string): unknown {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch {
    return undefined;
  }
}

export function loadWindowState(filePath: string): WindowState {
  return sanitizeWindowState(readJson(filePath));
}

export function loadShellState(filePath: string): ShellState {
  return sanitizeShellState(readJson(filePath));
}

export function writeJsonAtomic(filePath: string, value: unknown): void {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  const temporary = `${filePath}.tmp`;
  fs.writeFileSync(temporary, JSON.stringify(value, null, 2), "utf8");
  fs.renameSync(temporary, filePath);
}

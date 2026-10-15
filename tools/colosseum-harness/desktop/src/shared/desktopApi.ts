export type ThemePreference = "dark" | "system";
export type SettingsSection = "appearance" | "codex" | "behaviour" | "repository" | "harness";
export type WindowAction = "minimize" | "toggle-maximize" | "close";

export interface ShellState {
  leftPanelWidth: number;
  rightPanelWidth: number;
  inspectorCollapsed: boolean;
  selectedSettingsSection: SettingsSection;
  theme: ThemePreference;
}

export const DEFAULT_SHELL_STATE: ShellState = {
  leftPanelWidth: 248,
  rightPanelWidth: 326,
  inspectorCollapsed: false,
  selectedSettingsSection: "appearance",
  theme: "dark"
};

export interface DesktopApi {
  getShellState(): Promise<ShellState>;
  updateShellState(patch: Partial<ShellState>): Promise<ShellState>;
  windowAction(action: WindowAction): Promise<void>;
}

export const IPC_CHANNELS = {
  shellStateGet: "desktop:shell-state:get",
  shellStateSet: "desktop:shell-state:set",
  windowAction: "desktop:window:action"
} as const;

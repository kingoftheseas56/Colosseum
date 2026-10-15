import type { DesktopApi } from "./desktopApi";

declare global {
  interface Window {
    harnessDesktop: DesktopApi;
  }
}

export {};

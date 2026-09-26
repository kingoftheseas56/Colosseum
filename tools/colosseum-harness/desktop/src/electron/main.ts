import { app, BrowserWindow, screen } from "electron";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { registerDesktopIpc } from "./ipc";
import {
  loadWindowState,
  writeJsonAtomic,
  type WindowState
} from "./stateStore";
import { createSecureWindowOptions } from "./windowOptions";

const smokeTest = process.argv.includes("--smoke-test");
const smokeScreenshot = process.argv
  .find(argument => argument.startsWith("--smoke-screenshot="))
  ?.slice("--smoke-screenshot=".length);
let mainWindow: BrowserWindow | null = null;

if (smokeTest) {
  app.setPath("userData", path.join(os.tmpdir(), "colosseum-harness-desktop-smoke"));
}

function ensureVisible(state: WindowState): WindowState {
  if (state.x === undefined || state.y === undefined) {
    return state;
  }

  const centerX = state.x + Math.round(state.width / 2);
  const centerY = state.y + Math.round(state.height / 2);
  const visible = screen.getAllDisplays().some(({ workArea }) =>
    centerX >= workArea.x &&
    centerX <= workArea.x + workArea.width &&
    centerY >= workArea.y &&
    centerY <= workArea.y + workArea.height
  );

  if (visible) {
    return state;
  }

  const { x: _x, y: _y, ...withoutPosition } = state;
  return withoutPosition;
}

function saveWindowState(window: BrowserWindow, filePath: string): void {
  const bounds = window.getNormalBounds();
  writeJsonAtomic(filePath, {
    width: bounds.width,
    height: bounds.height,
    x: bounds.x,
    y: bounds.y,
    maximized: window.isMaximized()
  } satisfies WindowState);
}

async function createMainWindow(): Promise<void> {
  const userData = app.getPath("userData");
  const windowStatePath = path.join(userData, "window-state.json");
  const shellStatePath = path.join(userData, "shell-state.json");
  const preloadPath = path.join(__dirname, "preload.js");
  const state = ensureVisible(loadWindowState(windowStatePath));

  mainWindow = new BrowserWindow(createSecureWindowOptions(preloadPath, state));
  registerDesktopIpc(shellStatePath, () => mainWindow);

  mainWindow.on("close", () => {
    if (mainWindow) {
      saveWindowState(mainWindow, windowStatePath);
    }
  });

  mainWindow.on("closed", () => {
    mainWindow = null;
  });

  mainWindow.webContents.on("did-fail-load", (_event, errorCode, errorDescription) => {
    console.error(`Renderer failed to load: ${errorCode} ${errorDescription}`);
    if (smokeTest) {
      process.exitCode = 1;
      app.quit();
    }
  });

  mainWindow.webContents.once("did-finish-load", () => {
    if (!smokeTest || !mainWindow) {
      return;
    }

    void (async () => {
      await new Promise(resolve => setTimeout(resolve, 350));
      if (!mainWindow) {
        throw new Error("Smoke window closed before renderer verification");
      }

      const probe = await mainWindow.webContents.executeJavaScript(
        `({
          text: document.body.innerText,
          hasApi: typeof window.harnessDesktop === "object"
        })`,
        true
      ) as { text: string; hasApi: boolean };

      if (!probe.hasApi) {
        throw new Error("Smoke renderer did not receive the preload API");
      }
      if (!probe.text.includes("Colosseum Harness") || !probe.text.includes("The cockpit is awake.")) {
        throw new Error(`Smoke renderer did not paint expected shell text: ${probe.text.slice(0, 200)}`);
      }

      if (smokeScreenshot) {
        const image = await mainWindow.webContents.capturePage();
        fs.writeFileSync(smokeScreenshot, image.toPNG());
      }

      console.log("RENDERER_SMOKE_OK");
      setTimeout(() => app.quit(), 100);
    })().catch(error => {
      console.error(error);
      process.exitCode = 1;
      app.quit();
    });
  });

  mainWindow.once("ready-to-show", () => {
    if (state.maximized) {
      mainWindow?.maximize();
    }

    if (!smokeTest) {
      mainWindow?.show();
    }
  });

  await mainWindow.loadFile(path.join(__dirname, "../../renderer/index.html"));
}

app.whenReady().then(createMainWindow).catch(error => {
  console.error(error);
  process.exitCode = 1;
  app.quit();
});

app.on("window-all-closed", () => app.quit());

import { render, screen } from "@testing-library/react";
import { beforeEach, expect, test, vi } from "vitest";
import App from "./App";
import { DEFAULT_SHELL_STATE } from "../shared/desktopApi";

beforeEach(() => {
  window.harnessDesktop = {
    getShellState: vi.fn(async () => DEFAULT_SHELL_STATE),
    updateShellState: vi.fn(async patch => ({ ...DEFAULT_SHELL_STATE, ...patch })),
    windowAction: vi.fn(async () => undefined)
  };
});

test("renders the permanent Slice 1 cockpit", async () => {
  render(<App />);

  expect(screen.getByText("Colosseum Harness")).toBeInTheDocument();
  expect(screen.getAllByText("Feria").length).toBeGreaterThanOrEqual(1);
  expect(screen.getByText("Ratings & Reviews")).toBeInTheDocument();
  expect(screen.getByText("The cockpit is awake.")).toBeInTheDocument();
  expect(screen.getByText("Codex: offline")).toBeInTheDocument();
  expect(screen.getByText("Harness: offline")).toBeInTheDocument();
  expect(screen.getByRole("textbox", { name: "Task prompt" })).toBeDisabled();
});

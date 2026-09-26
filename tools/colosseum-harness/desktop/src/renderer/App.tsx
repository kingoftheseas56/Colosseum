import { useEffect, useMemo, useRef, useState } from "react";
import {
  DEFAULT_SHELL_STATE,
  type SettingsSection,
  type ShellState
} from "../shared/desktopApi";

const tasks = [
  { id: "feria", name: "Feria", detail: "Streaming world integration" },
  { id: "ratings", name: "Ratings & Reviews", detail: "Delivery + parity" },
  { id: "player2", name: "Player 2", detail: "Holy Grail lane" }
];

const inspectorTabs = ["Context", "Files", "Diff", "Evidence"] as const;
type InspectorTab = typeof inspectorTabs[number];

const settingsSections: Array<{ id: SettingsSection; label: string; note: string }> = [
  { id: "appearance", label: "Appearance", note: "Desktop theme and density." },
  { id: "codex", label: "Codex", note: "Model connection arrives in Slice 2." },
  { id: "behaviour", label: "Behaviour", note: "Developer directives arrive in Slice 7." },
  { id: "repository", label: "Repository", note: "Colosseum workspace binding arrives later." },
  { id: "harness", label: "Harness", note: "Python Harness bridge arrives in Slice 6." }
];

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export default function App() {
  const [shell, setShell] = useState<ShellState>(DEFAULT_SHELL_STATE);
  const [hydrated, setHydrated] = useState(false);
  const [selectedTask, setSelectedTask] = useState(tasks[0]?.id ?? "");
  const [inspectorTab, setInspectorTab] = useState<InspectorTab>("Context");
  const [settingsOpen, setSettingsOpen] = useState(false);
  const persistTimer = useRef<number | null>(null);

  useEffect(() => {
    window.harnessDesktop.getShellState()
      .then(state => setShell(state))
      .finally(() => setHydrated(true));
  }, []);

  useEffect(() => {
    if (!hydrated) {
      return;
    }

    if (persistTimer.current !== null) {
      window.clearTimeout(persistTimer.current);
    }

    persistTimer.current = window.setTimeout(() => {
      void window.harnessDesktop.updateShellState(shell);
    }, 160);

    return () => {
      if (persistTimer.current !== null) {
        window.clearTimeout(persistTimer.current);
      }
    };
  }, [shell, hydrated]);

  const activeTask = useMemo(
    () => tasks.find(task => task.id === selectedTask) ?? tasks[0],
    [selectedTask]
  );

  function beginResize(side: "left" | "right", startX: number) {
    const startWidth = side === "left" ? shell.leftPanelWidth : shell.rightPanelWidth;

    const onMove = (event: PointerEvent) => {
      const delta = event.clientX - startX;
      setShell(current => ({
        ...current,
        ...(side === "left"
          ? { leftPanelWidth: clamp(startWidth + delta, 210, 420) }
          : { rightPanelWidth: clamp(startWidth - delta, 280, 520) })
      }));
    };

    const onUp = () => {
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
    };

    window.addEventListener("pointermove", onMove);
    window.addEventListener("pointerup", onUp, { once: true });
  }

  const settings = settingsSections.find(section => section.id === shell.selectedSettingsSection)
    ?? settingsSections[0];

  return (
    <div className="app-shell" data-theme={shell.theme}>
      <header className="titlebar">
        <div className="brand-mark" aria-hidden="true">C</div>
        <div className="title-copy">
          <strong>Colosseum Harness</strong>
          <span>Desktop</span>
        </div>
        <div className="title-spacer" />
        <div className="window-controls">
          <button aria-label="Minimize" onClick={() => void window.harnessDesktop.windowAction("minimize")}>—</button>
          <button aria-label="Maximize" onClick={() => void window.harnessDesktop.windowAction("toggle-maximize")}>□</button>
          <button aria-label="Close" className="close" onClick={() => void window.harnessDesktop.windowAction("close")}>×</button>
        </div>
      </header>

      <main className="workspace-grid">
        <aside className="task-sidebar" style={{ width: shell.leftPanelWidth }}>
          <div className="sidebar-top">
            <button className="new-task" type="button">+ New Task</button>
          </div>
          <div className="sidebar-label">Recent</div>
          <nav className="task-list" aria-label="Recent tasks">
            {tasks.map(task => (
              <button
                key={task.id}
                className={task.id === selectedTask && !settingsOpen ? "task active" : "task"}
                onClick={() => {
                  setSelectedTask(task.id);
                  setSettingsOpen(false);
                }}
              >
                <span>{task.name}</span>
                <small>{task.detail}</small>
              </button>
            ))}
          </nav>
          <div className="sidebar-label archived-label">Archived</div>
          <div className="archived-empty">No archived tasks</div>
          <div className="sidebar-bottom">
            <button
              type="button"
              className={settingsOpen ? "settings-button active" : "settings-button"}
              onClick={() => setSettingsOpen(true)}
            >
              Settings
            </button>
          </div>
        </aside>

        <div
          className="resize-handle vertical"
          role="separator"
          aria-orientation="vertical"
          onPointerDown={event => beginResize("left", event.clientX)}
        />

        <section className="main-stage">
          {settingsOpen ? (
            <div className="settings-layout">
              <nav className="settings-nav" aria-label="Settings sections">
                {settingsSections.map(section => (
                  <button
                    key={section.id}
                    className={section.id === shell.selectedSettingsSection ? "active" : ""}
                    onClick={() => setShell(current => ({ ...current, selectedSettingsSection: section.id }))}
                  >
                    {section.label}
                  </button>
                ))}
              </nav>
              <div className="settings-content">
                <span className="eyebrow">Settings</span>
                <h1>{settings?.label}</h1>
                <p>{settings?.note}</p>
                {settings?.id === "appearance" && (
                  <div className="setting-card">
                    <div>
                      <strong>Theme</strong>
                      <span>Harness Desktop is dark-first in Slice 1.</span>
                    </div>
                    <select
                      value={shell.theme}
                      onChange={event => setShell(current => ({
                        ...current,
                        theme: event.target.value as ShellState["theme"]
                      }))}
                    >
                      <option value="dark">Dark</option>
                      <option value="system">System</option>
                    </select>
                  </div>
                )}
              </div>
            </div>
          ) : (
            <>
              <div className="stage-toolbar">
                <div>
                  <span className="eyebrow">Task</span>
                  <strong>{activeTask?.name}</strong>
                </div>
                <div className="stage-actions">
                  <button type="button" disabled>Resume</button>
                  <button type="button" disabled>Archive</button>
                </div>
              </div>
              <div className="conversation-empty">
                <div className="empty-orbit" aria-hidden="true"><span /></div>
                <span className="eyebrow">Slice 1</span>
                <h1>The cockpit is awake.</h1>
                <p>Codex arrives in Slice 2. This surface is already the permanent home for task conversation, activity, and handoff state.</p>
              </div>
              <div className="composer-shell">
                <textarea disabled aria-label="Task prompt" placeholder="Codex connection arrives in Slice 2…" />
                <button type="button" disabled>Send</button>
              </div>
            </>
          )}
        </section>

        {!shell.inspectorCollapsed && (
          <>
            <div
              className="resize-handle vertical"
              role="separator"
              aria-orientation="vertical"
              onPointerDown={event => beginResize("right", event.clientX)}
            />
            <aside className="inspector" style={{ width: shell.rightPanelWidth }}>
              <div className="inspector-tabs">
                {inspectorTabs.map(tab => (
                  <button
                    key={tab}
                    className={tab === inspectorTab ? "active" : ""}
                    onClick={() => setInspectorTab(tab)}
                  >
                    {tab}
                  </button>
                ))}
              </div>
              <div className="inspector-body">
                <span className="eyebrow">{inspectorTab}</span>
                <h2>{inspectorTab === "Context" ? "Nothing loaded yet" : `${inspectorTab} will appear here`}</h2>
                <p>
                  {inspectorTab === "Context"
                    ? "Slice 6 will feed this panel from the existing Python Harness."
                    : "This panel is reserved now so later slices do not need to redesign the cockpit."}
                </p>
              </div>
            </aside>
          </>
        )}

        <button
          type="button"
          className="inspector-toggle"
          aria-label={shell.inspectorCollapsed ? "Show inspector" : "Hide inspector"}
          onClick={() => setShell(current => ({ ...current, inspectorCollapsed: !current.inspectorCollapsed }))}
        >
          {shell.inspectorCollapsed ? "‹" : "›"}
        </button>
      </main>

      <footer className="statusbar">
        <span className="status-primary">Colosseum</span>
        <span>Repository: unbound</span>
        <span>Codex: offline</span>
        <span>Harness: offline</span>
        <span>Completion: unavailable</span>
        <span className="status-spacer" />
        <span>Model: —</span>
        <span>Reasoning: —</span>
        <span>Permissions: —</span>
      </footer>
    </div>
  );
}

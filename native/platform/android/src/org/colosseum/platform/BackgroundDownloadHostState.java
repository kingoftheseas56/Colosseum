package org.colosseum.platform;

// OS lifetime only. The native owners retain all jobs, bytes and retry state.
final class BackgroundDownloadHostState {
    enum Action { NONE, START, UPDATE, STOP, REQUEST_PERMISSION }
    enum Gate { ALLOWED, PERMISSION_REQUIRED, DISABLED }
    int jobs;
    boolean ready;
    boolean starting;
    boolean permissionPending;
    private boolean permissionRequested;
    private boolean blocked;
    String error = "";

    Action update(int count, boolean visible, Gate gate) {
        boolean hadWork = jobs > 0 || ready || starting;
        jobs = Math.max(0, count);
        if (jobs == 0) {
            ready = false;
            starting = false;
            blocked = false;
            error = "";
            return hadWork ? Action.STOP : Action.NONE;
        }
        if (gate != Gate.ALLOWED) {
            boolean wasRunning = ready || starting;
            ready = false;
            starting = false;
            if (gate == Gate.PERMISSION_REQUIRED && visible && !permissionRequested) {
                permissionRequested = true;
                permissionPending = true;
                return Action.REQUEST_PERMISSION;
            }
            if (!permissionPending) {
                error = "Background downloading needs notifications. Keep Colosseum open to continue.";
                blocked = true;
            }
            return wasRunning ? Action.STOP : Action.NONE;
        }
        if (ready) return Action.UPDATE;
        if (starting || permissionPending || blocked || !visible) return Action.NONE;
        starting = true;
        return Action.START;
    }

    boolean started() {
        if (jobs == 0 || !starting || blocked) return false;
        ready = true;
        starting = false;
        error = "";
        return true;
    }

    void permissionResult(boolean granted) {
        permissionPending = false;
        blocked = !granted;
        error = granted ? "" : "Allow notifications to keep downloads running outside Colosseum.";
    }

    void failed(String reason) {
        ready = false;
        starting = false;
        blocked = true;
        error = reason;
    }
}

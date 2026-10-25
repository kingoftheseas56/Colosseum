package org.colosseum.platform;

import static org.colosseum.platform.BackgroundDownloadHostState.Action.*;
import static org.colosseum.platform.BackgroundDownloadHostState.Gate.*;

public final class BackgroundDownloadHostStateTest {
    private static int assertions;
    private static void check(boolean value, String message) {
        ++assertions;
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        BackgroundDownloadHostState state = new BackgroundDownloadHostState();
        check(state.update(0, true, ALLOWED) == NONE, "idle must not start a service");
        check(state.update(1, false, ALLOWED) == NONE, "hidden jobs cannot start a service");
        check(state.update(1, true, ALLOWED) == START, "visible user download starts service");
        check(!state.ready, "request acceptance is not foreground-service readiness");
        check(state.update(2, true, ALLOWED) == NONE, "pending start cannot duplicate");
        check(state.started() && state.ready, "successful start grants background lifetime");
        check(state.update(2, false, ALLOWED) == UPDATE, "running service updates while hidden");
        check(state.update(1, false, ALLOWED) == UPDATE, "one remaining job retains service");
        check(state.update(0, false, ALLOWED) == STOP && !state.ready, "last job ends lifetime");
        check(!state.started(), "late start cannot resurrect a cancelled download");
        check(state.update(0, false, ALLOWED) == NONE, "idle stop is idempotent");

        state = new BackgroundDownloadHostState();
        check(state.update(1, true, PERMISSION_REQUIRED) == REQUEST_PERMISSION,
              "notification permission uses ordinary visible-app request");
        check(!state.ready && state.permissionPending, "permission request grants no lifetime");
        check(state.update(2, true, PERMISSION_REQUIRED) == NONE, "only one permission prompt");
        state.permissionResult(false);
        check(state.update(2, false, PERMISSION_REQUIRED) == NONE && !state.ready
              && !state.error.isEmpty(), "denial remains honest and cannot run hidden");
        check(state.update(0, true, PERMISSION_REQUIRED) == STOP, "cancel after denial settles");
        check(state.update(1, true, PERMISSION_REQUIRED) == NONE,
              "a later job must not loop permission prompts");
        check(state.update(0, true, ALLOWED) == STOP, "permission-denied job can be cancelled");
        check(state.update(1, true, ALLOWED) == START, "owner granting permission enables next job");
        state.failed("Service start refused");
        check(!state.ready && !state.starting, "start refusal does not grant lifetime");
        check(state.update(1, true, ALLOWED) == NONE, "refusal cannot spin automatic starts");
        check(state.update(0, true, ALLOWED) == STOP, "refused job can settle");

        state = new BackgroundDownloadHostState();
        check(state.update(1, true, PERMISSION_REQUIRED) == REQUEST_PERMISSION, "prompt starts");
        check(state.update(0, true, PERMISSION_REQUIRED) == STOP, "cancel while prompting");
        state.permissionResult(true);
        check(state.update(0, true, ALLOWED) == NONE, "late grant cannot start cancelled jobs");
        check(state.update(1, true, ALLOWED) == START && state.started(), "new download starts");
        check(state.update(1, false, DISABLED) == STOP && !state.ready,
              "revoked notification visibility removes background lifetime");
        check(state.update(1, false, ALLOWED) == NONE, "revocation cannot restart in background");

        state = new BackgroundDownloadHostState();
        check(state.update(1, true, ALLOWED) == START && state.started(), "timeout fixture starts");
        state.failed("Android download service time limit reached");
        check(!state.ready && state.update(1, false, ALLOWED) == NONE,
              "timeout withdraws lifetime and cannot restart hidden");
        check(state.update(0, false, ALLOWED) == STOP && state.error.isEmpty(),
              "paused/complete snapshot stops service and clears stale error");
        System.out.println("BACKGROUND_DOWNLOAD_HOST_STATE_OK assertions=" + assertions);
    }
}

package org.colosseum.platform;

import android.content.pm.ActivityInfo;

/** Owns only the player's temporary orientation request, never system rotation settings. */
public final class PlayerOrientationRequest {
    private boolean owned;
    private int previous;

    public boolean active() { return owned; }

    public int update(boolean enabled, int current) {
        if (enabled) {
            if (!owned) previous = current;
            owned = true;
            return ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        }
        if (!owned) return current;
        owned = false;
        return previous;
    }
}

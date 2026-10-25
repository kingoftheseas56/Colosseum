package org.colosseum.platform;

import android.content.Intent;
import android.os.Bundle;
import org.qtproject.qt.android.bindings.QtActivity;

public final class ColosseumActivity extends QtActivity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        DownloadService.handleIntent(getIntent());
    }

    @Override public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        DownloadService.handleIntent(intent);
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                                    int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        DownloadService.permissionResult(requestCode, grantResults);
    }
}

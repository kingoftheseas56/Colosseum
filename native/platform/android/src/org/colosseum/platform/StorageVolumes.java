package org.colosseum.platform;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import java.io.File;

/** The device's mounted storage places (internal storage, SD cards, USB drives) for the Vault chooser. */
public final class StorageVolumes {
    private StorageVolumes() {}

    /** One volume per line: label, path, removable ("1"/"0"), tab separated. Mounted volumes only. */
    public static String list(Context context) {
        StringBuilder out = new StringBuilder();
        try {
            StorageManager manager = (StorageManager) context.getSystemService(Context.STORAGE_SERVICE);
            if (manager == null) return "";
            File[] appDirs = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                    ? new File[0] : context.getExternalFilesDirs(null);
            for (StorageVolume volume : manager.getStorageVolumes()) {
                if (!Environment.MEDIA_MOUNTED.equals(volume.getState())
                        && !Environment.MEDIA_MOUNTED_READ_ONLY.equals(volume.getState())) continue;
                File dir = directoryOf(manager, volume, appDirs);
                if (dir == null) continue;
                String label = volume.getDescription(context);
                if (label == null || label.isEmpty()) label = dir.getName();
                out.append(label.replace('\t', ' ').replace('\n', ' ')).append('\t')
                   .append(dir.getAbsolutePath()).append('\t')
                   .append(volume.isRemovable() ? "1" : "0").append('\n');
            }
        } catch (RuntimeException error) {
            // An unreadable volume list reads as no places; the chooser then offers none.
        }
        return out.toString();
    }

    private static File directoryOf(StorageManager manager, StorageVolume volume, File[] appDirs) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) return volume.getDirectory();
        if (volume.isPrimary()) return Environment.getExternalStorageDirectory();
        // API 28-29: the app's own folder on each card lives under <volume>/Android/...; climb to the volume.
        for (File appDir : appDirs) {
            if (appDir == null) continue;
            StorageVolume owner = manager.getStorageVolume(appDir);
            if (owner == null || owner.getUuid() == null || !owner.getUuid().equals(volume.getUuid())) continue;
            String path = appDir.getAbsolutePath();
            int marker = path.indexOf("/Android");
            if (marker > 0) return new File(path.substring(0, marker));
        }
        return null;
    }
}

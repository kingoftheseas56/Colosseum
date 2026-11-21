package org.colosseum.platform;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

/** Display name of a single document handed in by Android's system picker (content://). */
public final class DocumentName {
    private DocumentName() {}

    public static String displayName(Context context, String source) {
        Uri uri = Uri.parse(source);
        if (!"content".equals(uri.getScheme()))
            return "";
        try (Cursor cursor = context.getContentResolver().query(
                uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0))
                return cursor.getString(0);
        } catch (RuntimeException error) {
            // Access failure stays fail-closed at the source/decoder admission gate.
        }
        return "";
    }
}

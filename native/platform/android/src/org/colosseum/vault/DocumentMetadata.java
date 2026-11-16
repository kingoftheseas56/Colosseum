package org.colosseum.vault;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.provider.DocumentsContract.Document;
import org.json.JSONObject;

/** Metadata for a document already authorized by the system picker. */
public final class DocumentMetadata {
    private DocumentMetadata() {}

    public static String facts(Context context, String source) {
        Uri uri = Uri.parse(source);
        if (!"content".equals(uri.getScheme()))
            return "";
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE,
                               Document.COLUMN_LAST_MODIFIED, Document.COLUMN_MIME_TYPE },
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                return new JSONObject().put("name", cursor.getString(0))
                    .put("size", cursor.getLong(1)).put("modified", cursor.getLong(2))
                    .put("mime", cursor.getString(3)).toString();
            }
        } catch (Exception unsupportedColumns) {
            // Non-document providers may expose only the standard OpenableColumns.
        }
        return "";
    }

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

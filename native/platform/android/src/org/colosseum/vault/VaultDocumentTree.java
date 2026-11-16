package org.colosseum.vault;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Set;

public final class VaultDocumentTree {
    public static final int PICK_TREE = 0x5641;
    private static final int READ = Intent.FLAG_GRANT_READ_URI_PERMISSION;
    private static final String[] COLUMNS = {
        Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
        Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS
    };

    private VaultDocumentTree() {}

    public static boolean pick(Activity activity, String previous) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(READ | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                       | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        if (!previous.isEmpty())
            intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, Uri.parse(previous));
        try {
            activity.startActivityForResult(intent, PICK_TREE);
            return true;
        } catch (RuntimeException error) {
            message(activity, "Could not open the folder picker.");
            return false;
        }
    }

    public static String takeGrant(Activity activity, Intent result, String previous) {
        Uri tree = result == null ? null : result.getData();
        if (tree == null || !"content".equals(tree.getScheme()) || !DocumentsContract.isTreeUri(tree))
            return "";
        if (!previous.isEmpty() && !tree.equals(Uri.parse(previous))) {
            message(activity, "Choose the same folder to restore access.");
            return "";
        }
        try {
            activity.getContentResolver().takePersistableUriPermission(tree, READ);
            return tree.toString();
        } catch (SecurityException error) {
            message(activity, "Could not keep access to this folder.");
            return "";
        }
    }

    public static void releaseGrant(Context context, String source) {
        try {
            context.getContentResolver().releasePersistableUriPermission(Uri.parse(source), READ);
        } catch (SecurityException ignored) {
            // It was already revoked.
        }
    }

    public static String snapshot(Context context, String rootsJson, String ignoreJson) {
        ContentResolver resolver = context.getContentResolver();
        Set<Uri> granted = new HashSet<>();
        for (UriPermission permission : resolver.getPersistedUriPermissions()) {
            if (permission.isReadPermission())
                granted.add(permission.getUri());
        }
        try {
            JSONArray sources = new JSONArray();
            JSONArray roots = new JSONArray(rootsJson);
            JSONArray ignored = new JSONArray(ignoreJson);
            for (int i = 0; i < roots.length(); ++i) {
                String source = roots.getString(i);
                Uri tree = Uri.parse(source);
                JSONObject folder = new JSONObject();
                folder.put("rootUri", source);
                folder.put("available", false);
                folder.put("permissionLost", !granted.contains(tree));
                if (granted.contains(tree)) {
                    try {
                        String documentId = DocumentsContract.getTreeDocumentId(tree);
                        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, documentId);
                        // A detached card can report an empty child cursor. First establish that
                        // the root itself is still a directory before accepting an empty scan.
                        try (Cursor cursor = resolver.query(root,
                                new String[] { Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE },
                                null, null, null)) {
                            if (cursor == null || !cursor.moveToFirst()
                                    || !Document.MIME_TYPE_DIR.equals(cursor.getString(1)))
                                throw new IllegalStateException("Folder unavailable");
                            folder.put("displayName", cursor.getString(0));
                        }
                        folder.put("entries", listTree(resolver, tree, documentId, ignored));
                        folder.put("available", true);
                    } catch (SecurityException revoked) {
                        folder.put("permissionLost", true);
                    } catch (RuntimeException unavailable) {
                        // Preserve the previous index if any subtree failed or is still loading.
                    }
                }
                sources.put(folder);
            }
            return new JSONObject().put("ok", true).put("sources", sources).toString();
        } catch (JSONException error) {
            return new JSONObject().toString();
        }
    }

    private static JSONArray listTree(ContentResolver resolver, Uri tree, String root,
                                      JSONArray ignored) throws JSONException {
        ArrayDeque<Folder> pending = new ArrayDeque<>();
        Set<String> visited = new HashSet<>();
        JSONArray entries = new JSONArray();
        pending.add(new Folder(root, ""));
        while (!pending.isEmpty()) {
            Folder folder = pending.removeLast();
            if (!visited.add(folder.id))
                continue;
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, folder.id);
            try (Cursor cursor = resolver.query(children, COLUMNS, null, null, null)) {
                if (cursor == null || cursor.getExtras().getBoolean(DocumentsContract.EXTRA_LOADING)
                        || cursor.getExtras().containsKey(DocumentsContract.EXTRA_ERROR))
                    throw new IllegalStateException("Incomplete folder listing");
                while (cursor.moveToNext()) {
                    String id = cursor.getString(0);
                    String name = cursor.getString(1);
                    String mime = cursor.getString(2);
                    if (id == null || name == null || name.isEmpty())
                        continue;
                    if (Document.MIME_TYPE_DIR.equals(mime)) {
                        if (!excluded(name, ignored)) {
                            String path = folder.path.isEmpty() ? name : folder.path + "/" + name;
                            pending.add(new Folder(id, path));
                        }
                    } else if ((cursor.getInt(5) & Document.FLAG_VIRTUAL_DOCUMENT) == 0) {
                        JSONObject entry = new JSONObject();
                        entry.put("uri", DocumentsContract.buildDocumentUriUsingTree(tree, id).toString());
                        entry.put("displayName", name);
                        entry.put("relativePath", folder.path);
                        entry.put("mimeType", mime);
                        entry.put("sizeBytes", cursor.getLong(3));
                        entry.put("modifiedMs", cursor.getLong(4));
                        entries.put(entry);
                    }
                }
            }
        }
        return entries;
    }

    private static boolean excluded(String name, JSONArray ignored) throws JSONException {
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        for (int i = 0; i < ignored.length(); ++i) {
            String needle = ignored.getString(i).toLowerCase(java.util.Locale.ROOT);
            if (!needle.isEmpty() && lower.contains(needle))
                return true;
        }
        return false;
    }

    private static void message(Context context, String text) {
        Toast.makeText(context, text, Toast.LENGTH_SHORT).show();
    }

    private static final class Folder {
        final String id;
        final String path;
        Folder(String id, String path) {
            this.id = id;
            this.path = path;
        }
    }
}

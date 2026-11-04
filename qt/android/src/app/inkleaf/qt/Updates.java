// Updating the app on Android (called from Updater.cpp): the new APK is
// downloaded by the system's download manager (with its notification), checked,
// and handed to Android's package installer, which asks the user to confirm.
package app.inkleaf.qt;

import android.app.DownloadManager;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import java.io.File;
import java.io.InputStream;
import java.security.MessageDigest;

public class Updates {
    private static final String FILE = "Inkleaf-update.apk";

    private static DownloadManager manager(Context ctx) {
        return (DownloadManager) ctx.getSystemService(Context.DOWNLOAD_SERVICE);
    }

    /** Starts the download; its id, or -1. */
    public static long download(Context ctx, String url, String title) {
        try {
            // An earlier one would make the manager pick another name.
            File old = new File(ctx.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS), FILE);
            if (old.exists()) old.delete();
            DownloadManager.Request r = new DownloadManager.Request(Uri.parse(url));
            r.setTitle(title);
            r.setMimeType("application/vnd.android.package-archive");
            r.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE);
            r.setDestinationInExternalFilesDir(ctx, Environment.DIRECTORY_DOWNLOADS, FILE);
            return manager(ctx).enqueue(r);
        } catch (Exception e) {
            return -1;
        }
    }

    /** [bytes so far, total (or -1), status (DownloadManager.STATUS_*)]. */
    public static long[] progress(Context ctx, long id) {
        long[] out = {0, -1, DownloadManager.STATUS_FAILED};
        try (Cursor c = manager(ctx).query(new DownloadManager.Query().setFilterById(id))) {
            if (c != null && c.moveToFirst()) {
                out[0] = c.getLong(c.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
                out[1] = c.getLong(c.getColumnIndexOrThrow(DownloadManager.COLUMN_TOTAL_SIZE_BYTES));
                out[2] = c.getInt(c.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS));
            }
        } catch (Exception e) {
            // Reported as failed.
        }
        return out;
    }

    /** The downloaded file's SHA-256 (hex), or "" when it can't be read. */
    public static String sha256(Context ctx, long id) {
        try (InputStream in = ctx.getContentResolver().openInputStream(manager(ctx).getUriForDownloadedFile(id))) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            StringBuilder sb = new StringBuilder();
            for (byte b : md.digest()) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    /** Opens Android's installer on the download (it asks the user to confirm). */
    public static boolean install(Context ctx, long id) {
        try {
            Uri uri = manager(ctx).getUriForDownloadedFile(id);
            if (uri == null) return false;
            Intent i = new Intent(Intent.ACTION_VIEW);
            i.setDataAndType(uri, "application/vnd.android.package-archive");
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(i);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    public static void cancel(Context ctx, long id) {
        try {
            manager(ctx).remove(id);
        } catch (Exception e) {
            // Already gone.
        }
    }
}

// PDF pages for the Qt app on Android (Qt PDF isn't shipped there): page
// sizes and pictures from Android's own PdfRenderer. Called from Pdf.cpp.
package app.inkleaf.qt;

import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Matrix;
import android.graphics.pdf.PdfRenderer;
import android.os.ParcelFileDescriptor;
import java.io.File;

public class PdfPages {
    /** [page count, width 0, height 0, width 1, …] in points; null when it can't be opened. */
    public static float[] sizes(String path) {
        try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(new File(path), ParcelFileDescriptor.MODE_READ_ONLY);
             PdfRenderer r = new PdfRenderer(fd)) {
            int n = r.getPageCount();
            float[] out = new float[1 + 2 * n];
            out[0] = n;
            for (int i = 0; i < n; i++) {
                try (PdfRenderer.Page p = r.openPage(i)) {
                    out[1 + 2 * i] = p.getWidth();
                    out[2 + 2 * i] = p.getHeight();
                }
            }
            return out;
        } catch (Exception e) {
            return null;
        }
    }

    /** ARGB pixels of the w×h part at (x, y) of the page drawn fullW×fullH; null on failure. */
    public static int[] render(String path, int page, int fullW, int fullH, int x, int y, int w, int h) {
        try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(new File(path), ParcelFileDescriptor.MODE_READ_ONLY);
             PdfRenderer r = new PdfRenderer(fd);
             PdfRenderer.Page p = r.openPage(page)) {
            Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            bmp.eraseColor(Color.WHITE);
            Matrix m = new Matrix();
            m.setScale(fullW / (float) p.getWidth(), fullH / (float) p.getHeight());
            m.postTranslate(-x, -y);
            p.render(bmp, null, m, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);
            int[] px = new int[w * h];
            bmp.getPixels(px, 0, w, 0, 0, w, h);
            bmp.recycle();
            return px;
        } catch (Exception e) {
            return null;
        }
    }
}

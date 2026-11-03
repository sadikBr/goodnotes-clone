// Export (src/editor/editor.ts exportAs): pages drawn like the editor's and
// made into a PDF, a PowerPoint or a PNG by the Rust core.
import QtQuick
import QtQuick.Dialogs
import Inkleaf

Item {
    id: root
    required property InkCanvas canvas
    property string title
    signal notify(string message, bool error)
    readonly property var scales: ({ pdf: 3, png: 3, pptx: 2 })
    property string format: "pdf"
    property string scope: "all"
    property var only: null

    /** Exports these page indexes as a PDF named after the notebook and `suffix` (a daftar class). */
    function runPages(list, suffix) {
        only = list
        format = "pdf"
        scope = "some"
        canvas.finishEdit()
        canvas.flush()
        dialog.selectedFile = InkleafCore.documentsFolder() + "/" + (title + " - " + suffix).replace(/[\\/:*?"<>|]/g, "_") + ".pdf"
        dialog.nameFilters = ["PDF (*.pdf)"]
        dialog.open()
    }
    function run(fmt, sc) {
        format = fmt
        scope = sc
        only = null
        canvas.finishEdit()
        canvas.flush()
        const name = sc === "all" ? title : title + " - page " + (canvas.currentPage + 1)
        dialog.selectedFile = InkleafCore.documentsFolder() + "/" + name.replace(/[\\/:*?"<>|]/g, "_") + "." + fmt
        dialog.nameFilters = [fmt === "pdf" ? "PDF (*.pdf)" : fmt === "pptx" ? "PowerPoint (*.pptx)" : "PNG image (*.png)"]
        dialog.open()
    }
    FileDialog {
        id: dialog
        fileMode: FileDialog.SaveFile
        title: "Export"
        onAccepted: {
            const pages = root.scope === "some" ? root.only : root.scope === "all" ? [...Array(root.canvas.pageCount).keys()] : [root.canvas.currentPage]
            root.notify("Exporting " + pages.length + " page" + (pages.length > 1 ? "s" : "") + "…", false)
            Qt.callLater(() => {
                try {
                    if (root.format === "pdf") {
                        // Vector pages: sharp at any zoom, with real (searchable) text.
                        const err = root.canvas.exportPdf(selectedFile.toString(), pages, root.title)
                        if (err) throw new Error(err)
                        root.notify("Exported to " + decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, "")), false)
                        return
                    }
                    const rendered = pages.map(i => root.canvas.exportPage(i, root.scales[root.format], root.format !== "png"))
                    const saved = InkleafCore.saveBytes(selectedFile.toString(), "buildDocument", { format: root.format, pages: rendered })
                    root.notify("Exported to " + saved, false)
                } catch (e) {
                    root.notify(String(e.message || e), true)
                }
            })
        }
    }
}

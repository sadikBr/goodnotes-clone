//! Document export: PDF, PowerPoint (.pptx) and PNG.
//!
//! The frontend renders each page to an image (JPEG for PDF/PPTX, PNG for
//! single-page PNG export). These writers wrap the images into documents
//! without any external tooling: PDF is written by hand (one image XObject
//! per page) and PPTX is a hand-assembled Office Open XML package with one
//! full-bleed picture per slide.

use base64::Engine;
use serde::Deserialize;
use std::io::Write;

use crate::error::{AppError, AppResult};

/// One rendered page as sent by the frontend.
#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ExportPage {
    /// `data:image/jpeg;base64,...` or `data:image/png;base64,...`
    pub data_url: String,
    /// Pixel size of the rendered image.
    pub width_px: u32,
    pub height_px: u32,
    /// Page size in Inkleaf units (96 per inch, so A4 = 794 x 1123).
    pub page_width: f64,
    pub page_height: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Format {
    Pdf,
    Pptx,
    Png,
}

impl Format {
    pub fn parse(s: &str) -> AppResult<Self> {
        match s {
            "pdf" => Ok(Format::Pdf),
            "pptx" => Ok(Format::Pptx),
            "png" => Ok(Format::Png),
            other => Err(AppError::Invalid(format!(
                "unsupported export format '{other}'"
            ))),
        }
    }

    pub fn extension(self) -> &'static str {
        match self {
            Format::Pdf => "pdf",
            Format::Pptx => "pptx",
            Format::Png => "png",
        }
    }
}

struct Image {
    bytes: Vec<u8>,
    jpeg: bool,
}

fn decode(page: &ExportPage) -> AppResult<Image> {
    let (jpeg, encoded) = if let Some(rest) = page.data_url.strip_prefix("data:image/jpeg;base64,")
    {
        (true, rest)
    } else if let Some(rest) = page.data_url.strip_prefix("data:image/png;base64,") {
        (false, rest)
    } else {
        return Err(AppError::Invalid("expected a JPEG or PNG data URL".into()));
    };
    let bytes = base64::engine::general_purpose::STANDARD
        .decode(encoded)
        .map_err(|e| AppError::Invalid(format!("invalid base64 image: {e}")))?;
    if page.width_px == 0
        || page.height_px == 0
        || page.page_width <= 0.0
        || page.page_height <= 0.0
    {
        return Err(AppError::Invalid(
            "page and image sizes must be positive".into(),
        ));
    }
    Ok(Image { bytes, jpeg })
}

/// Builds the requested document from rendered pages.
pub fn build(format: Format, pages: &[ExportPage]) -> AppResult<Vec<u8>> {
    if pages.is_empty() {
        return Err(AppError::Invalid("nothing to export".into()));
    }
    match format {
        Format::Png => {
            if pages.len() != 1 {
                return Err(AppError::Invalid(
                    "PNG export takes exactly one page".into(),
                ));
            }
            let img = decode(&pages[0])?;
            if img.jpeg {
                return Err(AppError::Invalid("PNG export needs a PNG image".into()));
            }
            Ok(img.bytes)
        }
        Format::Pdf => build_pdf(pages),
        Format::Pptx => build_pptx(pages),
    }
}

// ------------------------------------------------------------------- PDF

/// Inkleaf units are 96 per inch; PDF points are 72 per inch.
const UNITS_TO_PT: f64 = 72.0 / 96.0;

fn build_pdf(pages: &[ExportPage]) -> AppResult<Vec<u8>> {
    let images = pages.iter().map(decode).collect::<AppResult<Vec<_>>>()?;
    if images.iter().any(|i| !i.jpeg) {
        return Err(AppError::Invalid(
            "PDF export needs JPEG page images".into(),
        ));
    }
    // Object layout: 1 catalog, 2 page tree, then 3 objects per page
    // (page, content stream, image).
    let page_obj = |i: usize| 3 + i * 3;
    let mut out: Vec<u8> = Vec::new();
    let mut offsets: Vec<usize> = Vec::new();
    out.extend_from_slice(b"%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    let begin = |out: &mut Vec<u8>, offsets: &mut Vec<usize>, id: usize| {
        debug_assert_eq!(offsets.len() + 1, id);
        offsets.push(out.len());
        out.extend_from_slice(format!("{id} 0 obj\n").as_bytes());
    };

    begin(&mut out, &mut offsets, 1);
    out.extend_from_slice(b"<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    begin(&mut out, &mut offsets, 2);
    let kids: Vec<String> = (0..pages.len())
        .map(|i| format!("{} 0 R", page_obj(i)))
        .collect();
    out.extend_from_slice(
        format!(
            "<< /Type /Pages /Kids [{}] /Count {} >>\nendobj\n",
            kids.join(" "),
            pages.len()
        )
        .as_bytes(),
    );

    for (i, (page, img)) in pages.iter().zip(&images).enumerate() {
        let w = page.page_width * UNITS_TO_PT;
        let h = page.page_height * UNITS_TO_PT;
        let id = page_obj(i);

        begin(&mut out, &mut offsets, id);
        out.extend_from_slice(
            format!(
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {w:.2} {h:.2}] /Contents {} 0 R /Resources << /XObject << /Im0 {} 0 R >> >> >>\nendobj\n",
                id + 1,
                id + 2
            )
            .as_bytes(),
        );

        let content = format!("q {w:.2} 0 0 {h:.2} 0 0 cm /Im0 Do Q\n");
        begin(&mut out, &mut offsets, id + 1);
        out.extend_from_slice(
            format!(
                "<< /Length {} >>\nstream\n{content}endstream\nendobj\n",
                content.len()
            )
            .as_bytes(),
        );

        begin(&mut out, &mut offsets, id + 2);
        out.extend_from_slice(
            format!(
                "<< /Type /XObject /Subtype /Image /Width {} /Height {} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length {} >>\nstream\n",
                page.width_px,
                page.height_px,
                img.bytes.len()
            )
            .as_bytes(),
        );
        out.extend_from_slice(&img.bytes);
        out.extend_from_slice(b"\nendstream\nendobj\n");
    }

    let xref = out.len();
    out.extend_from_slice(
        format!("xref\n0 {}\n0000000000 65535 f \n", offsets.len() + 1).as_bytes(),
    );
    for off in &offsets {
        out.extend_from_slice(format!("{off:010} 00000 n \n").as_bytes());
    }
    out.extend_from_slice(
        format!(
            "trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n",
            offsets.len() + 1
        )
        .as_bytes(),
    );
    Ok(out)
}

// ------------------------------------------------------------------ PPTX

/// English Metric Units per Inkleaf unit (914400 EMU per inch / 96).
const EMU_PER_UNIT: f64 = 9525.0;

const NS: &str = r#"xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main""#;
const XML_DECL: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?>"#;
const EMPTY_TREE: &str = r#"<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>"#;
const REL_NS: &str = "http://schemas.openxmlformats.org/package/2006/relationships";
const R_DOC: &str = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

fn rels(entries: &[(&str, &str, String)]) -> String {
    let body: String = entries
        .iter()
        .map(|(id, kind, target)| {
            format!(r#"<Relationship Id="{id}" Type="{kind}" Target="{target}"/>"#)
        })
        .collect();
    format!(r#"{XML_DECL}<Relationships xmlns="{REL_NS}">{body}</Relationships>"#)
}

fn theme_xml() -> String {
    let fill = r#"<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>"#;
    let line = r#"<a:ln w="9525"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln>"#;
    let effect = r#"<a:effectStyle><a:effectLst/></a:effectStyle>"#;
    let color = |name: &str, hex: &str| format!(r#"<a:{name}><a:srgbClr val="{hex}"/></a:{name}>"#);
    format!(
        r#"{XML_DECL}<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Inkleaf"><a:themeElements><a:clrScheme name="Inkleaf"><a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1><a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1>{}{}{}{}{}{}{}{}{}{}</a:clrScheme><a:fontScheme name="Inkleaf"><a:majorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont><a:minorFont><a:latin typeface="Calibri"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont></a:fontScheme><a:fmtScheme name="Inkleaf"><a:fillStyleLst>{fill}{fill}{fill}</a:fillStyleLst><a:lnStyleLst>{line}{line}{line}</a:lnStyleLst><a:effectStyleLst>{effect}{effect}{effect}</a:effectStyleLst><a:bgFillStyleLst>{fill}{fill}{fill}</a:bgFillStyleLst></a:fmtScheme></a:themeElements></a:theme>"#,
        color("dk2", "1F2937"),
        color("lt2", "F3F4F6"),
        color("accent1", "2563EB"),
        color("accent2", "0D9488"),
        color("accent3", "16A34A"),
        color("accent4", "CA8A04"),
        color("accent5", "DC2626"),
        color("accent6", "7C3AED"),
        color("hlink", "2563EB"),
        color("folHlink", "7C3AED"),
    )
}

fn xml_escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

fn build_pptx(pages: &[ExportPage]) -> AppResult<Vec<u8>> {
    let images = pages.iter().map(decode).collect::<AppResult<Vec<_>>>()?;
    // The slide takes the first page's size; every page is fitted inside it.
    let slide_cx = ((pages[0].page_width * EMU_PER_UNIT).round() as i64).clamp(914_400, 51_206_400);
    let slide_cy =
        ((pages[0].page_height * EMU_PER_UNIT).round() as i64).clamp(914_400, 51_206_400);

    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let opts = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    let stored =
        zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored);
    let put = |zip: &mut zip::ZipWriter<std::io::Cursor<Vec<u8>>>,
                   name: &str,
                   data: &[u8],
                   compress: bool|
     -> AppResult<()> {
        zip.start_file(name, if compress { opts } else { stored })
            .map_err(|e| AppError::Invalid(format!("zip error: {e}")))?;
        zip.write_all(data)?;
        Ok(())
    };

    let n = pages.len();
    let mut overrides = String::new();
    for i in 1..=n {
        overrides.push_str(&format!(
            r#"<Override PartName="/ppt/slides/slide{i}.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>"#
        ));
    }
    let content_types = format!(
        r#"{XML_DECL}<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="jpeg" ContentType="image/jpeg"/><Default Extension="png" ContentType="image/png"/><Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/><Override PartName="/ppt/slideMasters/slideMaster1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"/><Override PartName="/ppt/slideLayouts/slideLayout1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"/><Override PartName="/ppt/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/>{overrides}<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/><Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/></Types>"#
    );
    put(
        &mut zip,
        "[Content_Types].xml",
        content_types.as_bytes(),
        true,
    )?;

    put(
        &mut zip,
        "_rels/.rels",
        rels(&[
            ("rId1", &format!("{R_DOC}/officeDocument"), "ppt/presentation.xml".into()),
            ("rId2", "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties", "docProps/core.xml".into()),
            ("rId3", &format!("{R_DOC}/extended-properties"), "docProps/app.xml".into()),
        ])
        .as_bytes(),
        true,
    )?;

    put(
        &mut zip,
        "docProps/core.xml",
        format!(r#"{XML_DECL}<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>{}</dc:title><dc:creator>Inkleaf</dc:creator></cp:coreProperties>"#, xml_escape("Inkleaf notebook")).as_bytes(),
        true,
    )?;
    put(
        &mut zip,
        "docProps/app.xml",
        format!(r#"{XML_DECL}<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties"><Application>Inkleaf</Application><Slides>{n}</Slides></Properties>"#).as_bytes(),
        true,
    )?;

    let slide_ids: String = (0..n)
        .map(|i| format!(r#"<p:sldId id="{}" r:id="rId{}"/>"#, 256 + i, 3 + i))
        .collect();
    put(
        &mut zip,
        "ppt/presentation.xml",
        format!(r#"{XML_DECL}<p:presentation {NS} saveSubsetFonts="1"><p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst><p:sldIdLst>{slide_ids}</p:sldIdLst><p:sldSz cx="{slide_cx}" cy="{slide_cy}"/><p:notesSz cx="6858000" cy="9144000"/></p:presentation>"#).as_bytes(),
        true,
    )?;
    let mut pres_rels = vec![
        (
            "rId1".to_string(),
            format!("{R_DOC}/slideMaster"),
            "slideMasters/slideMaster1.xml".to_string(),
        ),
        (
            "rId2".to_string(),
            format!("{R_DOC}/theme"),
            "theme/theme1.xml".to_string(),
        ),
    ];
    for i in 1..=n {
        pres_rels.push((
            format!("rId{}", i + 2),
            format!("{R_DOC}/slide"),
            format!("slides/slide{i}.xml"),
        ));
    }
    let pres_rels_ref: Vec<(&str, &str, String)> = pres_rels
        .iter()
        .map(|(a, b, c)| (a.as_str(), b.as_str(), c.clone()))
        .collect();
    put(
        &mut zip,
        "ppt/_rels/presentation.xml.rels",
        rels(&pres_rels_ref).as_bytes(),
        true,
    )?;

    put(
        &mut zip,
        "ppt/theme/theme1.xml",
        theme_xml().as_bytes(),
        true,
    )?;

    put(
        &mut zip,
        "ppt/slideMasters/slideMaster1.xml",
        format!(r#"{XML_DECL}<p:sldMaster {NS}><p:cSld><p:bg><p:bgRef idx="1001"><a:schemeClr val="bg1"/></p:bgRef></p:bg><p:spTree>{EMPTY_TREE}</p:spTree></p:cSld><p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/><p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst><p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></p:sldMaster>"#).as_bytes(),
        true,
    )?;
    put(
        &mut zip,
        "ppt/slideMasters/_rels/slideMaster1.xml.rels",
        rels(&[
            (
                "rId1",
                &format!("{R_DOC}/slideLayout"),
                "../slideLayouts/slideLayout1.xml".into(),
            ),
            (
                "rId2",
                &format!("{R_DOC}/theme"),
                "../theme/theme1.xml".into(),
            ),
        ])
        .as_bytes(),
        true,
    )?;
    put(
        &mut zip,
        "ppt/slideLayouts/slideLayout1.xml",
        format!(r#"{XML_DECL}<p:sldLayout {NS} type="blank" preserve="1"><p:cSld name="Blank"><p:spTree>{EMPTY_TREE}</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>"#).as_bytes(),
        true,
    )?;
    put(
        &mut zip,
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels",
        rels(&[(
            "rId1",
            &format!("{R_DOC}/slideMaster"),
            "../slideMasters/slideMaster1.xml".into(),
        )])
        .as_bytes(),
        true,
    )?;

    for (i, (page, img)) in pages.iter().zip(&images).enumerate() {
        let num = i + 1;
        let ext = if img.jpeg { "jpeg" } else { "png" };
        // Fit the page inside the slide, centred.
        let pw = page.page_width * EMU_PER_UNIT;
        let ph = page.page_height * EMU_PER_UNIT;
        let scale = (slide_cx as f64 / pw).min(slide_cy as f64 / ph);
        let (cx, cy) = ((pw * scale).round() as i64, (ph * scale).round() as i64);
        let (x, y) = ((slide_cx - cx) / 2, (slide_cy - cy) / 2);
        put(
            &mut zip,
            &format!("ppt/slides/slide{num}.xml"),
            format!(r#"{XML_DECL}<p:sld {NS}><p:cSld><p:spTree>{EMPTY_TREE}<p:pic><p:nvPicPr><p:cNvPr id="2" name="Page {num}"/><p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr/></p:nvPicPr><p:blipFill><a:blip r:embed="rId2"/><a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>"#).as_bytes(),
            true,
        )?;
        put(
            &mut zip,
            &format!("ppt/slides/_rels/slide{num}.xml.rels"),
            rels(&[
                (
                    "rId1",
                    &format!("{R_DOC}/slideLayout"),
                    "../slideLayouts/slideLayout1.xml".into(),
                ),
                (
                    "rId2",
                    &format!("{R_DOC}/image"),
                    format!("../media/image{num}.{ext}"),
                ),
            ])
            .as_bytes(),
            true,
        )?;
        put(
            &mut zip,
            &format!("ppt/media/image{num}.{ext}"),
            &img.bytes,
            false,
        )?;
    }

    let cursor = zip
        .finish()
        .map_err(|e| AppError::Invalid(format!("zip error: {e}")))?;
    Ok(cursor.into_inner())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;

    // A valid 1x1 baseline JPEG.
    const JPEG_1X1: &str = "/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////wgALCAABAAEBAREA/8QAFBABAAAAAAAAAAAAAAAAAAAAAP/aAAgBAQABPxA=";
    // A valid 1x1 PNG.
    const PNG_1X1: &str = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

    fn page(kind: &str, data: &str) -> ExportPage {
        ExportPage {
            data_url: format!("data:image/{kind};base64,{data}"),
            width_px: 1,
            height_px: 1,
            page_width: 794.0,
            page_height: 1123.0,
        }
    }

    #[test]
    fn pdf_has_one_page_per_image_and_a4_media_box() {
        let pdf = build(
            Format::Pdf,
            &[page("jpeg", JPEG_1X1), page("jpeg", JPEG_1X1)],
        )
        .unwrap();
        let text = String::from_utf8_lossy(&pdf);
        assert!(text.starts_with("%PDF-1.4"));
        assert!(text.contains("/Count 2"));
        assert_eq!(text.matches("/Type /Page ").count(), 2);
        assert!(text.contains("/MediaBox [0 0 595.50 842.25]"));
        assert!(text.trim_end().ends_with("%%EOF"));
        // xref offsets must point at the objects they name (checked on raw
        // bytes: the lossy UTF-8 view shifts offsets after binary data).
        let find = |hay: &[u8], needle: &[u8]| {
            hay.windows(needle.len())
                .rposition(|w| w == needle)
                .unwrap()
        };
        let sx = find(&pdf, b"startxref\n") + b"startxref\n".len();
        let xref_at: usize = std::str::from_utf8(&pdf[sx..])
            .unwrap()
            .lines()
            .next()
            .unwrap()
            .parse()
            .unwrap();
        assert!(pdf[xref_at..].starts_with(b"xref"));
        let table = std::str::from_utf8(&pdf[xref_at..]).unwrap();
        for (i, line) in table.lines().skip(3).take(8).enumerate() {
            let off: usize = line[..10].parse().unwrap();
            assert!(
                pdf[off..].starts_with(format!("{} 0 obj", i + 1).as_bytes()),
                "object {} misplaced",
                i + 1
            );
        }
    }

    #[test]
    fn pdf_rejects_png_pages() {
        assert!(build(Format::Pdf, &[page("png", PNG_1X1)]).is_err());
    }

    #[test]
    fn pptx_contains_required_parts_and_one_slide_per_page() {
        let bytes = build(
            Format::Pptx,
            &[
                page("jpeg", JPEG_1X1),
                page("jpeg", JPEG_1X1),
                page("jpeg", JPEG_1X1),
            ],
        )
        .unwrap();
        let mut archive = zip::ZipArchive::new(std::io::Cursor::new(bytes)).unwrap();
        for name in [
            "[Content_Types].xml",
            "_rels/.rels",
            "ppt/presentation.xml",
            "ppt/slideMasters/slideMaster1.xml",
            "ppt/slideLayouts/slideLayout1.xml",
            "ppt/theme/theme1.xml",
            "ppt/slides/slide3.xml",
            "ppt/media/image3.jpeg",
        ] {
            assert!(archive.by_name(name).is_ok(), "missing {name}");
        }
        let mut pres = String::new();
        archive
            .by_name("ppt/presentation.xml")
            .unwrap()
            .read_to_string(&mut pres)
            .unwrap();
        assert_eq!(pres.matches("<p:sldId ").count(), 3);
        assert!(pres.contains(r#"<p:sldSz cx="7562850" cy="10696575"/>"#));
    }

    #[test]
    fn png_export_passes_bytes_through() {
        let bytes = build(Format::Png, &[page("png", PNG_1X1)]).unwrap();
        assert_eq!(&bytes[1..4], b"PNG");
        assert!(build(Format::Png, &[page("jpeg", JPEG_1X1)]).is_err());
        assert!(build(Format::Png, &[]).is_err());
    }

    #[test]
    fn rejects_bad_input() {
        assert!(Format::parse("docx").is_err());
        let mut p = page("jpeg", JPEG_1X1);
        p.data_url = "data:text/html;base64,AAAA".into();
        assert!(build(Format::Pdf, &[p]).is_err());
    }
}

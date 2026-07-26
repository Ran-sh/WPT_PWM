/**
 * generate_docx.js — WPT V5.1.3 Markdown → 专业排版 .docx 转换器
 * 用法: node tools/generate_docx.js <md文件>
 */

const fs = require("fs");
const path = require("path");
const {
  Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
  Header, Footer, AlignmentType, HeadingLevel, BorderStyle,
  WidthType, ShadingType, PageNumber, TableOfContents
} = require("docx");

// ── 命令行参数 ──
const args = process.argv.slice(2);
const mdFiles = args.length > 0 ? args : [];
if (mdFiles.length === 0) {
  console.log("用法: node tools/generate_docx.js <文件1.md> [文件2.md ...]");
  process.exit(1);
}

// ── 字体与颜色常量 ──
const FONT_TITLE  = "Microsoft YaHei";
const FONT_BODY   = "SimSun";
const FONT_CODE   = "Consolas";
const CLR_TITLE   = "1A1A2E";
const CLR_H2      = "2B579A";
const CLR_H3      = "2B579A";
const CLR_H4      = "3A6EA5";
const CLR_BODY    = "333333";
const CLR_CODE_BG = "F5F5F5";
const CLR_TBL_HDR = "D5E8F0";
const CLR_BORDER  = "CCCCCC";

// A4 + 2.5cm margins
const PAGE_W = 11906;
const PAGE_H = 16838;
const MARGIN = 1417;
const CONTENT_W = PAGE_W - 2 * MARGIN;

// ── 公共辅助 ──
function emptyPara() { return new Paragraph({ children: [] }); }

function codePara(text) {
  return new Paragraph({
    spacing: { line: 240 },
    shading: { fill: CLR_CODE_BG, type: ShadingType.CLEAR },
    indent: { left: 360 },
    children: [new TextRun({ text, font: FONT_CODE, size: 19, color: "2D2D2D" })]
  });
}

function h2Para(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_2,
    spacing: { before: 240, after: 120 },
    children: [new TextRun({ text, font: FONT_TITLE, size: 32, color: CLR_H2, bold: true })]
  });
}

function h3Para(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_3,
    spacing: { before: 160, after: 80 },
    children: [new TextRun({ text, font: FONT_TITLE, size: 28, color: CLR_H2, bold: true })]
  });
}

function h4Para(text) {
  return new Paragraph({
    heading: HeadingLevel.HEADING_4,
    spacing: { before: 120, after: 60 },
    children: [new TextRun({ text, font: FONT_TITLE, size: 24, color: CLR_H4, bold: true })]
  });
}

function makeTable(headers, rows) {
  const border = { style: BorderStyle.SINGLE, size: 1, color: CLR_BORDER };
  const borders = { top: border, bottom: border, left: border, right: border };
  const nCols = headers.length;
  const colW = Math.floor(CONTENT_W / nCols);

  const headerRow = new TableRow({
    children: headers.map(h =>
      new TableCell({
        borders, width: { size: colW, type: WidthType.DXA },
        shading: { fill: CLR_TBL_HDR, type: ShadingType.CLEAR },
        margins: { top: 60, bottom: 60, left: 100, right: 100 },
        children: [new Paragraph({
          children: [new TextRun({ text: h, font: FONT_BODY, size: 20, color: CLR_BODY, bold: true })]
        })]
      })
    )
  });

  const dataRows = rows.map(row =>
    new TableRow({
      children: row.map(cell =>
        new TableCell({
          borders, width: { size: colW, type: WidthType.DXA },
          margins: { top: 60, bottom: 60, left: 100, right: 100 },
          children: [new Paragraph({
            spacing: { line: 288 },
            children: [new TextRun({ text: cell, font: FONT_BODY, size: 20, color: CLR_BODY })]
          })]
        })
      )
    })
  );

  return new Table({
    width: { size: CONTENT_W, type: WidthType.DXA },
    columnWidths: Array(nCols).fill(colW),
    rows: [headerRow, ...dataRows]
  });
}

// ── Markdown 解析器 ──
function parseMarkdown(md) {
  const lines = md.split("\n");
  const elements = [];
  let i = 0, inCode = false;

  while (i < lines.length) {
    const line = lines[i];
    const trimmed = line.trim();

    if (trimmed.startsWith("```")) { inCode = !inCode; i++; continue; }
    if (inCode) { elements.push({ type: "code", text: line }); i++; continue; }
    if (trimmed === "---") { elements.push({ type: "hr" }); i++; continue; }
    if (trimmed.startsWith("> ")) { elements.push({ type: "blockquote", text: trimmed.substring(2) }); i++; continue; }

    if (trimmed.startsWith("|")) {
      const tableLines = [];
      while (i < lines.length && lines[i].trim().startsWith("|")) { tableLines.push(lines[i].trim()); i++; }
      const headerLine = tableLines[0], sepLine = tableLines[1], dataLines = tableLines.slice(2);
      if (sepLine && sepLine.includes("---")) {
        const headers = headerLine.split("|").filter(c => c.trim()).map(c => c.trim());
        const rows = dataLines.map(rl => rl.split("|").filter(c => c.trim()).map(c => c.trim()));
        elements.push({ type: "table", headers, rows });
      }
      continue;
    }

    if (trimmed.startsWith("### ")) { elements.push({ type: "h3", text: trimmed.substring(4).replace(/\*\*/g, "") }); i++; continue; }
    if (trimmed.startsWith("## ")) { elements.push({ type: "h2", text: trimmed.substring(3).replace(/\*\*/g, "") }); i++; continue; }
    if (trimmed.startsWith("# ")) { elements.push({ type: "h1", text: trimmed.substring(2).replace(/\*\*/g, "") }); i++; continue; }

    if (trimmed.startsWith("**") && trimmed.endsWith("**")) { elements.push({ type: "bold", text: trimmed.replace(/\*\*/g, "") }); i++; continue; }
    if (trimmed === "") { elements.push({ type: "empty" }); i++; continue; }
    if (trimmed.startsWith("```mermaid")) { inCode = true; i++; continue; }

    elements.push({ type: "text", text: line });
    i++;
  }
  return elements;
}

// ── 元素 → Paragraph[] ──
function elementsToParagraphs(elements) {
  const p = [];
  for (const el of elements) {
    switch (el.type) {
      case "empty": p.push(emptyPara()); break;
      case "hr":
        p.push(new Paragraph({ border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: "CCCCCC", space: 1 } }, children: [] }));
        break;
      case "h1":
        p.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 360 },
          children: [new TextRun({ text: el.text, font: FONT_TITLE, size: 44, color: CLR_TITLE, bold: true })] }));
        break;
      case "h2": p.push(h2Para(el.text)); break;
      case "h3": p.push(h3Para(el.text)); break;
      case "h4": p.push(h4Para(el.text)); break;
      case "bold":
        p.push(new Paragraph({ spacing: { line: 360 },
          children: [new TextRun({ text: el.text, font: FONT_BODY, size: 22, color: CLR_BODY, bold: true })] }));
        break;
      case "blockquote":
        p.push(new Paragraph({ spacing: { line: 360 },
          border: { left: { style: BorderStyle.SINGLE, size: 12, color: "2B579A", space: 8 } },
          indent: { left: 360 },
          children: [new TextRun({ text: el.text, font: FONT_BODY, size: 20, color: "555555", italics: true })] }));
        break;
      case "code": p.push(codePara(el.text)); break;
      case "table": p.push(emptyPara(), makeTable(el.headers, el.rows), emptyPara()); break;
      case "text": {
        const parts = el.text.split(/(\*\*.*?\*\*)/g);
        const runs = parts.map(pt => {
          if (pt.startsWith("**") && pt.endsWith("**"))
            return new TextRun({ text: pt.slice(2, -2), font: FONT_BODY, size: 22, color: CLR_BODY, bold: true });
          return new TextRun({ text: pt, font: FONT_BODY, size: 22, color: CLR_BODY });
        });
        p.push(new Paragraph({ spacing: { line: 360 }, children: runs }));
        break;
      }
    }
  }
  return p;
}

// ── 单文件转换 ──
function convertMdToDocx(mdPath) {
  console.log(`处理: ${mdPath}`);
  const mdContent = fs.readFileSync(mdPath, "utf-8");
  const allElements = parseMarkdown(mdContent);

  // 提取封面: h1 标题 + 紧随的 blockquote
  const coverBlocks = [];
  let titleText = "文档";
  let metaLines = [];
  for (const el of allElements) {
    if (el.type === "h1") { titleText = el.text; coverBlocks.push(el); }
    else if (el.type === "blockquote") { metaLines.push(el.text); coverBlocks.push(el); }
    else if (el.type === "hr" || el.type === "empty") { /* skip in cover */ }
    else { break; }
  }

  // 正文从第一个 h2 开始
  let bodyStartIdx = 0;
  for (let i = 0; i < allElements.length; i++) {
    if (allElements[i].type === "h2") { bodyStartIdx = i; break; }
  }
  const bodyElements = allElements.slice(bodyStartIdx);
  const docTitle = titleText;
  const versionMatch = mdContent.match(/\|\s*\*\*(?:文档)?版本\*\*\s*\|\s*(V\d+\.\d+\.\d+)\s*\|/);
  const dateMatch = mdContent.match(/\|\s*\*\*最后更新\*\*\s*\|\s*(\d{4})-(\d{2})-\d{2}\s*\|/);
  const coverVersion = versionMatch ? versionMatch[1] : "";
  const coverDate = dateMatch ? `${dateMatch[1]} 年 ${Number(dateMatch[2])} 月` : "";

  // 公共属性
  const pageProps = {
    page: { size: { width: PAGE_W, height: PAGE_H }, margin: { top: MARGIN, right: MARGIN, bottom: MARGIN, left: MARGIN } }
  };
  const sharedHeader = {
    default: new Header({ children: [new Paragraph({ alignment: AlignmentType.RIGHT,
      children: [new TextRun({ text: `${docTitle} — 技术文档`, font: FONT_TITLE, size: 16, color: "999999", italics: true })] })] })
  };
  const pageFooter = {
    default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
      children: [
        new TextRun({ text: "— ", font: FONT_BODY, size: 18, color: "999999" }),
        new TextRun({ children: [PageNumber.CURRENT], font: FONT_BODY, size: 18, color: "999999" }),
        new TextRun({ text: " —", font: FONT_BODY, size: 18, color: "999999" })
      ] })] })
  };
  const coverFooter = {
    default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER, children: [] })] })
  };

  // 封面子元素
  const coverChildren = [];
  coverChildren.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 3600, after: 600 },
    children: [new TextRun({ text: docTitle, font: FONT_TITLE, size: 44, color: CLR_TITLE, bold: true })] }));
  coverChildren.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 2400 },
    children: [new TextRun({ text: "技术与联调文档", font: FONT_TITLE, size: 36, color: CLR_H2, bold: true })] }));

  if (coverVersion) {
    coverChildren.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 360 },
      children: [new TextRun({ text: `版本 ${coverVersion}`, font: FONT_BODY, size: 24, color: CLR_BODY, bold: true })] }));
  }

  metaLines.forEach(m => {
    coverChildren.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { line: 420 },
      children: [new TextRun({ text: m, font: FONT_BODY, size: 22, color: CLR_BODY })] }));
  });
  if (coverDate) {
    coverChildren.push(new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 1200 },
      children: [new TextRun({ text: coverDate, font: FONT_BODY, size: 22, color: "888888" })] }));
  }

  const doc = new Document({
    styles: {
      default: { document: { run: { font: FONT_BODY, size: 22 } } },
      paragraphStyles: [
        { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
          run: { size: 32, bold: true, font: FONT_TITLE, color: CLR_H2 },
          paragraph: { spacing: { before: 240, after: 120 }, outlineLevel: 1 } },
        { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
          run: { size: 28, bold: true, font: FONT_TITLE, color: CLR_H2 },
          paragraph: { spacing: { before: 160, after: 80 }, outlineLevel: 2 } },
        { id: "Heading4", name: "Heading 4", basedOn: "Normal", next: "Normal", quickFormat: true,
          run: { size: 24, bold: true, font: FONT_TITLE, color: CLR_H4 },
          paragraph: { spacing: { before: 120, after: 60 }, outlineLevel: 3 } }
      ]
    },
    sections: [
      // 第 1 节: 封面
      { properties: pageProps, footers: coverFooter, children: coverChildren },
      // 第 2 节: 目录
      { properties: { ...pageProps, page: { ...pageProps.page, pageNumbers: { formatType: "upperRoman" } } },
        headers: sharedHeader, footers: pageFooter,
        children: [
          new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { after: 360 },
            children: [new TextRun({ text: "目  录", font: FONT_TITLE, size: 32, color: CLR_H2, bold: true })] }),
          new TableOfContents("目录", { hyperlink: true, headingStyleRange: "1-3" })
        ]
      },
      // 第 3 节: 正文
      { properties: { ...pageProps, page: { ...pageProps.page, pageNumbers: { start: 1 } } },
        headers: sharedHeader, footers: pageFooter,
        children: [
          new Paragraph({ pageBreakBefore: true, children: [] }),
          ...elementsToParagraphs(bodyElements)
        ]
      }
    ]
  });

  const outPath = mdPath.replace(/\.md$/, ".docx");
  Packer.toBuffer(doc).then(buffer => {
    fs.writeFileSync(outPath, buffer);
    console.log(`  → ${outPath} (${(buffer.length / 1024).toFixed(0)} KB)`);
  });
}

// ── 批量执行 ──
async function main() {
  // 确保 docx 模块可用
  console.log("生成 .docx 文件...\n");
  for (const f of mdFiles) {
    convertMdToDocx(f);
  }
  // 等待所有异步完成
  await new Promise(r => setTimeout(r, 2000));
  console.log("\n全部完成。");
}
main();

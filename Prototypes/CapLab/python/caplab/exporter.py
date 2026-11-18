"""Atomic project data exports; physics and graph rendering stay in their existing layers."""
import csv
import json
import os
from pathlib import Path
import tempfile


def cells(value, path=""):
    """Lossless JSON-pointer rows, including empty containers and explicit nulls."""
    if isinstance(value, dict) and value:
        for key, child in value.items():
            yield from cells(child, path+"/"+key.replace("~", "~0").replace("/", "~1"))
    elif isinstance(value, list) and value:
        for i, child in enumerate(value):
            yield from cells(child, path+"/"+str(i))
    else:
        yield path, json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def export_json(data_json, destination, cancelled=lambda: False):
    """CSV/XLSX include values, explicit-unit field names, models and full provenance.

    CSV uses JSON-pointer/value_json rows. XLSX adds normal measurement tables
    with names/units and stable row IDs; Metadata contains the same lossless rows.
    """
    data = json.loads(data_json)
    path = Path(destination)
    format = path.suffix.lower()
    if format not in (".csv", ".xlsx"):
        raise ValueError("Choose CSV or XLSX data export")
    rows = list(cells(data))
    if cancelled():
        raise RuntimeError("Export cancelled")
    fd, temporary = tempfile.mkstemp(prefix=".caplab-", suffix=format, dir=path.parent)
    os.close(fd)
    try:
        if format == ".csv":
            with open(temporary, "w", encoding="utf-8", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["path", "value_json"])
                for row in rows:
                    if cancelled():
                        raise RuntimeError("Export cancelled")
                    writer.writerow(row)
        else:
            # OOXML inline strings need only the standard library; no optional Excel engine.
            from xml.etree.ElementTree import Element, SubElement, tostring
            from zipfile import ZipFile, ZIP_DEFLATED
            ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
            rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
            sheets = [("Metadata", [["path", "value_json"]] + rows)]
            for m in data["measurement_data"]:
                header = ["row_id", "source_row"] + [f"{n} [{u}]" for n, u in zip(m["names"], m["units"])]
                values = [[str(rid), m["source_rows"][i]] + [c[i] for c in m["columns"]] for i, rid in enumerate(m["row_ids"])]
                sheets.append(("M " + str(m["id"]), [header] + values))
            workbook = Element("workbook", xmlns=ns)
            names = SubElement(workbook, "sheets")
            relationships = Element("Relationships", xmlns="http://schemas.openxmlformats.org/package/2006/relationships")
            content = Element("Types", xmlns="http://schemas.openxmlformats.org/package/2006/content-types")
            SubElement(content, "Default", Extension="rels", ContentType="application/vnd.openxmlformats-package.relationships+xml")
            SubElement(content, "Default", Extension="xml", ContentType="application/xml")
            SubElement(content, "Override", PartName="/xl/workbook.xml", ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml")
            with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
                for index, (name, values) in enumerate(sheets, 1):
                    if len(values) > 1048576 or any(len(row) > 16384 for row in values):
                        raise ValueError("XLSX sheet limit exceeded; export CSV instead")
                    SubElement(names, "sheet", name=name, sheetId=str(index), attrib={"{"+rel+"}id": "rId"+str(index)})
                    SubElement(relationships, "Relationship", Id="rId"+str(index), Type=rel+"/worksheet", Target=f"worksheets/sheet{index}.xml")
                    SubElement(content, "Override", PartName=f"/xl/worksheets/sheet{index}.xml", ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")
                    sheet = Element("worksheet", xmlns=ns)
                    body = SubElement(sheet, "sheetData")
                    for row in values:
                        if cancelled():
                            raise RuntimeError("Export cancelled")
                        node = SubElement(body, "row")
                        for value in row:
                            cell = SubElement(node, "c")
                            if value is None:
                                continue
                            if isinstance(value, (int, float)) and not isinstance(value, bool):
                                SubElement(cell, "v").text = str(value)
                            else:
                                text = str(value)
                                if len(text) > 32767 or any(ord(c) < 32 and c not in "\t\n\r" for c in text):
                                    raise ValueError("Text cannot be represented in XLSX; export CSV instead")
                                cell.set("t", "inlineStr")
                                SubElement(SubElement(cell, "is"), "t", attrib={"{http://www.w3.org/XML/1998/namespace}space": "preserve"}).text = text
                    archive.writestr(f"xl/worksheets/sheet{index}.xml", tostring(sheet, encoding="utf-8", xml_declaration=True))
                archive.writestr("xl/workbook.xml", tostring(workbook, encoding="utf-8", xml_declaration=True))
                archive.writestr("xl/_rels/workbook.xml.rels", tostring(relationships, encoding="utf-8", xml_declaration=True))
                archive.writestr("[Content_Types].xml", tostring(content, encoding="utf-8", xml_declaration=True))
                root = Element("Relationships", xmlns="http://schemas.openxmlformats.org/package/2006/relationships")
                SubElement(root, "Relationship", Id="rId1", Type=rel+"/officeDocument", Target="xl/workbook.xml")
                archive.writestr("_rels/.rels", tostring(root, encoding="utf-8", xml_declaration=True))
        if cancelled():
            raise RuntimeError("Export cancelled")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return json.dumps(dict(format=format[1:], path=str(path), metadata_rows=len(rows)))

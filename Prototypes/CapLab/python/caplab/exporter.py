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



def lab_results(data):
    """Tidy lab table: one measured/fitted quantity per row, with stable IDs and units."""
    header = ["analysis_id", "analysis_type", "model", "status", "sample_id", "sample_name", "section", "row_id", "quantity", "value", "unit"]
    rows = [header]
    units = dict(V_b_V="V", N_m3="m^-3", E_F_eV="eV", E_a_eV="eV", sigma_cm2="cm^2",
                 x_e_m="m", N_DL_m3="m^-3", W_m="m", density_cm3="cm^-3", density_cm3_eV="cm^-3 eV^-1",
                 frequency_Hz="Hz", temperature_K="K", bias_V="V", energy_eV="eV", capacitance_F="F",
                 processed_capacitance_F="F", D_F="F", raw_D_F="F", G_over_omega_F="F", D_c_F_m2="F m^-2",
                 measured_ReZ_Ohm="Ohm", measured_ImZ_Ohm="Ohm", model_ReZ_Ohm="Ohm", model_ImZ_Ohm="Ohm",
                 residual_ReZ_Ohm="Ohm", residual_ImZ_Ohm="Ohm")
    samples = {sample["id"]: sample["name"] for sample in data.get("samples", [])}
    for analysis in data.get("analyses", []):
        sample_id = analysis.get("sample_id")
        if sample_id is None and analysis.get("inputs"): sample_id = analysis["inputs"][0].get("sample_id")
        identity = [str(analysis["id"]), analysis["type"], analysis["model"], analysis["status"],
                    str(sample_id) if sample_id is not None else "", samples.get(sample_id, "")]
        def record(values, section, index=""):
            for key, value in values.items():
                if key in units and not isinstance(value, (dict, list)):
                    rows.append(identity + [section, str(index), key, value, units[key]])
            if "prefactor" in values:
                rows.append(identity + [section, str(index), "prefactor", values["prefactor"], values["prefactor_unit"]])
            for i, value in enumerate(values.get("coefficients", [])):
                rows.append(identity + [section, str(index), "C"+str(i), value, values["coefficient_units"][i]])
            arrays = {k: v for k, v in values.items() if k in units and isinstance(v, list)}
            for key, values_list in arrays.items():
                ids = values.get("row_ids", list(range(1, len(values_list)+1)))
                for i, (row_id, value) in enumerate(zip(ids, values_list)):
                    quality = values.get("quality", [])
                    # Null values remain blank; status/quality never turns an invalid result into zero.
                    row_section = section + (" / " + str(quality[i]) if quality else "")
                    rows.append(identity + [row_section, str(row_id), key, value, units[key]])
        record(analysis, "summary")
        profile = dict(analysis.get("profile", {}))
        if "selected_row_ids" in analysis: profile["row_ids"] = analysis["selected_row_ids"]
        record(profile, "profile")
        for section in ("groups", "fits", "spectra", "candidates"):
            for i, value in enumerate(analysis.get(section, []), 1):
                label = section + (" / " + value["model"] if "model" in value else "")
                if "status" in value: label += " / " + value["status"]
                record(value, label, value.get("row_id",i))
                record(value.get("integral", {}), label+" / integral", i)
        for value in analysis.get("parameters", []):
            rows.append(identity + ["parameters", value["element"]+"."+value["field"], "fitted", value["fitted"], value["unit"]])
    return rows

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
    results = lab_results(data)
    readable = path.with_suffix(".results.csv") if format == ".csv" else None
    result_temporary = None
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
            sheets.append(("Results", results))
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
        from sst_plot_export.files import commit_files
        files = [(Path(temporary), path)]
        if readable is not None:
            fd, result_temporary = tempfile.mkstemp(prefix=".caplab-results-", suffix=".csv", dir=path.parent)
            with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
                writer = csv.writer(stream)
                for row in results:
                    if cancelled(): raise RuntimeError("Export cancelled")
                    writer.writerow([("'"+v if isinstance(v,str) and v.startswith(("=","+","-","@","\t","\r")) else v) for v in row])
                stream.flush(); os.fsync(stream.fileno())
            files.append((Path(result_temporary), readable))
        commit_files(files, cancelled)
    finally:
        if result_temporary is not None and os.path.exists(result_temporary):
            os.unlink(result_temporary)
        if os.path.exists(temporary):
            os.unlink(temporary)
    return json.dumps(dict(format=format[1:], path=str(path), metadata_rows=len(rows), result_rows=len(results)-1,
                           results_path=str(readable) if readable is not None else str(path)+"#Results"))

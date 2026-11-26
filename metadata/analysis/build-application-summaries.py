#!/usr/bin/env python3
"""Export application JSON reports to two chart-free Excel workbooks.

The runtime and peak-memory workbook has one sheet per algorithm and representation.
Each graph has five adjacent (algorithm seconds, process peak RSS bytes) pairs.
MVM sheets also identify partial or early-complete runs and record their
completed sample counts and stopping reasons.
The solution workbook keeps the scalar matching and weighted results separate.
MVM weights are listed for each run because the randomized results can differ.
For WebGraph and Zuckerli MVM, run columns 1–5 mean sample seeds 1–5.
The artifact includes the report selected for each seed.
Other run columns follow Slurm job ID order.

From the artifact root, run ``python3 metadata/analysis/build-application-summaries.py``.
The writer uses only Python's standard library.
"""

import json
from collections import defaultdict
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile


ARTIFACT_ROOT = Path(__file__).resolve().parents[2]
REPORTS_ROOT = ARTIFACT_ROOT / "data" / "evals_app" / "run-reports"
SUMMARIES_DIR = REPORTS_ROOT.parent / "summaries"
ALGORITHMS = ("matching", "mvm", "bfs", "dfs", "wvc")
REPORT_DIRECTORIES = {
    "cp_plain": REPORTS_ROOT / "cp_algo" / "plain",
    "cp_encoded": REPORTS_ROOT / "cp_algo" / "vbyte",
    "wg": REPORTS_ROOT / "wg_algo",
    "zk": REPORTS_ROOT / "zk_algo",
}
REPRESENTATIONS = tuple(REPORT_DIRECTORIES)
RESULTS = {
    "matching": ("matching_size", False),
    "wvc": ("selected_weight_sum", True),
    "mvm": ("matched_vertex_weight_sum", True),
}
MVM_SEEDS = tuple(range(1, 6))

MAIN = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
DOC_REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG_REL = "http://schemas.openxmlformats.org/package/2006/relationships"
CONTENT = "http://schemas.openxmlformats.org/package/2006/content-types"

ET.register_namespace("", MAIN)
ET.register_namespace("r", DOC_REL)


def tag(namespace, name):
    return "{" + namespace + "}" + name


def xml_bytes(element):
    return ET.tostring(element, encoding="utf-8", xml_declaration=True)


def column_name(number):
    name = ""
    while number:
        number, digit = divmod(number - 1, 26)
        name = chr(ord("A") + digit) + name
    return name


def worksheet(rows):
    root = ET.Element(tag(MAIN, "worksheet"))
    last = column_name(len(rows[0])) + str(len(rows))
    ET.SubElement(root, tag(MAIN, "dimension"), ref="A1:" + last)

    views = ET.SubElement(root, tag(MAIN, "sheetViews"))
    view = ET.SubElement(views, tag(MAIN, "sheetView"), workbookViewId="0")
    ET.SubElement(view, tag(MAIN, "pane"), ySplit="1", topLeftCell="A2",
                  activePane="bottomLeft", state="frozen")

    columns = ET.SubElement(root, tag(MAIN, "cols"))
    ET.SubElement(columns, tag(MAIN, "col"), min="1", max="1",
                  width="26", customWidth="1")
    if len(rows[0]) > 1:
        ET.SubElement(columns, tag(MAIN, "col"), min="2",
                      max=str(len(rows[0])), width="20", customWidth="1")

    data = ET.SubElement(root, tag(MAIN, "sheetData"))
    for row_number, values in enumerate(rows, start=1):
        row = ET.SubElement(data, tag(MAIN, "row"), r=str(row_number))
        for column_number, value in enumerate(values, start=1):
            if value is None:
                continue
            address = column_name(column_number) + str(row_number)
            if isinstance(value, str):
                cell = ET.SubElement(row, tag(MAIN, "c"), r=address,
                                     t="inlineStr")
                ET.SubElement(ET.SubElement(cell, tag(MAIN, "is")),
                              tag(MAIN, "t")).text = value
            else:
                cell = ET.SubElement(row, tag(MAIN, "c"), r=address)
                ET.SubElement(cell, tag(MAIN, "v")).text = str(value)
    ET.SubElement(root, tag(MAIN, "autoFilter"), ref="A1:" + last)
    return xml_bytes(root)


def workbook_file(path, sheets):
    """Write a minimal Office Open XML workbook without Excel dependencies."""
    workbook = ET.Element(tag(MAIN, "workbook"))
    sheet_list = ET.SubElement(workbook, tag(MAIN, "sheets"))
    relationships = ET.Element(tag(PKG_REL, "Relationships"))
    types = ET.Element(tag(CONTENT, "Types"))
    ET.SubElement(types, tag(CONTENT, "Default"), Extension="rels",
                  ContentType="application/vnd.openxmlformats-package.relationships+xml")
    ET.SubElement(types, tag(CONTENT, "Default"), Extension="xml",
                  ContentType="application/xml")
    ET.SubElement(types, tag(CONTENT, "Override"), PartName="/xl/workbook.xml",
                  ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml")

    package_rels = ET.Element(tag(PKG_REL, "Relationships"))
    ET.SubElement(package_rels, tag(PKG_REL, "Relationship"), Id="rId1",
                  Type=DOC_REL + "/officeDocument", Target="xl/workbook.xml")

    temporary = path.with_suffix(path.suffix + ".writing")
    with ZipFile(temporary, "w", ZIP_DEFLATED) as archive:
        for index, (name, rows) in enumerate(sheets, start=1):
            if len(name) > 31:
                raise ValueError("Excel sheet name is too long: " + name)
            relation_id = "rId" + str(index)
            ET.SubElement(sheet_list, tag(MAIN, "sheet"), name=name,
                          sheetId=str(index), **{tag(DOC_REL, "id"): relation_id})
            ET.SubElement(relationships, tag(PKG_REL, "Relationship"),
                          Id=relation_id, Type=DOC_REL + "/worksheet",
                          Target="worksheets/sheet" + str(index) + ".xml")
            ET.SubElement(types, tag(CONTENT, "Override"),
                          PartName="/xl/worksheets/sheet" + str(index) + ".xml",
                          ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")
            archive.writestr("xl/worksheets/sheet" + str(index) + ".xml",
                             worksheet(rows))
        archive.writestr("[Content_Types].xml", xml_bytes(types))
        archive.writestr("_rels/.rels", xml_bytes(package_rels))
        archive.writestr("xl/workbook.xml", xml_bytes(workbook))
        archive.writestr("xl/_rels/workbook.xml.rels", xml_bytes(relationships))
    temporary.replace(path)


def mvm_execution(representation, report):
    """Return completion data; CP uses rounds rather than RAMA samples."""
    if representation.startswith("cp_"):
        return "complete", None, None
    execution = report.get("mvm_execution", {})
    status = execution.get("status")
    samples = execution.get("samples_completed")
    target = report.get("parameters", {}).get("mvm_sample_limit")
    if status not in ("complete", "time_limit_partial") or type(samples) is not int:
        raise ValueError("Missing MVM completion data")
    if type(target) is not int or not 0 <= samples <= target:
        raise ValueError("Invalid MVM sample count")
    # Reports written before all-matched stopping did not record a reason.
    reason = execution.get("stop_reason")
    if reason is None:
        reason = "sample_limit" if status == "complete" else "slurm_warning"
    valid = ((reason == "sample_limit" and status == "complete" and samples == target)
             or (reason == "all_vertices_matched" and status == "complete")
             or (reason == "slurm_warning" and status == "time_limit_partial"
                 and samples < target))
    if not valid:
        raise ValueError("MVM stopping reason disagrees with status or sample count")
    return status, samples, reason


def select_mvm_runs(representation, key, runs):
    """Keep one report per fixed seed, preferring a completed retry."""
    by_seed = defaultdict(list)
    for entry in runs:
        report = entry[2]
        seed = report.get("parameters", {}).get("mvm_seed")
        if type(seed) is not int or seed not in MVM_SEEDS:
            raise ValueError("Invalid MVM seed for " + str((representation, key)))
        status, samples, _ = mvm_execution(representation, report)
        by_seed[seed].append((status, samples, entry))

    selected = []
    for seed in MVM_SEEDS:
        candidates = by_seed.get(seed, [])
        completed = [item for item in candidates if item[0] == "complete"]
        if completed:
            weights = {item[2][2]["result"]["matched_vertex_weight_sum"]
                       for item in completed}
            if len(weights) != 1:
                raise ValueError("Completed MVM retries disagree for " + str((representation, key, seed)))
            # Keep the first completed attempt; later copies remain in JSON.
            chosen = min(completed, key=lambda item: (int(item[2][0]), item[2][1]))
        elif candidates:
            # A later retry may have made more progress before its deadline.
            chosen = max(candidates, key=lambda item: (item[1], int(item[2][0]), item[2][1]))
        else:
            continue
        selected.append(chosen[2])
    return selected


def workbook_runs(representation, algorithm, graph, groups):
    """Return five positions, including gaps for missing runs or seeds."""
    runs = groups.get((algorithm, graph), [])
    if algorithm == "mvm" and representation in ("wg", "zk"):
        by_seed = {entry[2]["parameters"]["mvm_seed"]: entry for entry in runs}
        return [by_seed.get(seed) for seed in MVM_SEEDS]
    return runs + [None] * (5 - len(runs))


def load_reports(graphs):
    expected_graphs = set(graphs)
    reports = {}
    graph_sizes = {}
    job_ids = set()
    for representation, directory in REPORT_DIRECTORIES.items():
        groups = defaultdict(list)
        for path in sorted(directory.glob("*.json")):
            report = json.loads(path.read_text(encoding="utf-8"))
            algorithm = report["algorithm"]
            graph = report["input"]["graph_name"]
            if algorithm not in ALGORITHMS or graph not in expected_graphs:
                raise ValueError("Unexpected algorithm or graph in " + str(path))
            if representation.startswith("cp_"):
                if report["cover_mode"] != representation[3:]:
                    raise ValueError("Cover mode differs from folder in " + str(path))
            elif report["representation"] != {"wg": "webgraph", "zk": "zuckerli"}[representation]:
                raise ValueError("Representation differs from folder in " + str(path))

            size = (report["input"]["n"], report["input"]["m"])
            if graph in graph_sizes and graph_sizes[graph] != size:
                raise ValueError("Graph size differs across reports: " + graph)
            graph_sizes[graph] = size
            seconds = report["timing_seconds"]["algorithm"]
            rss = report["memory"]["process_peak_rss_bytes"]
            if seconds <= 0 or rss <= 0:
                raise ValueError("Missing positive time or RSS in " + str(path))
            job_id = report["slurm_job_id"]
            if job_id in job_ids:
                raise ValueError("Repeated Slurm job ID: " + str(job_id))
            job_ids.add(job_id)
            groups[algorithm, graph].append((job_id, path.name, report))
        for key, runs in groups.items():
            runs.sort(key=lambda entry: (int(entry[0]), entry[1]))
            if key[0] == "mvm" and representation in ("wg", "zk"):
                groups[key] = select_mvm_runs(representation, key, runs)
                runs = groups[key]
            elif len(runs) > 5:
                raise ValueError("More than five runs for " + str((representation, key)))
            if key[0] in RESULTS:
                result_key, _ = RESULTS[key[0]]
                values = {run[2]["result"][result_key] for run in runs}
                variable_mvm = key[0] == "mvm" and representation in ("wg", "zk")
                if len(values) != 1 and not variable_mvm:
                    raise ValueError("Result varies across repetitions for " + str((representation, key)))
        reports[representation] = groups
    return reports


def performance_sheets(graphs, reports):
    for algorithm in ALGORITHMS:
        for representation in REPRESENTATIONS:
            groups = reports[representation]
            if not any((algorithm, graph) in groups for graph in graphs):
                continue
            sampled_mvm = algorithm == "mvm" and representation in ("wg", "zk")
            header = ["graph"]
            for run in range(1, 6):
                label = f"seed{run}" if sampled_mvm else str(run)
                header.extend((f"algorithm_seconds_{label}", f"process_peak_rss_bytes_{label}"))
                if sampled_mvm:
                    header.extend((f"status_{label}", f"samples_completed_{label}",
                                   f"stop_reason_{label}"))
            rows = [header]
            for graph in graphs:
                row = [graph]
                for entry in workbook_runs(representation, algorithm, graph, groups):
                    if entry is None:
                        row.extend([None] * (5 if sampled_mvm else 2))
                        continue
                    report = entry[2]
                    row.extend((report["timing_seconds"]["algorithm"],
                                report["memory"]["process_peak_rss_bytes"]))
                    if sampled_mvm:
                        row.extend(mvm_execution(representation, report))
                rows.append(row)
            yield algorithm + "_" + representation, rows


def result_sheets(graphs, reports):
    for algorithm, (key, as_text) in RESULTS.items():
        if algorithm == "mvm":
            for representation in REPRESENTATIONS:
                groups = reports[representation]
                if not any((algorithm, graph) in groups for graph in graphs):
                    continue
                sampled_mvm = representation in ("wg", "zk")
                header = ["graph"]
                for run in range(1, 6):
                    label = f"seed{run}" if sampled_mvm else str(run)
                    header.append(f"weight_{label}")
                    if sampled_mvm:
                        header.extend((f"status_{label}", f"samples_completed_{label}",
                                       f"stop_reason_{label}"))
                rows = [header]
                for graph in graphs:
                    row = [graph]
                    for entry in workbook_runs(representation, algorithm, graph, groups):
                        if entry is None:
                            row.extend((None,) * (4 if sampled_mvm else 1))
                            continue
                        report = entry[2]
                        row.append(str(report["result"][key]))
                        if sampled_mvm:
                            row.extend(mvm_execution(representation, report))
                    rows.append(row)
                yield "mvm_" + representation + "_results", rows
            continue
        rows = [["graph", *REPRESENTATIONS]]
        for graph in graphs:
            row = [graph]
            for representation in REPRESENTATIONS:
                runs = reports[representation].get((algorithm, graph), [])
                value = runs[0][2]["result"][key] if runs else None
                row.append(str(value) if as_text and value is not None else value)
            rows.append(row)
        yield algorithm + "_results", rows


def main():
    graph_list = ARTIFACT_ROOT / "repos" / "cp_algo" / "graphs.txt"
    graphs = [line.strip() for line in graph_list.read_text(encoding="utf-8").splitlines()
              if line.strip() and not line.lstrip().startswith("#")]
    if len(graphs) != len(set(graphs)):
        raise ValueError(f"Repeated graph in {graph_list}")
    reports = load_reports(graphs)
    performance = list(performance_sheets(graphs, reports))
    results = list(result_sheets(graphs, reports))
    performance_path = SUMMARIES_DIR / "application-runtime-and-peak-memory.xlsx"
    results_path = SUMMARIES_DIR / "application-solution-values.xlsx"
    workbook_file(performance_path, performance)
    workbook_file(results_path, results)
    print(f"{performance_path.name} contains {len(performance)} timing and memory sheets.")
    print(f"{results_path.name} contains {len(results)} result sheets.")


if __name__ == "__main__":
    main()

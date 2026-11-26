#!/usr/bin/env python3
"""Build the clique-cover construction and baselines workbook from saved reports."""

from __future__ import annotations

import json
from collections import defaultdict
from copy import copy, deepcopy
from pathlib import Path
from statistics import median

try:
    from openpyxl import load_workbook
    from openpyxl.styles import Alignment, Font, PatternFill
except ImportError as exc:
    raise SystemExit(
        "OpenPyXL is required. Install it with: python3 -m pip install openpyxl"
    ) from exc


ANALYSIS_DIR = Path(__file__).resolve().parent
ARTIFACT_ROOT = ANALYSIS_DIR.parents[1]
CONSTRUCTION_DATA = ARTIFACT_ROOT / "data" / "evals_con"
REPORTS_DIR = CONSTRUCTION_DATA / "run-reports"
TEMPLATE_PATH = ANALYSIS_DIR / "clique-cover-construction-summary-template.xlsx"
CC_RESULTS_DIR = REPORTS_DIR / "cc_con" / "construction"
FM_RESULTS_DIR = REPORTS_DIR / "fm_con" / "construction"
BOUND_RESULTS_DIR = REPORTS_DIR / "cc_con" / "lower-bounds"
OUTPUT_PATH = CONSTRUCTION_DATA / "summaries" / "clique-cover-construction-and-baselines.xlsx"

AM_ALGORITHMS = ["bpa", "cpa", "fpa"]
BASE_ALGORITHMS = ["lzf", "bcc", "dpf"]
COLUMNS = ["BPA", "BPA-AM", "CPA", "CPA-AM", "FPA", "FPA-AM", "LZF", "BCC", "DPF"]
COMPARISON_COLUMNS = ["LZF", "BCC", "FPA", "CPA", "FM"]
AM_COMPARISON_COLUMNS = ["BPA-AM", "FPA-AM", "CPA-AM", "DPF"]
BOUND_METHOD_ORDER = ["linear", "local-mis", "local-mis-truss", "local-color"]
DATASET_COLLECTIONS = [
    (
        "SuiteSparse",
        [
            "coPapersCiteseer",
            "RM07R",
            "audikw_1",
            "dielFilterV3real",
            "hollywood-2009",
            "HV15R",
        ],
    ),
    (
        "Affiliation-Sparse",
        [
            "mathoverflow-answers",
            "askubuntu-threads",
            "math-sx-threads",
            "amazon-reviews",
            "stackoverflow-threads",
            "stackoverflow-answers",
        ],
    ),
    (
        "AffiliationSX-LD",
        ["askubuntu", "tex", "serverfault", "math", "wordpress", "unix"],
    ),
    (
        "AffiliationSX-MD",
        ["superuser", "physics", "stats", "english", "scifi", "gamedev"],
    ),
    (
        "AffiliationSX-HD",
        ["gis", "codereview", "rpg", "drupal", "dba", "codegolf"],
    ),
    (
        "RI-Power-Law",
        [
            "ri-pl-250m",
            "ri-pl-500m",
            "ri-pl-1b",
            "ri-pl-2b",
            "ri-pl-4b",
            "ri-pl-8b",
        ],
    ),
    (
        "RI-Uniform",
        [
            "ri-u-250m",
            "ri-u-500m",
            "ri-u-1b",
            "ri-u-2b",
            "ri-u-4b",
            "ri-u-8b",
        ],
    ),
    (
        "RI-Lognormal",
        [
            "ri-ln-250m",
            "ri-ln-500m",
            "ri-ln-1b",
            "ri-ln-2b",
            "ri-ln-4b",
            "ri-ln-8b",
        ],
    ),
]
HEADER_COLORS = {
    "BPA": "ED7D31",
    "BPA-AM": "F4B183",
    "CPA": "70AD47",
    "CPA-AM": "A9D18E",
    "FPA": "FFC000",
    "FPA-AM": "FFE699",
    "LZF": "5B9BD5",
    "BCC": "4472C4",
    "DPF": "A5A5A5",
    "FM": "8064A2",
}
DISTRIBUTION_HEADER_COLORS = {
    "LZF": "7F7F7F",
    "BCC": "ED7D31",
    "FPA": "70AD47",
    "CPA": "5B9BD5",
    "FM": "8064A2",
}


def load_results(directory: Path, algorithms: list[str]) -> dict[tuple[str, str], dict]:
    grouped: dict[tuple[str, str], list[tuple[Path, dict]]] = defaultdict(list)
    for path in sorted(directory.glob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        if data["algorithm"]["name"] not in algorithms:
            continue
        key = (data["graph"]["name"], data["algorithm"]["name"])
        grouped[key].append((path, data))

    results: dict[tuple[str, str], dict] = {}
    for key, entries in grouped.items():
        if len(entries) != 5:
            raise ValueError(f"Expected five cc_con reports for {key}, found {len(entries)}")

        first = entries[0][1]
        for path, data in entries[1:]:
            if data["algorithm"] != first["algorithm"]:
                raise ValueError(f"Algorithm parameters differ in {path.name}")
            graph_identity = {
                field: value
                for field, value in data["graph"].items()
                if field != "time_seconds"
            }
            first_graph_identity = {
                field: value
                for field, value in first["graph"].items()
                if field != "time_seconds"
            }
            if graph_identity != first_graph_identity:
                raise ValueError(f"Graph metadata differ in {path.name}")
            if (
                data["constructed_cover"].get("num_cliques"),
                data["constructed_cover"].get("cover_size"),
            ) != (
                first["constructed_cover"].get("num_cliques"),
                first["constructed_cover"].get("cover_size"),
            ):
                raise ValueError(f"Constructed cover differs in {path.name}")
            if {
                key: value
                for key, value in data["postprocess"].items()
                if key != "time_seconds"
            } != {
                key: value
                for key, value in first["postprocess"].items()
                if key != "time_seconds"
            }:
                raise ValueError(f"Postprocessed cover differs in {path.name}")
            if data.get("algorithm_statistics") != first.get("algorithm_statistics"):
                raise ValueError(f"Algorithm statistics differ in {path.name}")

        result = deepcopy(first)
        result["constructed_cover"]["time_seconds"] = median(
            data["constructed_cover"]["time_seconds"] for _path, data in entries
        )
        if "time_seconds" in first["postprocess"]:
            result["postprocess"]["time_seconds"] = median(
                data["postprocess"]["time_seconds"] for _path, data in entries
            )
            result["_total_time_seconds"] = median(
                data["constructed_cover"]["time_seconds"]
                + data["postprocess"]["time_seconds"]
                for _path, data in entries
            )
        result["_source_files"] = [path.name for path, _data in entries]
        results[key] = result
    return results


def regroup_dataset_sheet(ws) -> None:
    """Apply the paper's named collections and canonical graph order."""
    existing = {
        str(ws.cell(row, 2).value): [ws.cell(row, column).value for column in range(2, 6)]
        for row in range(5, ws.max_row + 1)
        if ws.cell(row, 2).value
    }
    ordered_graphs = [graph for _collection, graphs in DATASET_COLLECTIONS for graph in graphs]
    if len(ordered_graphs) != 48 or len(set(ordered_graphs)) != 48:
        raise ValueError("Dataset collection specification must contain 48 unique graphs")
    if set(existing) != set(ordered_graphs):
        missing = sorted(set(ordered_graphs) - set(existing))
        extra = sorted(set(existing) - set(ordered_graphs))
        raise ValueError(f"Dataset collection mismatch; missing={missing}, extra={extra}")

    row = 5
    for collection, graphs in DATASET_COLLECTIONS:
        for graph in graphs:
            ws.cell(row, 1, collection)
            for column, value in enumerate(existing[graph], start=2):
                ws.cell(row, column, value)
            row += 1
    ws.column_dimensions["A"].width = 24


def dataset_rows(ws) -> list[tuple[int, str, str]]:
    rows: list[tuple[int, str, str]] = []
    for row in range(5, ws.max_row + 1):
        group = ws.cell(row, 1).value
        graph = ws.cell(row, 2).value
        if group is not None and graph:
            rows.append((row, str(group), str(graph)))
    return rows


def validate_results(
    rows: list[tuple[int, str, str]],
    results: dict[tuple[str, str], dict],
    algorithms: list[str],
    require_postprocess: bool,
) -> None:
    graphs = [graph for _, _, graph in rows]
    expected = {(graph, algorithm) for graph in graphs for algorithm in algorithms}
    actual = set(results)
    if expected != actual:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ValueError(f"cc_con matrix mismatch; missing={missing[:8]}, extra={extra[:8]}")

    for key, data in results.items():
        constructed = data.get("constructed_cover", {})
        required = [constructed.get("cover_size"), constructed.get("time_seconds")]
        postprocess = data.get("postprocess", {})
        if require_postprocess:
            required.extend([postprocess.get("cover_size"), postprocess.get("time_seconds")])
        if any(value is None for value in required):
            raise ValueError(f"Missing required metric in {data['_source_files'][0]}")
        if require_postprocess and not postprocess.get("assignment_minimal"):
            raise ValueError(
                f"Postprocess is not assignment-minimal in {data['_source_files'][0]}"
            )


def fm_total_size(data: dict, source_name: str) -> int:
    """Read current FM total size while accepting existing schema-2/3 reports."""
    partition = data.get("partition", {})
    value = partition.get("total_size")
    if value is None:
        value = partition.get("total_order")
    if value is None:
        raise ValueError(f"Missing FM partition total size in {source_name}")
    return int(value)


def load_fm_results(rows: list[tuple[int, str, str]]) -> dict[str, dict]:
    """Load five fixed-delta FM reports per graph and take timing medians."""
    expected_graphs = {graph for _row, _group, graph in rows}
    grouped: dict[str, list[tuple[Path, dict]]] = defaultdict(list)
    for path in sorted(FM_RESULTS_DIR.glob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        if data.get("algorithm", {}).get("name") != "fm":
            raise ValueError(f"Unexpected algorithm in {path.name}")
        graph = data.get("graph", {}).get("name")
        if graph not in expected_graphs:
            raise ValueError(f"Unexpected graph in {path.name}: {graph}")
        grouped[graph].append((path, data))

    results: dict[str, dict] = {}
    for _row, _group, graph in rows:
        entries = grouped.get(graph, [])
        if len(entries) != 5:
            raise ValueError(f"Expected five FM reports for {graph}, found {len(entries)}")

        first = entries[0][1]
        total_sizes = {fm_total_size(data, path.name) for path, data in entries}
        if len(total_sizes) != 1:
            raise ValueError(f"FM total size differs across runs for {graph}")
        if any(data["partition"] != first["partition"] for _path, data in entries[1:]):
            raise ValueError(f"FM partition differs across runs for {graph}")
        selected_deltas = {
            data.get("delta_search", {}).get("selected_delta") for _path, data in entries
        }
        if len(selected_deltas) != 1:
            raise ValueError(f"FM selected delta differs across runs for {graph}")

        construction_times: list[float] = []
        for path, data in entries:
            timing = data.get("timing", {})
            construction_time = timing.get("selected_run_seconds")
            if construction_time is None:
                construction_time = timing.get("selected_trial_seconds")
            if construction_time is None:
                raise ValueError(f"Missing requested FM metric in {path.name}")
            construction_times.append(float(construction_time))

        results[graph] = {
            "total_size": total_sizes.pop(),
            "construction_time": median(construction_times),
            "source_files": [path.name for path, _data in entries],
        }
    return results


def bound_variant(data: dict) -> str:
    """Return the displayed method name, distinguishing exact-truss local MIS."""
    algorithm = data.get("algorithm", {})
    method = algorithm.get("method")
    if not isinstance(method, str) or not method:
        raise ValueError("Bound result has no algorithm.method")
    if method == "local-mis" and algorithm.get("exact_truss") is True:
        return "local-mis-truss"
    return method


def load_all_bound_results(
    rows: list[tuple[int, str, str]],
) -> dict[str, dict[str, int]]:
    """Load every final bound file, grouped by displayed result variant."""
    expected_graphs = {graph for _row, _group, graph in rows}
    unordered: dict[str, dict[str, int]] = {}
    for path in sorted(BOUND_RESULTS_DIR.glob("*.json")):
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        variant = bound_variant(data)
        graph = data.get("graph", {}).get("name")
        if graph not in expected_graphs:
            raise ValueError(f"Unexpected graph in {path.name}: {graph}")
        combined_best = data.get("lower_bounds", {}).get("combined_best")
        if not isinstance(combined_best, (int, float)) or combined_best <= 0:
            raise ValueError(f"Invalid lower_bounds.combined_best in {path.name}")
        method_results = unordered.setdefault(variant, {})
        if graph in method_results:
            raise ValueError(f"Duplicate {variant} bound result for {graph}")
        method_results[graph] = int(combined_best)

    methods = [method for method in BOUND_METHOD_ORDER if method in unordered]
    methods.extend(sorted(set(unordered) - set(methods)))
    results = {method: unordered[method] for method in methods}
    missing_all = sorted(
        graph
        for graph in expected_graphs
        if not any(graph in method_results for method_results in results.values())
    )
    if missing_all:
        raise ValueError(f"Graphs with no lower bound: {missing_all}")
    return results


def retain_data_sheets(
    wb,
) -> tuple[object, object, object, object, object, object, object, object, object]:
    for ws in list(wb.worksheets):
        if ws.title not in {"datasets", "construction time"}:
            wb.remove(ws)
    datasets_ws = wb["datasets"]
    time_ws = wb["construction time"]
    cover_ws = wb.copy_worksheet(time_ws)
    cover_ws.title = "cover size"
    wb._sheets.remove(cover_ws)
    wb._sheets.insert(2, cover_ws)
    fm_ws = wb.create_sheet("FM")
    comp_cr_ws = wb.create_sheet("comp_cr")
    comp_time_ws = wb.create_sheet("comp_time")
    comp_cr_am_ws = wb.create_sheet("comp_cr_am")
    comp_time_am_ws = wb.create_sheet("comp_time_am")
    lb_sigma_ws = wb.create_sheet("lb_sigma")
    return (
        datasets_ws,
        time_ws,
        cover_ws,
        fm_ws,
        comp_cr_ws,
        comp_time_ws,
        comp_cr_am_ws,
        comp_time_am_ws,
        lb_sigma_ws,
    )


def prepare_metric_sheet(ws, title: str, note: str) -> None:
    for merged_range in list(ws.merged_cells.ranges):
        if merged_range.min_row <= 2:
            ws.unmerge_cells(str(merged_range))
    ws.merge_cells("A1:K1")
    ws.merge_cells("A2:K2")
    ws["A1"] = title
    ws["A2"] = note
    ws["A2"].alignment = Alignment(wrap_text=True, vertical="center")
    headers = ["Group", "Graph"] + COLUMNS
    for column, header in enumerate(headers, start=1):
        cell = ws.cell(4, column, header)
        cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
        if header in HEADER_COLORS:
            cell.fill = PatternFill("solid", fgColor=HEADER_COLORS[header])
            cell.font = Font(color="FFFFFF" if not header.endswith("-AM") else "1F1F1F", bold=True)
    ws.freeze_panes = "C5"
    ws.auto_filter.ref = "A4:K52"
    ws.column_dimensions["A"].width = 24
    ws.column_dimensions["B"].width = 28
    for column in "CDEFGHIJK":
        ws.column_dimensions[column].width = 17
    ws.sheet_view.showGridLines = False


def copy_row_identity(source_ws, target_ws, row: int) -> None:
    for column in (1, 2):
        source = source_ws.cell(row, column)
        target = target_ws.cell(row, column)
        target.value = source.value
        target._style = copy(source._style)
        target.number_format = source.number_format
        target.alignment = copy(source.alignment)


def add_dataset_degeneracy(
    datasets_ws,
    rows: list[tuple[int, str, str]],
    base_results: dict[tuple[str, str], dict],
) -> None:
    """Add graph degeneracy reported by DPF to the dataset summary."""
    header = datasets_ws.cell(4, 6, "Degeneracy")
    header._style = copy(datasets_ws.cell(4, 5)._style)
    header.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
    datasets_ws.column_dimensions["F"].width = 17
    datasets_ws.auto_filter.ref = "A4:F52"

    for row, _group, graph in rows:
        degeneracy = base_results[(graph, "dpf")].get("algorithm_statistics", {}).get(
            "degeneracy"
        )
        if not isinstance(degeneracy, int) or degeneracy < 0:
            raise ValueError(f"Invalid DPF degeneracy for {graph}: {degeneracy}")
        cell = datasets_ws.cell(row, 6, degeneracy)
        cell.number_format = "#,##0"


def prepare_fm_sheet(ws, datasets_ws) -> None:
    ws.merge_cells("A1:D1")
    ws.merge_cells("A2:D2")
    ws["A1"] = "FM Construction Results"
    ws["A2"] = (
        "The total order in the saved Feder–Motwani reports is partition.total_size. "
        "It agrees across the five fixed selected-delta runs. Construction time is the "
        "median of timing.selected_run_seconds across those runs."
    )
    ws["A1"]._style = copy(datasets_ws["A1"]._style)
    ws["A1"].alignment = copy(datasets_ws["A1"].alignment)
    ws["A2"]._style = copy(datasets_ws["A2"]._style)
    ws["A2"].alignment = Alignment(wrap_text=True, vertical="center")

    headers = ["Group", "Graph", "total-order", "construction-time"]
    for column, header in enumerate(headers, start=1):
        source = datasets_ws.cell(4, min(column, 5))
        cell = ws.cell(4, column, header)
        cell._style = copy(source._style)
        cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
        if column >= 3:
            cell.fill = PatternFill("solid", fgColor="8064A2")
            cell.font = Font(color="FFFFFF", bold=True)

    ws.freeze_panes = "C5"
    ws.auto_filter.ref = "A4:D52"
    ws.column_dimensions["A"].width = 24
    ws.column_dimensions["B"].width = 28
    ws.column_dimensions["C"].width = 20
    ws.column_dimensions["D"].width = 20
    ws.sheet_view.showGridLines = False


def populate_fm_metrics(
    datasets_ws,
    fm_ws,
    rows: list[tuple[int, str, str]],
    fm_results: dict[str, dict],
) -> None:
    for row, _group, graph in rows:
        copy_row_identity(datasets_ws, fm_ws, row)
        result = fm_results[graph]
        total_size = fm_ws.cell(row, 3, result["total_size"])
        total_size.number_format = "#,##0"
        construction_time = fm_ws.cell(row, 4, result["construction_time"])
        construction_time.number_format = "0.000"


def prepare_comparison_sheet(
    ws,
    datasets_ws,
    title: str,
    note: str,
    methods: list[str],
) -> None:
    final_column = ws.cell(1, 2 + len(methods)).column_letter
    ws.merge_cells(f"A1:{final_column}1")
    ws.merge_cells(f"A2:{final_column}2")
    ws["A1"] = title
    ws["A2"] = note
    ws["A1"]._style = copy(datasets_ws["A1"]._style)
    ws["A1"].alignment = copy(datasets_ws["A1"].alignment)
    ws["A2"]._style = copy(datasets_ws["A2"]._style)
    ws["A2"].alignment = Alignment(wrap_text=True, vertical="center")

    headers = ["group", "graph_name"] + methods
    for column, header in enumerate(headers, start=1):
        source = datasets_ws.cell(4, min(column, 5))
        cell = ws.cell(4, column, header)
        cell._style = copy(source._style)
        cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
        if header in HEADER_COLORS:
            cell.fill = PatternFill("solid", fgColor=HEADER_COLORS[header])
            cell.font = Font(color="FFFFFF", bold=True)

    ws.freeze_panes = "C5"
    ws.auto_filter.ref = f"A4:{final_column}52"
    ws.column_dimensions["A"].width = 24
    ws.column_dimensions["B"].width = 28
    for column in range(3, 3 + len(methods)):
        ws.column_dimensions[ws.cell(4, column).column_letter].width = 17
    ws.sheet_view.showGridLines = False


def populate_comparison_metrics(
    datasets_ws,
    comp_cr_ws,
    comp_time_ws,
    rows: list[tuple[int, str, str]],
    am_results: dict[tuple[str, str], dict],
    base_results: dict[tuple[str, str], dict],
    fm_results: dict[str, dict],
) -> None:
    for row, _group, graph in rows:
        copy_row_identity(datasets_ws, comp_cr_ws, row)
        copy_row_identity(datasets_ws, comp_time_ws, row)
        m = datasets_ws.cell(row, 4).value
        if not isinstance(m, (int, float)) or m <= 0:
            raise ValueError(f"Invalid edge count for {graph}: {m}")

        lzf = base_results[(graph, "lzf")]["constructed_cover"]
        bcc = base_results[(graph, "bcc")]["constructed_cover"]
        fpa_data = am_results[(graph, "fpa")]
        cpa_data = am_results[(graph, "cpa")]
        fm = fm_results[graph]

        sizes = [
            lzf["cover_size"],
            bcc["cover_size"],
            fpa_data["constructed_cover"]["cover_size"],
            cpa_data["constructed_cover"]["cover_size"],
            fm["total_size"],
        ]
        times = [
            lzf["time_seconds"],
            bcc["time_seconds"],
            fpa_data["constructed_cover"]["time_seconds"],
            cpa_data["constructed_cover"]["time_seconds"],
            fm["construction_time"],
        ]

        for offset, size in enumerate(sizes, start=3):
            if not isinstance(size, (int, float)) or size <= 0:
                raise ValueError(f"Invalid comparison size for {graph}: {size}")
            cell = comp_cr_ws.cell(row, offset, (2 * m) / size)
            cell.number_format = "0.000"
        for offset, construction_time in enumerate(times, start=3):
            cell = comp_time_ws.cell(row, offset, construction_time)
            cell.number_format = "0.000"


def populate_am_comparison_metrics(
    datasets_ws,
    comp_cr_am_ws,
    comp_time_am_ws,
    rows: list[tuple[int, str, str]],
    am_results: dict[tuple[str, str], dict],
    base_results: dict[tuple[str, str], dict],
) -> None:
    for row, _group, graph in rows:
        copy_row_identity(datasets_ws, comp_cr_am_ws, row)
        copy_row_identity(datasets_ws, comp_time_am_ws, row)
        m = datasets_ws.cell(row, 4).value
        if not isinstance(m, (int, float)) or m <= 0:
            raise ValueError(f"Invalid edge count for {graph}: {m}")

        am_data = [am_results[(graph, algorithm)] for algorithm in ("bpa", "fpa", "cpa")]
        dpf = base_results[(graph, "dpf")]["constructed_cover"]
        sizes = [data["postprocess"]["cover_size"] for data in am_data] + [
            dpf["cover_size"]
        ]
        times = [data["_total_time_seconds"] for data in am_data] + [
            dpf["time_seconds"]
        ]

        for offset, size in enumerate(sizes, start=3):
            if not isinstance(size, (int, float)) or size <= 0:
                raise ValueError(f"Invalid AM comparison size for {graph}: {size}")
            cell = comp_cr_am_ws.cell(row, offset, (2 * m) / size)
            cell.number_format = "0.000"
        for offset, construction_time in enumerate(times, start=3):
            cell = comp_time_am_ws.cell(row, offset, construction_time)
            cell.number_format = "0.000"


def empirical_quantile_points(values: list[float]) -> list[tuple[float, float]]:
    """Return percentile/value points for an inverse empirical CDF."""
    ordered = sorted(float(value) for value in values)
    if any(value <= 0 for value in ordered):
        raise ValueError("Quantile values must be positive")
    if len(ordered) == 1:
        return [(0.5, ordered[0])]
    return [(rank / (len(ordered) - 1), value) for rank, value in enumerate(ordered)]


def add_distribution_data(ws, rows: list[tuple[int, str, str]], metric: str) -> None:
    if metric not in {"cr", "time"}:
        raise ValueError(f"Unsupported distribution metric: {metric}")
    is_cr = metric == "cr"
    data_title_row = 55
    method_header_row = 56
    field_header_row = 57
    data_start_row = 58
    final_column = 2 * len(COMPARISON_COLUMNS)

    ws.merge_cells(
        start_row=53,
        start_column=1,
        end_row=53,
        end_column=7,
    )
    explanation = ws.cell(
        53,
        1,
        (
            "How to read: x is the graph percentile and y is the corresponding CR; "
            "curves higher indicate stronger compression throughout the dataset."
            if is_cr
            else "How to read: x is the graph percentile and y is the corresponding construction time; "
            "curves lower indicate faster construction throughout the dataset."
        ),
    )
    explanation.alignment = Alignment(wrap_text=True, vertical="center")
    explanation.font = Font(italic=True)

    ws.merge_cells(
        start_row=data_title_row,
        start_column=1,
        end_row=data_title_row,
        end_column=final_column,
    )
    title_cell = ws.cell(
        data_title_row,
        1,
        "Compression-ratio quantile-curve data"
        if is_cr
        else "Construction-time quantile-curve data",
    )
    title_cell.font = Font(bold=True)
    title_cell.alignment = Alignment(horizontal="center")

    for method_index, method in enumerate(COMPARISON_COLUMNS):
        raw_column = 3 + method_index
        pair_start = 1 + 2 * method_index
        pair_end = pair_start + 1
        ws.merge_cells(
            start_row=method_header_row,
            start_column=pair_start,
            end_row=method_header_row,
            end_column=pair_end,
        )
        method_cell = ws.cell(method_header_row, pair_start, method)
        method_cell.alignment = Alignment(horizontal="center")
        method_cell.fill = PatternFill("solid", fgColor=DISTRIBUTION_HEADER_COLORS[method])
        method_cell.font = Font(color="FFFFFF", bold=True)
        ws.cell(
            field_header_row,
            pair_start,
            "percentile",
        )
        ws.cell(
            field_header_row,
            pair_end,
            "CR" if is_cr else "time_seconds",
        )

        values = [float(ws.cell(row, raw_column).value) for row, _, _ in rows]
        points = empirical_quantile_points(values)
        for point_offset, (x_value, y_value) in enumerate(points):
            output_row = data_start_row + point_offset
            x_cell = ws.cell(output_row, pair_start, x_value)
            x_cell.number_format = "0%"
            y_cell = ws.cell(output_row, pair_end, y_value)
            y_cell.number_format = "0.000"

    for column in range(1, final_column + 1):
        ws.column_dimensions[ws.cell(field_header_row, column).column_letter].width = max(
            ws.column_dimensions[ws.cell(field_header_row, column).column_letter].width or 0,
            15,
        )


def prepare_lb_sigma_sheet(ws, datasets_ws, bound_methods: list[str]) -> None:
    if not bound_methods:
        raise ValueError("At least one lower-bound method is required")
    lower_start = 3
    lower_end = lower_start + len(bound_methods)

    ws.merge_cells(start_row=1, start_column=1, end_row=1, end_column=lower_end)
    ws.merge_cells(start_row=2, start_column=1, end_row=2, end_column=lower_end)
    ws["A1"] = "Lower Bound on Clique-Cover Total Size"
    ws["A2"] = (
        "Each method column stores lower_bounds.combined_best from the final JSON "
        "lower-bound reports. Missing graph-method results are blank. "
        "max_lb is the maximum available bound."
    )
    ws["A1"]._style = copy(datasets_ws["A1"]._style)
    ws["A1"].alignment = copy(datasets_ws["A1"].alignment)
    ws["A2"]._style = copy(datasets_ws["A2"]._style)
    ws["A2"].alignment = Alignment(wrap_text=True, vertical="center")

    ws.merge_cells(
        start_row=3,
        start_column=lower_start,
        end_row=3,
        end_column=lower_end,
    )
    lower_group = ws.cell(3, lower_start, "lower bound")
    lower_group.alignment = Alignment(horizontal="center", vertical="center")
    lower_group.fill = PatternFill("solid", fgColor="5B9BD5")
    lower_group.font = Font(color="FFFFFF", bold=True)

    headers = ["group", "graph_name"] + bound_methods + ["max_lb"]
    for column, header in enumerate(headers, start=1):
        source = datasets_ws.cell(4, min(column, 5))
        cell = ws.cell(4, column, header)
        cell._style = copy(source._style)
        cell.alignment = Alignment(horizontal="center", vertical="center", wrap_text=True)
        if lower_start <= column <= lower_end:
            cell.fill = PatternFill("solid", fgColor="5B9BD5")
            cell.font = Font(color="FFFFFF", bold=True)

    ws.freeze_panes = "C5"
    ws.auto_filter.ref = f"A4:{ws.cell(4, lower_end).column_letter}52"
    ws.column_dimensions["A"].width = 24
    ws.column_dimensions["B"].width = 28
    for column in range(lower_start, lower_end + 1):
        ws.column_dimensions[ws.cell(4, column).column_letter].width = 20
    ws.sheet_view.showGridLines = False


def populate_lb_sigma(
    datasets_ws,
    lb_sigma_ws,
    rows: list[tuple[int, str, str]],
    bounds_by_method: dict[str, dict[str, int]],
) -> None:
    bound_methods = list(bounds_by_method)
    lower_start = 3
    max_column = lower_start + len(bound_methods)
    for row, _group, graph in rows:
        copy_row_identity(datasets_ws, lb_sigma_ws, row)
        available_bounds: list[int] = []
        for offset, method in enumerate(bound_methods):
            lower_bound = bounds_by_method[method].get(graph)
            if lower_bound is None:
                continue
            available_bounds.append(lower_bound)
            cell = lb_sigma_ws.cell(row, lower_start + offset, lower_bound)
            cell.number_format = "#,##0"
        if not available_bounds:
            raise ValueError(f"No lower bound available for {graph}")
        maximum = lb_sigma_ws.cell(row, max_column, max(available_bounds))
        maximum.number_format = "#,##0"


def populate_metrics(
    datasets_ws,
    time_ws,
    cover_ws,
    rows: list[tuple[int, str, str]],
    am_results: dict[tuple[str, str], dict],
    base_results: dict[tuple[str, str], dict],
) -> None:
    for row, _group, graph in rows:
        copy_row_identity(datasets_ws, time_ws, row)
        copy_row_identity(datasets_ws, cover_ws, row)

        time_values: list[float] = []
        cover_values: list[int] = []
        for algorithm in AM_ALGORITHMS:
            data = am_results[(graph, algorithm)]
            constructed = data["constructed_cover"]
            postprocess = data["postprocess"]
            cover_values.extend([constructed["cover_size"], postprocess["cover_size"]])
            time_values.extend(
                [
                    constructed["time_seconds"],
                    data["_total_time_seconds"],
                ]
            )
        for algorithm in BASE_ALGORITHMS:
            data = base_results[(graph, algorithm)]
            cover_values.append(data["constructed_cover"]["cover_size"])
            time_values.append(data["constructed_cover"]["time_seconds"])

        for offset, value in enumerate(time_values, start=3):
            cell = time_ws.cell(row, offset, value)
            cell.number_format = "0.000"
        for offset, value in enumerate(cover_values, start=3):
            cell = cover_ws.cell(row, offset, value)
            cell.number_format = "#,##0"


def build_workbook() -> None:
    if not TEMPLATE_PATH.exists():
        raise SystemExit(f"Template workbook not found: {TEMPLATE_PATH}")

    wb = load_workbook(TEMPLATE_PATH, data_only=False)
    (
        datasets_ws,
        time_ws,
        cover_ws,
        fm_ws,
        comp_cr_ws,
        comp_time_ws,
        comp_cr_am_ws,
        comp_time_am_ws,
        lb_sigma_ws,
    ) = retain_data_sheets(wb)
    regroup_dataset_sheet(datasets_ws)
    rows = dataset_rows(datasets_ws)
    am_results = load_results(CC_RESULTS_DIR, AM_ALGORITHMS)
    base_results = load_results(CC_RESULTS_DIR, BASE_ALGORITHMS)
    fm_results = load_fm_results(rows)
    bounds_by_method = load_all_bound_results(rows)
    validate_results(rows, am_results, AM_ALGORITHMS, require_postprocess=True)
    validate_results(rows, base_results, BASE_ALGORITHMS, require_postprocess=False)
    add_dataset_degeneracy(datasets_ws, rows, base_results)

    prepare_metric_sheet(
        time_ws,
        "Construction and Assignment-Minimal Total Time",
        "Each value is the median of five saved runs. X is construction time. "
        "X-AM is the median of each run's construction and postprocessing times added together. "
        "LZF, BCC, and DPF use constructed_cover only.",
    )
    prepare_metric_sheet(
        cover_ws,
        "Constructed and Assignment-Minimal Cover Size",
        "Cover sizes agree across all five saved runs. X is the constructed cover size. "
        "X-AM is the postprocessed cover size. LZF, BCC, and DPF use constructed_cover only.",
    )
    prepare_fm_sheet(fm_ws, datasets_ws)
    prepare_comparison_sheet(
        comp_cr_ws,
        datasets_ws,
        "Main Construction Comparison: Compression Ratio",
        "Compression ratio is 2m / representation size. LZF/BCC/FPA/CPA use constructed_cover.cover_size; FM uses partition.total_size (partition.total_order in existing schema-2/3 reports).",
        COMPARISON_COLUMNS,
    )
    prepare_comparison_sheet(
        comp_time_ws,
        datasets_ws,
        "Main Construction Comparison: Construction Time",
        "Times are medians of five runs in seconds. LZF/BCC/FPA/CPA use constructed_cover.time_seconds; FM uses timing.selected_run_seconds from the fixed selected-delta runs.",
        COMPARISON_COLUMNS,
    )
    prepare_comparison_sheet(
        comp_cr_am_ws,
        datasets_ws,
        "Assignment-Minimal Comparison: Compression Ratio",
        "Compression ratio is 2m / representation size. BPA-AM/FPA-AM/CPA-AM use postprocess.cover_size; DPF uses constructed_cover.cover_size.",
        AM_COMPARISON_COLUMNS,
    )
    prepare_comparison_sheet(
        comp_time_am_ws,
        datasets_ws,
        "Assignment-Minimal Comparison: Total Construction Time",
        "Times are medians of five runs in seconds. BPA-AM/FPA-AM/CPA-AM use each run's constructed_cover.time_seconds + postprocess.time_seconds; DPF uses constructed_cover.time_seconds.",
        AM_COMPARISON_COLUMNS,
    )
    prepare_lb_sigma_sheet(lb_sigma_ws, datasets_ws, list(bounds_by_method))
    populate_metrics(datasets_ws, time_ws, cover_ws, rows, am_results, base_results)
    populate_fm_metrics(datasets_ws, fm_ws, rows, fm_results)
    populate_comparison_metrics(
        datasets_ws,
        comp_cr_ws,
        comp_time_ws,
        rows,
        am_results,
        base_results,
        fm_results,
    )
    populate_am_comparison_metrics(
        datasets_ws,
        comp_cr_am_ws,
        comp_time_am_ws,
        rows,
        am_results,
        base_results,
    )
    add_distribution_data(comp_cr_ws, rows, metric="cr")
    add_distribution_data(comp_time_ws, rows, metric="time")
    populate_lb_sigma(datasets_ws, lb_sigma_ws, rows, bounds_by_method)

    wb.calculation.fullCalcOnLoad = True
    wb.calculation.forceFullCalc = True
    wb.save(OUTPUT_PATH)

    check = load_workbook(OUTPUT_PATH, data_only=False, read_only=False)
    assert check.sheetnames == [
        "datasets",
        "construction time",
        "cover size",
        "FM",
        "comp_cr",
        "comp_time",
        "comp_cr_am",
        "comp_time_am",
        "lb_sigma",
    ]
    assert len(dataset_rows(check["datasets"])) == 48
    assert check["datasets"]["F4"].value == "Degeneracy"
    assert all(check["datasets"].cell(row, 6).value is not None for row, _, _ in rows)
    assert [check["construction time"].cell(4, column).value for column in range(3, 12)] == COLUMNS
    assert [check["cover size"].cell(4, column).value for column in range(3, 12)] == COLUMNS
    assert [check["FM"].cell(4, column).value for column in range(1, 5)] == [
        "Group",
        "Graph",
        "total-order",
        "construction-time",
    ]
    assert all(check["FM"].cell(row, 3).value is not None for row, _, _ in rows)
    assert all(check["FM"].cell(row, 4).value is not None for row, _, _ in rows)
    comparison_headers = ["group", "graph_name"] + COMPARISON_COLUMNS
    assert [check["comp_cr"].cell(4, column).value for column in range(1, 8)] == comparison_headers
    assert [check["comp_time"].cell(4, column).value for column in range(1, 8)] == comparison_headers
    assert all(
        check[sheet].cell(row, column).value is not None
        for sheet in ("comp_cr", "comp_time")
        for row, _, _ in rows
        for column in range(3, 8)
    )
    am_comparison_headers = ["group", "graph_name"] + AM_COMPARISON_COLUMNS
    assert [
        check["comp_cr_am"].cell(4, column).value
        for column in range(1, 3 + len(AM_COMPARISON_COLUMNS))
    ] == am_comparison_headers
    assert [
        check["comp_time_am"].cell(4, column).value
        for column in range(1, 3 + len(AM_COMPARISON_COLUMNS))
    ] == am_comparison_headers
    assert all(
        check[sheet].cell(row, column).value is not None
        for sheet in ("comp_cr_am", "comp_time_am")
        for row, _, _ in rows
        for column in range(3, 3 + len(AM_COMPARISON_COLUMNS))
    )
    bound_count = len(bounds_by_method)
    assert check["lb_sigma"].cell(3, 3).value == "lower bound"
    assert [
        check["lb_sigma"].cell(4, column).value
        for column in range(1, 4 + bound_count)
    ] == ["group", "graph_name"] + list(bounds_by_method) + ["max_lb"]
    for offset, method in enumerate(bounds_by_method):
        lower_column = 3 + offset
        available_graphs = set(bounds_by_method[method])
        for row, _, graph in rows:
            lower_value = check["lb_sigma"].cell(row, lower_column).value
            if graph in available_graphs:
                assert lower_value is not None
            else:
                assert lower_value is None
    max_column = 3 + bound_count
    for row, _, graph in rows:
        expected_max = max(
            method_bounds[graph]
            for method_bounds in bounds_by_method.values()
            if graph in method_bounds
        )
        assert check["lb_sigma"].cell(row, max_column).value == expected_max
    check.close()

    print(f"Created {OUTPUT_PATH}")
    bound_metric_count = sum(len(values) for values in bounds_by_method.values()) + len(rows)
    print(
        "Included 48 graphs, 9 sheets, 96 FM metrics, "
        f"864 comparison metrics, and {bound_metric_count} bound metrics"
    )
    print("Construction and FM timings are medians of five runs")


if __name__ == "__main__":
    build_workbook()

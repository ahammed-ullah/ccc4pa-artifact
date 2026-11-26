//! Compress a cc_con clique cover directly into the Rust WebGraph BV format.

mod ccf;
mod order;

use ccf::{Ccf, CcfArcs};
use dsi_bitstream::dispatch::Codes;
use dsi_bitstream::traits::BE;
use std::env;
use std::error::Error;
use std::fs::{self, File};
use std::io::{BufWriter, Write};
use std::path::PathBuf;
use std::sync::Arc;
use webgraph::graphs::arc_list_graph::ArcListGraph;
use webgraph::graphs::bvgraph::{BvComp, BvCompZ, CompFlags};

#[derive(Clone, Copy, PartialEq, Eq)]
enum VertexOrder {
    Original,
    BfsLlp,
}

struct Arguments {
    ccf: PathBuf,
    basename: PathBuf,
    order: VertexOrder,
    order_cache: Option<PathBuf>,
    window: usize,
    max_ref_count: usize,
    min_interval_length: usize,
    outdegrees: Codes,
    references: Codes,
    blocks: Codes,
    intervals: Codes,
    residuals: Codes,
    bvgraphz: bool,
}

/// WebGraph consumes arcs in the chosen vertex order. A reordered run keeps
/// only one expanded neighborhood at a time; no adjacency graph is built.
#[derive(Clone)]
struct PermutedArcs {
    cover: Arc<Ccf>,
    new_to_old: Arc<Vec<u32>>,
    old_to_new: Arc<Vec<u32>>,
    source: usize,
    neighbors: Vec<u32>,
    next_neighbor: usize,
    loaded: bool,
    observed_arcs: u64,
}

impl Iterator for PermutedArcs {
    type Item = (usize, usize);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if !self.loaded {
                if self.source == self.cover.num_nodes() {
                    assert_eq!(self.observed_arcs, self.cover.num_arcs(),
                               "reordered CCF adjacency count differs from header");
                    return None;
                }
                self.cover.fill_neighbors(self.new_to_old[self.source] as usize,
                                          &mut self.neighbors);
                for v in &mut self.neighbors {
                    *v = self.old_to_new[*v as usize];
                }
                self.neighbors.sort_unstable();
                self.next_neighbor = 0;
                self.loaded = true;
            }
            if self.next_neighbor < self.neighbors.len() {
                let edge = (self.source, self.neighbors[self.next_neighbor] as usize);
                self.next_neighbor += 1;
                self.observed_arcs += 1;
                return Some(edge);
            }
            self.source += 1;
            self.loaded = false;
        }
    }
}

#[derive(Clone)]
enum CoverArcs {
    Original(CcfArcs),
    Reordered(PermutedArcs),
}

impl Iterator for CoverArcs {
    type Item = (usize, usize);

    fn next(&mut self) -> Option<Self::Item> {
        match self {
            Self::Original(arcs) => arcs.next(),
            Self::Reordered(arcs) => arcs.next(),
        }
    }
}

fn read_permutation(path: &PathBuf, n: usize) -> Result<Vec<u32>, Box<dyn Error>> {
    let bytes = fs::read(path)?;
    if bytes.len() != n.checked_mul(4).ok_or("permutation byte count overflows")? {
        return Err(format!("permutation has wrong byte length: {}", path.display()).into());
    }
    Ok(bytes.chunks_exact(4).map(|part| {
        u32::from_le_bytes(part.try_into().unwrap())
    }).collect())
}

fn write_permutation(path: &PathBuf, new_to_old: &[u32]) -> Result<(), Box<dyn Error>> {
    let mut file = BufWriter::new(File::create(path)?);
    for &vertex in new_to_old {
        file.write_all(&vertex.to_le_bytes())?;
    }
    file.flush()?;
    Ok(())
}

fn usage() -> &'static str {
    "usage: wg_encode --ccf GRAPH.ccf --basename OUTPUT --order original|bfs-llp --window N \
--max-ref-count N|unlimited --min-interval-length N [--bvgraphz] \
[--order-cache PATH] \
[--outdegrees CODE] [--references CODE] [--blocks CODE] \
[--intervals CODE] [--residuals CODE]\n\
CODE: unary, gamma, delta, zeta1..zeta7, pi1..pi4"
}

fn parse_code(value: &str, option: &str) -> Result<Codes, Box<dyn Error>> {
    let code = match value {
        "unary" => Codes::Unary,
        "gamma" => Codes::Gamma,
        "delta" => Codes::Delta,
        "zeta1" => Codes::Zeta(1),
        "zeta2" => Codes::Zeta(2),
        "zeta3" => Codes::Zeta(3),
        "zeta4" => Codes::Zeta(4),
        "zeta5" => Codes::Zeta(5),
        "zeta6" => Codes::Zeta(6),
        "zeta7" => Codes::Zeta(7),
        "pi1" => Codes::Pi(1),
        "pi2" => Codes::Pi(2),
        "pi3" => Codes::Pi(3),
        "pi4" => Codes::Pi(4),
        _ => return Err(format!("invalid {option} code {value:?}; {}", usage()).into()),
    };
    Ok(code)
}

fn parse_ref_count(value: &str) -> Result<usize, Box<dyn Error>> {
    if value == "unlimited" {
        Ok(usize::MAX)
    } else {
        Ok(value.parse()?)
    }
}

fn parse_arguments() -> Result<Arguments, Box<dyn Error>> {
    let mut ccf = None;
    let mut basename = None;
    let mut order = VertexOrder::Original;
    let mut order_cache = None;
    let mut window = None;
    let mut max_ref_count = None;
    let mut min_interval_length = None;
    // Defaults for direct use; tune.py passes every code explicitly.
    let mut outdegrees = Codes::Gamma;
    let mut references = Codes::Unary;
    let mut blocks = Codes::Gamma;
    let mut intervals = Codes::Gamma;
    let mut residuals = Codes::Zeta(3);
    let mut bvgraphz = false;
    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--ccf" => ccf = Some(PathBuf::from(args.next().ok_or("--ccf needs a path")?)),
            "--basename" => basename = Some(PathBuf::from(args.next().ok_or("--basename needs a path")?)),
            "--order-cache" => order_cache = Some(PathBuf::from(
                args.next().ok_or("--order-cache needs a path")?)),
            "--order" => {
                order = match args.next().ok_or("--order needs original or bfs-llp")?.as_str() {
                    "original" => VertexOrder::Original,
                    "bfs-llp" => VertexOrder::BfsLlp,
                    value => return Err(format!("unknown vertex order {value:?}").into()),
                };
            }
            "--window" => window = Some(args.next().ok_or("--window needs an integer")?.parse()?),
            "--max-ref-count" => {
                let value = args.next().ok_or("--max-ref-count needs an integer or unlimited")?;
                max_ref_count = Some(parse_ref_count(&value)?);
            }
            "--min-interval-length" => min_interval_length = Some(args.next().ok_or("--min-interval-length needs an integer")?.parse()?),
            "--outdegrees" => {
                outdegrees = parse_code(&args.next().ok_or("--outdegrees needs a code")?, "--outdegrees")?;
            }
            "--references" => {
                references = parse_code(&args.next().ok_or("--references needs a code")?, "--references")?;
            }
            "--blocks" => {
                blocks = parse_code(&args.next().ok_or("--blocks needs a code")?, "--blocks")?;
            }
            "--intervals" => {
                intervals = parse_code(&args.next().ok_or("--intervals needs a code")?, "--intervals")?;
            }
            "--residuals" => {
                residuals = parse_code(&args.next().ok_or("--residuals needs a code")?, "--residuals")?;
            }
            "--bvgraphz" => bvgraphz = true,
            "--help" | "-h" => { println!("{}", usage()); std::process::exit(0); }
            "--version" => {
                println!("wg_encode {} (webgraph 0.6.2)", env!("CARGO_PKG_VERSION"));
                std::process::exit(0);
            }
            _ => return Err(format!("unknown argument {arg}; {}", usage()).into()),
        }
    }
    Ok(Arguments {
        ccf: ccf.ok_or("missing --ccf")?,
        basename: basename.ok_or("missing --basename")?,
        order,
        order_cache,
        window: window.ok_or("missing --window")?,
        max_ref_count: max_ref_count.ok_or("missing --max-ref-count")?,
        min_interval_length: min_interval_length.ok_or("missing --min-interval-length")?,
        outdegrees,
        references,
        blocks,
        intervals,
        residuals,
        bvgraphz,
    })
}

fn run() -> Result<(), Box<dyn Error>> {
    let args = parse_arguments()?;
    let graph = Arc::new(Ccf::open(&args.ccf)?);
    let (arcs, permutation) = match args.order {
        VertexOrder::Original => (CoverArcs::Original(graph.arcs()), None),
        VertexOrder::BfsLlp => {
            let work_dir = args.basename.parent().ok_or("output basename has no directory")?
                .join("llp_work");
            let new_to_old = if let Some(cache) = &args.order_cache {
                if cache.exists() {
                    read_permutation(cache, graph.num_nodes())?
                } else {
                    let order = order::bfs_llp_order(&graph, &work_dir)?;
                    let temporary = cache.with_extension("tmp");
                    write_permutation(&temporary, &order)?;
                    fs::rename(temporary, cache)?;
                    order
                }
            } else {
                order::bfs_llp_order(&graph, &work_dir)?
            };
            if new_to_old.len() != graph.num_nodes() {
                return Err("BFS+LLP permutation has wrong length".into());
            }
            let mut old_to_new = vec![u32::MAX; graph.num_nodes()];
            for (new, &old) in new_to_old.iter().enumerate() {
                let slot = old_to_new.get_mut(old as usize)
                    .ok_or("BFS+LLP permutation has an out-of-range vertex")?;
                if *slot != u32::MAX {
                    return Err("BFS+LLP permutation repeats a vertex".into());
                }
                *slot = new as u32;
            }
            let permuted = PermutedArcs {
                cover: graph.clone(),
                new_to_old: Arc::new(new_to_old.clone()),
                old_to_new: Arc::new(old_to_new),
                source: 0, neighbors: Vec::new(), next_neighbor: 0,
                loaded: false, observed_arcs: 0,
            };
            (CoverArcs::Reordered(permuted), Some(new_to_old))
        }
    };
    let arcs = ArcListGraph::new(graph.num_nodes(), arcs);

    let flags = CompFlags {
        outdegrees: args.outdegrees,
        references: args.references,
        blocks: args.blocks,
        intervals: args.intervals,
        residuals: args.residuals,
        compression_window: args.window,
        max_ref_count: args.max_ref_count,
        min_interval_length: args.min_interval_length,
    };
    let mut encoder = (if args.bvgraphz {
        BvCompZ::with_basename(&args.basename)
    } else {
        BvComp::with_basename(&args.basename)
    }).with_comp_flags(flags);
    let bits = encoder.comp_graph::<BE>(&arcs)?;

    if let Some(new_to_old) = permutation {
        // For later weighted algorithms, this maps each encoded vertex back
        // to the canonical CCF vertex whose weight it must receive.
        let path = args.basename.with_extension("perm");
        write_permutation(&path, &new_to_old)?;
    }

    println!("{{\"n\":{},\"arcs\":{},\"graph_bits\":{},\"graph_fingerprint_sha256\":\"{}\"}}",
             graph.num_nodes(), graph.num_arcs(), bits, graph.graph_fingerprint());
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("wg_encode: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_all_supported_codes() {
        let names = [
            "unary", "gamma", "delta", "zeta1", "zeta2", "zeta3", "zeta4",
            "zeta5", "zeta6", "zeta7", "pi1", "pi2", "pi3", "pi4",
        ];
        for name in names {
            assert!(parse_code(name, "--blocks").is_ok(), "{name}");
        }
        assert!(parse_code("zeta8", "--blocks").is_err());
    }

    #[test]
    fn parses_unlimited_reference_depth() {
        assert_eq!(parse_ref_count("unlimited").unwrap(), usize::MAX);
        assert_eq!(parse_ref_count("8").unwrap(), 8);
        assert!(parse_ref_count("-2").is_err());
    }
}

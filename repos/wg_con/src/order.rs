//! BFS followed by WebGraph's layered label propagation (LLP).
//!
//! The CCF is already a symmetric, loopless graph. Its clique incidence index
//! supplies one neighborhood at a time to both BFS and the WebGraph LLP code;
//! this module never builds the complete adjacency graph.

use crate::ccf::Ccf;
use predicates::prelude::*;
use std::error::Error;
use std::fs;
use std::path::Path;
use sux::dict::EliasFanoBuilder;
use sux::rank_sel::{SelectAdaptConst, SelectZeroAdaptConst};
use webgraph::graphs::bvgraph::DCF;
use webgraph::traits::{
    LenderImpl, RandomAccessGraph, RandomAccessLabeling, SequentialGraph, SequentialLabeling,
};
use webgraph::utils::Granularity;
use webgraph_algo::llp;
use webgraph_algo::llp::preds::{MaxUpdates, MinAvgImprov, MinGain};

// These are the eleven resolution values used by WebGraph's `run llp` CLI.
const GAMMAS: [f64; 11] = [
    1.0 / 1024.0, 1.0 / 512.0, 1.0 / 256.0, 1.0 / 128.0,
    1.0 / 64.0, 1.0 / 32.0, 1.0 / 16.0, 1.0 / 8.0,
    1.0 / 4.0, 1.0 / 2.0, 1.0,
];
const LLP_SEED: u64 = 0;
const LLP_MAX_UPDATES: usize = 100;

/// Return a BFS forest order, with components started at their smallest
/// unvisited vertex and neighbors scanned in increasing canonical ID order.
/// The degree array is reused by the random-access graph adapter below.
fn bfs_order(ccf: &Ccf) -> (Vec<u32>, Vec<usize>) {
    let n = ccf.num_nodes();
    let mut seen = vec![false; n];
    let mut order = Vec::with_capacity(n);
    let mut degrees = vec![0; n];
    let mut neighbors = Vec::new();
    let mut head = 0;

    for root in 0..n {
        if seen[root] {
            continue;
        }
        seen[root] = true;
        order.push(root as u32);
        while head < order.len() {
            let u = order[head] as usize;
            head += 1;
            ccf.fill_neighbors(u, &mut neighbors);
            degrees[u] = neighbors.len();
            for &v in &neighbors {
                if !seen[v as usize] {
                    seen[v as usize] = true;
                    order.push(v);
                }
            }
        }
    }
    (order, degrees)
}

/// A read-only graph whose vertex IDs are BFS ranks. Every call to `labels`
/// constructs just that vertex's neighbor list from the CCF. WebGraph's LLP
/// needs random neighborhood access, while `LenderImpl` supplies its sequential
/// traversal from the same method.
struct BfsCcf {
    ccf: Ccf,
    bfs_to_old: Vec<u32>,
    old_to_bfs: Vec<usize>,
    degrees: Vec<usize>,
}

impl BfsCcf {
    fn new(ccf: Ccf, bfs_to_old: Vec<u32>, degrees: Vec<usize>) -> Self {
        let mut old_to_bfs = vec![0; bfs_to_old.len()];
        for (rank, &old) in bfs_to_old.iter().enumerate() {
            old_to_bfs[old as usize] = rank;
        }
        Self { ccf, bfs_to_old, old_to_bfs, degrees }
    }
}

impl SequentialLabeling for BfsCcf {
    type Label = usize;
    type Lender<'a> = LenderImpl<'a, Self> where Self: 'a;

    fn num_nodes(&self) -> usize { self.ccf.num_nodes() }
    fn num_arcs_hint(&self) -> Option<u64> { Some(self.ccf.num_arcs()) }

    // The BFS pass already counted every degree. Build the cumulative degree
    // index directly instead of decoding all arcs again through the default
    // SequentialLabeling::build_dcf implementation.
    fn build_dcf(&self) -> DCF {
        let mut builder = EliasFanoBuilder::new(self.num_nodes() + 1, self.ccf.num_arcs() as usize);
        builder.push(0);
        let mut total = 0;
        for &old in &self.bfs_to_old {
            total += self.degrees[old as usize];
            builder.push(total);
        }
        assert_eq!(total as u64, self.ccf.num_arcs());
        unsafe {
            builder.build().map_high_bits(|high_bits| {
                SelectZeroAdaptConst::<_, _, 12, 4>::new(
                    SelectAdaptConst::<_, _, 12, 4>::new(high_bits),
                )
            })
        }
    }

    fn iter_from(&self, from: usize) -> Self::Lender<'_> {
        LenderImpl { labeling: self, nodes: from..self.num_nodes() }
    }
}

impl SequentialGraph for BfsCcf {}

impl RandomAccessLabeling for BfsCcf {
    type Labels<'a> = std::vec::IntoIter<usize> where Self: 'a;

    fn num_arcs(&self) -> u64 { self.ccf.num_arcs() }

    fn outdegree(&self, node: usize) -> usize {
        self.degrees[self.bfs_to_old[node] as usize]
    }

    fn labels(&self, node: usize) -> Self::Labels<'_> {
        let old = self.bfs_to_old[node] as usize;
        let mut neighbors = Vec::new();
        self.ccf.fill_neighbors(old, &mut neighbors);
        neighbors.into_iter()
            .map(|v| self.old_to_bfs[v as usize])
            .collect::<Vec<_>>()
            .into_iter()
    }
}

impl RandomAccessGraph for BfsCcf {}

/// Return `new_to_old`: the original vertex ID at each final WebGraph ID.
///
/// `work_dir` is a dedicated temporary directory inside the graph's
/// `format_wg/<graph>` folder. WebGraph LLP writes one label file per gamma
/// there; all of these files and the directory are removed before return.
pub fn bfs_llp_order(ccf: &Ccf, work_dir: &Path) -> Result<Vec<u32>, Box<dyn Error>> {
    let (bfs_to_old, degrees) = bfs_order(ccf);
    if bfs_to_old.is_empty() {
        return Ok(Vec::new());
    }
    let graph = BfsCcf::new(ccf.clone(), bfs_to_old.clone(), degrees);
    // A killed Slurm job can leave incomplete labels behind. Remove only those
    // known LLP files before retrying. Anything else in this dedicated folder
    // is an error rather than something we can safely delete.
    if work_dir.exists() {
        for entry in fs::read_dir(work_dir)? {
            let entry = entry?;
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if !entry.file_type()?.is_file()
                || !name.starts_with("labels_")
                || !name.ends_with(".bin")
            {
                return Err(format!("unexpected file in LLP work directory: {}", entry.path().display()).into());
            }
            fs::remove_file(entry.path())?;
        }
    } else {
        fs::create_dir(work_dir)?;
    }

    let result = (|| -> Result<Vec<u32>, Box<dyn Error>> {
        let deg_cumul = graph.build_dcf();
        let stop = MinGain::default()
            .or(MinAvgImprov::default())
            .or(MaxUpdates::from(LLP_MAX_UPDATES));
        let labels = llp::layered_label_propagation(
            graph,
            &deg_cumul,
            GAMMAS.to_vec(),
            None,
            Granularity::default(),
            LLP_SEED,
            stop,
            work_dir,
        )?;
        let ranks = llp::labels_to_ranks(&labels);
        let mut new_to_old = vec![0; bfs_to_old.len()];
        for (bfs, &new_id) in ranks.iter().enumerate() {
            new_to_old[new_id] = bfs_to_old[bfs];
        }
        Ok(new_to_old)
    })();

    // This dedicated directory contains only LLP's label files. Try cleanup
    // even when LLP fails or the job's current candidate cannot be completed.
    let cleanup = fs::remove_dir_all(work_dir);
    match (result, cleanup) {
        (Err(error), _) => Err(error),
        (Ok(_), Err(error)) => Err(error.into()),
        (Ok(order), Ok(())) => Ok(order),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::time::{SystemTime, UNIX_EPOCH};

    static NEXT_FIXTURE: AtomicU64 = AtomicU64::new(0);

    fn tiny_cover() -> (Ccf, std::path::PathBuf) {
        // The cliques {0,1,2} and {1,2,3} overlap; vertex 4 is isolated.
        let mut bytes = vec![0u8; 192];
        bytes[0..8].copy_from_slice(b"CCF\0\r\n\x1a\n");
        bytes[8..12].copy_from_slice(&1u32.to_le_bytes());
        bytes[12..16].copy_from_slice(&160u32.to_le_bytes());
        bytes[16..20].copy_from_slice(&1u32.to_le_bytes());
        for (position, value) in [
            (24, 5u64), (32, 5), (40, 2), (48, 6),
            (56, 160), (64, 3), (72, 184), (80, 8), (88, 192),
            (160, 184), (168, 188), (176, 192),
        ] {
            bytes[position..position + 8].copy_from_slice(&value.to_le_bytes());
        }
        bytes[184..192].copy_from_slice(&[3, 0, 1, 1, 3, 1, 1, 1]);
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let sequence = NEXT_FIXTURE.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!("wg-con-order-{}-{stamp}-{sequence}.ccf", std::process::id()));
        fs::write(&path, bytes).unwrap();
        (Ccf::open(&path).unwrap(), path)
    }

    #[test]
    fn bfs_scans_unique_neighbors_and_keeps_isolate() {
        let (ccf, path) = tiny_cover();
        let (order, degrees) = bfs_order(&ccf);
        assert_eq!(order, vec![0, 1, 2, 3, 4]);
        assert_eq!(degrees, vec![2, 3, 3, 2, 0]);
        let graph = BfsCcf::new(ccf.clone(), order, degrees);
        assert_eq!(graph.successors(1).collect::<Vec<_>>(), vec![0, 2, 3]);
        assert_eq!(graph.outdegree(4), 0);
        drop(graph);
        drop(ccf);
        fs::remove_file(path).unwrap();
    }

    #[test]
    fn official_llp_returns_a_permutation_and_cleans_work_files() {
        let (ccf, path) = tiny_cover();
        let work = path.with_extension("llp-work");
        // A previous job may have been killed in the middle of LLP.
        fs::create_dir(&work).unwrap();
        fs::write(work.join("labels_0.bin"), b"incomplete").unwrap();
        let order = bfs_llp_order(&ccf, &work).unwrap();
        let mut sorted = order.clone();
        sorted.sort_unstable();
        assert_eq!(sorted, (0..5).collect::<Vec<u32>>());
        assert!(!work.exists());
        drop(ccf);
        fs::remove_file(path).unwrap();
    }
}

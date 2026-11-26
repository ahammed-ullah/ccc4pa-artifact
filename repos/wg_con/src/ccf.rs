//! Read a CCF clique cover and expose the graph as sorted adjacency lists.
//!
//! A clique record contains sorted vertex IDs. We decode the cover once and
//! build, for each vertex, the IDs of its incident cliques. A neighborhood is
//! then the sorted union of those clique lists with the vertex itself removed.
//! Overlapping cliques may contain the same edge, so the union removes repeated
//! neighbors. At no point do we keep adjacency lists for the whole graph.

use memmap2::Mmap;
use std::cmp::Reverse;
use std::collections::BinaryHeap;
use std::fs::File;
use std::io;
use std::path::Path;
use std::sync::Arc;

const MAGIC: &[u8; 8] = b"CCF\0\r\n\x1a\n";
const HEADER_BYTES: usize = 160;

fn invalid(message: impl Into<String>) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message.into())
}

fn u32_at(bytes: &[u8], position: usize) -> u32 {
    u32::from_le_bytes(bytes[position..position + 4].try_into().unwrap())
}

fn u64_at(bytes: &[u8], position: usize) -> u64 {
    u64::from_le_bytes(bytes[position..position + 8].try_into().unwrap())
}

fn read_vbyte(bytes: &[u8], position: &mut usize, end: usize) -> io::Result<u64> {
    let mut value = 0u64;
    let mut shift = 0;
    while *position < end {
        let byte = bytes[*position];
        *position += 1;
        let low = u64::from(byte & 127);
        if shift == 63 && low > 1 {
            return Err(invalid("CCF vbyte integer overflows u64"));
        }
        value |= low << shift;
        if byte & 128 == 0 {
            if shift > 0 && low == 0 {
                return Err(invalid("CCF vbyte integer is not shortest-form"));
            }
            return Ok(value);
        }
        if shift >= 63 {
            return Err(invalid("CCF vbyte integer is overlong"));
        }
        shift += 7;
    }
    Err(invalid("CCF clique ends inside a vbyte integer"))
}

struct Cover {
    // Clique c occupies vertices[clique_offsets[c]..clique_offsets[c + 1]].
    vertices: Vec<u32>,
    clique_offsets: Vec<usize>,
    // Vertex u belongs to the clique IDs in this CSR slice.
    incidence: Vec<u32>,
    incidence_offsets: Vec<usize>,
}

impl Cover {
    fn fill_neighbors(
        &self,
        u: usize,
        out: &mut Vec<u32>,
        heap: &mut BinaryHeap<Reverse<(u32, u32, usize)>>,
    ) {
        assert!(u + 1 < self.incidence_offsets.len(), "vertex out of range");
        out.clear();
        heap.clear();
        let incident = &self.incidence
            [self.incidence_offsets[u]..self.incidence_offsets[u + 1]];
        if let [clique] = incident {
            // A vertex in one clique already has a sorted neighborhood.
            let c = *clique as usize;
            for &v in &self.vertices[self.clique_offsets[c]..self.clique_offsets[c + 1]] {
                if v != u as u32 { out.push(v); }
            }
            return;
        }

        // Each clique list is already sorted. Merge the lists rather than
        // concatenating and sorting all their entries for every vertex.
        for &clique in incident {
            let first = self.clique_offsets[clique as usize];
            heap.push(Reverse((self.vertices[first], clique, first)));
        }
        let mut previous = None;
        while let Some(Reverse((v, clique, position))) = heap.pop() {
            if v != u as u32 && previous != Some(v) {
                out.push(v);
                previous = Some(v);
            }
            let next = position + 1;
            if next < self.clique_offsets[clique as usize + 1] {
                heap.push(Reverse((self.vertices[next], clique, next)));
            }
        }
    }
}

#[derive(Clone)]
pub struct Ccf {
    cover: Arc<Cover>,
    n: usize,
    arcs: u64,
    graph_fingerprint: String,
}

impl Ccf {
    pub fn open(path: &Path) -> io::Result<Self> {
        let file = File::open(path)?;
        if file.metadata()?.len() < HEADER_BYTES as u64 {
            return Err(invalid("CCF file is smaller than its 160-byte header"));
        }
        // The input is read-only and must not be changed while it is opened.
        let bytes = unsafe { Mmap::map(&file)? };
        if &bytes[0..8] != MAGIC {
            return Err(invalid("CCF magic is invalid"));
        }
        if u32_at(&bytes, 8) != 1 || u32_at(&bytes, 12) != 160 {
            return Err(invalid("unsupported CCF version or header size"));
        }
        if u32_at(&bytes, 16) != 1 || u32_at(&bytes, 20) != 0 {
            return Err(invalid("unsupported CCF encoding or flags"));
        }

        let n64 = u64_at(&bytes, 24);
        let m = u64_at(&bytes, 32);
        let records = u64_at(&bytes, 40);
        let cover_size = u64_at(&bytes, 48);
        let offsets_start = u64_at(&bytes, 56);
        let offsets_count = u64_at(&bytes, 64);
        let payload_start = u64_at(&bytes, 72);
        let payload_bytes = u64_at(&bytes, 80);
        let file_bytes = u64_at(&bytes, 88);
        if n64 > i32::MAX as u64 {
            return Err(invalid("CCF n exceeds cc_con's 32-bit vertex limit"));
        }
        if records > u32::MAX as u64 {
            return Err(invalid("CCF has more cliques than u32 IDs can address"));
        }
        let n = n64 as usize;
        let records = records as usize;
        let cover_size = usize::try_from(cover_size)
            .map_err(|_| invalid("CCF cover size does not fit usize"))?;
        let expected_offsets = (records as u64)
            .checked_add(1).ok_or_else(|| invalid("CCF clique count overflows"))?;
        let expected_payload = expected_offsets.checked_mul(8)
            .and_then(|size| size.checked_add(HEADER_BYTES as u64))
            .ok_or_else(|| invalid("CCF offset table overflows"))?;
        let expected_file = payload_start.checked_add(payload_bytes)
            .ok_or_else(|| invalid("CCF payload length overflows"))?;
        if offsets_start != HEADER_BYTES as u64 || offsets_count != expected_offsets
            || payload_start != expected_payload
        {
            return Err(invalid("CCF header and offset-table layout disagree"));
        }
        if expected_file != file_bytes || file_bytes != bytes.len() as u64 {
            return Err(invalid("CCF file length disagrees with its header"));
        }
        if m > n64.saturating_mul(n64.saturating_sub(1)) / 2 {
            return Err(invalid("CCF m exceeds the simple-graph maximum"));
        }
        let arcs = m.checked_mul(2).ok_or_else(|| invalid("CCF 2m overflows"))?;
        if cover_size as u64 > payload_bytes {
            return Err(invalid("CCF cover size exceeds payload bytes"));
        }

        let mut graph_fingerprint = String::with_capacity(64);
        for byte in &bytes[96..128] {
            graph_fingerprint.push_str(&format!("{byte:02x}"));
        }

        let mut vertices = Vec::with_capacity(cover_size);
        let mut clique_offsets = Vec::with_capacity(records + 1);
        let mut incidence_offsets = vec![0usize; n + 1];
        let mut begin = u64_at(&bytes, HEADER_BYTES) as usize;
        if begin != payload_start as usize {
            return Err(invalid("CCF first offset does not start the payload"));
        }
        for clique in 0..records {
            let end = u64_at(&bytes, HEADER_BYTES + 8 * (clique + 1)) as usize;
            if end <= begin || end > bytes.len() {
                return Err(invalid(format!("CCF offsets are invalid at clique {clique}")));
            }
            let mut position = begin;
            let cardinality = read_vbyte(&bytes, &mut position, end)?;
            if cardinality < 2 || cardinality > n64 {
                return Err(invalid(format!("CCF clique {clique} has invalid cardinality")));
            }
            clique_offsets.push(vertices.len());
            let mut previous = 0u64;
            for i in 0..cardinality {
                let gap = read_vbyte(&bytes, &mut position, end)?;
                if i > 0 && gap == 0 {
                    return Err(invalid(format!("CCF clique {clique} has duplicate vertices")));
                }
                previous = previous.checked_add(gap)
                    .ok_or_else(|| invalid(format!("CCF clique {clique} delta overflows")))?;
                if previous >= n64 {
                    return Err(invalid(format!("CCF clique {clique} vertex is out of range")));
                }
                let v = previous as usize;
                vertices.push(previous as u32);
                incidence_offsets[v + 1] = incidence_offsets[v + 1].checked_add(1)
                    .ok_or_else(|| invalid("CCF incidence count overflows"))?;
            }
            if position != end {
                return Err(invalid(format!("CCF clique {clique} has trailing bytes")));
            }
            begin = end;
        }
        if begin != bytes.len() || vertices.len() != cover_size {
            return Err(invalid("CCF final offset or decoded cover size is incorrect"));
        }
        clique_offsets.push(vertices.len());

        for u in 0..n {
            incidence_offsets[u + 1] = incidence_offsets[u + 1]
                .checked_add(incidence_offsets[u])
                .ok_or_else(|| invalid("CCF incidence offsets overflow"))?;
        }
        let mut next = incidence_offsets[..n].to_vec();
        let mut incidence = vec![0u32; vertices.len()];
        for clique in 0..records {
            for &v in &vertices[clique_offsets[clique]..clique_offsets[clique + 1]] {
                let slot = &mut next[v as usize];
                incidence[*slot] = clique as u32;
                *slot += 1;
            }
        }

        Ok(Self {
            cover: Arc::new(Cover { vertices, clique_offsets, incidence, incidence_offsets }),
            n, arcs, graph_fingerprint,
        })
    }

    pub fn num_nodes(&self) -> usize { self.n }
    pub fn num_arcs(&self) -> u64 { self.arcs }
    pub fn graph_fingerprint(&self) -> &str { &self.graph_fingerprint }

    /// Fill `out` with the sorted unique neighbors of `u`, reusing its buffer.
    pub fn fill_neighbors(&self, u: usize, out: &mut Vec<u32>) {
        let mut heap = BinaryHeap::new();
        self.cover.fill_neighbors(u, out, &mut heap);
    }

    pub fn arcs(&self) -> CcfArcs {
        CcfArcs {
            cover: self.cover.clone(), n: self.n,
            u: 0, neighbors: Vec::new(), next_neighbor: 0,
            heap: BinaryHeap::new(), observed_arcs: 0, expected_arcs: self.arcs,
        }
    }
}

/// ArcListGraph needs a cloneable, source-sorted iterator. Isolated vertices
/// yield no arcs but remain included in `Ccf::num_nodes()`.
#[derive(Clone)]
pub struct CcfArcs {
    cover: Arc<Cover>,
    n: usize,
    u: usize,
    neighbors: Vec<u32>,
    next_neighbor: usize,
    heap: BinaryHeap<Reverse<(u32, u32, usize)>>,
    observed_arcs: u64,
    expected_arcs: u64,
}

impl Iterator for CcfArcs {
    type Item = (usize, usize);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if self.next_neighbor < self.neighbors.len() {
                let v = self.neighbors[self.next_neighbor];
                self.next_neighbor += 1;
                self.observed_arcs += 1;
                return Some((self.u - 1, v as usize));
            }
            if self.u == self.n {
                assert_eq!(self.observed_arcs, self.expected_arcs,
                           "CCF decoded adjacency count differs from header");
                return None;
            }
            self.cover.fill_neighbors(self.u, &mut self.neighbors, &mut self.heap);
            self.u += 1;
            self.next_neighbor = 0;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn overlapping_cliques_make_unique_arcs_and_keep_isolates() {
        // {0,1,2} and {1,2,3} share the edge 1--2; vertex 4 is isolated.
        let mut bytes = vec![0u8; 192];
        bytes[0..8].copy_from_slice(MAGIC);
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
        let unique = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let path = std::env::temp_dir().join(format!("wg-con-ccf-{}-{unique}.ccf", std::process::id()));
        std::fs::write(&path, bytes).unwrap();
        let graph = Ccf::open(&path).unwrap();
        assert_eq!(graph.num_nodes(), 5);
        assert_eq!(graph.num_arcs(), 10);
        let mut neighbors = Vec::new();
        graph.fill_neighbors(1, &mut neighbors);
        assert_eq!(neighbors, vec![0, 2, 3]);
        graph.fill_neighbors(4, &mut neighbors);
        assert_eq!(neighbors, Vec::<u32>::new());
        let arcs = graph.arcs().collect::<Vec<_>>();
        assert_eq!(arcs, vec![
            (0, 1), (0, 2), (1, 0), (1, 2), (1, 3),
            (2, 0), (2, 1), (2, 3), (3, 1), (3, 2),
        ]);
        drop(graph);
        std::fs::remove_file(path).unwrap();
    }
}

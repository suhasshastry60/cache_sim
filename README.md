# Cache Simulator with Stream Buffer Prefetching

A configurable two-level cache hierarchy simulator with optional stream buffer prefetching. Simulates L1 and L2 caches with write-back policy, LRU replacement, and sequential prefetching.

---

## Building

```bash
make              # Build the simulator
make clean        # Remove all build artifacts
```

This creates an executable named `sim`.

---

## Running

### Command Format

```bash
./sim <BLOCKSIZE> <L1_SIZE> <L1_ASSOC> <L2_SIZE> <L2_ASSOC> <PREF_N> <PREF_M> <trace_file>
```

### Parameters

| Parameter    | Description                                                    |
|------------- |----------------------------------------------------------------|
| `BLOCKSIZE`  | Block size in bytes                                             |
| `L1_SIZE`    | L1 cache size in bytes                                          |
| `L1_ASSOC`   | L1 associativity (1=direct-mapped, N=N-way)                     |
| `L2_SIZE`    | L2 cache size in bytes (0 = no L2)                              |
| `L2_ASSOC`   | L2 associativity                                                |
| `PREF_N`     | Number of stream buffers (0 = no prefetching)                   |
| `PREF_M`     | Entries per stream buffer (0 = no prefetching)                  |
| `trace_file` | Path to memory trace file                                       |

**Notes:**
- If `L2_SIZE = 0`: L1-only, prefetcher attached to L1
- If `L2_SIZE > 0`: Two-level cache, prefetcher attached to L2
- If `PREF_N = 0` or `PREF_M = 0`: No prefetching

### Examples

```bash
# L1 + L2 with prefetching
./sim 32 8192 4 262144 8 3 10 traces/gcc_trace.txt

# L1 only with prefetching
./sim 16 1024 1 0 0 2 8 traces/example_trace.txt

# L1 + L2 without prefetching
./sim 64 16384 2 524288 16 0 0 traces/example_trace.txt

# Save output to file
./sim 32 8192 4 262144 8 3 10 traces/gcc_trace.txt > results.txt

# View with pager
./sim 32 8192 4 262144 8 3 10 traces/gcc_trace.txt | less
```

---

## Trace File Format

One memory access per line:

```
<operation> <hex_address>
```

- `<operation>`: `r` (read) or `w` (write)
- `<hex_address>`: 32-bit hexadecimal address (no 0x prefix)

**Example:**
```
r 00000000
w 00000004
r 00000008
```

---

## Output

The simulator outputs four sections:

### 1. Configuration
Shows the parameters used for the simulation.

### 2. Cache Contents
Final state of L1 and L2 caches:
```
===== L1 contents =====
set      0:      3a9        3aa        3ab        3ac  
set      1:      3ad        3ae        3af D      3b0  
```
- Blocks shown in LRU order (left=most recent, right=least recent)
- `D` = dirty block (modified)
- Tag values in hexadecimal

### 3. Stream Buffer Contents
Current state of prefetch buffers (if enabled):
```
===== Stream Buffer(s) contents =====
  1a2f40  1a2f41  1a2f42  1a2f43  
  1b3c20  1b3c21  1b3c22  
```
- Each line is one buffer
- Buffers shown in LRU order

### 4. Measurements
Performance statistics:
```
===== Measurements =====
a. L1 reads:                   1000000
b. L1 read misses:             50000
c. L1 writes:                  250000
d. L1 write misses:            12000
e. L1 miss rate:               0.0496
f. L1 writebacks:              8000
g. L1 prefetches:              0
h. L2 reads (demand):          62000
i. L2 read misses (demand):    15000
j. L2 reads (prefetch):        0
k. L2 read misses (prefetch):  0
l. L2 writes:                  8000
m. L2 write misses:            2000
n. L2 miss rate:               0.2419
o. L2 writebacks:              5000
p. L2 prefetches:              150000
q. memory traffic:             170000
```

**Key Metrics:**
- **Miss rate**: (read_misses + write_misses) / (reads + writes)
- **Memory traffic**: Total blocks transferred to/from main memory
  - With L2: `L2_read_misses + L2_write_misses + L2_writebacks + L2_prefetches`
  - Without L2: `L1_read_misses + L1_write_misses + L1_writebacks + L1_prefetches`

---

## File Structure

- `sim.h` - Cache class declarations and methods
- `sim.cc` - Main program and implementation
- `Makefile` - Build configuration
- `runs_and_traces/` - Test traces and expected outputs

---

## Features

- Two-level cache hierarchy (L1 + L2)
- Configurable block size, cache size, and associativity
- LRU replacement policy
- Write-back with write-allocate
- Stream buffer prefetching with multiple buffers
- Comprehensive performance statistics

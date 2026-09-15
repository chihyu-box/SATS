# SATS

SATS is a microarchitecture-level SystemC TLM-2.0 simulator of an output-stationary systolic
array accelerator for evaluating instruction scheduling strategies, with cycle-accurate DRAM timing
provided by DRAMSys.

It takes a stream of `mvin`, `mvout`, `gemm` and `drain` instructions, returns the simulated time
and a trace of every hardware event, and validates the result against a software reference.

## Architecture

- A `DIM` x `DIM` output-stationary PE array with two partial-sum buffers, one accumulating while
  the other drains.
- A row-addressed scratchpad of `N_BANKS` banks x `N_ROWS_PER_BANK` rows x `DIM` bytes. Each bank
  serves one request per cycle; a memory arbiter queues the rest.
- Three controllers -- load (DRAM -> scratchpad), store (scratchpad -> DRAM) and exec (gemm and
  drain) -- each with an `INSTR_QUEUE_SIZE`-entry instruction queue; load and store also have
  `SPAD_CREDIT_SIZE` scratchpad requests and `LOAD_N_DATA_BUF` / `STORE_N_DATA_BUF` DRAM requests
  in flight.
- DRAM through DRAMSys, with any of its example configurations (`dram_config/`).
- `inputType` inputs, `accType` accumulators, and a scaler that multiplies drained results by
  `SCALE_FACTOR` back into `inputType`.

The constants live in `src/include/config.h`:

| constant | default | |
|---|---|---|
| `DIM` | 32 | array dimension and rows per tile |
| `N_BANKS` | 4 | scratchpad banks |
| `N_ROWS_PER_BANK` | 8192 | rows per bank |
| `inputType`, `accType` | uint8_t, uint32_t | input and accumulator types |
| `SCALE_FACTOR` | 1 / 10000 | applied by the scaler on drain |
| `CLOCK_NS` | 1 | cycle time in ns |
| `BANK_ACCESS_TIME`, `PE_MAC_TIME`, `PE_DRAIN_TIME`, `SCALER_SCALE_TIME`, `SKEW_BUF_SHIFT_TIME` | 1 cycle each | time for a scratchpad bank access, a PE multiply-accumulate, a psum row drained, a row scaled and a skew buffer shift |
| `INSTR_QUEUE_SIZE` | 4 | instruction queue depth per controller |
| `SPAD_CREDIT_SIZE` | 4 | scratchpad requests in flight for the load and store controllers |
| `LOAD_N_DATA_BUF`, `STORE_N_DATA_BUF` | 16, 8 | DRAM requests in flight for the load and store controllers |
| `DRAMSYS_CONFIG_PATH` | `dram_config/hbm2-example.json` | DRAMSys configuration; `--dramsys-config` overrides it |

The ISA has four instructions:

| instruction | fields | effect |
|---|---|---|
| `mvin`  | `dram_src`, `spad_dst`, `rows`, `dram_stride`, `spad_stride` | copy a tile of `rows` rows from DRAM into the scratchpad; row i goes from `dram_src + i * dram_stride` to `spad_dst + i * spad_stride` |
| `mvout` | `spad_src`, `dram_dst`, `rows`, `dram_stride`, `spad_stride` | copy a tile of `rows` rows from the scratchpad back to DRAM, the reverse of `mvin` |
| `gemm`  | `A_addr`, `B_addr`, `a_stride`, `b_stride`, `psum_index`, `is_flush` | stream the `DIM`-row tiles at `A_addr` and `B_addr` through the array, adding their products into `psum[psum_index]`; with `is_flush`, stream zeros instead to push the last products through |
| `drain` | `C_addr`, `c_stride`, `psum_index` | scale `psum[psum_index]` down to `inputType` and write it to the scratchpad at `C_addr` |

A gemm completes when its last row has entered the array, not when its products have reached
`psum`; gemms issued on the same `psum_index` accumulate, and a flush closes the chain -- only
after it is `psum[psum_index]` complete and ready to drain.

## Building and running

```
git clone https://github.com/chihyu-box/SATS.git
cd SATS
git submodule update --init --recursive   # DRAMSys
mkdir build
cd build
cmake ..
make -j
cd ..
```

CMake 3.25 or newer and a C++20 compiler are needed. SystemC 2.3.4 is fetched and built along
with DRAMSys's other dependencies. Run from the repo root:

```
./build/testbench/scheduling/scheduling --scheduler <a|b|c> [options]

  --layout <row_major|tile_major>  how A and B are laid out in DRAM (default: row_major)
  --trace <path>                   trace CSV to write (default: trace_scheduler_<x>[_tile_major].csv)
  --no-trace                       do not write a trace
  --dramsys-config <path>          DRAMSys configuration (default: dram_config/hbm2-example.json)
  --print                          print the input and output matrices
```

Every run computes a padded 65 x 66 x 67 GEMM, checks the result against a software reference and
prints `PASS`/`FAIL` with the simulated time. `--layout tile_major` stores each DIM x DIM tile of A
and B contiguously in DRAM instead of row by row.

The three schedulers issue the same instructions in different orders:

| scheduler | order |
|---|---|
| a | every instruction completes before the next is issued |
| b | program order through a scoreboard that dispatches an instruction as soon as no earlier one conflicts with it on an operand |
| c | a hand-written order, tuned on HBM2 for this problem size, with 14 prefetched mvins, 6 reordered C tiles, 2 reordered gemms, 8 deprioritized drains and 7 deprioritized mvouts |

### Traces and viewers

A run writes `trace_<name>.csv` (every event, with time, engine, instruction id and detail) and
`deps_<name>.csv` (the instructions each one waited for). Three self-contained HTML viewers in
`testbench/trace_viewer/` are rebuilt from a trace by their `update_*.py` scripts:

| viewer | shows |
|---|---|
| `pipeline-timeline.html` | every engine's activity over time, with stalls, bank contention and instruction dependencies |
| `controller-viewer.html` | each controller's queue occupancy, per-row DRAM/scratchpad phases and buffer usage |
| `memory-arbiter-viewer.html` | every scratchpad access by bank and initiator, with the time spent queued |

`run.sh` does build, run and viewer update in one step:

```
./run.sh <a|b|c> [--layout tile_major] [--dramsys-config <path>] [--print]
```

## Results

Simulated time of each run, from `./run.sh <a|b|c> [--layout tile_major] --dramsys-config
dram_config/{hbm2,lpddr4}-example.json`; each percentage is relative to the column to its left:

| | a | b | c | c, tile_major |
|---|---|---|---|---|
| HBM2   | 4214 ns | 2310 ns (-45%) | 1828 ns (-21%) | 1828 ns (0%) |
| LPDDR4 | 9290.6 ns | 9103.1 ns (-2%) | 7105.6 ns (-22%) | 6448.8 ns (-9%) |

Scratchpad accesses delayed by a bank conflict, out of 2880 per run:

| | a | b | c | c, tile_major |
|---|---|---|---|---|
| HBM2   | 63 (2.2%) | 781 (27.1%) | 95 (3.3%) | 95 (3.3%) |
| LPDDR4 | 63 (2.2%) | 146 (5.1%) | 122 (4.2%) | 121 (4.2%) |

- **a -> b** is the gain from overlapping instructions. Under HBM2 it nearly halves the time.
  Under LPDDR4 it is 2%: moving the data takes most of the time.
- **b -> c** comes from different things under each memory. Under HBM2 b's out-of-order dispatch
  makes 27% of its scratchpad accesses wait for a bank against 3% of c's, so c is 21% faster.
  Under LPDDR4 b interleaves mvouts with the mvins and every read/write turnaround costs DRAM
  time, while c issues all mvouts after the last mvin and is 22% faster.
- **c -> tile_major** depends on the DRAM row size. The whole working set (about 27 KB) fits in
  one 64 KB HBM2 row, so no layout can change which rows are opened and the results are
  identical. An LPDDR4 row is 2 KB, so row-major tile rows land in different rows and the
  contiguous layout saves 9%.

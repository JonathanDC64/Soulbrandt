# Frame rate after breaking obstacles (2026-10-21)

Since ps3recomp #89 there is no stutter at a break. But for a few seconds afterwards the frame rate is visibly lower, unlocked. This note says where that time goes, what was tried, and what is left.

Labels as in docs/48: **measured** (from a run), **read** (seen in the code or the lift), **inferred**.

## Short answer

- For 2-4 s after a break, the average frame rate is about 10% lower (measured). Before a break it is about 235 fps; in the first 2 s after it is about 213 fps.
- The cost is in the frames that step the physics, one in four to eight at these rates with `DES_PHYS_HZ=30` (measured):
  - those frames average 6.8-8.4 ms after a break, against 4.9 ms before it, with 1-3 frames over 16 ms per break;
  - the frames in between barely change (4.2 ms before, 4.3-4.6 ms after).
- Most of the extra time is the main thread waiting for the Havok step (`sys_cond_wait` on cond 0x0D): 2.2-2.5 ms a step after a break, 0.5-0.6 ms calm (measured, console `waits`).
- The step is SPU work. Havok runs four image 7 tasks, then four image 8 tasks. The image 8 phase grows with the debris: 1.3 ms of image 8 time per step calm, 5-33 ms after a break, summed over the four tasks (measured, trace).
- It goes back to normal as the debris goes to sleep. The number of moving bodies goes from about 100 to 150-180 after a break and falls back over 4-6 s (measured, `DES_PHYST`).

## How it was measured

- The 10 replayed breaks from docs/71 (the user's recording, `rep2.txt`), on a copy of that save. Every run was uncapped, `RSX_PRESENT=0`.
- A scratch build with the `DES_PHYST` probe gave step and non-step frame times every 2 s.
- The console `waits 3` command, right after each roll, gave the wait times above.
- Console `trace 2` after each break, with scratch marks for the SPU history events (dispatch, start, exit, by host thread). Per Havok step, it gave the span between the Havok PPU Thread's idle waits (cond 0x13), the main thread's wait, and every image 7/8 job in between.

## What a long step looks like (measured, trace)

A 10.1 ms step, 0.5 ms into which the main thread starts waiting:

- 0.0-2.1 ms: image 7, on four tasks at once;
- 0.7-2.1 ms: image 8 starts, with short jobs at first;
- 1.3-10.1 ms: four image 8 tasks run to the end, and all four finish at the same moment.

The Havok PPU Thread is asleep 93-95% of the time, even after a break (measured, console `prof`), so the PPU side is not the limit.

## The image 8 wait loop (read)

Image 8 (`spu_0007_at_017C5900`) takes work through a semaphore at 0x11B5C:
- GETLLAR on line 0x477FD980 and read a halfword count;
- if the count is 0, read `SPU_RdEventStat` with only the lock-line-lost event enabled (0x400), acknowledge it, and retry;
- otherwise decrement the count with PUTLLC.

Our runtime does not stall on that event read unless the global `SPU_CH_BLOCK` knob is set. The GETLLAR back-off only naps after 20,000 unchanged reads, longer than the tail. So the waiting tasks spin: `SPU_LLAR_HIST=8` counts 1.5-3.5 million GETLLARs of that line per 2 s after a break (measured).

## Tried and dropped: sleeping in that event read

`SPU_LR_WAIT` (scratch, patch kept locally in `work/tmp/scratch_20261021/breaks/lr_wait.patch`): when only the lock-line-lost event is enabled, the reservation is still held and the line is unchanged, nap until a write to the line wakes the SPU (the existing `spu_llar_idle_wait`, 2 ms timeout).

- It fired as intended. Polls of the line fell to under 6,000 per 2 s, and about 1 in 900 naps ran out the timeout (measured).
- **It made the steps slower in every round.** Step frames after a break: 8.1-8.5 ms with it, 6.9 ms without (3 interleaved pairs, measured). Two of the six runs with it had 480-500 ms frames.
- The spinning tasks are what hand jobs over quickly. The image 8 tail is not one long job with three idle tasks: tasks are woken about 15 times a step, and each wake from a sleep costs more than the spin saves (inferred from the nap counts and the slower steps).

## Watch out

- In `spu_rdch_full`, the branch for `SPU_RdEventStat` with `event_mask != 0` comes first and calls `spu_ch_wait`. That returns at once unless `SPU_CH_BLOCK` is set. A new case for a particular event mask has to go before it: the first version of `SPU_LR_WAIT` came after it and never ran.
- `SPU_RATE=<image>` does not work for SPURS task images such as 7 and 8. Its counters mix the per-job context's `steps` and `dbg_getllar`, which start again with every job, with thread-local baselines that do not. Its "steps" and "getllar" columns are garbage there. Its napping and polling shares are of the wall clock, not of the image's own time.
- The SPU pool threads (`spu_async_thread`) run many images: 3, 4, 5, 6, 9 and 10 as well as 7 and 8. A console `prof` of one of them is not a profile of image 8.
- `cpu 1` lists only the busiest threads. The Havok PPU Thread is missing from it unless something is breaking at that moment.
- The wait-on runs had 480-500 ms frames in two of six runs, and calm play was slower too. One bad run is not noise to average away; repeat the pair.

## Tools used (all scratch, never committed)

Copies are kept locally in `work/tmp/scratch_20261021/breaks/`, with a README; `work/` is not in git.


- `rag/physt.inc` and `rag/build_pt.sh`: the `DES_PHYST` probe build (docs/71's probe, built on top of any runtime tree).
- `rag/rep2.txt`: the 10 replayed breaks. `rag/shots.sh` runs a command file on a save copy.
- `rag/brkstat.py <run.txt>`: per run, the two 2 s `[physt]` windows after each roll and the one before each warp. It gives fps, the step-frame mean, the worst frame and the frames over 16 ms.
- `rag/profbrk.sh <tag> <thread pattern> [prof|profrip]`: replays the breaks and profiles one thread for 3 s after each roll. It finds the thread's host id with `cpu 1` during the first break.
- `rag/trace_marks.patch`: a runtime change that writes every SPU history event (`hD`, `hS`, `hX`, `hW`, `hw`, `hG`, `hP`) into the console trace, with the image, the task and the host thread. It also tags `spu_nap`/`spu_wake` with the host thread.
- `rag/stepan.py <trace>`: per Havok step (the span between the Havok PPU Thread's waits on cond 0x13), the main thread's wait on cond 0x0D, the SPU busy time and the image 7/8 jobs. It also prints a job timeline for the three longest steps.
- `rag/jobnap.py <trace>`: per image 7/8 job in the longest steps, the time napped and the time busy. It needs the tagged nap marks and was not run in the end.
- `rag/lr_wait.patch`: the dropped `SPU_LR_WAIT`.

## What is left (inferred)

Update 2026-10-21: docs/77 profiled image 8 on its own. It mostly waits for the Havok PPU Thread, whose `usleep(1)` polls sometimes lost the core (fixed in ps3recomp #97). Items 1 and 2 below were tried there: the region lift is not a consistent gain, and spin-then-sleep lowers the frame rate.

1. **A faster SPU path for Havok's image 8**, as was done for image 2 (docs/48, docs/60, docs/72). The step's tail is image 8's lifted code plus the runtime's per-hop cost (`spu_step_check`, `spu_drain_call` and the DMA helpers show in the pool workers' profiles). This needs a profile of image 8 on its own first, since the pool threads also run images 3-6, 9 and 10.
2. **A spin-then-sleep wait** (spin for some tens of microseconds, then nap; start from `lr_wait.patch`) would keep the fast handoff and free the cores in long waits. The free cores showed no fps gain in these runs, so this is for CPU use, not frame rate.
3. Stepping more often with smaller steps (`DES_PHYS_HZ=60`) does not help. The cost of a step does not shrink with dt (the solver iterates a fixed number of times), so it would double the physics work (inferred from docs/71).

## CPUs with 2-4 cores (2026-11-07)

Players with 4-thread CPUs (an i5-6500, and issue #310's i7-7700 with 8) saw frame drops every time they rolled into barrels or crates. The tests below hold the game to some of this PC's CPUs with `PS3_AFFINITY`. This PC's cores are about twice as fast as an i5-6500's, so 60 fps on 4 cores here is too easy to show it; those runs are uncapped.

Each run warps to the 9 roll spots of `rep2.txt` and rolls into the obstacle there (circle), on a copy of the 10-20 break save. The numbers count the 4 s after each roll (measured).

### The GETLLAR wait (ps3recomp #261)

Image 8's waiting tasks (section "The image 8 wait loop") spun for 20,000 unchanged GETLLARs before the 2 ms nap. At 2-5 us a read that is 40-100 ms of a core per wait. With 16 threads there are cores to spare; with 4 the spinners take time from the threads that make the frame.

ps3recomp #261 naps after 64 unchanged reads (`SPU_LLAR_NAP_AFTER`), about 130-320 us, the same order as RPCS3's spin before it sleeps (400k TSC ticks, about 100-130 us, read). Section "Tried and dropped" still holds: napping at once was slower, so the short spin stays.

| CPUs | Cap | 20,000 reads | 64 reads |
|---|---|---|---|
| 2 cores (`PS3_AFFINITY=5`) | 60 fps | 32.5 and 39.0 fps; 388 and 278 frames over 25 ms | 56.6 and 56.9 fps; 107 and 86 |
| 4 cores, no SMT (`55`) | none | 160.6 and 187.2 fps; 23 and 15 | 203.0 and 205.4 fps; 6 and 4 |
| 16 threads | none | 261.7 fps | 259.7 fps |

### Havok's threads at normal priority (port run default)

With #261, 2 cores still felt choppy away from breaks: 2.6-2.8% of frames over 25 ms, and 3-4 ms of jitter between frames (4 cores: 0.1%, 0.33 ms). Standing outdoors used only 1.14 of the 2 cores, so this is the order threads run in, not the amount of work (measured).

- **In a console trace,** the long frames are the main thread waiting 34-42 ms for the Havok step (cond 0x0D; median 1.35 ms). In each, the Havok PPU Thread makes no call for about 40 ms after it takes the step's event (measured, `sys_event_queue_receive` on queue 5).
- **ps3recomp raises every thread below guest priority 1000** to above normal (`PPU_RAISE_BELOW`, `sys_ppu_thread.c`). It is meant for audio and I/O pollers that sleep between blocks. In this game those are Havok's: hkSpuUtil Helper (80), Havok PPU Thread (100) and SPURuntimeService (200). libmixer's threads go up by name, two steps, apart from this line.
- **The Havok PPU Thread polls the job queue's lock with `usleep(1)`,** which waits in place since ps3recomp #97 (docs/77). Raised, on 2 CPUs it likely keeps the SPU tasks of its own step off the cores (inferred).

2 cores at 60 fps, all with #261's 64 (two rounds, `PPU_RAISE_BELOW=<line>`; gaps from `AUDIO_GAPS=1`):

| Raised (line) | Frames over 25 ms, away from breaks | Jitter | Frames over 25 ms after 9 breaks | Audio gaps |
|---|---|---|---|---|
| all three (1000, the runtime's) | 3.1%, 2.9% | 2.82, 2.78 ms | 68, 88 | 5, 12 |
| Havok PPU + helper (150) | 2.1% | 2.44 ms | 84 | 6 |
| helper only (90) | 0.4%, 0.5% | 0.89, 1.35 ms | 3, 12 | 2, 29 |
| none (0) | 0.5%, 0.6% | 1.06, 1.16 ms | 8, 11 | 1, 4 |

The port's run defaults (`run_defaults`, `port/des_options.cpp`) set `PPU_RAISE_BELOW=0` for this game. The runtime's line stays at 1000 for other titles, such as The Simpsons Arcade Game's music thread at priority 0.

### Watch out

- A 60 fps test on 4 of this PC's cores shows little: after a roll it uses 1.6-1.85 cores. Use 2 cores at 60 fps, or 4 cores uncapped.
- The console `cpu` "os" column counts samples in ntdll and kernel32, and a thread just back from a block is there. Parked pool threads look like "os" time; it is not waste.
- One run in three on 4 cores without SMT at 60 fps (20,000 reads) had frames of 1-5.6 s a few seconds after a roll; the same walk without rolls had none. It is not reproduced with 64 yet.

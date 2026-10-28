# Simulation worker pool

`oa::sim::job::Pool` keeps a fixed set of worker threads for work a
simulation tick can split by index. A tick gathers the inputs it wants
read, calls `run` with one function over a count of items, waits until
every item is done, and then reads the results itself.

## Entry points

`oa/sim/job.hpp` (namespace `oa::sim::job`):

- `Pool` starts its threads in the constructor and joins them in the
  destructor. The count is clamped to `max_workers`; zero keeps no threads
  and `run` does the batch on the calling thread.
- `run` cuts `count` items into one run per thread that receives anything. With `runs` such threads,
  run `n` holds items `n * count / runs` up to `(n + 1) * count / runs`,
  so the items one thread receives depend on
  the thread count and on nothing else. The caller takes the first run.
  `run` returns only after every run has returned.
- `WorkFunction` is the work over one run. It is given the context the
  caller passed and the run's first and past-the-end item index.

## State

The pool owns its threads, the mutex and the two condition variables, and
one slot per worker. A slot holds the function, the context and the run of
the batch in progress. The batch counter is the only signal: a worker
sleeps until it changes, runs the run its slot holds, and marks the slot
done. A batch that gives a worker no items leaves the slot's function null,
and the worker counts that batch as seen so the next one still wakes it.

Nothing here is simulation state. The function reads and writes only the
memory the caller reserved, and two runs of one batch never cover the same
index.

## Invariants

- `run` does not return while any of its runs is still inside the function.
- The runs of one batch are contiguous, ordered by item index and disjoint,
  and together they hold every item.
- An empty batch, or a null function, returns without calling anything.
- `run` is called from one thread. The work function does not call `run`
  on the same pool and does not throw.
- The pool is destroyed only while no batch is running.

## Tests

`sim-job` runs a batch on a pool with no workers and on a pool of three,
checks each item is written exactly once, repeats the batch so the workers'
view of the counter advances, runs a batch smaller than the thread count
and one after it, checks a count past `max_workers` is clamped, and checks
the index-order sum is the same for one thread and for four.

## Limitations

The pool has no way to stop a batch early, and no batch can start while
one is running. The caller provides every buffer; the pool allocates
nothing per batch.

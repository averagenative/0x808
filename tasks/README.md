# Task Coordination System

File-based task distribution for autonomous and human workers.

## Structure

```
tasks/
  engine/     # DSP, audio processing, synth, effects, sampler
  gui/        # Nuklear UI, drum grid, synth editor, virtual keyboard
  plugin/     # VST3/CLAP integration, plugin_gui.c, host communication
  infra/      # Build system, CI, testing, packaging, distribution
  security/   # Thread safety, input validation, memory safety, scanning
```

## Task File Format

Each `.task` file represents one unit of work:

```
status: pending|in_progress|done|blocked
priority: critical|high|medium|low
depends: TASK-NNN (optional)
agent: engine|gui|plugin|infra|security
---
Brief description of what to do.

## Acceptance Criteria
- [ ] Criterion 1
- [ ] Criterion 2

## Test Plan
- [ ] Test 1
- [ ] Test 2
```

## Work Cycle

1. Pick next `status: pending` task (highest priority first)
2. Set `status: in_progress`
3. Implement the change
4. Write/update tests
5. Run `./scripts/test_all.sh` — must pass
6. Set `status: done`
7. Commit with task reference

## Adding a Task

```bash
./scripts/add_task.sh engine high "Fix realloc in audio thread" "TASK-153"
```

## Running Tests

```bash
./scripts/test_all.sh        # Full suite
./scripts/test_all.sh quick   # Unit tests only (no fuzz/sanitizer)
```

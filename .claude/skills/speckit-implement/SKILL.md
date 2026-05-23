---
name: "speckit-implement"
description: "Execute the implementation plan by processing and executing all tasks defined in tasks.md"
argument-hint: "Optional implementation guidance or task filter"
compatibility: "Requires spec-kit project structure with .specify/ directory"
metadata:
  author: "github-spec-kit"
  source: "templates/commands/implement.md"
  # >>> LOCAL PATCH (Antreprenoriat) — DO NOT regenerate blindly <<<
  # Two local patches against upstream:
  # 1. Step 2 ("Check checklists status") was rewritten to route the
  #    pipeline.md §9 CHECKLIST AUDIT through /speckit-checklist-audit and
  #    to remove the weak "proceed anyway? (yes/no)" bypass.
  # 2. Step 5a was added (between step 5 and step 6) to bind the
  #    per-phase executor to the `phase-implementer-sonnet` agent
  #    (`.claude/agents/phase-implementer-sonnet.md` at parent root).
  #    See pipeline.md [H] (2026-05-22) and
  #    `[[feedback_speckit_subagent_phasing]]`.
  # If spec-kit is re-vendored or regenerated, this header WILL be
  # clobbered — re-apply both changes (see `git log` of this file).
user-invocable: true
disable-model-invocation: false
---


## User Input

```text
$ARGUMENTS
```

You **MUST** consider the user input before proceeding (if not empty).

## Pre-Execution Checks

**Check for extension hooks (before implementation)**:
- Check if `.specify/extensions.yml` exists in the project root.
- If it exists, read it and look for entries under the `hooks.before_implement` key
- If the YAML cannot be parsed or is invalid, skip hook checking silently and continue normally
- Filter out hooks where `enabled` is explicitly `false`. Treat hooks without an `enabled` field as enabled by default.
- For each remaining hook, do **not** attempt to interpret or evaluate hook `condition` expressions:
  - If the hook has no `condition` field, or it is null/empty, treat the hook as executable
  - If the hook defines a non-empty `condition`, skip the hook and leave condition evaluation to the HookExecutor implementation
- When constructing slash commands from hook command names, replace dots (`.`) with hyphens (`-`). For example, `speckit.git.commit` → `/speckit-git-commit`.
- For each executable hook, output the following based on its `optional` flag:
  - **Optional hook** (`optional: true`):
    ```
    ## Extension Hooks

    **Optional Pre-Hook**: {extension}
    Command: `/{command}`
    Description: {description}

    Prompt: {prompt}
    To execute: `/{command}`
    ```
  - **Mandatory hook** (`optional: false`):
    ```
    ## Extension Hooks

    **Automatic Pre-Hook**: {extension}
    Executing: `/{command}`
    EXECUTE_COMMAND: {command}
    
    Wait for the result of the hook command before proceeding to the Outline.
    ```
- If no hooks are registered or `.specify/extensions.yml` does not exist, skip silently

## Outline

1. Run `.specify/scripts/bash/check-prerequisites.sh --json --require-tasks --include-tasks` from repo root and parse FEATURE_DIR and AVAILABLE_DOCS list. All paths must be absolute. For single quotes in args like "I'm Groot", use escape syntax: e.g 'I'\''m Groot' (or double-quote if possible: "I'm Groot").

2. **Check checklists status** (if FEATURE_DIR/checklists/ exists):
   - Scan all checklist files in the checklists/ directory
   - For each checklist, count:
     - Total items: All lines matching `- [ ]` or `- [X]` or `- [x]`
     - Completed items: Lines matching `- [X]` or `- [x]`
     - Incomplete items: Lines matching `- [ ]`
   - Create a status table:

     ```text
     | Checklist | Total | Completed | Incomplete | Status |
     |-----------|-------|-----------|------------|--------|
     | ux.md     | 12    | 12        | 0          | ✓ PASS |
     | test.md   | 8     | 5         | 3          | ✗ FAIL |
     | security.md | 6   | 6         | 0          | ✓ PASS |
     ```

   - Calculate overall status:
     - **PASS**: All checklists have 0 incomplete items
     - **FAIL**: One or more checklists have incomplete items

   - **`pipeline.md` step 9 — CHECKLIST AUDIT — is the gate here, NOT a
     yes/no bypass.** The audit is a MANDATORY gate that BLOCKS this step.
     Its executor is the `/speckit-checklist-audit` skill. `requirements.md`
     (the auto-`/specify` spec-quality checklist) is exempt; every *domain*
     checklist in `FEATURE_DIR/checklists/` is in scope.

   - **A domain checklist counts as satisfied ONLY if every box is `[x]`
     AND carries an inline disposition tag** (`PASS:` / `SPEC-FIXED:` /
     `DD-DECIDED §X:` / `WAIVED:<reason>`). Boxes ticked WITHOUT a
     disposition tag are pre-ticked-but-unaudited and do **not** satisfy
     the gate (this is the failure mode that lets §9 be silently skipped).

   - **If any domain checklist has an incomplete OR un-dispositioned box**:
     - Display the table.
     - **STOP** and instruct the user to run `/speckit-checklist-audit`
       (offer to invoke it now). Do NOT offer a "proceed anyway (yes/no)"
       bypass — a genuine Completeness/Clarity/Consistency gap may NEVER be
       waived (pipeline.md §9). Resume `/speckit-implement` only after the
       audit reports GREEN.
     - If the audit SPEC-FIXED anything, `/speckit-analyze` must be re-run
       before continuing (the audit will say so).

   - **If every domain checklist is fully dispositioned (audit GREEN)**:
     - Display the table showing all checklists passed + audited.
     - Automatically proceed to step 3.

3. Load and analyze the implementation context:
   - **REQUIRED**: Read tasks.md for the complete task list and execution plan
   - **REQUIRED**: Read plan.md for tech stack, architecture, and file structure
   - **IF EXISTS**: Read data-model.md for entities and relationships
   - **IF EXISTS**: Read contracts/ for API specifications and test requirements
   - **IF EXISTS**: Read research.md for technical decisions and constraints
   - **IF EXISTS**: Read .specify/memory/constitution.md for governance constraints
   - **IF EXISTS**: Read quickstart.md for integration scenarios

4. **Project Setup Verification**:
   - **REQUIRED**: Create/verify ignore files based on actual project setup:

   **Detection & Creation Logic**:
   - Check if the following command succeeds to determine if the repository is a git repo (create/verify .gitignore if so):

     ```sh
     git rev-parse --git-dir 2>/dev/null
     ```

   - Check if Dockerfile* exists or Docker in plan.md → create/verify .dockerignore
   - Check if .eslintrc* exists → create/verify .eslintignore
   - Check if eslint.config.* exists → ensure the config's `ignores` entries cover required patterns
   - Check if .prettierrc* exists → create/verify .prettierignore
   - Check if .npmrc or package.json exists → create/verify .npmignore (if publishing)
   - Check if terraform files (*.tf) exist → create/verify .terraformignore
   - Check if .helmignore needed (helm charts present) → create/verify .helmignore

   **If ignore file already exists**: Verify it contains essential patterns, append missing critical patterns only
   **If ignore file missing**: Create with full pattern set for detected technology

   **Common Patterns by Technology** (from plan.md tech stack):
   - **Node.js/JavaScript/TypeScript**: `node_modules/`, `dist/`, `build/`, `*.log`, `.env*`
   - **Python**: `__pycache__/`, `*.pyc`, `.venv/`, `venv/`, `dist/`, `*.egg-info/`
   - **Java**: `target/`, `*.class`, `*.jar`, `.gradle/`, `build/`
   - **C#/.NET**: `bin/`, `obj/`, `*.user`, `*.suo`, `packages/`
   - **Go**: `*.exe`, `*.test`, `vendor/`, `*.out`
   - **Ruby**: `.bundle/`, `log/`, `tmp/`, `*.gem`, `vendor/bundle/`
   - **PHP**: `vendor/`, `*.log`, `*.cache`, `*.env`
   - **Rust**: `target/`, `debug/`, `release/`, `*.rs.bk`, `*.rlib`, `*.prof*`, `.idea/`, `*.log`, `.env*`
   - **Kotlin**: `build/`, `out/`, `.gradle/`, `.idea/`, `*.class`, `*.jar`, `*.iml`, `*.log`, `.env*`
   - **C++**: `build/`, `bin/`, `obj/`, `out/`, `*.o`, `*.so`, `*.a`, `*.exe`, `*.dll`, `.idea/`, `*.log`, `.env*`
   - **C**: `build/`, `bin/`, `obj/`, `out/`, `*.o`, `*.a`, `*.so`, `*.exe`, `*.dll`, `autom4te.cache/`, `config.status`, `config.log`, `.idea/`, `*.log`, `.env*`
   - **Swift**: `.build/`, `DerivedData/`, `*.swiftpm/`, `Packages/`
   - **R**: `.Rproj.user/`, `.Rhistory`, `.RData`, `.Ruserdata`, `*.Rproj`, `packrat/`, `renv/`
   - **Universal**: `.DS_Store`, `Thumbs.db`, `*.tmp`, `*.swp`, `.vscode/`, `.idea/`

   **Tool-Specific Patterns**:
   - **Docker**: `node_modules/`, `.git/`, `Dockerfile*`, `.dockerignore`, `*.log*`, `.env*`, `coverage/`
   - **ESLint**: `node_modules/`, `dist/`, `build/`, `coverage/`, `*.min.js`
   - **Prettier**: `node_modules/`, `dist/`, `build/`, `coverage/`, `package-lock.json`, `yarn.lock`, `pnpm-lock.yaml`
   - **Terraform**: `.terraform/`, `*.tfstate*`, `*.tfvars`, `.terraform.lock.hcl`
   - **Kubernetes/k8s**: `*.secret.yaml`, `secrets/`, `.kube/`, `kubeconfig*`, `*.key`, `*.crt`

5. Parse tasks.md structure and extract:
   - **Task phases**: Setup, Tests, Core, Integration, Polish
   - **Task dependencies**: Sequential vs parallel execution rules
   - **Task details**: ID, description, file paths, parallel markers [P]
   - **Execution flow**: Order and dependency requirements

5a. **Per-phase executor binding** (LOCAL PATCH — see metadata header,
   pipeline.md [H] 2026-05-22).

   The canonical executor for each phase's implementation work is the
   `phase-implementer-sonnet` agent at
   `.claude/agents/phase-implementer-sonnet.md` (parent root). The
   orchestrator (Opus main session) SHOULD NOT implement task bodies
   directly — it spawns one `phase-implementer-sonnet` subagent per
   phase via `Agent(subagent_type="phase-implementer-sonnet", ...)`,
   re-verifies the result per the parent-verification checklist
   (`[[feedback_subagent_phase_verification_two_traps]]`,
   `[[feedback_tracking_pmr_resource_false_pass]]`), and only then
   proceeds to the next phase.

   The agent encodes the stable persona (anchor citation, TDD ordering,
   scope discipline, constitutional bindings, anti-pattern library,
   reporting contract) so the orchestrator passes ONLY the per-call
   delta: task IDs in scope, feature directory, design-doc anchor
   path(s), and any phase-specific anchors the orchestrator decided
   matter.

   The orchestrator MAY implement task bodies directly only when (a)
   the agent escalates with a question that requires reading code the
   orchestrator already has cached, OR (b) a phase is trivially one
   task with no test gate (rare — Setup/Polish only). Anything else
   goes through the agent so the persona stays consistent.

   **Between phases — CodeGraph freshness gate.** After each phase
   agent returns, BEFORE spawning the next phase's agent (or before
   handing off to a reviewer in steps 11 / 14), the orchestrator
   verifies the index is fresh:

   1. The agent's reporting contract item #7 must confirm `codegraph
      sync` ran. If missing or unclear, run it from the submodule:

      ```bash
      cd research/G19-fix-fpml-iso20022/library
      codegraph sync
      ```

   2. Run `codegraph status` and confirm the file count is ≥188 (per
      `[[project_codegraph_library_autoresolve]]`; new sources bump
      it). A 0-file count means the index resolved against the empty
      parent root — re-run from the submodule.

   3. If the phase touched codegen (`fixpp-codegen` emitter changes,
      `_codegen/` regeneration), prefer `codegraph index --force`
      over `sync` per the parent CLAUDE.md rule for structural
      changes — the file-count delta will be larger than `sync`
      tolerates cleanly.

   A stale index between phases silently mislabels impact and
   callers, so the next phase agent's `codegraph_impact` lookups
   become unreliable. This gate is cheap (`sync` is incremental); do
   not skip it even when the diff "looks small."

6. Execute implementation following the task plan:
   - **Phase-by-phase execution**: Complete each phase before moving to the next
   - **Respect dependencies**: Run sequential tasks in order, parallel tasks [P] can run together  
   - **Follow TDD approach**: Execute test tasks before their corresponding implementation tasks
   - **File-based coordination**: Tasks affecting the same files must run sequentially
   - **Validation checkpoints**: Verify each phase completion before proceeding

7. Implementation execution rules:
   - **Setup first**: Initialize project structure, dependencies, configuration
   - **Tests before code**: If you need to write tests for contracts, entities, and integration scenarios
   - **Core development**: Implement models, services, CLI commands, endpoints
   - **Integration work**: Database connections, middleware, logging, external services
   - **Polish and validation**: Unit tests, performance optimization, documentation

8. Progress tracking and error handling:
   - Report progress after each completed task
   - Halt execution if any non-parallel task fails
   - For parallel tasks [P], continue with successful tasks, report failed ones
   - Provide clear error messages with context for debugging
   - Suggest next steps if implementation cannot proceed
   - **IMPORTANT** For completed tasks, make sure to mark the task off as [X] in the tasks file.

9. Completion validation:
   - Verify all required tasks are completed
   - Check that implemented features match the original specification
   - Validate that tests pass and coverage meets requirements
   - Confirm the implementation follows the technical plan
   - Report final status with summary of completed work

Note: This command assumes a complete task breakdown exists in tasks.md. If tasks are incomplete or missing, suggest running `/speckit-tasks` first to regenerate the task list.

10. **Check for extension hooks**: After completion validation, check if `.specify/extensions.yml` exists in the project root.
    - If it exists, read it and look for entries under the `hooks.after_implement` key
    - If the YAML cannot be parsed or is invalid, skip hook checking silently and continue normally
    - Filter out hooks where `enabled` is explicitly `false`. Treat hooks without an `enabled` field as enabled by default.
    - For each remaining hook, do **not** attempt to interpret or evaluate hook `condition` expressions:
      - If the hook has no `condition` field, or it is null/empty, treat the hook as executable
      - If the hook defines a non-empty `condition`, skip the hook and leave condition evaluation to the HookExecutor implementation
    - When constructing slash commands from hook command names, replace dots (`.`) with hyphens (`-`). For example, `speckit.git.commit` → `/speckit-git-commit`.
    - For each executable hook, output the following based on its `optional` flag:
      - **Optional hook** (`optional: true`):
        ```
        ## Extension Hooks

        **Optional Hook**: {extension}
        Command: `/{command}`
        Description: {description}

        Prompt: {prompt}
        To execute: `/{command}`
        ```
      - **Mandatory hook** (`optional: false`):
        ```
        ## Extension Hooks

        **Automatic Hook**: {extension}
        Executing: `/{command}`
        EXECUTE_COMMAND: {command}
        ```
    - If no hooks are registered or `.specify/extensions.yml` does not exist, skip silently

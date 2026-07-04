# Upstream CI Merge Plan

**Epic:** Infra
**Design:** N/A

## Summary

Merge current `upstream/master` into `focal` to pick up upstream GitHub
Actions fixes for macOS Xcode selection and recent SVF core maintenance
changes. The immediate trigger is `svf-build #1122` failing in `mac-setup`
before compilation with `Could not find Xcode version that satisfied version
spec: '16.0.0'`.

**Decisions locked in:**
- Prefer a normal upstream merge because `upstream/master` already contains the
  workflow fix and the merge applied without conflicts.
- Preserve unrelated local `Dockerfile`, `Dockerfile.bk`, and `testcase/`
  changes outside the merge commit.
- Verify `svf-harness` after the upstream core merge before pushing.

---

## Phase 1: Inspect CI Failure

### [x] Task 1.1: Identify failing check
- [x] Confirmed latest pushed commit `c6bc64e8` triggered `svf-build #1122`.
- [x] Confirmed failing job was `build (macos-latest)`.
- [x] Confirmed failure happened in `mac-setup` before compilation.

### [x] Task 1.2: Identify upstream fix
- [x] Compared `HEAD..upstream/master` workflow changes.
- [x] Confirmed upstream changes `XCODE_VERSION` to `latest-stable` in
  `.github/workflows/github-action.yml`.
- [x] Confirmed upstream replaces the fixed `/Applications/Xcode_${version}.app`
  symlink with `xcode-select -p` discovery.

## Phase 2: Merge Upstream

### [x] Task 2.1: Protect unrelated local work
- [x] Stashed the local `Dockerfile` modification before merge.
- [x] Left untracked `Dockerfile.bk` and `testcase/` untouched.

### [x] Task 2.2: Merge `upstream/master`
- [x] Ran `git merge --no-commit --no-ff upstream/master`.
- [x] Merge completed without conflicts.

## Phase 3: Verification

### [x] Task 3.1: Build harness
- [x] Ran `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c 'source ./setup.sh >
  /dev/null && cmake --build Release-build --target svf-harness -j2'`.
- [x] `svf-harness` target built successfully.

### [x] Task 3.2: Run focused verification
- [x] Harness Python tests passed 64/64.
- [x] MCP smoke passed 4/4.
- [x] Tutorial examples passed 5/5.
- [x] mdBook coverage checker passed 28/28 methods.
- [x] mdBook build passed.
- [x] Generated Test-Suite artifacts cleaned back to 0.

## Verification

- [x] `env -u LLVM_DIR -u Z3_DIR -u SVF_DIR bash -c 'source ./setup.sh >
  /dev/null && cmake --build Release-build --target svf-harness -j2'`
- [x] `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness python3
  svf-llvm/tools/Harness/tests/run_tests.py -v`
- [x] `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness
  /home/xiao/program/py311-mcp/bin/python mcp/svf_harness_mcp/test_smoke.py -v`
- [x] `SVF_HARNESS_BIN=$PWD/Release-build/bin/svf-harness bash
  svf-llvm/tools/Harness/examples/run_all.sh`
- [x] `python3 docs/harness-book/check_coverage.py --schema
  /tmp/svf-harness-schema.json --book docs/harness-book`
- [x] `/tmp/svf-mdbook-bin/mdbook build docs/harness-book`
- [x] generated Test-Suite artifact count is 0

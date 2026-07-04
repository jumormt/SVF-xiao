# SVF Harness Surface Patterns

## Lazy Analysis Query

For analyses not needed by the core harness bootstrap:

1. Add forward declarations and mutable cache fields in `QueryEngine.h`.
2. Add `getAnalysis()` helper in a dedicated `*Queries.cpp`.
3. Set SVF options to avoid stats, validation, or dot dumps when possible.
4. Silence or isolate unavoidable stdout/stderr and temporary files.
5. Add one or more query methods with stable JSON contracts.
6. Register methods in `QueryEngine::methodTable()`.
7. Document methods in `Schema.cpp`.
8. Add tests and MCP wrapper entries.

Examples already present:

- `CFLAlias`: `cfl_pts`, `cfl_aliases`
- `FlowDDA`: `dda_pts`, `dda_aliases`
- `SABER`: `saber_leaks`, `saber_double_frees`, `saber_file_leaks`
- `MTA`: `mta_summary`, `mta_mhp`
- `AE`: `ae_summary`, `ae_state`

## JSON Contract Rules

- Include `analysis` or `checker` when multiple engines expose similar data.
- Include `total` and `truncated` for capped collections.
- Include evidence records for source locations and graph nodes.
- Keep values machine-readable but do not overfit private SVF internals.
- Treat source locations as optional; bitcode may have empty `file` and line 0.

## Test Rules

- Add focused fixture tests for behavior.
- Add schema/config tests so unsupported/planned drift is caught.
- Add MCP smoke expected tool entries.
- Add at least one direct Test-Suite bitcode smoke when the surface maps to
  existing Test-Suite categories.
- Run full harness and examples before claiming completion.

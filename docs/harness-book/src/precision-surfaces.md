# Precision Surfaces

Question: when should I compare Andersen, CFLAlias, and FlowDDA results, and
what should I record so the comparison is reproducible?

Fixture: `/tmp/demo.ll` from `demo.c`.

## 1. Record The Active Configuration

```bash
Release-build/bin/svf-harness --oneshot analysis_config /tmp/demo.ll
```

Representative output:

```json
{
  "pointer_analysis": {"active": "andersen-wave-diff"},
  "svfg": {"mode": "full"},
  "surfaces": [
    {"name":"dda","status":"supported"},
    {"name":"cfl","status":"supported"},
    {"name":"saber","status":"supported"}
  ]
}
```

Put this next to experiment notes. Precision comparisons without configuration
context are hard to reproduce.

## 2. Establish The Andersen Baseline

```bash
Release-build/bin/svf-harness --oneshot pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

Default `pts` and `aliases` use the active core Andersen analysis. Start here
because it is already built during normal harness loading.

## 3. Ask CFLAlias Only For A Focused Anchor

```bash
Release-build/bin/svf-harness --oneshot cfl_pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

```bash
Release-build/bin/svf-harness --oneshot cfl_aliases \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

CFLAlias is lazy and can be expensive on larger programs. Use it when a
specific alias or points-to question needs another precision view.

## 4. Use FlowDDA For Demand-Driven Checks

```bash
Release-build/bin/svf-harness --oneshot dda_pts \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

```bash
Release-build/bin/svf-harness --oneshot dda_aliases \
  --params '{"var":{"func":"malloc","ret":true}}' /tmp/demo.ll
```

Demand-driven results are useful when one anchor matters. Do not present them
as whole-program exhaustive facts.

## 5. Compare Results Carefully

For each surface, record:

- the anchor JSON;
- `analysis_config`;
- result `total` and `truncated`;
- object evidence for any key target;
- whether the query timed out or was interrupted.

If a result differs, follow with `vfpath` or graph browsing to understand the
effect on the downstream claim.

## Common Pitfalls

- Comparing precision surfaces without saving the config.
- Running broad CFLAlias sweeps over real programs.
- Treating a demand-driven negative as proof that no alias exists anywhere.

## Check Yourself

- Which query gives the default Andersen points-to set?
- Which two methods expose FlowDDA?
- Why should `analysis_config` be copied into experiment notes?

Next: use the selected precision surface to explain value movement.

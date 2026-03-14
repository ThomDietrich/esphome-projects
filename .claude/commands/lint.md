Run ESPHome config validation on device YAML files.

If $ARGUMENTS is provided, treat it as a file name or glob pattern.
If no arguments, validate all *.yaml files in the repo root (exclude common/, include/, system/, homeassistant/).

For each file, run:
```
PIPENV_IGNORE_VIRTUALENVS=1 pipenv run esphome config <file>
```

Validation procedure (follows aurora-smart-home pattern):
1. Check the **exit code** first. Exit code 0 = valid, non-zero = errors present.
2. On failure (non-zero exit code), show the **full stderr output** unfiltered — never grep/filter error output by keyword pattern, as error message wording varies.
3. On success, only suppress INFO-level lines and the config dump. Report any warnings or deprecation notices that appear in stderr.
4. Summarize results per file: PASSED (with warning count) or FAILED (with error details).

If a file requires substitution variables passed via `-s` (like bienenstockwaage.yaml), check the file header comments for the correct invocation and use appropriate values.

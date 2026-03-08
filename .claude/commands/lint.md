Run ESPHome config validation on device YAML files.

If $ARGUMENTS is provided, treat it as a file name or glob pattern.
If no arguments, validate all *.yaml files in the repo root (exclude common/, include/, system/, homeassistant/).

For each file, run:
```
pipenv run esphome config <file>
```

Collect and summarize all errors and warnings. Ignore INFO-level output.
If a file requires substitution variables passed via `-s` (like bienenstockwaage.yaml), check the file header comments for the correct invocation and use appropriate values.

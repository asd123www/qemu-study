#!/usr/bin/env bash
# Usage:
#   ./extract_times.sh                 # scans *downtime*.dat in cwd
#   ./extract_times.sh files...        # or pass specific files
#
# Output (CSV to stdout):
#   workload,downtime_s,migration_s,file

set -euo pipefail
shopt -s nullglob

# Collect inputs
if [[ $# -gt 0 ]]; then
  files=("$@")
else
  files=(*downtime*.dat)
fi

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No input files found (expected *downtime*.dat) and none supplied." >&2
  exit 1
fi

echo "workload,downtime_s,migration_s,file"

for f in "${files[@]}"; do
  [[ -f "$f" ]] || { echo "Skipping non-file: $f" >&2; continue; }

  bn=$(basename -- "$f")
  wkld=$(sed -E 's/.*_downtime_([^./]+)\.dat/\1/' <<<"$bn")
  [[ -n "$wkld" ]] || wkld="unknown"

  awk -v wk="$wkld" -v file="$f" '
    /^vm downtime:/     { dow = $3 }
    /^Migration start:/ { st  = $3 }
    /^Migration end:/   { en  = $3 }
    END {
      if (dow == "" || st == "" || en == "") {
        printf("Error: missing fields in %s (wkld=%s)\n", file, wk) > "/dev/stderr";
        exit 2
      }
      dow_s  = dow / 1e9
      diff_s = (en - st) / 1e9
      printf("%s,%.2f,%.2f,%s\n", wk, dow_s, diff_s, file)
    }
  ' "$f"
done


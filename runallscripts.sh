#!/usr/bin/env bash
set -uo pipefail

# Path to your xojoscript executable
XOSCRIPT="./crossbasic"

# Directory containing .xs scripts
SCRIPT_DIR="Scripts"

# Track whether anything failed
any_failed=0

# Loop over all .xs files in SCRIPT_DIR
for script in "${SCRIPT_DIR}"/*.xs; do
  # If no files match, the glob will remain literal—guard against that:
  if [[ ! -e "$script" ]]; then
    echo "No .xs files found in ${SCRIPT_DIR}."
    break
  fi

  echo "Executing $script"

  # Run the script, but do NOT exit this driver script on failure.
  # Capture the exit code and continue.
  set +e
  "$XOSCRIPT" --s "$script"
  exit_code=$?
  set -e

  if [[ $exit_code -ne 0 ]]; then
    any_failed=1
    echo "❌ Script failed (exit code $exit_code): $script"
  else
    echo "✅ Script succeeded: $script"
  fi

  echo
done

if [[ $any_failed -ne 0 ]]; then
  echo "Finished: one or more scripts failed."
else
  echo "Finished: all scripts succeeded."
fi

# Pause (press any key to continue)
read -n1 -r -p $'\nPress any key to continue...\n'

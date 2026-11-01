#!/usr/bin/env bash
# Source/patch validation only. Never configures, compiles, fetches or deploys.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
for script in "$root"/ps5/tools/*.sh; do bash -n "$script"; done
python3 -B "$root/ps5/tools/setup-submodules.py" --check
python3 -B "$root/ps5/tests/submodules.py"
python3 -B "$root/ps5/tests/maintenance.py"
python3 -B "$root/ps5/tests/controller-ui.py"
python3 -B "$root/ps5/tests/regions.py"
python3 -B "$root/ps5/tests/region-assets.py"
python3 -B "$root/ps5/tests/asset-metadata.py"
python3 -B "$root/ps5/tests/source-layout.py"
python3 -B "$root/ps5/tests/menu-language.py"
python3 -B "$root/ps5/tests/visual-layout.py"
python3 -B "$root/ps5/tests/patch-workflow.py"
python3 -B "$root/ps5/tools/manage-patches.py"

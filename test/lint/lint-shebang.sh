#!/usr/bin/env bash
# Assert expected shebang lines

export LC_ALL=C
EXIT_CODE=0

# We lint for:
# - Last commit files that we skip if doesn't it
# - Current files that are yet to be added

for PYTHON_FILE in $(git ls-files -- "*.py") $(git ls-files --others --exclude-standard -- "*.py"); do
    # Make sure to check to ensure it's not just one that's
    # being deleted since we use git ls-files
    if [[ ! -f "$PYTHON_FILE" ]]; then continue; fi
    if [[ $(head -c 2 "${PYTHON_FILE}") == "#!" &&
          $(head -n 1 "${PYTHON_FILE}") != "#!/usr/bin/env python3" ]]; then
        echo "Missing shebang \"#!/usr/bin/env python3\" in ${PYTHON_FILE} (do not use python or python2)"
        EXIT_CODE=1
    fi
done
for SHELL_FILE in  $(git ls-files -- "*.sh") $(git ls-files --others --exclude-standard -- "*.sh"); do
    # Make sure to check to ensure it's not just one that's
    # being deleted since we use git ls-files
    if [[ ! -f "$SHELL_FILE" ]]; then continue; fi
    if [[ $(head -n 1 "${SHELL_FILE}") != "#!/bin/bash" &&
            $(head -n 1 "${SHELL_FILE}") != "#!/usr/bin/env bash" &&
            $(head -n 1 "${SHELL_FILE}") != "#!/bin/sh" ]]; then
        echo "Missing expected shebang \
            \"#!/bin/bash\" or \
            \"#!/usr/bin/env bash\" or \
            \"#!/bin/sh\" in ${SHELL_FILE}"
        EXIT_CODE=1
    fi
done
exit ${EXIT_CODE}

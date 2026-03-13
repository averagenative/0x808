#!/bin/bash
# Usage: ./scripts/add_task.sh <agent> <priority> <description> [task-ref]
# Example: ./scripts/add_task.sh engine high "Fix realloc in audio thread" "TASK-153"

set -e

AGENT="${1:?Usage: add_task.sh <agent> <priority> <description> [task-ref]}"
PRIORITY="${2:?Missing priority (critical|high|medium|low)}"
DESCRIPTION="${3:?Missing description}"
REF="${4:-TASK-XXX}"

# Validate agent
case "$AGENT" in
    engine|gui|plugin|infra|security) ;;
    *) echo "Error: agent must be engine|gui|plugin|infra|security"; exit 1 ;;
esac

# Validate priority
case "$PRIORITY" in
    critical|high|medium|low) ;;
    *) echo "Error: priority must be critical|high|medium|low"; exit 1 ;;
esac

# Generate filename from description
SLUG=$(echo "$DESCRIPTION" | tr '[:upper:]' '[:lower:]' | tr ' ' '-' | tr -cd 'a-z0-9-' | head -c 50)
FILENAME="tasks/${AGENT}/${REF}-${SLUG}.task"

cat > "$FILENAME" << EOF
status: pending
priority: ${PRIORITY}
depends: none
agent: ${AGENT}
ref: ${REF}
---
${DESCRIPTION}

## Acceptance Criteria
- [ ] TODO: Define acceptance criteria

## Test Plan
- [ ] TODO: Define test plan
EOF

echo "Created: ${FILENAME}"
echo "Edit the file to add acceptance criteria and test plan."

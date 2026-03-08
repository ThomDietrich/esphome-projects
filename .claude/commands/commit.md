Commit all pending changes in this repository, grouping them into well-structured, logical commits.

## Process

1. Run `git status` and `git diff` (staged + unstaged) to understand all pending changes.
2. Analyze the changes and group them by concern — each commit should address one logical topic (e.g., "fix linter issues", "add new device", "refactor templates", "update dependencies").
3. Present the proposed commit plan to the user for approval before executing.
4. Execute the plan using hunk-level staging (`git add -p` with split when needed) to separate unrelated changes within the same file into different commits.
5. Write concise commit messages that explain the *why*, not the *what*. Use imperative mood. Add a body with bullet points when a commit touches multiple files.

## Rules

- Never combine unrelated changes in one commit.
- If a single file contains changes for multiple concerns, use hunk-level staging to split them.
- Do not add "Co-Authored-By" lines.
- Do not push to remote unless explicitly asked.
- If $ARGUMENTS is provided, use it as guidance for how to group or what to commit (e.g., "only heizungsregelung changes" or "squash into one commit").

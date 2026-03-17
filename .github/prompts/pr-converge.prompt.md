---
description: Converge a pull request to merge-ready state
---

# Pull Request Convergence

Bring the current pull request to a **merge-ready state**.

This requires:

1. Addressing all PR review comments.
2. Ensuring all CI checks for the branch are green.
3. Iterating until the pull request stabilizes.

Use the **gh tool** to interact with GitHub.

---

# Convergence Loop

Work iteratively until all conditions are satisfied.

A PR is considered converged when:

- all review comments have been addressed
- all review threads are resolved
- all CI checks for the branch are passing
- the repository builds successfully

Continue iterating until this state is reached.

---

# Step 1 — Review Comments

Using the gh tool:

1. Retrieve all review comments on the pull request.
2. Process each comment individually.

Default assumption:

Every comment is **potentially valid**.

For each comment:

1. Read the comment and surrounding code.
2. Investigate the concern raised.
3. Determine whether the issue is:

- a real defect
- a valid improvement
- a misunderstanding
- already resolved
- no longer applicable

---

# Step 2 — Implement Fixes

If the comment identifies a real issue:

1. Implement the fix.
2. Keep the fix minimal and focused.
3. Follow repository coding standards.
4. Add a regression test if the issue represents a bug.

After implementing the fix:

- respond to the review comment explaining the change
- resolve the thread using the gh tool

Example response:

"Implemented the suggested change. Input validation is now performed before the API call and a regression test was added."

---

# Step 3 — Handle Non-Applicable Comments

If the comment is not applicable:

1. Verify the reasoning against the code.
2. Write a concise technical explanation.

Example:

- "This behavior is already enforced in `<file>`."
- "This was resolved earlier in the PR."
- "The current implementation intentionally behaves this way because `<technical reason>`."

Then resolve the comment thread.

Never resolve a comment without explanation.

---

# Step 4 — Validate the Repository

After addressing comments:

1. Ensure the repository builds successfully.
2. Run the minimal validation required by the changes.

Follow repository validation rules.

Examples:

```
npm run lint
npm run test
npm run build
```

Add additional validation if relevant to the touched components.

---

# Step 5 — Push Fixes

If changes were made:

1. Commit the fixes.
2. Push them to the current branch.

Use the gh tool where appropriate.

---

# Step 6 — Check CI Status

Using the gh tool:

1. Retrieve the CI status for the current branch.
2. Identify any failing checks.

If failures exist:

1. Read the CI logs.
2. Determine the root cause.
3. Implement the fix locally.
4. Commit and push the fix.

Repeat the process.

---

# Step 7 — Wait for CI

After pushing fixes:

1. Wait for CI workflows to complete.
2. Re-check their status.

If any job fails:

- analyze the failure
- implement a fix
- push again

Continue until **all CI checks pass**.

---

# Convergence Criteria

Stop only when all of the following are true:

- no unresolved review comments remain
- every comment thread has a response
- all CI checks for the branch are passing
- the repository builds successfully

---

# Completion Summary

Provide a concise summary including:

- fixes implemented
- review comments resolved
- CI failures addressed
- confirmation that all checks are green

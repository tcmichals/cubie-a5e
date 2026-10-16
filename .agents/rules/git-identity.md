# Git Author Identity & Email Policy

## MANDATORY IDENTITY RULE
Across ALL Git repositories, commits, patches, and configurations in this workspace:

* **Name**: `Tim Michals`
* **Email**: `tcmichals@gmail.com`

## STRICT PROHIBITION
* **NEVER** use `tcmichals@verityinst.com`, `tmichals@verityinst.com`, or any `@verityinst.com` domain.
* **NEVER** include `Co-Authored-By` (or other) trailers for AI assistants (e.g. Claude, Gemini, ChatGPT) in commit messages.
* An AI **never** adds a `Signed-off-by` (only a human can certify the DCO, `Documentation/process/coding-assistants.rst`); the owner adds his own after reviewing.
* Upstream kernel series **disclose** AI assistance in a short "Tools" section of the cover letter (`Documentation/process/generated-content.rst`). Decided 2026-10-15.
* All commits, tags, Signed-off-by tags, and committers MUST strictly use `Tim Michals <tcmichals@gmail.com>`.
* Before running any `git commit`, verify that `user.email` is set to `tcmichals@gmail.com`.


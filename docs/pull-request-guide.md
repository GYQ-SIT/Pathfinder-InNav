# How we work with pull requests

Every change to `main` goes through a pull request (PR) that **someone other than the author reviews and approves**. This page is the
whole process, step by step. It is written for someone who has never opened a PR: read it once, then use the checklists.

Why we do it: a PR is the place where a second person reads the change and asks "why is it done this way?". The author has to explain
the code, and the reviewer has to understand it. That is how every member comes to understand the whole product, including code written
with the help of AI. **AI can write the code; the author and the reviewer still have to understand it.**

## The flow at a glance

```
1. Pick a Jira story (PAT-123)
2. Branch from an up-to-date main
3. Make small commits, each starting with PAT-123
4. Check on your own machine: lint, tests
5. Review your own diff   <-- before the PR, not instead of the review
6. Push the branch, open the PR, ask one person to review
7. The reviewer reads, asks, approves (or requests changes)
8. Answer every comment, push the fixes
9. After the approval, the author merges, deletes the branch, updates local main
```

The rules, short:

- Never commit or push directly to `main`.
- Never approve your own PR. One approval from another person is needed. (This is the team rule; GitHub enforces it once branch
  protection is on for `main`.)
- A PR does one thing. Small PRs get reviewed faster and better.
- A PR you cannot explain is not ready, even if it works.

## One-time setup

```bash
git clone https://github.com/GYQ-SIT/Pathfinder-InNav.git
cd Pathfinder-InNav
git config user.name  "Your Name"
git config user.email "the email of your GitHub account"
```

Use the e-mail of your own GitHub account. The contributors page and the statistics only count commits whose e-mail is linked to a
GitHub account, so a wrong e-mail makes your work invisible. Check with `git log -1` after your first commit.

## Step by step

### 1. Pick the story

Every piece of work is a story in the Jira project **PAT**. Note its key, for example `PAT-123`. One owner per story. If there is no
story for your work, ask for one before you start.

### 2. Make a branch from an up-to-date `main`

```bash
git checkout main
git pull
git checkout -b YQ_nav_panel
```

The name is your initials, an underscore and what it is: `YQ_nav_panel`, `PL_wall_shading`, `WS_testing_guide`. One branch per story.
Never work on `main` itself.

### 3. Commit in small steps

```bash
git status                      # what changed
git add path/to/the/files       # add the files you mean, not everything
git commit -m "PAT-123 Short summary of what changed"
```

- Start every commit message with the Jira key, then one short sentence in English that says what changed and why it matters.
- Add files by name. Do not use `git add .` blindly: it picks up personal files (your own `buildings.json`, saved layouts, keys) that must
  never be committed. `python scripts/lint.py` fails on some of these, but do not rely on it.
- Commit often. Ten small commits are easier to review than one huge one.

### 4. Check on your own machine

A PR gets only the lint and JSON checks on GitHub. The build, static analysis and tests run **after** the merge on `main`, so a
mistake you do not catch now lands in `main` first. Before you open the PR:

```bash
python scripts/lint.py
```

Then, with the Unreal editor closed, run the tests of the part you changed (the full command and the static analysis are in `README.md`):

```bash
UnrealEditor-Cmd.exe InNavi/InNavi.uproject -ExecCmds="Automation RunTests InNavi.Navigation; Quit" -unattended -nullrhi -nosplash
```

Replace `InNavi.Navigation` with the group you touched (`InNavi.Mobile`, `InNavi.LayoutValidation`, ...), or use `InNavi` for all. If you
changed behaviour, add or change a test for it. If you cannot build or run something, say so in the PR; do not leave it out silently.

### 5. Review your own diff first

This is the step most people skip, and it is the one the professor asked for: **review your own change before anybody else does.**

```bash
git diff main...HEAD
```

Read every line as if somebody else wrote it. Ask yourself:

- Can I explain what each changed block does, and why it is written this way?
- Is there anything I do not understand (for example code AI wrote for me)? Ask the AI to explain it, or ask a teammate, now.
- Is there debug code, a commented-out block, a personal file, a key or an address?
- Does the change do one thing, or did unrelated changes sneak in?

### 6. Push and open the PR

```bash
git push -u origin YQ_nav_panel
```

Git prints a link; open it, or go to the repository on GitHub and press **Compare & pull request**. Check the top of the page: the base
must be `main` and the compare branch must be yours. Fill in the template that appears:

- **What and why**: one or two sentences. What does the change do and why is it needed?
- **Jira**: the issue key. Tick the box if this PR finishes the story.
- **Checks**: tick only what is true. Write "not run, because ..." instead of ticking something you did not do.
- Add a screenshot or a short clip when the change is visible (UI, 3D, the phone app).

Then choose a reviewer under **Reviewers** (right side) and tell them in the team chat that the PR is ready. Pick a reviewer who does not
already know the code: that is the point of the review. Anyone in the team can review.

### 7. Reviewing someone else's PR

Open the PR, then **Files changed**. First read the description, then read the diff.

What a reviewer checks:

1. **Understand it.** Can you say in your own words what the change does? If not, ask. "Why is it done this way?" is a good review comment.
2. **Does it match the story?** Nothing missing, nothing extra.
3. **Is it correct?** Look for missed cases (empty list, first or last item, missing data), and for anything that could crash.
4. **Tests.** Is there a test for the new behaviour? Does the test really fail without the change?
5. **Docs.** If a document describes this part (`docs/`, `README.md`), is it updated?
6. **UI.** If the change adds, changes or removes anything on screen, check it against "UI changes follow our global UI way" below, and ask the author to follow it. Do not approve a UI change that makes its own way of building a panel, a menu row or a style.
7. **Safety.** No personal files, keys or addresses; no generated files; no unrelated formatting changes.
8. **Run it** when the change is visible or risky: check out the branch and try it.

```bash
git fetch
git checkout YQ_nav_panel
```

To leave a comment, hover over a line in **Files changed**, press the **+** button and write it. When you are done, press
**Review changes** (top right of that page) and choose one:

- **Comment**: only remarks, no decision yet.
- **Approve**: you read it, you understand it, it can be merged.
- **Request changes**: something has to change first; say what.

Write comments about the code, not the person. Say what you see and why it matters: "this loop runs once for each room, so it is slow
for 225 rooms; could we build the list first?" is better than "slow". Mark small remarks as optional ("nit:") so the author knows what is
required.

A reviewer should answer within two days. If you cannot, say so and ask someone else.

### 8. Answering the review

- Read every comment. Fix it, or reply with a reason. Do not ignore one.
- Push the fixes to the **same branch**; the PR updates by itself:

```bash
git add path/to/the/files
git commit -m "PAT-123 Address review: check for an empty room list"
git push
```

- When a comment is dealt with, reply "Done" or press **Resolve conversation**.
- Do not force-push (`git push --force`) a branch someone is reviewing: it throws away their comments' context.
- When you are done, press **Re-request review** next to the reviewer's name.

### 9. Merge and clean up

When the PR has an approval from someone else and the lint and JSON checks are green, the **author** presses **Merge pull request**
(the merge commit is what our history shows), then **Delete branch**. Then update your own machine:

```bash
git checkout main
git pull
git branch -d YQ_nav_panel
```

After the merge, check that the build on `main` stays green (the **Actions** tab). If it fails because of your change, fixing it comes
first. Then move the Jira story to Done if the PR finished it.

## UI changes follow our global UI way

Every UI addition, change or removal is built the way the rest of the UI is built. As the author, read `docs/design/ui-panels-guide.md` before you
start; as the reviewer, check the PR against this list and ask the author to change whatever does not follow it.

- **A panel is a dock panel.** It subclasses `UDockPanelContentWidget`, is registered with the screen's dock host, and has one check box in the Window
  menu that only shows or hides it. Nothing is added straight onto a screen's root canvas (the `InNavi.UI.Guard.*` tests fail if it is).
- **Reuse, do not reinvent.** Build controls with the existing helpers and styles (`StartStyle::*`, `ApplyStandardFont`, the dropdown row helpers,
  `AddCheckRow`, `PlayButtonClickSound` on every button). Give every control a stable name (`Btn_`, `Chk_`, `Txt_`, `Cbx_`, `Pnl_`) so tests can find it.
- **Respect the sizes the helpers fix.** A button row of a dropdown is 120 wide, so its label is short ("Reset Layout"); a dock panel sizes its controls
  from the width the dock gives it. Text wraps instead of running out of its box.
- **Handlers live on the screen**, like every other menu row (`OnWindowXxxClicked`), and both editor screens (exterior and interior) are kept in step.
- **Tested.** A new kind of control or rule comes with a test next to `InNavi.UI.Panels.*` in `UiPanelTests.cpp`; a check that measures the result
  (Slate's own measure of the controls) is better than a note saying "looks fine". The whole `InNavi.UI.*` group passes.
- **Documented.** The guide (`ui-panels-guide.md`) or `interior-editor-guide.md` says what the new thing does and where it starts.
- **Shown.** The PR has a screenshot of the result. The reviewer looks at it too, instead of trusting the author's word.

## When something goes wrong

| Problem | What to do |
|---|---|
| `git push` is rejected | `git pull` first (or ask for help). Do **not** use `--force` to get around it. |
| "This branch has conflicts" | Update your branch: `git checkout main`, `git pull`, `git checkout YQ_nav_panel`, `git merge main`. Fix the files marked with `<<<<<<<`, `git add` them, `git commit`, `git push`. If it is more than a few lines, ask. |
| I committed to `main` by mistake | Do not push. Make a branch from it (`git checkout -b YQ_my_change`), then ask for help to put local `main` back. |
| I committed a personal file or a key | Tell the tech lead before you push. A key that was pushed must be replaced, even if the commit is removed later. |
| The PR got big | Split it: one PR per story, and one story per piece you can explain in two minutes. |
| Nobody reviews it | Ask in the team chat after two days; name the reviewer you chose. |

## The checklist

**Author, before pressing "Create pull request":**

- [ ] Branch from a fresh `main`; it holds one story
- [ ] Commits start with the Jira key
- [ ] `python scripts/lint.py` is green; the tests of my part ran
- [ ] I read my own diff and can explain every block
- [ ] No personal files, keys, debug code
- [ ] If it touches the UI: it follows "UI changes follow our global UI way", `InNavi.UI.*` pass, a screenshot is in the PR
- [ ] The template is filled in; a reviewer is chosen and told

**Reviewer, before pressing "Approve":**

- [ ] I understand what the change does and why
- [ ] It matches the story; nothing extra
- [ ] Edge cases, tests and docs looked at
- [ ] I ran it, if it is visible or risky
- [ ] A UI change follows "UI changes follow our global UI way" (dock panel, existing helpers and styles, handlers on the screen, both screens, tests, screenshot)
- [ ] Every question I had is answered

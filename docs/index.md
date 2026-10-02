# InNavi Wiki

Team **Pathfinder** · Project **InNavi** · CSD3401 / UXG3400 (Fall 2026)

An Unreal Engine application that adds an interior layer to the school's 3D model:
select a building, author or import its interior layout, generate a schematic 3D
interior, and navigate inside it - including across floors.

## Pages

- [Proposal](proposal.md) - what we are building and why
- [Architecture](architecture.md) - system design and tech stack
- [Toolchain Decision](toolchain-decision.md) - stack/platform decisions and the reasoning behind them
- [Deliverables](deliverables.md) - ownership and M1/M2/M3 targets
- [Interior Editor Guide](design/interior-editor-guide.md) - how to use the interior editor
- [Interior Editor UI Spec](design/interior-editor-ui-spec.md) - editing tools and the C++ interface for UI
- [Product Design](design/product-design.md)

## How to edit this wiki

Every page is a Markdown file under `docs/` in the project repository
(`GYQ-SIT/Pathfinder-InNav`).

1. Edit the file - either directly on GitHub (open the file, click the pencil icon) or
   locally, then commit.
2. To add a page, create a new `.md` file under `docs/` and list it under `nav:` in
   `mkdocs.yml` at the repository root.
3. Push to `main`. The **Deploy wiki** GitHub Actions workflow rebuilds the site and
   publishes it to the team server automatically - the updated page is live about a
   minute later.

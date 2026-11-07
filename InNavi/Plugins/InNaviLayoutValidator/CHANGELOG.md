# Changelog

Versions follow Semantic Versioning. Changes to exported report types or tool interface methods require a major version when incompatible.

## 1.1.1

- Allow a simple open-wall path to return to its starting point without reporting its closing joint as a self-intersection.

## 1.1.0

- Add cancellable data validation, report duplicate mobile place IDs, and enforce the editor's supported scale range.

## 1.0.1

- Detect overlapping room areas regardless of vertex order or collinear edges, while allowing rooms to share an edge or corner.

## 1.0.0 — 2026-10-27

- Initial versioned Unreal Runtime plugin release with read-only layout validation, stable issue codes, snapshot fingerprints and JSON report export.
- Public `LayoutValidation` API independent of InNavi game actors and UnrealEd.
- `LayoutTools::IProvider` / `FRegistry` extension contract and `ILayoutToolPanel` optional UI adapter contract; module lifecycle registration and duplicate-ID rejection.
- Standalone Unreal consumer example with valid and invalid data.
- GitHub Packages source distribution and exact-version consumer installation.

# Deprecation Policy

This document defines how norvex manages API deprecations and breaking changes.

## Policy Overview

We follow [Semantic Versioning](https://semver.org/) (SemVer) and provide clear migration paths for deprecated features.

### Deprecation Timeline

| Stage | Duration | Behavior |
|-------|----------|----------|
**Deprecated** | 2 minor versions | Runtime warning emitted via `process.emitWarning` |
**Obsolete** | Until next major | Feature still works, warning continues |
**Removed** | Major version bump | Feature removed, may throw error |

### Example Timeline

```
v1.0: Feature introduced
v1.3: Feature deprecated (warning added)
v1.4: Deprecation warning continues
v1.5: Deprecation warning continues
v2.0: Feature removed (breaking change)
```

## Version Guarantees

### Major Versions (X.0.0)
- **May** contain breaking changes
- All deprecated features from previous major may be removed
- Migration guide published with release

### Minor Versions (x.Y.0)
- **No** breaking changes
- **May** deprecate features (with runtime warnings)
- New features added

### Patch Versions (x.y.Z)
- **No** breaking changes
- **No** new deprecations
- Bug fixes only

## Runtime Deprecation Warnings

Deprecated features emit warnings via `process.emitWarning()`:

```
(node:12345) DeprecationWarning: norvex: 'legacyMode' option is deprecated since v1.3.0, will be removed in v2.0.0. Use 'mode: "modern"' instead.
```

### Enabling Deprecation Warnings

By default, warnings are printed to stderr. Additional flags:

```bash
# Show pending deprecations (not yet active by default)
node --pending-deprecation app.js

# Show all deprecation warnings
node --throw-deprecation app.js      # Throws exceptions instead of warnings
node --trace-deprecation app.js      # Shows stack traces
node --no-deprecation app.js         # Silence warnings (not recommended)
```

## CI Integration

All tests run with `--pending-deprecation` to surface future deprecations early:

```json
{
  "scripts": {
    "test": "node --pending-deprecation --test",
    "test:strict": "node --throw-deprecation --test"
  }
}
```

## Migration Guides

Each major version includes a migration guide covering:
- All removed features
- Replacement APIs
- Code transformation examples
- Timeline for migration

## Communication Channels

Deprecations are announced via:
1. Runtime warnings (immediate feedback)
2. CHANGELOG.md (version history)
3. GitHub Releases (release notes)
4. npm deprecation warnings (for package-level deprecations)

## For Maintainers

### Adding a Deprecation

1. Document in CHANGELOG.md under "Deprecations"
2. Add runtime warning using `emitDeprecationWarning()`
3. Include removal version in warning message
4. Provide migration example in JSDoc

### Removing a Deprecated Feature

1. Ensure 2 minor versions have passed since deprecation
2. Remove feature and associated warnings
3. Document in MIGRATION.md
4. If used at runtime, throw clear error directing to migration guide

## References

- [Node.js Deprecation Policy](https://nodejs.org/api/deprecations.html)
- [SemVer Specification](https://semver.org/)
- [MIGRATION.md](./MIGRATION.md) - Version-specific migration guides

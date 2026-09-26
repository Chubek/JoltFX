# Contributing to JoltFX

Thank you for your interest in contributing!

## Getting Started

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run tests: `ctest --test-dir build`
5. Format code: `./scripts/format.sh`
6. Commit with clear messages
7. Push and create a pull request

## Code Style

- C code follows the `.clang-format` configuration
- Rust code follows `rustfmt.toml`
- Keep functions focused and well-documented

## Testing

All new features must include tests. Run the full test suite before submitting.

## Documentation

Update relevant documentation in `docs/` when adding or changing features.


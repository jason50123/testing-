# SimpleSSD ISC Module

## Dependencies

- `clang++` (`clang` in apt)
- `libstdc++` (`libstdc++-???-dev` in apt)
  - find the package version with `apt-cache search --names-only '^libstdc\+\+-.*-dev$'`
- `libext2fs` (`libext2fs-dev` in apt)
- `libcom_err` (`comerr-dev` in apt)
- `zlib` (`zlib1g-dev` in apt)
- `libzstd` (`libzstd-dev` in apt)
- `libpcre++` (`libpcre++-dev` in apt)

## Build and Test

```bash
$ make test
```

(target (schema 1) (id QBE) (kind native) (status implemented)
 (driver qbe) (description "Typed i64/bool native scalar compiler")
 (artifacts qbe asm obj bin) (platforms amd64 arm64 rv64) (requires qbe keystone)
 (features i64 bool functions recursion control-flow))

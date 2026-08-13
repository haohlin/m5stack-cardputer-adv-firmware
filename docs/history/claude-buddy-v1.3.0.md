# Claude Buddy v1.3.0 historical baseline

Source recovered from `claude-desktop-buddy-m5stack-cardputer-adv` working tree.
Build only with `apps/claude-buddy` environment `cardputer-adv`.
Observed historical raw image SHA-256:
`e969dbfe9fd2f444c477e6df0d9bcd9c7d564ce203f80c2f4815d0e58bcb5051`.
Clean rebuild SHA-256: `3cff66a91a7efdea96eb94272ba84bf0b435c9bcd52450925b1a00ce0e15b7b1`.
Difference is expected: historical source used floating M5Unified/M5GFX
dependencies; clean build resolves newer compatible versions.
This predates Launcher OTA support. It is preserved for source and binary
reproducibility only; never stage, install, or direct-flash it.

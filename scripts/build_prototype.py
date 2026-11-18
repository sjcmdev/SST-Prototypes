#!/usr/bin/env python
from __future__ import annotations

from prototype_cli import build_parser, build_target, prototype_name


def main() -> int:
	args = build_parser("Build one prototype executable.").parse_args()
	build_target(prototype_name(args.prototype), args.configuration, args.dry_run)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())


#!/usr/bin/env python
from __future__ import annotations

from prototype_cli import build_parser, build_target, target_executable_path, prototype_name, run_executable


def main() -> int:
	args = build_parser("Build and run one prototype test executable.").parse_args()
	prototype = prototype_name(args.prototype)
	test_target = f"{prototype}Tests"
	build_target(test_target, args.configuration, args.dry_run)
	return run_executable(target_executable_path(test_target, args.configuration), args.dry_run)


if __name__ == "__main__":
	raise SystemExit(main())


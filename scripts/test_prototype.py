#!/usr/bin/env python
from __future__ import annotations

from prototype_cli import build_parser, build_target, output_dir, prototype_name, run_executable


def main() -> int:
	args = build_parser("Build and run one prototype test executable.").parse_args()
	prototype = prototype_name(args.prototype)
	test_target = f"{prototype}Tests"
	build_target(test_target, args.configuration)
	return run_executable(output_dir(args.configuration) / test_target / f"{test_target}.exe")


if __name__ == "__main__":
	raise SystemExit(main())


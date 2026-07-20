import argparse
import csv
from pathlib import Path


def read_columns(path):
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle, delimiter="\t")
        header = next(reader)
        columns = {name: [] for name in header}
        for row in reader:
            for index, name in enumerate(header):
                if index < len(row) and row[index]:
                    columns[name].append(float(row[index]))
    return columns


def triple(columns, prefix):
    keys = [key for key in columns if key.startswith(prefix + "_")]
    if len(keys) < 3:
        return [], [], []
    return columns[keys[0]], columns[keys[1]], columns[keys[2]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--xlabel", required=True)
    parser.add_argument("--ylabel", required=True)
    parser.add_argument("--color1", default="#FF1F14FF")
    parser.add_argument("--color2", default="#FFE60FFF")
    parser.add_argument("--color3", default="#00FFE8FF")
    parser.add_argument("--xlim", nargs=2, type=float)
    parser.add_argument("--ylim", nargs=2, type=float)
    args = parser.parse_args()

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    columns = read_columns(args.input)
    classes = [
        ("2_3_sigma", "2-3 sigma", args.color3),
        ("1_2_sigma", "1-2 sigma", args.color2),
        ("le_1_sigma", "<= 1 sigma", args.color1),
    ]

    fig, ax = plt.subplots(figsize=(8, 7), dpi=150)
    for prefix, label, color in classes:
        x, y, _ = triple(columns, prefix)
        if x and y:
            ax.scatter(x, y, s=8, c=color, label=label, edgecolors="none")

    ax.set_title(args.title)
    ax.set_xlabel(args.xlabel)
    ax.set_ylabel(args.ylabel)
    ax.grid(True, which="both", alpha=0.35)
    ax.tick_params(axis="both", which="both", direction="in", top=True, right=True)
    if args.xlim:
        ax.set_xlim(args.xlim)
    if args.ylim:
        ax.set_ylim(args.ylim)
    ax.legend(frameon=True)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(args.output)
    plt.close(fig)


if __name__ == "__main__":
    main()

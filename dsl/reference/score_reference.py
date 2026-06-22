#!/usr/bin/env python3
"""Plaintext reference scorer for Furever Home (FHE design Stage 3: ground truth).

Computes exactly what the homomorphic circuit in server.niob computes, but in the
clear, so the decrypted FHE result can be checked against it.

Scoring (linear, multiplicative depth 1):
    category_score[pet][cat] = sum_q answers[q] * W[pet][cat][q] + offset[pet][cat]
    overall[pet]             = sum_cat category_score[pet][cat]

Raw category scores can be negative (a demanding pet you fall short of); the
display clamps them to 0, but --json emits the raw values so they line up with
the (unclamped) decrypted FHE result.

Reads the SAME weight file the C++ bridge reads (dsl/rubric.dat), so the two
implementations cannot drift.

Usage:
    score_reference.py a0 a1 ... a11          # 12 answers, each 0..4
    score_reference.py --json a0 ... a11       # machine-readable output
    score_reference.py                         # uses a default profile
"""
import os
import sys
import json

CATEGORIES = ["Housing", "Time", "Finances", "Experience"]
NUM_QUESTIONS = 12
NUM_CATEGORIES = 4

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_RUBRIC = os.path.normpath(os.path.join(HERE, "..", "rubric.dat"))


def load_rubric(path=DEFAULT_RUBRIC):
    """Parse rubric.dat -> list of dicts: {emoji, name, weights[cat][q]}."""
    pets = []
    cur = None
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("PET"):
                # "PET <emoji> <name...>"
                parts = line.split(None, 2)
                emoji = parts[1] if len(parts) > 1 else "?"
                name = parts[2] if len(parts) > 2 else "Unknown"
                cur = {"emoji": emoji, "name": name, "weights": [], "offsets": []}
                pets.append(cur)
            else:
                vals = [float(x) for x in line.split()]
                if len(vals) != NUM_QUESTIONS + 1:
                    raise ValueError(
                        f"category line has {len(vals)} values, expected "
                        f"{NUM_QUESTIONS + 1} (12 weights + 1 offset): {line}"
                    )
                if cur is None:
                    raise ValueError("weight line before any PET header")
                cur["weights"].append(vals[:NUM_QUESTIONS])
                cur["offsets"].append(vals[NUM_QUESTIONS])
    for p in pets:
        if len(p["weights"]) != NUM_CATEGORIES:
            raise ValueError(
                f"pet {p['name']} has {len(p['weights'])} category rows, expected {NUM_CATEGORIES}"
            )
    return pets


def score(answers, pets):
    """Return list of {emoji, name, categories[cat], overall} per pet."""
    results = []
    for p in pets:
        cats = []
        for c in range(NUM_CATEGORIES):
            w = p["weights"][c]
            cats.append(
                sum(answers[q] * w[q] for q in range(NUM_QUESTIONS)) + p["offsets"][c]
            )
        results.append(
            {
                "emoji": p["emoji"],
                "name": p["name"],
                "categories": cats,
                "overall": sum(cats),
            }
        )
    return results


def main(argv):
    as_json = False
    args = [a for a in argv if a != "--json"]
    if "--json" in argv:
        as_json = True

    if len(args) == NUM_QUESTIONS:
        answers = [float(x) for x in args]
    elif len(args) == 0:
        # Default profile (matches client.niob encrypt_answers defaults): a modest
        # first-time owner in a small apartment — should favor Mochi.
        answers = [3, 1, 0, 1, 1, 2, 2, 1, 1, 0, 0, 3]
    else:
        sys.exit(f"expected {NUM_QUESTIONS} answers (0..4), got {len(args)}")

    pets = load_rubric()
    results = score(answers, pets)

    if as_json:
        print(json.dumps({"answers": answers, "pets": results}, ensure_ascii=False))
        return

    best = max(results, key=lambda r: r["overall"])
    print("=== Furever Home — adoption readiness (plaintext reference) ===")
    print(f"answers: {answers}\n")
    header = "pet".ljust(14) + "".join(c.ljust(11) for c in CATEGORIES) + "overall"
    print(header)
    print("-" * len(header))
    for r in results:
        label = f"{r['emoji']} {r['name']}".ljust(13)
        # Clamp to 0 for display (raw values, which may be negative, are what
        # the FHE result is checked against via --json).
        cats = "".join(f"{max(0.0, v):6.2f}".ljust(11) for v in r["categories"])
        print(f"{label} {cats}{max(0.0, r['overall']):6.2f}")
    print(f"\nBest match: {best['emoji']} {best['name']} ({best['overall']:.2f})")


if __name__ == "__main__":
    main(sys.argv[1:])
